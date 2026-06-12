#include "bin_io.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t read_u32_be(const uint8_t *buffer) {
    return
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8)  |
        ((uint32_t)buffer[3]);
}

int bin_read_file(
    const char *filename,
    BinHeader *header,
    uint8_t **payload_out
) {
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    uint8_t raw_header[HEADER_SIZE_BYTES];

    if (fread(raw_header, 1, HEADER_SIZE_BYTES, fp)
        != HEADER_SIZE_BYTES) {
        printf("ERROR: no se pudo leer header completo.\n");
        fclose(fp);
        return -1;
    }

    header->magic =
        read_u32_be(&raw_header[0]);

    header->version =
        read_u32_be(&raw_header[4]);

    header->width =
        read_u32_be(&raw_header[8]);

    header->height =
        read_u32_be(&raw_header[12]);

    header->row_stride_bytes =
        read_u32_be(&raw_header[16]);

    header->speed_ms =
        read_u32_be(&raw_header[20]);

    header->payload_size =
        read_u32_be(&raw_header[24]);

    header->cipher_rounds =
        read_u32_be(&raw_header[28]);

    header->nonce0 =
        read_u32_be(&raw_header[32]);

    header->nonce1 =
        read_u32_be(&raw_header[36]);

    if (header->magic != BIN_MAGIC) {
        printf("ERROR: magic inválido. Recibido=0x%08X\n",
               header->magic);
        fclose(fp);
        return -1;
    }

    if (header->version != BIN_VERSION) {
        printf("ERROR: versión inválida. Recibida=%u\n",
               header->version);
        fclose(fp);
        return -1;
    }

    if (header->width == 0 ||
        header->height == 0 ||
        header->row_stride_bytes == 0 ||
        header->payload_size == 0) {
        printf("ERROR: header contiene dimensiones inválidas.\n");
        fclose(fp);
        return -1;
    }

    uint64_t expected_size =
        (uint64_t)header->height *
        (uint64_t)header->row_stride_bytes;

    if (expected_size != header->payload_size) {
        printf("ERROR: payload_size inválido.\n");
        printf("Esperado=%llu, recibido=%u\n",
               (unsigned long long)expected_size,
               header->payload_size);
        fclose(fp);
        return -1;
    }

    uint8_t *payload =
        malloc(header->payload_size);

    if (payload == NULL) {
        printf("ERROR: malloc payload falló.\n");
        fclose(fp);
        return -1;
    }

    if (fread(payload, 1, header->payload_size, fp)
        != header->payload_size) {
        printf("ERROR: no se pudo leer payload completo.\n");
        free(payload);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    *payload_out = payload;

    return 0;
}

void bin_print_header(const BinHeader *header) {
    printf("\n[BIN HEADER]\n");
    printf("Magic            : 0x%08X\n", header->magic);
    printf("Version          : %u\n", header->version);
    printf("Width            : %u\n", header->width);
    printf("Height           : %u\n", header->height);
    printf("Row stride bytes : %u\n", header->row_stride_bytes);
    printf("Speed ms         : %u\n", header->speed_ms);
    printf("Payload size     : %u bytes\n", header->payload_size);
    printf("Cipher rounds    : %u\n", header->cipher_rounds);
    printf("Nonce0           : 0x%08X\n", header->nonce0);
    printf("Nonce1           : 0x%08X\n\n", header->nonce1);
}