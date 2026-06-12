#include "pov_driver_lib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int fd;
    int ret;
    size_t columns = 1;
    uint16_t velocidad_ms = 4;

    /*
     * 1 columna * 9 LEDs * 3 bytes RGB = 27 bytes.
     * Esta columna enciende los 9 LEDs en rojo.
     */
    uint8_t rgb[POV_COLUMN_BYTES];

    for (size_t i = 0; i < POV_NUM_LEDS; i++) {
        size_t base = i * POV_RGB_BYTES;

        rgb[base + 0] = 255; /* R */
        rgb[base + 1] = 0;   /* G */
        rgb[base + 2] = 0;   /* B */
    }

    fd = pov_driver_open(NULL);
    if (fd < 0) {
        fprintf(stderr, "No se pudo abrir el driver. errno=%d\n", -fd);
        return 1;
    }

    ret = pov_driver_send_packet(fd, velocidad_ms, rgb, columns);

    pov_driver_close(fd);

    if (ret < 0) {
        fprintf(stderr, "Error enviando paquete. errno=%d\n", -ret);
        return 1;
    }

    printf("Paquete enviado correctamente.\n");
    return 0;
}