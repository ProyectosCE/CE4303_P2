#ifndef POV_DRIVER_H
#define POV_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#define POV_DRIVER_DEFAULT_DEVICE "/dev/raspberry_usb"

#define POV_NUM_LEDS 9
#define POV_RGB_BYTES 3
#define POV_COLUMN_BYTES (POV_NUM_LEDS * POV_RGB_BYTES)

#ifdef __cplusplus
extern "C" {
#endif

int pov_driver_open(const char *device_path);

void pov_driver_close(int fd);

size_t pov_driver_packet_size(size_t columns);

int pov_driver_build_packet(
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns,
    uint8_t **out_packet,
    size_t *out_len
);

int pov_driver_send_raw(
    int fd,
    const uint8_t *packet,
    size_t packet_len
);

int pov_driver_send_packet(
    int fd,
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns
);

int pov_driver_send_text(
    int fd,
    const char *text
);

#ifdef __cplusplus
}
#endif

#endif