#ifndef LIBRARY_PLACEHOLDER_H
#define LIBRARY_PLACEHOLDER_H

#include <stdint.h>

int pov_placeholder_write_frame(
    const char *filename,
    uint32_t speed_ms,
    uint32_t width,
    uint32_t height,
    const uint8_t *rgb_matrix
);

#endif