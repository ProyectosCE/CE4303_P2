#include "display_frame.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint64_t display_accum_count(uint32_t width) {
    return (uint64_t)width * (uint64_t)PICO_LED_COUNT;
}

uint64_t display_accum_size_bytes(uint32_t width) {
    return display_accum_count(width) * sizeof(DisplayAccum);
}

void display_accum_merge(
    DisplayAccum *destination,
    const DisplayAccum *source,
    uint32_t width
) {
    uint64_t count = display_accum_count(width);

    for (uint64_t i = 0; i < count; i++) {
        destination[i].on_count += source[i].on_count;
        destination[i].total_count += source[i].total_count;
    }
}

int display_frame_build_from_accum(
    uint32_t width,
    const DisplayAccum *accum,
    DisplayFrame *frame
) {
    frame->columns = width;
    frame->leds_per_column = PICO_LED_COUNT;

    uint64_t rgb_size =
        (uint64_t)width *
        (uint64_t)PICO_LED_COUNT *
        3u;

    frame->rgb = malloc(rgb_size);

    if (frame->rgb == NULL) {
        printf("ERROR: malloc display frame falló.\n");
        return -1;
    }

    memset(frame->rgb, 0, rgb_size);

    for (uint32_t col = 0; col < width; col++) {
        for (uint32_t led = 0; led < PICO_LED_COUNT; led++) {

            uint64_t accum_index =
                (uint64_t)col * PICO_LED_COUNT + led;

            uint64_t rgb_index =
                accum_index * 3u;

            uint32_t on_count =
                accum[accum_index].on_count;

            uint32_t total_count =
                accum[accum_index].total_count;

            /*
             * Si la mayoría de pixeles del bloque vertical estaban encendidos,
             * encendemos ese LED de la columna.
             */
            int is_on =
                total_count > 0 &&
                ((uint64_t)on_count * 2u >= (uint64_t)total_count);

            if (is_on) {
                frame->rgb[rgb_index + 0] = COLOR_ON_R;
                frame->rgb[rgb_index + 1] = COLOR_ON_G;
                frame->rgb[rgb_index + 2] = COLOR_ON_B;
            } else {
                frame->rgb[rgb_index + 0] = COLOR_OFF_R;
                frame->rgb[rgb_index + 1] = COLOR_OFF_G;
                frame->rgb[rgb_index + 2] = COLOR_OFF_B;
            }
        }
    }

    return 0;
}

void display_frame_free(DisplayFrame *frame) {
    if (frame == NULL) {
        return;
    }

    free(frame->rgb);
    frame->rgb = NULL;
    frame->columns = 0;
    frame->leds_per_column = 0;
}