/*
Nombre archivo: pov_send_demo.c
Que hace: Programa de demostración que genera un dibujo de 9x9 (corazón
rosa sobre fondo celeste, aunque también incluye funciones para dibujar
un círculo y una casa) y lo envía al driver POV como un paquete de 9
columnas. La velocidad (en ms) puede pasarse como argumento.
*/

#include "pov_driver_lib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DRAWING_SIZE 9 

/*
Nombre de funcion: set_pixel
Que hace: Escribe el color (r,g,b) de un pixel específico (columna, led)
dentro del buffer plano "rgb", calculando su posición usando
POV_COLUMN_BYTES y POV_RGB_BYTES.
Que recibe:
  - rgb: buffer de salida (columns * POV_COLUMN_BYTES bytes).
  - column: índice de columna (eje X) donde escribir.
  - led: índice de LED dentro de la columna (eje Y, 0..POV_NUM_LEDS-1).
  - r, g, b: componentes de color a escribir.
Que retorna: Nada (void). Modifica "rgb" en el lugar correspondiente.
*/
static void set_pixel(uint8_t *rgb, size_t column, size_t led, uint8_t r, uint8_t g, uint8_t b)
{
    size_t base = column * POV_COLUMN_BYTES + led * POV_RGB_BYTES;

    rgb[base + 0] = r;
    rgb[base + 1] = g;
    rgb[base + 2] = b;
}

/*
Nombre de funcion: fill_circle_blue_background_red
Que hace: Llena el buffer "rgb" con un dibujo de 9x9: un círculo azul
de radio 4 centrado en (4,4), sobre un fondo rojo. Usa la ecuación
dx^2+dy^2 <= radio^2 para decidir si un pixel pertenece al círculo.
Que recibe:
  - rgb: buffer de salida (columns * POV_COLUMN_BYTES bytes).
  - columns: cantidad de columnas a recorrer (eje X).
Que retorna: Nada (void). No se usa en el main actual (queda disponible
como alternativa de dibujo).
*/
static void fill_circle_blue_background_red(uint8_t *rgb, size_t columns)
{
    /*
     * Dibujo 9x9:
     *
     * R = fondo rojo
     * B = círculo azul
     *
     * R R R B B B R R R
     * R R B B B B B R R
     * R B B B B B B B R
     * B B B B B B B B B
     * B B B B B B B B B
     * B B B B B B B B B
     * R B B B B B B B R
     * R R B B B B B R R
     * R R R B B B R R R
     *
     * column = eje X
     * led    = eje Y
     */

    const int center = 4;
    const int radius_squared = 16; /* radio 4 */

    for (size_t column = 0; column < columns; column++) {
        for (size_t led = 0; led < POV_NUM_LEDS; led++) {
            int dx = (int)column - center;
            int dy = (int)led - center;
            int distance_squared = dx * dx + dy * dy;

            if (distance_squared <= radius_squared) {
                /* Azul */
                set_pixel(rgb, column, led, 0, 0, 255);
            } else {
                /* Rojo */
                set_pixel(rgb, column, led, 255, 0, 0);
            }
        }
    }
}

/*
Nombre de funcion: fill_house_blue_green_roof_red_background
Que hace: Llena el buffer "rgb" con un dibujo de 9x9: una "casa" con
techo triangular verde (filas 0-3, ancho creciente hacia abajo) y
cuerpo azul (filas 4-8, columnas 2-6), sobre fondo rojo.
Que recibe:
  - rgb: buffer de salida (columns * POV_COLUMN_BYTES bytes).
  - columns: cantidad de columnas a recorrer (eje X).
Que retorna: Nada (void). No se usa en el main actual (queda disponible
como alternativa de dibujo).
*/
static void fill_house_blue_green_roof_red_background(uint8_t *rgb, size_t columns)
{
    /*
     * Dibujo 9x9:
     *
     * R = fondo rojo
     * G = techo verde
     * B = casa azul
     *
     * R R R R G R R R R
     * R R R G G G R R R
     * R R G G G G G R R
     * R G G G G G G G R
     * R R B B B B B R R
     * R R B B B B B R R
     * R R B B B B B R R
     * R R B B B B B R R
     * R R B B B B B R R
     *
     * column = eje X
     * led    = eje Y
     */

    for (size_t column = 0; column < columns; column++) {
        for (size_t led = 0; led < POV_NUM_LEDS; led++) {

            /* Fondo rojo por defecto */
            uint8_t r = 255;
            uint8_t g = 0;
            uint8_t b = 0;

            /*
             * Techo verde triangular.
             * led 0: solo columna 4
             * led 1: columnas 3..5
             * led 2: columnas 2..6
             * led 3: columnas 1..7
             */
            if (led <= 3) {
                int left = 4 - (int)led;
                int right = 4 + (int)led;

                if ((int)column >= left && (int)column <= right) {
                    r = 0;
                    g = 255;
                    b = 0;
                }
            }

            /*
             * Cuerpo azul de la casa.
             * Filas 4..8, columnas 2..6.
             */
            if (led >= 4 && column >= 2 && column <= 6) {
                r = 0;
                g = 0;
                b = 255;
            }

            set_pixel(rgb, column, led, r, g, b);
        }
    }
}

/*
Nombre de funcion: fill_heart_pink_sky_background
Que hace: Llena el buffer "rgb" con un dibujo de 9x9: un corazón rosa
sobre fondo celeste, definido mediante una máscara de caracteres
("heart"), donde '#' = rosa y '.' = celeste. Esta es la figura usada
por el main.
Que recibe:
  - rgb: buffer de salida (columns * POV_COLUMN_BYTES bytes).
  - columns: cantidad de columnas a recorrer (debe ser <= DRAWING_SIZE,
    ya que "heart" tiene DRAWING_SIZE caracteres por fila).
Que retorna: Nada (void).
*/
static void fill_heart_pink_sky_background(uint8_t *rgb, size_t columns)
{
    /*
     * Dibujo 9x9:
     *
     * C = fondo celeste
     * P = corazón rosa
     *
     * C P P C C C P P C
     * P P P P C P P P P
     * P P P P P P P P P
     * P P P P P P P P P
     * C P P P P P P P C
     * C C P P P P P C C
     * C C C P P P C C C
     * C C C C P C C C C
     * C C C C C C C C C
     *
     * column = eje X
     * led    = eje Y
     */

    const char *heart[DRAWING_SIZE] = {
        ".##...##.",
        "####.####",
        "#########",
        "#########",
        ".#######.",
        "..#####..",
        "...###...",
        "....#....",
        "........."
    };

    for (size_t led = 0; led < POV_NUM_LEDS; led++) {
        for (size_t column = 0; column < columns; column++) {

            if (heart[led][column] == '#') {
                /* Rosa */
                set_pixel(rgb, column, led, 255, 0, 180);
            } else {
                /* Celeste */
                set_pixel(rgb, column, led, 0, 180, 255);
            }
        }
    }
}

/*
Nombre de funcion: main
Que hace:
  - Reserva un buffer RGB de DRAWING_SIZE columnas y lo llena con el
    dibujo del corazón (fill_heart_pink_sky_background).
  - Abre el driver POV y envía el buffer como un paquete con la
    velocidad indicada (por defecto 0, o argv[1] si se proporciona).
  - Imprime un resumen del envío.
Que recibe:
  - argc, argv: argv[1] opcional, velocidad en ms (entero).
Que retorna:
  - 0 si el dibujo se envió correctamente.
  - 1 si falla malloc, la apertura del driver o el envío del paquete.
*/
int main(int argc, char **argv)
{
    uint16_t velocidad_ms = 0.01;
    size_t columns = DRAWING_SIZE;
    uint8_t *rgb;
    int fd;
    int ret;

    /*
     * Para este dibujo, lo normal es usar 9 columnas.
     * Pero se permite cambiar velocidad desde argv[1].
     *
     * Uso:
     *   ./pov_send_demo
     *   ./pov_send_demo 10
     *
     * Donde 10 sería velocidad_ms.
     */
    if (argc >= 2)
        velocidad_ms = (uint16_t)strtoul(argv[1], NULL, 10);

    rgb = malloc(columns * POV_COLUMN_BYTES);
    if (!rgb) {
        perror("malloc");
        return 1;
    }

    fill_heart_pink_sky_background(rgb, columns);

    fd = pov_driver_open(NULL);
    if (fd < 0) {
        fprintf(stderr, "No se pudo abrir el driver. errno=%d\n", -fd);
        free(rgb);
        return 1;
    }

    ret = pov_driver_send_packet(fd, velocidad_ms, rgb, columns);

    pov_driver_close(fd);
    free(rgb);

    if (ret < 0) {
        fprintf(stderr, "Error enviando al driver. errno=%d\n", -ret);
        return 1;
    }

    printf("Dibujo 9x9 enviado correctamente.\n");
    printf("Figura: círculo azul sobre fondo rojo\n");
    printf("Columnas: %zu\n", columns);
    printf("Velocidad: %u ms\n", velocidad_ms);

    return 0;
}