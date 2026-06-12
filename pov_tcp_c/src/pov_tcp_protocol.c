#include "pov_tcp_protocol.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

uint32_t pov_read_u32_be(const uint8_t *buffer) {
    return
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8)  |
        ((uint32_t)buffer[3]);
}

uint64_t pov_read_u64_be(const uint8_t *buffer) {
    uint64_t high = pov_read_u32_be(buffer);
    uint64_t low = pov_read_u32_be(buffer + 4);
    return (high << 32) | low;
}

void pov_write_u32_be(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)((value >> 24) & 0xFFu);
    buffer[1] = (uint8_t)((value >> 16) & 0xFFu);
    buffer[2] = (uint8_t)((value >> 8) & 0xFFu);
    buffer[3] = (uint8_t)(value & 0xFFu);
}

void pov_write_u64_be(uint8_t *buffer, uint64_t value) {
    pov_write_u32_be(buffer, (uint32_t)((value >> 32) & 0xFFFFFFFFu));
    pov_write_u32_be(buffer + 4, (uint32_t)(value & 0xFFFFFFFFu));
}

int send_all(int fd, const void *buffer, size_t length) {
    const uint8_t *ptr = (const uint8_t *)buffer;
    size_t sent_total = 0;

    while (sent_total < length) {
        ssize_t sent = send(fd, ptr + sent_total, length - sent_total, 0);

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        sent_total += (size_t)sent;
    }

    return 0;
}

int recv_all(int fd, void *buffer, size_t length) {
    uint8_t *ptr = (uint8_t *)buffer;
    size_t received_total = 0;

    while (received_total < length) {
        ssize_t received = recv(fd, ptr + received_total, length - received_total, 0);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (received == 0) {
            return -1;
        }

        received_total += (size_t)received;
    }

    return 0;
}
