#ifndef LIBRARY_PLACEHOLDER_H
#define LIBRARY_PLACEHOLDER_H

#include <stdint.h>

#include "display_frame.h"

int pov_placeholder_write_packet(
    const char *filename,
    uint32_t speed_ms,
    const DisplayFrame *frame
);

#endif