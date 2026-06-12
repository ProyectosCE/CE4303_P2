#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define BIN_MAGIC   0x50424D42u
#define BIN_VERSION 1u

#define HEADER_FIELD_COUNT 10
#define HEADER_SIZE_BYTES  40

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t row_stride_bytes;
    uint32_t speed_ms;
    uint32_t payload_size;
    uint32_t cipher_rounds;
    uint32_t nonce0;
    uint32_t nonce1;
} BinHeader;

#endif