#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <mpi.h>

#include "mpi_tags.h"
#include "crypto.h"
#include "mpi_transfer.h"
#include "display_frame.h"
#include "processing.h"

/*
Archivo: src/worker.c
Descripción:
Implementación de la función run_worker, 
que es la función principal que ejecuta cada proceso 
trabajador en el sistema MPI. Esta función se encarga
de recibir los metadatos y el segmento de imagen cifrada, realizar el descifrado utilizando XTEA en modo CTR, procesar las filas descifradas para obtener los acumuladores por LED, y enviar los resultados de vuelta al proceso driver. La función también mide el tiempo de procesamiento y calcula un checksum para verificar la integridad del procesamiento. Esta implementación es crucial para el rendimiento general del sistema, ya que cada trabajador realiza una parte
*/

/

int run_worker(int rank) {

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    uint32_t meta_u32[9];
    uint64_t meta_u64[2];

    MPI_Recv(
        meta_u32,
        9,
        MPI_UINT32_T,
        0,
        TAG_WORK_META_U32,
        MPI_COMM_WORLD,
        MPI_STATUS_IGNORE
    );

    MPI_Recv(
        meta_u64,
        2,
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

    uint64_t global_block_offset =
        meta_u64[0];

    uint64_t segment_size =
        meta_u64[1];

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

    uint64_t accum_size = display_final_accum_size_bytes();

    DisplayAccum *local_accum =
        calloc(1, accum_size);

    if (local_accum == NULL) {
        printf("WORKER rank %d ERROR: malloc local_accum falló.\n", rank);
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (segment_size > 0) {
        encrypted_segment =
            malloc(segment_size);

        plain_segment =
            malloc(segment_size);

        if (encrypted_segment == NULL ||
            plain_segment == NULL) {
            printf("WORKER rank %d ERROR: malloc segment falló.\n", rank);
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
            xtea_ctr_crypt_segment(
                encrypted_segment,
                plain_segment,
                segment_size,
                cipher_rounds,
                nonce0,
                nonce1,
                global_block_offset
            );

            if (process_rows_three_rounds(
            plain_segment,
            width,
            height,
            row_stride_bytes,
            start_row,
            row_count,
            local_accum,
            &active_pixels,
            &checksum
        ) < 0) {
        printf("WORKER rank %d ERROR: procesamiento falló.\n", rank);
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    }

    double end_time =
        MPI_Wtime();

    double processing_seconds =
        end_time - start_time;

    uint32_t result_u32[2];

    result_u32[0] = start_row;
    result_u32[1] = row_count;

    uint64_t result_u64[4];

    result_u64[0] = accum_size;
    result_u64[1] = segment_size;
    result_u64[2] = active_pixels;
    result_u64[3] = checksum;

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
        4,
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

    if (accum_size > 0) {
        mpi_send_bytes(
            (const uint8_t *)local_accum,
            accum_size,
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
        "WORKER rank %d sent result | rows=%u | accum_9x9=%llu bytes | active=%llu | time=%.6f s\n",
        rank,
        row_count,
        (unsigned long long)accum_size,
        (unsigned long long)active_pixels,
        processing_seconds
    );
    fflush(stdout);

    free(encrypted_segment);
    free(plain_segment);
    free(local_accum);

    return 0;
}