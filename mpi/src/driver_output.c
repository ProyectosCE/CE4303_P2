#include "driver_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "pov_driver.h"

int driver_output_write_and_optionally_send(
    const char *output_filename,
    uint32_t velocidad_ms,
    const DisplayFrame *frame,
    const char *device_path,
    int send_to_driver
) {
    if (frame == NULL || frame->rgb == NULL) {
        printf("ERROR: frame inválido.\n");
        return -1;
    }

    if (velocidad_ms > 65535u) {
        printf("ERROR: velocidad_ms=%u no cabe en uint16_t.\n", velocidad_ms);
        return -1;
    }

    uint8_t *packet = NULL;
    size_t packet_len = 0;

    int build_result =
        pov_driver_build_packet(
            (uint16_t)velocidad_ms,
            frame->rgb,
            frame->columns,
            &packet,
            &packet_len
        );

    if (build_result < 0) {
        printf("ERROR: pov_driver_build_packet falló.\n");
        return -1;
    }

    FILE *fp =
        fopen(output_filename, "wb");

    if (fp == NULL) {
        perror("fopen driver_packet.bin");
        free(packet);
        return -1;
    }

    if (fwrite(packet, 1, packet_len, fp) != packet_len) {
        perror("fwrite driver_packet.bin");
        fclose(fp);
        free(packet);
        return -1;
    }

    fclose(fp);

    printf("\n[POV DRIVER OUTPUT]\n");
    printf("Archivo generado   : %s\n", output_filename);
    printf("Speed ms           : %u\n", velocidad_ms);
    printf("Columnas           : %u\n", frame->columns);
    printf("LEDs por columna   : %u\n", frame->leds_per_column);
    printf("RGB bytes          : %llu\n",
           (unsigned long long)(
               (uint64_t)frame->columns *
               (uint64_t)frame->leds_per_column *
               3u
           ));
    printf("Total packet bytes : %llu\n",
           (unsigned long long)packet_len);

    if (send_to_driver) {
        const char *path =
            device_path != NULL
                ? device_path
                : POV_DRIVER_DEFAULT_DEVICE;

        printf("\n[POV DRIVER SEND]\n");
        printf("Device             : %s\n", path);

        int fd =
            pov_driver_open(path);

        if (fd < 0) {
            printf("ERROR: no se pudo abrir el driver %s\n", path);
            free(packet);
            return -1;
        }

        int send_result =
            pov_driver_send_raw(
                fd,
                packet,
                packet_len
            );

        pov_driver_close(fd);

        if (send_result < 0) {
            printf("ERROR: pov_driver_send_raw falló.\n");
            free(packet);
            return -1;
        }

        printf("Paquete enviado correctamente al driver.\n");
    } else {
        printf("\n[POV DRIVER SEND]\n");
        printf("Envío desactivado. Solo se generó driver_packet.bin.\n");
    }

    free(packet);

    return 0;
}