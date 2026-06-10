/*
Master:
- lee el .bin
- valida magic
- divide filas
- manda segmentos cifrados
- recibe RGB procesado
- ensambla
- llama al placeholder

Workers:
- reciben filas cifradas
- descifran con XTEA-CTR
- procesan bits
- convierten a RGB
- devuelven resultado
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

#include "mpi_tags.h"
#include "protocol.h"
#include "crypto.h"
#include "library_placeholder.h"

#define OUTPUT_PACKET_FILE "driver_packet.bin"
#define DECRYPTED_PBM_FILE "imagen_desc.pbm"

/*
 * Colores del display.
 * PBM 1 -> azul
 * PBM 0 -> apagado
 */
#define COLOR_ON_R   0
#define COLOR_ON_G   0
#define COLOR_ON_B   30

#define COLOR_OFF_R  0
#define COLOR_OFF_G  0
#define COLOR_OFF_B  0

/*
 * Carga extra de procesamiento por pixel.
 *
 * Esto ayuda a que el profesor pueda observar mejor el comportamiento MPI.
 * El resultado visual no depende de este cálculo; se usa para generar carga
 * y una métrica checksum.
 */
#define PIXEL_WORK_ROUNDS 24

static uint32_t read_u32_be(const uint8_t *buffer) {
    return
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8)  |
        ((uint32_t)buffer[3]);
}

static int read_bin_file(
    const char *filename,
    BinHeader *header,
    uint8_t **payload_out
) {
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    uint8_t raw_header[HEADER_SIZE_BYTES];

    if (fread(raw_header, 1, HEADER_SIZE_BYTES, fp)
        != HEADER_SIZE_BYTES) {
        printf("ERROR: no se pudo leer header completo.\n");
        fclose(fp);
        return -1;
    }

    header->magic =
        read_u32_be(&raw_header[0]);

    header->version =
        read_u32_be(&raw_header[4]);

    header->width =
        read_u32_be(&raw_header[8]);

    header->height =
        read_u32_be(&raw_header[12]);

    header->row_stride_bytes =
        read_u32_be(&raw_header[16]);

    header->speed_ms =
        read_u32_be(&raw_header[20]);

    header->payload_size =
        read_u32_be(&raw_header[24]);

    header->cipher_rounds =
        read_u32_be(&raw_header[28]);

    header->nonce0 =
        read_u32_be(&raw_header[32]);

    header->nonce1 =
        read_u32_be(&raw_header[36]);

    if (header->magic != BIN_MAGIC) {
        printf("ERROR: magic inválido. Recibido=0x%08X\n",
               header->magic);
        fclose(fp);
        return -1;
    }

    if (header->version != BIN_VERSION) {
        printf("ERROR: versión inválida. Recibida=%u\n",
               header->version);
        fclose(fp);
        return -1;
    }

    if (header->width == 0 ||
        header->height == 0 ||
        header->row_stride_bytes == 0 ||
        header->payload_size == 0) {
        printf("ERROR: header contiene dimensiones inválidas.\n");
        fclose(fp);
        return -1;
    }

    uint64_t expected_size =
        (uint64_t)header->height *
        (uint64_t)header->row_stride_bytes;

    if (expected_size != header->payload_size) {
        printf("ERROR: payload_size inválido.\n");
        printf("Esperado=%llu, recibido=%u\n",
               (unsigned long long)expected_size,
               header->payload_size);
        fclose(fp);
        return -1;
    }

    uint8_t *payload =
        malloc(header->payload_size);

    if (payload == NULL) {
        printf("ERROR: malloc payload falló.\n");
        fclose(fp);
        return -1;
    }

    if (fread(payload, 1, header->payload_size, fp)
        != header->payload_size) {
        printf("ERROR: no se pudo leer payload completo.\n");
        free(payload);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    *payload_out = payload;

    return 0;
}

/*
 * Enviar buffers grandes en chunks.
 * MPI_Send recibe int como count, por eso conviene partir.
 */
static void mpi_send_bytes(
    const uint8_t *buffer,
    uint64_t size,
    int destination,
    int tag
) {
    const uint64_t CHUNK_SIZE = 64ull * 1024ull * 1024ull;

    uint64_t offset = 0;

    while (offset < size) {

        uint64_t remaining = size - offset;

        int chunk =
            (int)(remaining > CHUNK_SIZE
                  ? CHUNK_SIZE
                  : remaining);

        MPI_Send(
            (void *)(buffer + offset),
            chunk,
            MPI_BYTE,
            destination,
            tag,
            MPI_COMM_WORLD
        );

        offset += (uint64_t)chunk;
    }
}

static void mpi_recv_bytes(
    uint8_t *buffer,
    uint64_t size,
    int source,
    int tag
) {
    const uint64_t CHUNK_SIZE = 64ull * 1024ull * 1024ull;

    uint64_t offset = 0;

    while (offset < size) {

        uint64_t remaining = size - offset;

        int chunk =
            (int)(remaining > CHUNK_SIZE
                  ? CHUNK_SIZE
                  : remaining);

        MPI_Recv(
            buffer + offset,
            chunk,
            MPI_BYTE,
            source,
            tag,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        offset += (uint64_t)chunk;
    }
}

static uint8_t get_pbm_bit(
    const uint8_t *plain_rows,
    uint32_t row_stride_bytes,
    uint32_t local_row,
    uint32_t col
) {
    uint64_t byte_index =
        (uint64_t)local_row * row_stride_bytes +
        (uint64_t)(col / 8u);

    uint32_t bit_offset =
        7u - (col % 8u);

    return
        (plain_rows[byte_index] >> bit_offset) & 1u;
}

/*
 * Carga extra artificial, pero determinista.
 * Sirve para medir procesamiento en workers.
 */
static uint64_t heavy_pixel_work(
    uint32_t global_row,
    uint32_t col,
    uint8_t bit
) {
    uint64_t value =
        ((uint64_t)global_row << 32) ^
        ((uint64_t)col << 1) ^
        (uint64_t)bit ^
        0x9E3779B97F4A7C15ull;

    for (int i = 0; i < PIXEL_WORK_ROUNDS; i++) {
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        value *= 2685821657736338717ull;
    }

    return value;
}

static void process_rows_to_rgb(
    const uint8_t *plain_rows,
    uint8_t *rgb_rows,
    uint32_t width,
    uint32_t row_stride_bytes,
    uint32_t start_row,
    uint32_t row_count,
    uint64_t *active_pixels_out,
    uint64_t *checksum_out
) {
    uint64_t active_pixels = 0;
    uint64_t checksum = 0;

    for (uint32_t local_row = 0;
         local_row < row_count;
         local_row++) {

        uint32_t global_row =
            start_row + local_row;

        for (uint32_t col = 0;
             col < width;
             col++) {

            uint8_t bit =
                get_pbm_bit(
                    plain_rows,
                    row_stride_bytes,
                    local_row,
                    col
                );

            checksum ^=
                heavy_pixel_work(
                    global_row,
                    col,
                    bit
                );

            uint64_t rgb_index =
                (
                    (uint64_t)local_row *
                    (uint64_t)width +
                    (uint64_t)col
                ) * 3u;

            if (bit) {
                rgb_rows[rgb_index + 0] = COLOR_ON_R;
                rgb_rows[rgb_index + 1] = COLOR_ON_G;
                rgb_rows[rgb_index + 2] = COLOR_ON_B;
                active_pixels++;
            } else {
                rgb_rows[rgb_index + 0] = COLOR_OFF_R;
                rgb_rows[rgb_index + 1] = COLOR_OFF_G;
                rgb_rows[rgb_index + 2] = COLOR_OFF_B;
            }
        }
    }

    *active_pixels_out = active_pixels;
    *checksum_out = checksum;
}



static int write_decrypted_pbm(
    const char *filename,
    uint32_t width,
    uint32_t height,
    uint32_t row_stride_bytes,
    const uint8_t *plain_payload
) {
    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        perror("fopen imagen_desc.pbm");
        return -1;
    }

    fprintf(fp, "P1\n");
    fprintf(fp, "%u %u\n", width, height);

    for (uint32_t row = 0; row < height; row++) {

        for (uint32_t col = 0; col < width; col++) {

            uint64_t byte_index =
                (uint64_t)row * row_stride_bytes +
                (uint64_t)(col / 8u);

            uint32_t bit_offset =
                7u - (col % 8u);

            uint8_t bit =
                (plain_payload[byte_index] >> bit_offset) & 1u;

            fprintf(fp, "%u", bit);

            if (col + 1 < width) {
                fputc(' ', fp);
            }
        }

        fputc('\n', fp);
    }

    fclose(fp);

    printf("\n[DEBUG PBM]\n");
    printf("Imagen descifrada guardada en: %s\n", filename);

    return 0;
}


static int run_master(
    int size,
    const char *input_file
) {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    printf("\nMASTER rank 0 running on %s\n", hostname);
    printf("Input file: %s\n", input_file);
    fflush(stdout);
    double master_start_time = MPI_Wtime();

    BinHeader header;
    uint8_t *encrypted_payload = NULL;

    if (read_bin_file(input_file, &header, &encrypted_payload) < 0) {
        return -1;
    }

    printf("\n[BIN HEADER]\n");
    printf("Magic            : 0x%08X\n", header.magic);
    printf("Version          : %u\n", header.version);
    printf("Width            : %u\n", header.width);
    printf("Height           : %u\n", header.height);
    printf("Row stride bytes : %u\n", header.row_stride_bytes);
    printf("Speed ms         : %u\n", header.speed_ms);
    printf("Payload size     : %u bytes\n", header.payload_size);
    printf("Cipher rounds    : %u\n", header.cipher_rounds);
    printf("Nonce0           : 0x%08X\n", header.nonce0);
    printf("Nonce1           : 0x%08X\n\n", header.nonce1);
    fflush(stdout);

    int workers_count = size - 1;

    uint64_t final_rgb_size =
        (uint64_t)header.width *
        (uint64_t)header.height *
        3u;

    uint8_t *final_rgb =
        malloc(final_rgb_size);

    if (final_rgb == NULL) {
        printf("ERROR: malloc final_rgb falló.\n");
        free(encrypted_payload);
        return -1;
    }

    memset(final_rgb, 0, final_rgb_size);

    uint8_t *final_plain_payload =
    malloc(header.payload_size);

    if (final_plain_payload == NULL) {
        printf("ERROR: malloc final_plain_payload falló.\n");
        free(final_rgb);
        free(encrypted_payload);
        return -1;
    }

    memset(final_plain_payload, 0, header.payload_size);

    uint32_t rows_per_worker =
        header.height / (uint32_t)workers_count;

    uint32_t remainder =
        header.height % (uint32_t)workers_count;

    uint32_t current_start_row = 0;

    /*
     * Reparto de filas.
     * El master solo divide y envía; no descifra ni procesa.
     */
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

        uint32_t meta_u32[10];

        meta_u32[0] = header.width;
        meta_u32[1] = header.height;
        meta_u32[2] = header.row_stride_bytes;
        meta_u32[3] = header.speed_ms;
        meta_u32[4] = header.cipher_rounds;
        meta_u32[5] = header.nonce0;
        meta_u32[6] = header.nonce1;
        meta_u32[7] = start_row;
        meta_u32[8] = row_count;
        meta_u32[9] = (uint32_t)segment_size;

        printf(
            "MASTER sending rows [%u - %u] to worker rank %d | bytes=%llu\n",
            start_row,
            row_count == 0 ? start_row : start_row + row_count - 1,
            worker,
            (unsigned long long)segment_size
        );
        fflush(stdout);

        MPI_Send(
            meta_u32,
            10,
            MPI_UINT32_T,
            worker,
            TAG_WORK_META_U32,
            MPI_COMM_WORLD
        );

        MPI_Send(
            &global_block_offset,
            1,
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

    /*
     * Recibir resultados.
     */
    for (int worker = 1; worker < size; worker++) {

        uint32_t result_u32[2];
        uint64_t result_u64[3];
        double processing_seconds = 0.0;

        MPI_Recv(
            result_u32,
            2,
            MPI_UINT32_T,
            worker,
            TAG_RESULT_META_U32,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        MPI_Recv(
            result_u64,
            3,
            MPI_UINT64_T,
            worker,
            TAG_RESULT_META_U64,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

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

        uint64_t rgb_size =
            result_u64[0];

        uint64_t active_pixels =
            result_u64[1];

        uint64_t checksum =
            result_u64[2];

        uint64_t rgb_offset =
            (uint64_t)start_row *
            (uint64_t)header.width *
            3u;

        if (rgb_size > 0) {
            mpi_recv_bytes(
                final_rgb + rgb_offset,
                rgb_size,
                worker,
                TAG_RESULT_PAYLOAD
            );
        }

        uint64_t plain_offset =
            (uint64_t)start_row *
            (uint64_t)header.row_stride_bytes;

        uint64_t plain_size =
            (uint64_t)row_count *
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
            "MASTER received rows [%u - %u] from worker rank %d | RGB=%llu bytes | active=%llu | time=%.6f s\n",
            start_row,
            row_count == 0 ? start_row : start_row + row_count - 1,
            worker,
            (unsigned long long)rgb_size,
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

    if (write_decrypted_pbm(
            DECRYPTED_PBM_FILE,
            header.width,
            header.height,
            header.row_stride_bytes,
            final_plain_payload
        ) < 0) {
        printf("WARNING: no se pudo escribir imagen_desc.pbm\n");
    }

    /*
     * El master no procesa imagen.
     * Solo entrega la matriz final ya ensamblada al placeholder de biblioteca.
     */
    int lib_result =
        pov_placeholder_write_frame(
            OUTPUT_PACKET_FILE,
            header.speed_ms,
            header.width,
            header.height,
            final_rgb
        );

    double master_end_time = MPI_Wtime();
    double master_total_time = master_end_time - master_start_time;

    printf("\n[TOTAL EXECUTION]\n");
    printf("Master total time : %.6f s\n", master_total_time);
    printf("Workers used      : %d\n", workers_count);
    printf("Image size        : %u x %u\n", header.width, header.height);
    printf("Cipher rounds     : %u\n", header.cipher_rounds);
    printf("Payload bytes     : %u\n", header.payload_size);
    printf("Output RGB bytes  : %llu\n",
        (unsigned long long)final_rgb_size);

    free(final_plain_payload);
    free(final_rgb);
    free(encrypted_payload);
}

static int run_worker(int rank) {

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    uint32_t meta_u32[10];
    uint64_t global_block_offset = 0;

    MPI_Recv(
        meta_u32,
        10,
        MPI_UINT32_T,
        0,
        TAG_WORK_META_U32,
        MPI_COMM_WORLD,
        MPI_STATUS_IGNORE
    );

    MPI_Recv(
        &global_block_offset,
        1,
        MPI_UINT64_T,
        0,
        TAG_WORK_META_U64,
        MPI_COMM_WORLD,
        MPI_STATUS_IGNORE
    );

    uint32_t width =
        meta_u32[0];

    uint32_t height =
        meta_u32[1];

    uint32_t row_stride_bytes =
        meta_u32[2];

    uint32_t speed_ms =
        meta_u32[3];

    uint32_t cipher_rounds =
        meta_u32[4];

    uint32_t nonce0 =
        meta_u32[5];

    uint32_t nonce1 =
        meta_u32[6];

    uint32_t start_row =
        meta_u32[7];

    uint32_t row_count =
        meta_u32[8];

    uint64_t segment_size =
        (uint64_t)meta_u32[9];

    (void)height;
    (void)speed_ms;

    printf(
        "WORKER rank %d running on %s received rows [%u - %u] | encrypted bytes=%llu\n",
        rank,
        hostname,
        start_row,
        row_count == 0 ? start_row : start_row + row_count - 1,
        (unsigned long long)segment_size
    );
    fflush(stdout);

    uint8_t *encrypted_segment = NULL;
    uint8_t *plain_segment = NULL;
    uint8_t *rgb_segment = NULL;

    uint64_t rgb_size =
        (uint64_t)row_count *
        (uint64_t)width *
        3u;

    if (segment_size > 0) {
        encrypted_segment =
            malloc(segment_size);

        plain_segment =
            malloc(segment_size);

        rgb_segment =
            malloc(rgb_size);

        if (encrypted_segment == NULL ||
            plain_segment == NULL ||
            rgb_segment == NULL) {
            printf("WORKER rank %d ERROR: malloc falló.\n", rank);
            fflush(stdout);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        mpi_recv_bytes(
            encrypted_segment,
            segment_size,
            0,
            TAG_WORK_PAYLOAD
        );
    }

    double start_time =
        MPI_Wtime();

    uint64_t active_pixels = 0;
    uint64_t checksum = 0;

    if (segment_size > 0) {

        /*
         * 1. Descifrado pesado.
         */
        xtea_ctr_crypt_segment(
            encrypted_segment,
            plain_segment,
            segment_size,
            cipher_rounds,
            nonce0,
            nonce1,
            global_block_offset
        );

        /*
         * 2. Procesamiento por filas.
         */
        process_rows_to_rgb(
            plain_segment,
            rgb_segment,
            width,
            row_stride_bytes,
            start_row,
            row_count,
            &active_pixels,
            &checksum
        );
    }

    double end_time =
        MPI_Wtime();

    double processing_seconds =
        end_time - start_time;

    uint32_t result_u32[2];

    result_u32[0] = start_row;
    result_u32[1] = row_count;

    uint64_t result_u64[3];

    result_u64[0] = rgb_size;
    result_u64[1] = active_pixels;
    result_u64[2] = checksum;

    MPI_Send(
        result_u32,
        2,
        MPI_UINT32_T,
        0,
        TAG_RESULT_META_U32,
        MPI_COMM_WORLD
    );

    MPI_Send(
        result_u64,
        3,
        MPI_UINT64_T,
        0,
        TAG_RESULT_META_U64,
        MPI_COMM_WORLD
    );

    MPI_Send(
        &processing_seconds,
        1,
        MPI_DOUBLE,
        0,
        TAG_RESULT_TIME,
        MPI_COMM_WORLD
    );

    if (rgb_size > 0) {
        mpi_send_bytes(
            rgb_segment,
            rgb_size,
            0,
            TAG_RESULT_PAYLOAD
        );
    }

    if (segment_size > 0) {
        mpi_send_bytes(
            plain_segment,
            segment_size,
            0,
            TAG_RESULT_PLAIN_PAYLOAD
        );
    }

    printf(
        "WORKER rank %d sent result | rows=%u | RGB=%llu bytes | active=%llu | time=%.6f s\n",
        rank,
        row_count,
        (unsigned long long)rgb_size,
        (unsigned long long)active_pixels,
        processing_seconds
    );
    fflush(stdout);

    free(encrypted_segment);
    free(plain_segment);
    free(rgb_segment);

    return 0;
}

int main(int argc, char **argv) {

    int rank;
    int size;

    MPI_Init(&argc, &argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int fatal_error = 0;

    if (rank == 0) {
        if (argc < 2) {
            printf("Uso:\n");
            printf("  mpirun -np 5 ./pov_mpi imagen_payload.bin\n");
            fatal_error = 1;
        }

        if (size < 2) {
            printf("ERROR: se necesita al menos 2 procesos.\n");
            printf("rank 0 = master, rank 1..N = workers\n");
            fatal_error = 1;
        }
    }

    MPI_Bcast(
        &fatal_error,
        1,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );

    if (fatal_error) {
        MPI_Finalize();
        return 1;
    }

    int result = 0;

    if (rank == 0) {
        result =
            run_master(
                size,
                argv[1]
            );
    } else {
        result =
            run_worker(rank);
    }

    MPI_Finalize();

    return result == 0 ? 0 : 1;
}