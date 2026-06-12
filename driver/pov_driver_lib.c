#include "pov_driver_lib.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t *data, size_t len)
{
    size_t written = 0;

    while (written < len) {
        ssize_t n;

        if (len - written > SSIZE_MAX)
            n = write(fd, data + written, SSIZE_MAX);
        else
            n = write(fd, data + written, len - written);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -errno;
        }

        if (n == 0)
            return -EIO;

        written += (size_t)n;
    }

    return 0;
}

int pov_driver_open(const char *device_path)
{
    int fd;

    if (!device_path)
        device_path = POV_DRIVER_DEFAULT_DEVICE;

    fd = open(device_path, O_WRONLY);
    if (fd < 0)
        return -errno;

    return fd;
}

void pov_driver_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

size_t pov_driver_packet_size(size_t columns)
{
    return 2 + columns * POV_COLUMN_BYTES;
}

int pov_driver_build_packet(
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns,
    uint8_t **out_packet,
    size_t *out_len
)
{
    size_t rgb_len;
    size_t packet_len;
    uint8_t *packet;

    if (!rgb || !out_packet || !out_len)
        return -EINVAL;

    if (columns == 0)
        return -EINVAL;

    if (columns > (SIZE_MAX - 2) / POV_COLUMN_BYTES)
        return -EOVERFLOW;

    rgb_len = columns * POV_COLUMN_BYTES;
    packet_len = 2 + rgb_len;

    packet = malloc(packet_len);
    if (!packet)
        return -ENOMEM;

    packet[0] = (uint8_t)((velocidad_ms >> 8) & 0xFF);
    packet[1] = (uint8_t)(velocidad_ms & 0xFF);

    memcpy(packet + 2, rgb, rgb_len);

    *out_packet = packet;
    *out_len = packet_len;

    return 0;
}

int pov_driver_send_raw(
    int fd,
    const uint8_t *packet,
    size_t packet_len
)
{
    if (fd < 0 || !packet || packet_len == 0)
        return -EINVAL;

    return write_all(fd, packet, packet_len);
}

int pov_driver_send_packet(
    int fd,
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns
)
{
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    int ret;

    ret = pov_driver_build_packet(
        velocidad_ms,
        rgb,
        columns,
        &packet,
        &packet_len
    );

    if (ret < 0)
        return ret;

    ret = pov_driver_send_raw(fd, packet, packet_len);

    free(packet);
    return ret;
}

int pov_driver_send_text(int fd, const char *text)
{
    if (fd < 0 || !text)
        return -EINVAL;

    return pov_driver_send_raw(
        fd,
        (const uint8_t *)text,
        strlen(text)
    );
}