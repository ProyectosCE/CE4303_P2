#ifndef BIN_IO_H
#define BIN_IO_H

#include <stdint.h>

#include "protocol.h"

int bin_read_file(
    const char *filename,
    BinHeader *header,
    uint8_t **payload_out
);

void bin_print_header(const BinHeader *header);

#endif