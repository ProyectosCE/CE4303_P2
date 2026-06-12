#include "driver_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "pov_driver_lib.h"

/*
Archivo: src/driver_output.c
Descripción:
Implementación de funciones relacionadas con la generación del paquete de salida para el driver y su envío opcional.
Estas funciones incluyen la construcción del paquete binario que el driver espera, la escritura de este
paquete a un archivo, y la opción de enviarlo directamente al dispositivo del driver.
*/


/*
driver_output_write_and_optionally_send
Genera el paquete de salida para el driver a partir de un DisplayFrame, lo guarda en un archivo, y opcionalmente lo envía al dispositivo del driver.
Recibe:
- output_filename: ruta al archivo donde se guardará el paquete generado.
- velocidad_ms: velocidad en milisegundos que se incluirá en el paquete para el driver.
- frame: puntero al DisplayFrame que contiene la información de la imagen a enviar al driver.
- device_path: ruta al dispositivo del driver (ej. /dev/raspberry_usb). Si es NULL, se usará el valor por defecto definido en la biblioteca.
- send_to_driver: flag que indica si se debe enviar el paquete al driver después de generarlo. Si es 0, solo se genera el archivo.
Retorna: 0 si la operación fue exitosa, -1 si hubo un error en la generación del paquete o en la escritura del archivo. En caso de error, no se intenta enviar al driver.
*/
int driver_output_write_and_optionally_send(
    const char *output_filename,
    uint32_t velocidad_ms,
    const DisplayFrame *frame,
    const char *device_path,
    int send_to_driver
) {
    if (frame == NULL || frame->rgb == NULL) {
        printf("ERROR: frame inválido.\n");
        return -1;
    }

    if (velocidad_ms > 65535u) {
        printf("ERROR: velocidad_ms=%u no cabe en uint16_t.\n", velocidad_ms);
        return -1;
    }

    uint8_t *packet = NULL;
    size_t packet_len = 0;

    int build_result =
        pov_driver_build_packet(
            (uint16_t)velocidad_ms,
            frame->rgb,
            frame->columns,
            &packet,
            &packet_len
        );

    if (build_result < 0) {
        printf("ERROR: pov_driver_build_packet falló.\n");
        return -1;
    }

    FILE *fp =
        fopen(output_filename, "wb");

    if (fp == NULL) {
        perror("fopen driver_packet.bin");
        free(packet);
        return -1;
    }

    if (fwrite(packet, 1, packet_len, fp) != packet_len) {
        perror("fwrite driver_packet.bin");
        fclose(fp);
        free(packet);
        return -1;
    }

    fclose(fp);

    printf("\n[POV DRIVER OUTPUT]\n");
    printf("Archivo generado   : %s\n", output_filename);
    printf("Speed ms           : %u\n", velocidad_ms);
    printf("Columnas           : %u\n", frame->columns);
    printf("LEDs por columna   : %u\n", frame->leds_per_column);
    printf("RGB bytes          : %llu\n",
           (unsigned long long)(
               (uint64_t)frame->columns *
               (uint64_t)frame->leds_per_column *
               3u
           ));
    printf("Total packet bytes : %llu\n",
           (unsigned long long)packet_len);

    if (!send_to_driver) {
        printf("\n[POV DRIVER SEND]\n");
        printf("Envío desactivado. Solo se generó driver_packet.bin.\n");

        free(packet);
        return 0;
    }

    const char *path =
        device_path != NULL
            ? device_path
            : POV_DRIVER_DEFAULT_DEVICE;

    printf("\n[POV DRIVER SEND]\n");
    printf("Device             : %s\n", path);

    int fd =
        pov_driver_open(path);

    if (fd < 0) {
        printf("WARNING: no se pudo abrir el driver %s\n", path);
        printf("WARNING: el paquete ya fue generado en %s\n", output_filename);
        printf("WARNING: MPI terminará correctamente porque el procesamiento sí finalizó.\n");

        free(packet);

        /*
         * Importante:
         * No retornar -1 aquí.
         * El procesamiento MPI fue exitoso; solo falló la salida al hardware.
         */
        return 0;
    }

    int send_result =
        pov_driver_send_raw(
            fd,
            packet,
            packet_len
        );

    pov_driver_close(fd);

    if (send_result < 0) {
        printf("WARNING: pov_driver_send_raw falló.\n");
        printf("WARNING: código retornado por la biblioteca: %d\n", send_result);

        if (send_result < 0) {
            printf("WARNING: posible errno reportado: %d\n", -send_result);
        }

        printf("WARNING: probablemente la Raspberry/Pico no está conectada o no respondió.\n");
        printf("WARNING: el paquete ya fue generado en %s\n", output_filename);
        printf("WARNING: MPI terminará correctamente porque el procesamiento sí finalizó.\n");

        free(packet);

        /*
         * Importante:
         * No retornar -1 aquí.
         * Esto evita que prterun marque todo el job como fallido.
         */
        return 0;
    }

    printf("Paquete enviado correctamente al driver.\n");

    free(packet);

    return 0;
}