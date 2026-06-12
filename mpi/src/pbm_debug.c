#include "pbm_debug.h"

#include <stdio.h>


/*
Archivo: src/pbm_debug.c
Descripción:
Implementación de funciones auxiliares para generar archivos PBM de depuración. Estas funciones permiten guardar
imágenes en formato PBM para visualizar el resultado del descifrado de la imagen original, así como el resultado final que se envía al driver. Estas funciones son útiles para verificar que el procesamiento de la imagen se está realizando correctamente en cada etapa del pipeline.
*/


/*
Genera un archivo PBM que representa la imagen descifrada a partir del payload plano.
Recibe:
- filename: ruta al archivo PBM que se generará.
- width: ancho de la imagen en píxeles.
- height: alto de la imagen en píxeles.
*/
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

int pbm_debug_write_driver_frame(
    const char *filename,
    const DisplayFrame *frame
) {
    if (frame == NULL || frame->rgb == NULL) {
        printf("ERROR: frame inválido para PBM de salida.\n");
        return -1;
    }

    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        perror("fopen resultado_mpi_driver.pbm");
        return -1;
    }

    /*
     * PBM final enviado al driver:
     *
     * 1 = negro del PBM = rojo en hardware
     * 0 = blanco del PBM = fondo cian/blanco en hardware
     */
    fprintf(fp, "P1\n");
    fprintf(fp, "%u %u\n", frame->columns, frame->leds_per_column);

    for (uint32_t led = 0; led < frame->leds_per_column; led++) {

        for (uint32_t col = 0; col < frame->columns; col++) {

            uint64_t rgb_index =
                (
                    (uint64_t)col *
                    (uint64_t)frame->leds_per_column +
                    (uint64_t)led
                ) * 3u;

            uint8_t r =
                frame->rgb[rgb_index + 0];

            uint8_t g =
                frame->rgb[rgb_index + 1];

            uint8_t b =
                frame->rgb[rgb_index + 2];

            uint8_t bit =
                (
                    r == PBM_FOREGROUND_R &&
                    g == PBM_FOREGROUND_G &&
                    b == PBM_FOREGROUND_B
                ) ? 1u : 0u;

            fprintf(fp, "%u", bit);

            if (col + 1 < frame->columns) {
                fputc(' ', fp);
            }
        }

        fputc('\n', fp);
    }

    fclose(fp);

    printf("\n[RESULT PBM]\n");
    printf("Resultado MPI guardado en : %s\n", filename);
    printf("Columnas                 : %u\n", frame->columns);
    printf("LEDs por columna         : %u\n", frame->leds_per_column);
    printf("Formato                  : PBM P1\n");
    printf("PBM 1                    : rojo en hardware\n");
    printf("PBM 0                    : fondo cian/blanco en hardware\n");

    return 0;
}