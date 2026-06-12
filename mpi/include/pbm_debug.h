#ifndef PBM_DEBUG_H
#define PBM_DEBUG_H

#include <stdint.h>

#include "display_frame.h"

int pbm_debug_write_decrypted(
    const char *filename,
    uint32_t width,
    uint32_t height,
    uint32_t row_stride_bytes,
    const uint8_t *plain_payload
);

int pbm_debug_write_driver_frame(
    const char *filename,
    const DisplayFrame *frame
);

#endif