/*
Nombre archivo: mi_cliente.c
Que hace: Programa de ejemplo que abre el dispositivo del driver POV
(/dev/raspberry_usb), construye una columna RGB con los 9 LEDs en rojo,
y la envía como un único paquete usando la librería pov_driver_lib.
*/

#include "pov_driver_lib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
Nombre de funcion: main
Que hace:
  - Construye un buffer RGB de 1 columna (9 LEDs * 3 bytes = 27 bytes)
    con todos los LEDs en color rojo (255,0,0).
  - Abre el driver POV, envía el paquete (velocidad 4ms) y lo cierra.
Que recibe: Nada (no usa argc/argv).
Que retorna:
  - 0 si el paquete se envió correctamente.
  - 1 si falla la apertura del driver o el envío del paquete.
*/
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