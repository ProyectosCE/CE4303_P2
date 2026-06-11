#include "library_placeholder.h"

#include <stdio.h>
#include <stdint.h>

int pov_placeholder_write_packet(
    const char *filename,
    uint32_t speed_ms,
    const DisplayFrame *frame
) {
    FILE *fp = fopen(filename, "wb");

    if (fp == NULL) {
        perror("fopen driver packet");
        return -1;
    }

    if (speed_ms > 65535u) {
        printf("ERROR: speed_ms no cabe en 2 bytes.\n");
        fclose(fp);
        return -1;
    }

    if (frame == NULL || frame->rgb == NULL) {
        printf("ERROR: frame inválido.\n");
        fclose(fp);
        return -1;
    }

    /*
     * Compatible con:
     * struct.pack("!H", velocidad_ms)
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

    /*
     * Compatible con:
     *
     * for columna in matriz_ejemplo:
     *     for led in columna:
     *         paquete_binario += struct.pack("BBB", ...)
     *
     * frame->rgb ya viene en orden:
     * columna 0, led 0..8
     * columna 1, led 0..8
     * ...
     */
    uint64_t rgb_size =
        (uint64_t)frame->columns *
        (uint64_t)frame->leds_per_column *
        3u;

    if (fwrite(frame->rgb, 1, rgb_size, fp) != rgb_size) {
        perror("fwrite frame rgb");
        fclose(fp);
        return -1;
    }

    fclose(fp);

    printf("\n[POV PLACEHOLDER]\n");
    printf("Archivo generado   : %s\n", filename);
    printf("Speed ms           : %u\n", speed_ms);
    printf("Columnas           : %u\n", frame->columns);
    printf("LEDs por columna   : %u\n", frame->leds_per_column);
    printf("RGB bytes          : %llu\n",
           (unsigned long long)rgb_size);
    printf("Total packet bytes : %llu\n",
           (unsigned long long)(rgb_size + 2u));

    return 0;
}