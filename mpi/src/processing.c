#include "processing.h"

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

void process_rows_to_display_accum(
    const uint8_t *plain_rows,
    uint32_t width,
    uint32_t height,
    uint32_t row_stride_bytes,
    uint32_t start_row,
    uint32_t row_count,
    DisplayAccum *accum,
    uint64_t *active_pixels_out,
    uint64_t *checksum_out
) {
    uint64_t active_pixels = 0;
    uint64_t checksum = 0;

    for (uint32_t local_row = 0; local_row < row_count; local_row++) {

        uint32_t global_row =
            start_row + local_row;

        /*
         * Mapeo vertical:
         * una imagen de height filas se comprime a 9 LEDs.
         */
        uint32_t led_index =
            (uint32_t)(((uint64_t)global_row * PICO_LED_COUNT) / height);

        if (led_index >= PICO_LED_COUNT) {
            led_index = PICO_LED_COUNT - 1;
        }

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

            uint64_t accum_index =
                (uint64_t)col * PICO_LED_COUNT + led_index;

            accum[accum_index].total_count++;

            if (bit) {
                accum[accum_index].on_count++;
                active_pixels++;
            }
        }
    }

    *active_pixels_out = active_pixels;
    *checksum_out = checksum;
}