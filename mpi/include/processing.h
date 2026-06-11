#ifndef PROCESSING_H
#define PROCESSING_H

#include <stdint.h>

#include "display_frame.h"

#define PIXEL_WORK_ROUNDS 2400

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
);

#endif