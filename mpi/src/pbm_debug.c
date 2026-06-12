#include "pbm_debug.h"

#include <stdio.h>

int pbm_debug_write_decrypted(
    const char *filename,
    uint32_t width,
    uint32_t height,
    uint32_t row_stride_bytes,
    const uint8_t *plain_payload
) {
    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        perror("fopen imagen_desc.pbm");
        return -1;
    }

    fprintf(fp, "P1\n");
    fprintf(fp, "%u %u\n", width, height);

    for (uint32_t row = 0; row < height; row++) {

        for (uint32_t col = 0; col < width; col++) {

            uint64_t byte_index =
                (uint64_t)row * row_stride_bytes +
                (uint64_t)(col / 8u);

            uint32_t bit_offset =
                7u - (col % 8u);

            uint8_t bit =
                (plain_payload[byte_index] >> bit_offset) & 1u;

            fprintf(fp, "%u", bit);

            if (col + 1 < width) {
                fputc(' ', fp);
            }
        }

        fputc('\n', fp);
    }

    fclose(fp);

    printf("\n[DEBUG PBM]\n");
    printf("Imagen descifrada guardada en: %s\n", filename);

    return 0;
}