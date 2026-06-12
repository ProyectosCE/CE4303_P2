#ifndef DRIVER_OUTPUT_H
#define DRIVER_OUTPUT_H

#include <stdint.h>

#include "display_frame.h"

int driver_output_write_and_optionally_send(
    const char *output_filename,
    uint32_t velocidad_ms,
    const DisplayFrame *frame,
    const char *device_path,
    int send_to_driver
);

#endif