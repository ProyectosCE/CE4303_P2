#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_USB_INTERFACE "usb0"
#define DEFAULT_PICO_PORT 5001

#define CUSTOM_ETHERTYPE 0x88B5

#define POV_FRAME_MAGIC 0x504F5631U /* ASCII: POV1 */
#define POV_FRAME_TYPE_DATA 1
#define POV_FRAME_TYPE_END  2

#define NUM_LEDS 9
#define RGB_BYTES 3
#define COLUMN_BYTES (NUM_LEDS * RGB_BYTES)

#define MAX_FRAME_SIZE 2048
#define MAX_POV_PACKET_SIZE (512 * 1024)

struct pov_frame_header {
    uint32_t magic_be;
    uint8_t type;
    uint16_t length_be;
} __attribute__((packed));

struct dynamic_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

static void buffer_reset(struct dynamic_buffer *buffer)
{
    buffer->size = 0;
}

static void buffer_free(struct dynamic_buffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

static int buffer_append(struct dynamic_buffer *buffer, const uint8_t *data, size_t len)
{
    uint8_t *new_data;
    size_t new_capacity;

    if (len == 0)
        return 0;

    if (buffer->size + len > MAX_POV_PACKET_SIZE) {
        fprintf(stderr, "[Error] Paquete POV excede el máximo permitido: %d bytes\n",
                MAX_POV_PACKET_SIZE);
        return -1;
    }

    if (buffer->size + len <= buffer->capacity) {
        memcpy(buffer->data + buffer->size, data, len);
        buffer->size += len;
        return 0;
    }

    new_capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;

    while (new_capacity < buffer->size + len)
        new_capacity *= 2;

    new_data = realloc(buffer->data, new_capacity);
    if (!new_data) {
        perror("[Error] realloc");
        return -1;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;

    memcpy(buffer->data + buffer->size, data, len);
    buffer->size += len;

    return 0;
}

static int open_raw_receiver(const char *interface_name)
{
    int sockfd;
    struct ifreq ifr;
    struct sockaddr_ll sll;

    sockfd = socket(AF_PACKET, SOCK_RAW, htons(CUSTOM_ETHERTYPE));
    if (sockfd < 0) {
        perror("[Error] socket(AF_PACKET)");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);

    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        perror("[Error] SIOCGIFINDEX");
        close(sockfd);
        return -1;
    }

    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(CUSTOM_ETHERTYPE);

    if (bind(sockfd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("[Error] bind(AF_PACKET)");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static int send_all(int sockfd, const uint8_t *data, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(sockfd, data + sent, len - sent, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("[Error] send");
            return -1;
        }

        if (n == 0) {
            fprintf(stderr, "[Error] send retornó 0\n");
            return -1;
        }

        sent += (size_t)n;
    }

    return 0;
}

static int forward_to_pico(const char *pico_ip, uint16_t pico_port, const uint8_t *data, size_t len)
{
    int sockfd;
    struct sockaddr_in addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[Error] socket(AF_INET)");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(pico_port);

    if (inet_pton(AF_INET, pico_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "[Error] IP de Pico inválida: %s\n", pico_ip);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Error] connect hacia Pico");
        close(sockfd);
        return -1;
    }

    if (send_all(sockfd, data, len) < 0) {
        close(sockfd);
        return -1;
    }

    /*
     * La Pico lee hasta que recv() retorna vacío.
     * shutdown(SHUT_WR) ayuda a indicar fin de transmisión.
     */
    shutdown(sockfd, SHUT_WR);
    close(sockfd);

    return 0;
}

static int validate_pico_packet(const uint8_t *data, size_t len)
{
    size_t matrix_len;
    size_t columns;
    uint16_t speed_ms;

    if (len < 2 + COLUMN_BYTES) {
        fprintf(stderr,
                "[Error] Paquete demasiado pequeño. Mínimo: %d bytes, recibido: %zu\n",
                2 + COLUMN_BYTES,
                len);
        return -1;
    }

    matrix_len = len - 2;

    if (matrix_len % COLUMN_BYTES != 0) {
        fprintf(stderr,
                "[Error] Tamaño de matriz inválido. len-2 debe ser múltiplo de %d. Recibido: %zu\n",
                COLUMN_BYTES,
                matrix_len);
        return -1;
    }

    speed_ms = ((uint16_t)data[0] << 8) | data[1];
    columns = matrix_len / COLUMN_BYTES;

    printf("[OK] Paquete compatible con Pico\n");
    printf("     Velocidad: %u ms\n", speed_ms);
    printf("     Columnas:  %zu\n", columns);
    printf("     LEDs/col:  %d\n", NUM_LEDS);
    printf("     Total:     %zu bytes\n", len);

    return 0;
}

static int process_raw_frame(
    const uint8_t *frame,
    ssize_t frame_size,
    struct dynamic_buffer *rx_buffer,
    const char *pico_ip,
    uint16_t pico_port
)
{
    const struct ethhdr *eth;
    const uint8_t *payload;
    size_t payload_len;
    struct pov_frame_header header;
    uint32_t magic;
    uint16_t chunk_len;

    if (frame_size < ETH_HLEN + (ssize_t)sizeof(struct pov_frame_header))
        return 0;

    eth = (const struct ethhdr *)frame;

    if (ntohs(eth->h_proto) != CUSTOM_ETHERTYPE)
        return 0;

    payload = frame + ETH_HLEN;
    payload_len = (size_t)frame_size - ETH_HLEN;

    memcpy(&header, payload, sizeof(header));

    magic = ntohl(header.magic_be);
    chunk_len = ntohs(header.length_be);

    if (magic != POV_FRAME_MAGIC) {
        fprintf(stderr, "[Warn] Frame con EtherType correcto pero magic inválido\n");
        return 0;
    }

    if (sizeof(struct pov_frame_header) + chunk_len > payload_len) {
        fprintf(stderr,
                "[Warn] Frame incompleto. payload_len=%zu, chunk_len=%u\n",
                payload_len,
                chunk_len);
        return 0;
    }

    payload += sizeof(struct pov_frame_header);

    if (header.type == POV_FRAME_TYPE_DATA) {
        if (buffer_append(rx_buffer, payload, chunk_len) < 0) {
            buffer_reset(rx_buffer);
            return -1;
        }

        printf("[RX] Fragmento DATA: %u bytes. Acumulado: %zu bytes\n",
               chunk_len,
               rx_buffer->size);

        return 0;
    }

    if (header.type == POV_FRAME_TYPE_END) {
        printf("[RX] Frame END recibido. Paquete completo: %zu bytes\n", rx_buffer->size);

        if (validate_pico_packet(rx_buffer->data, rx_buffer->size) == 0) {
            if (forward_to_pico(pico_ip, pico_port, rx_buffer->data, rx_buffer->size) == 0) {
                printf("[OK] Paquete reenviado a Pico %s:%u\n", pico_ip, pico_port);
            } else {
                fprintf(stderr, "[Error] No se pudo reenviar a la Pico\n");
            }
        } else {
            fprintf(stderr, "[Error] Paquete descartado por formato incompatible\n");
        }

        buffer_reset(rx_buffer);
        return 0;
    }

    fprintf(stderr, "[Warn] Tipo de frame desconocido: %u\n", header.type);
    return 0;
}

int main(int argc, char **argv)
{
    const char *interface_name = DEFAULT_USB_INTERFACE;
    const char *pico_ip;
    uint16_t pico_port = DEFAULT_PICO_PORT;
    int raw_sock;
    uint8_t frame[MAX_FRAME_SIZE];
    struct dynamic_buffer rx_buffer = {0};

    if (argc < 2) {
        fprintf(stderr, "Uso:\n");
        fprintf(stderr, "  sudo %s <pico_ip> [interfaz_usb] [puerto]\n", argv[0]);
        fprintf(stderr, "\nEjemplo:\n");
        fprintf(stderr, "  sudo %s 192.168.1.80 usb0 5001\n", argv[0]);
        return 1;
    }

    pico_ip = argv[1];

    if (argc >= 3)
        interface_name = argv[2];

    if (argc >= 4) {
        int port = atoi(argv[3]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "[Error] Puerto inválido: %s\n", argv[3]);
            return 1;
        }
        pico_port = (uint16_t)port;
    }

    printf("RASPBERRY PI 4: receptor puente USB-Ethernet -> Pico TCP\n");
    printf("Interfaz USB: %s\n", interface_name);
    printf("Pico destino: %s:%u\n", pico_ip, pico_port);
    printf("EtherType:    0x%04X\n", CUSTOM_ETHERTYPE);

    raw_sock = open_raw_receiver(interface_name);
    if (raw_sock < 0)
        return 1;

    printf("Escuchando frames POV...\n");

    while (1) {
        ssize_t frame_size = recvfrom(raw_sock, frame, sizeof(frame), 0, NULL, NULL);

        if (frame_size < 0) {
            if (errno == EINTR)
                continue;

            perror("[Error] recvfrom");
            break;
        }

        process_raw_frame(frame, frame_size, &rx_buffer, pico_ip, pico_port);
    }

    buffer_free(&rx_buffer);
    close(raw_sock);

    return 0;
}