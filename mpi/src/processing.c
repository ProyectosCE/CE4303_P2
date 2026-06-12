#include "processing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/*
 * RONDA 2:
 *
 * Reduce columnas originales:
 *
 * width columnas -> 9 columnas
 *
 * Resultado:
 *
 * row_count filas locales x 9 columnas reducidas
 */
static void reduce_columns_to_9(
    const uint8_t *plain_rows,
    uint32_t width,
    uint32_t row_stride_bytes,
    uint32_t start_row,
    uint32_t row_count,
    DisplayAccum *row_column_accum,
    uint64_t *active_pixels_out,
    uint64_t *checksum_out
) {
    uint64_t active_pixels = 0;
    uint64_t checksum = 0;

    for (uint32_t local_row = 0; local_row < row_count; local_row++) {

        uint32_t global_row =
            start_row + local_row;

        for (uint32_t col = 0; col < width; col++) {

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

            uint32_t reduced_col =
                (uint32_t)(((uint64_t)col * PICO_COLUMN_COUNT) / width);

            if (reduced_col >= PICO_COLUMN_COUNT) {
                reduced_col = PICO_COLUMN_COUNT - 1;
            }

            uint64_t accum_index =
                ((uint64_t)local_row * PICO_COLUMN_COUNT) +
                reduced_col;

            row_column_accum[accum_index].total_count++;

            if (bit) {
                row_column_accum[accum_index].on_count++;
                active_pixels++;
            }
        }
    }

    *active_pixels_out = active_pixels;
    *checksum_out = checksum;
}

/*
 * RONDA 3:
 *
 * Toma la matriz:
 *
 * row_count filas locales x 9 columnas reducidas
 *
 * y la reduce a:
 *
 * 9 columnas x 9 filas/LEDs
 *
 * Conceptualmente es como trabajar por columnas reducidas y comprimir
 * las filas globales a 9 buckets verticales.
 */
static void reduce_rows_to_9(
    const DisplayAccum *row_column_accum,
    uint32_t height,
    uint32_t start_row,
    uint32_t row_count,
    DisplayAccum *final_accum
) {
    for (uint32_t local_row = 0; local_row < row_count; local_row++) {

        uint32_t global_row =
            start_row + local_row;

        uint32_t reduced_row =
            (uint32_t)(((uint64_t)global_row * PICO_LED_COUNT) / height);

        if (reduced_row >= PICO_LED_COUNT) {
            reduced_row = PICO_LED_COUNT - 1;
        }

        for (uint32_t reduced_col = 0;
             reduced_col < PICO_COLUMN_COUNT;
             reduced_col++) {

            uint64_t source_index =
                ((uint64_t)local_row * PICO_COLUMN_COUNT) +
                reduced_col;

            uint64_t destination_index =
                ((uint64_t)reduced_col * PICO_LED_COUNT) +
                reduced_row;

            final_accum[destination_index].on_count +=
                row_column_accum[source_index].on_count;

            final_accum[destination_index].total_count +=
                row_column_accum[source_index].total_count;
        }
    }
}

/*
 * RONDA 1 ya ocurrió antes de llamar esta función:
 * plain_rows ya viene descifrado.
 *
 * Esta función ejecuta:
 *
 * RONDA 2: reducción de columnas
 * RONDA 3: reducción de filas
 */
int process_rows_three_rounds(
    const uint8_t *plain_rows,
    uint32_t width,
    uint32_t height,
    uint32_t row_stride_bytes,
    uint32_t start_row,
    uint32_t row_count,
    DisplayAccum *final_accum,
    uint64_t *active_pixels_out,
    uint64_t *checksum_out
) {
    if (row_count == 0) {
        *active_pixels_out = 0;
        *checksum_out = 0;
        return 0;
    }

    uint64_t row_column_count =
        (uint64_t)row_count *
        (uint64_t)PICO_COLUMN_COUNT;

    DisplayAccum *row_column_accum =
        calloc(row_column_count, sizeof(DisplayAccum));

    if (row_column_accum == NULL) {
        printf("ERROR: malloc row_column_accum falló.\n");
        return -1;
    }

    uint64_t active_pixels = 0;
    uint64_t checksum = 0;

    reduce_columns_to_9(
        plain_rows,
        width,
        row_stride_bytes,
        start_row,
        row_count,
        row_column_accum,
        &active_pixels,
        &checksum
    );

    reduce_rows_to_9(
        row_column_accum,
        height,
        start_row,
        row_count,
        final_accum
    );

    free(row_column_accum);

    *active_pixels_out = active_pixels;
    *checksum_out = checksum;

    return 0;
}