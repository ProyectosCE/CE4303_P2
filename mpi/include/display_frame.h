#ifndef DISPLAY_FRAME_H
#define DISPLAY_FRAME_H

#include <stdint.h>
#include <stddef.h>

#define PICO_LED_COUNT 9

#define COLOR_ON_R   0
#define COLOR_ON_G   0
#define COLOR_ON_B   30

#define COLOR_OFF_R  0
#define COLOR_OFF_G  0
#define COLOR_OFF_B  0

typedef struct {
    uint32_t on_count;
    uint32_t total_count;
} DisplayAccum;

typedef struct {
    uint32_t columns;
    uint32_t leds_per_column;
    uint8_t *rgb;
} DisplayFrame;

uint64_t display_accum_count(uint32_t width);

uint64_t display_accum_size_bytes(uint32_t width);

void display_accum_merge(
    DisplayAccum *destination,
    const DisplayAccum *source,
    uint32_t width
);

int display_frame_build_from_accum(
    uint32_t width,
    const DisplayAccum *accum,
    DisplayFrame *frame
);

void display_frame_free(DisplayFrame *frame);

#endif