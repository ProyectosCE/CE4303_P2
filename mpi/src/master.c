#include "master.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

#include "protocol.h"
#include "mpi_tags.h"
#include "bin_io.h"
#include "mpi_transfer.h"
#include "pbm_debug.h"
#include "display_frame.h"
#include "driver_output.h"


/*
Archivo: src/master.c
Descripción:
Implementación de la función run_master, que actúa como el proceso maestro en la arquitectura MPI
para el proyecto. Esta función es responsable de leer el archivo de entrada, distribuir el trabajo entre los procesos trabajadores,
recopilar los resultados, construir el frame final para el driver, y generar la salida para el driver.
*/

#define OUTPUT_PACKET_FILE "driver_packet.bin"
#define DECRYPTED_PBM_FILE "imagen_desc.pbm"
#define RESULT_DRIVER_PBM_FILE "resultado_mpi_driver.pbm"


/*
run_master
Función principal del proceso maestro. Se encarga de leer el archivo de entrada, distribuir el
trabajo entre los procesos trabajadores, recopilar los resultados, construir el frame final para el driver, y generar la salida para el driver.
Recibe:
- size: número total de procesos MPI (incluyendo el maestro).
- input_file: ruta al archivo binario de entrada que contiene la imagen cifrada y su header.
- device_path: ruta al dispositivo del driver (ej. /dev/raspberry_usb). Si es NULL, se usará el valor por defecto definido en la biblioteca.
- send_to_driver: flag que indica si se debe enviar el paquete al driver después de generarlo. Si   es 0, solo se genera el archivo.
Retorna: 0 si la operación fue exitosa, -1 si hubo un error en cualquier etapa del proceso. En caso de error, se imprime un mensaje descriptivo.
*/
int run_master(
    int size,
    const char *input_file,
    const char *device_path,
    int send_to_driver
) {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    printf("\nMASTER rank 0 running on %s\n", hostname);
    printf("Input file: %s\n", input_file);
    fflush(stdout);

    double master_start_time =
        MPI_Wtime();

    // Leer el archivo de entrada y obtener el header y el payload cifrado.
    BinHeader header;
    uint8_t *encrypted_payload = NULL;

    if (bin_read_file(input_file, &header, &encrypted_payload) < 0) {
        return -1;
    }

    // Imprimir el contenido del header para depuración.
    bin_print_header(&header);
    
    int workers_count =
        size - 1;

    uint8_t *final_plain_payload =
        malloc(header.payload_size);

    if (final_plain_payload == NULL) {
        printf("ERROR: malloc final_plain_payload falló.\n");
        free(encrypted_payload);
        return -1;
    }


    // Inicializar el buffer del payload descifrado a cero. Esto es importante para que los procesos trabajadores puedan escribir solo las partes que les corresponden sin afectar otras partes del buffer.
    memset(final_plain_payload, 0, header.payload_size);


    uint64_t accum_size =
        display_final_accum_size_bytes();


    // Inicializar el acumulador global a cero. Este acumulador se irá llenando con los resultados de cada trabajador.
    DisplayAccum *global_accum =
        calloc(1, accum_size);

    // También se necesita un acumulador local para recibir los datos de cada trabajador antes de fusionarlos en el acumulador global.
    DisplayAccum *worker_accum =
        malloc(accum_size);

    if (global_accum == NULL || worker_accum == NULL) {
        printf("ERROR: malloc accum falló.\n");
        free(worker_accum);
        free(global_accum);
        free(final_plain_payload);
        free(encrypted_payload);
        return -1;
    }


    // Distribuir el trabajo entre los procesos trabajadores. Cada trabajador recibirá un segmento de filas para procesar, junto con la información necesaria para descifrar su segmento.
    uint32_t rows_per_worker =
        header.height / (uint32_t)workers_count;

    // El maestro se encarga de calcular los offsets y tamaños de cada segmento, y enviar esta información junto con el payload cifrado correspondiente a cada trabajador.
    // El restante de filas (si header.height no es divisible exactamente por workers_count) se distribuye entre los primeros 'remainder' trabajadores, dándoles una fila adicional a cada uno hasta que se agoten las filas restantes.
    uint32_t remainder =
        header.height % (uint32_t)workers_count;

    uint32_t current_start_row = 0;

    // Enviar a cada trabajador su segmento de filas junto con la información necesaria para el procesamiento.
    for (int worker = 1; worker < size; worker++) {

        uint32_t row_count =
            rows_per_worker;

        if ((uint32_t)(worker - 1) < remainder) {
            row_count++;
        }

        uint32_t start_row =
            current_start_row;

        current_start_row += row_count;

        uint64_t byte_offset =
            (uint64_t)start_row *
            (uint64_t)header.row_stride_bytes;

        uint64_t segment_size =
            (uint64_t)row_count *
            (uint64_t)header.row_stride_bytes;

        uint64_t global_block_offset =
            byte_offset / 8u;

        // Preparar los metadatos que se enviarán al trabajador junto con el segmento cifrado. Estos metadatos incluyen la información del header relevante para el procesamiento, así como el rango de filas que el trabajador debe procesar.
        
        uint32_t meta_u32[9];

        meta_u32[0] = header.width;
        meta_u32[1] = header.height;
        meta_u32[2] = header.row_stride_bytes;
        meta_u32[3] = header.speed_ms;
        meta_u32[4] = header.cipher_rounds;
        meta_u32[5] = header.nonce0;
        meta_u32[6] = header.nonce1;
        meta_u32[7] = start_row;
        meta_u32[8] = row_count;

        uint64_t meta_u64[2];

        meta_u64[0] = global_block_offset;
        meta_u64[1] = segment_size;

        // Imprimir información sobre el segmento que se está enviando a cada trabajador para depuración.

        printf(
            "MASTER sending rows [%u - %u] to worker rank %d | bytes=%llu\n",
            start_row,
            row_count == 0 ? start_row : start_row + row_count - 1,
            worker,
            (unsigned long long)segment_size
        );
        fflush(stdout);


        // IMPORTANTE: el maestro envía primero los metadatos (meta_u32 y meta_u64) 
        // para que el trabajador sepa qué esperar, y luego envía el segmento cifrado.
        // Esto permite que el trabajador pueda prepararse adecuadamente para recibir el 
        // segmento cifrado, especialmente si el tamaño del segmento es grande.
        
        
        // Enviar los metadatos al trabajador. Se envían en dos mensajes separados para mantener la claridad y flexibilidad en el protocolo de comunicación.
        MPI_Send(
            meta_u32,
            9,
            MPI_UINT32_T,
            worker,
            TAG_WORK_META_U32,
            MPI_COMM_WORLD
        );

        // Enviar los metadatos adicionales en un mensaje separado para mantener la claridad y flexibilidad en el protocolo de comunicación.
        MPI_Send(
            meta_u64,
            2,
            MPI_UINT64_T,
            worker,
            TAG_WORK_META_U64,
            MPI_COMM_WORLD
        );

        if (segment_size > 0) {
            mpi_send_bytes(
                encrypted_payload + byte_offset,
                segment_size,
                worker,
                TAG_WORK_PAYLOAD
            );
        }
    }

    uint64_t total_active_pixels = 0;
    uint64_t global_checksum = 0;

    for (int worker = 1; worker < size; worker++) {

        uint32_t result_u32[2];
        uint64_t result_u64[4];
        double processing_seconds = 0.0;

        // Recibir los resultados de cada trabajador. Cada trabajador enviará de vuelta el número de filas procesadas, el tamaño del acumulador que envía, el número de píxeles activos encontrados, el checksum parcial, y el tiempo que tomó procesar su segmento.
        MPI_Recv(
            result_u32,
            2,
            MPI_UINT32_T,
            worker,
            TAG_RESULT_META_U32,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );


        // Recibir los resultados adicionales en un mensaje separado para mantener la claridad y flexibilidad en el protocolo de comunicación.
        MPI_Recv(
            result_u64,
            4,
            MPI_UINT64_T,
            worker,
            TAG_RESULT_META_U64,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        // Recibir el tiempo de procesamiento del trabajador en un mensaje separado para mantener la claridad y flexibilidad en el protocolo de comunicación.
        MPI_Recv(
            &processing_seconds,
            1,
            MPI_DOUBLE,
            worker,
            TAG_RESULT_TIME,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        uint32_t start_row =
            result_u32[0];

        uint32_t row_count =
            result_u32[1];

        uint64_t received_accum_size =
            result_u64[0];

        uint64_t plain_size =
            result_u64[1];

        uint64_t active_pixels =
            result_u64[2];

        uint64_t checksum =
            result_u64[3];

        if (received_accum_size != accum_size) {
            printf("ERROR: accum_size inesperado desde worker %d.\n", worker);
            free(worker_accum);
            free(global_accum);
            free(final_plain_payload);
            free(encrypted_payload);
            return -1;
        }

        if (received_accum_size > 0) {
            mpi_recv_bytes(
                (uint8_t *)worker_accum,
                received_accum_size,
                worker,
                TAG_RESULT_PAYLOAD
            );

            display_accum_merge(
                global_accum,
                worker_accum
            );
        }

        uint64_t plain_offset =
            (uint64_t)start_row *
            (uint64_t)header.row_stride_bytes;

        if (plain_size > 0) {
            mpi_recv_bytes(
                final_plain_payload + plain_offset,
                plain_size,
                worker,
                TAG_RESULT_PLAIN_PAYLOAD
            );
        }

        total_active_pixels += active_pixels;
        global_checksum ^= checksum;

        printf(
            "MASTER received rows [%u - %u] from worker rank %d | accum=%llu bytes | plain=%llu bytes | active=%llu | time=%.6f s\n",
            start_row,
            row_count == 0 ? start_row : start_row + row_count - 1,
            worker,
            (unsigned long long)received_accum_size,
            (unsigned long long)plain_size,
            (unsigned long long)active_pixels,
            processing_seconds
        );
        fflush(stdout);
    }

    printf("\n[MPI RESULT]\n");
    printf("Total active pixels : %llu\n",
           (unsigned long long)total_active_pixels);
    printf("Global checksum     : 0x%016llX\n",
           (unsigned long long)global_checksum);

    if (pbm_debug_write_decrypted(
            DECRYPTED_PBM_FILE,
            header.width,
            header.height,
            header.row_stride_bytes,
            final_plain_payload
        ) < 0) {
        printf("WARNING: no se pudo escribir imagen_desc.pbm\n");
    }

    DisplayFrame frame;

    if (display_frame_build_from_accum(
            global_accum,
            &frame
        ) < 0) {
        free(worker_accum);
        free(global_accum);
        free(final_plain_payload);
        free(encrypted_payload);
        return -1;
    }

    /*
    * Guardar SIEMPRE el resultado final del trabajo MPI.
    *
    * Este PBM representa exactamente la matriz que se envía al driver:
    *
    * columnas x LEDs
    *
    * Ejemplo actual:
    * 9 columnas x 9 LEDs
    */
    if (pbm_debug_write_driver_frame(
            RESULT_DRIVER_PBM_FILE,
            &frame
        ) < 0) {
        printf("WARNING: no se pudo escribir resultado_mpi_driver.pbm\n");
    }

    int lib_result =
        driver_output_write_and_optionally_send(
            OUTPUT_PACKET_FILE,
            header.speed_ms,
            &frame,
            device_path,
            send_to_driver
        );

    display_frame_free(&frame);

    if (lib_result < 0) {
        printf("ERROR: fallo al generar el paquete del driver.\n");

        free(worker_accum);
        free(global_accum);
        free(final_plain_payload);
        free(encrypted_payload);

        return -1;
    }

    double master_end_time =
        MPI_Wtime();

    double master_total_time =
        master_end_time - master_start_time;

    uint64_t final_packet_rgb_size =
        (uint64_t)PICO_COLUMN_COUNT *
        (uint64_t)PICO_LED_COUNT *
        3u;

    printf("\n[TOTAL EXECUTION]\n");
    printf("Master total time   : %.6f s\n", master_total_time);
    printf("Workers used        : %d\n", workers_count);
    printf("Image size          : %u x %u\n", header.width, header.height);
    printf("Reduced output     : %u x %u\n",
        PICO_COLUMN_COUNT,
        PICO_LED_COUNT);
    printf("Cipher rounds       : %u\n", header.cipher_rounds);
    printf("Input payload bytes : %u\n", header.payload_size);
    printf("Output RGB bytes    : %llu\n",
        (unsigned long long)final_packet_rgb_size);
    printf("Output packet bytes : %llu\n",
        (unsigned long long)(final_packet_rgb_size + 2u));

    free(worker_accum);
    free(global_accum);
    free(final_plain_payload);
    free(encrypted_payload);

    return 0;
}