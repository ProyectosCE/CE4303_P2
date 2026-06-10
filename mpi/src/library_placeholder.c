#include "library_placeholder.h"

#include <stdio.h>
#include <stdint.h>

int pov_placeholder_write_frame(
    const char *filename,
    uint32_t speed_ms,
    uint32_t width,
    uint32_t height,
    const uint8_t *rgb_matrix
) {
    FILE *fp = fopen(filename, "wb");

    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    if (speed_ms > 65535u) {
        printf("ERROR: speed_ms no cabe en 2 bytes.\n");
        fclose(fp);
        return -1;
    }

    /*
     * Velocidad en 2 bytes, big-endian.
     * Equivalente a struct.pack("!H", velocidad_ms) en Python.
     */
    uint8_t speed_bytes[2];

    speed_bytes[0] =
        (uint8_t)((speed_ms >> 8) & 0xFF);

    speed_bytes[1] =
        (uint8_t)(speed_ms & 0xFF);

    if (fwrite(speed_bytes, 1, 2, fp) != 2) {
        perror("fwrite speed");
        fclose(fp);
        return -1;
    }

    uint64_t rgb_size =
        (uint64_t)width * (uint64_t)height * 3u;

    if (fwrite(rgb_matrix, 1, rgb_size, fp) != rgb_size) {
        perror("fwrite rgb");
        fclose(fp);
        return -1;
    }

    fclose(fp);

    printf("\n[POV PLACEHOLDER]\n");
    printf("Archivo generado : %s\n", filename);
    printf("Speed ms         : %u\n", speed_ms);
    printf("Width            : %u\n", width);
    printf("Height           : %u\n", height);
    printf("RGB bytes        : %llu\n",
           (unsigned long long)rgb_size);
    printf("Total packet     : %llu bytes\n",
           (unsigned long long)(rgb_size + 2u));

    return 0;
}