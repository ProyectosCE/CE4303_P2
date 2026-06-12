/*
Nombre archivo: receptor.c

Que hace: Programa de espacio de usuario para la Raspberry Pi 4 que actúa
como puente USB-Ethernet <-> Pico. Abre un socket AF_PACKET crudo sobre
una interfaz USB-Ethernet, escucha tramas con EtherType custom (0x88B5)
generadas por mi_driver.c, reensambla los fragmentos (tipo DATA) según el
header pov_frame_header hasta recibir un frame tipo END, y según el modo
de operación: reenvía el mensaje completo a la Pico por TCP (modo "normal"),
lo muestra como texto (modo "txt"), o lo muestra en hexadecimal (modo "frame").
*/

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
#include <ctype.h>

#define DEFAULT_USB_INTERFACE "usb0" // Interfaz por defecto si no se especifica
#define DEFAULT_PICO_PORT 5001 // Puerto TCP por defecto de la Pico

#define CUSTOM_ETHERTYPE 0x88B5

#define POV_FRAME_MAGIC 0x504F5631U /* ASCII: POV1 */
#define POV_FRAME_TYPE_DATA 1
#define POV_FRAME_TYPE_END  2

#define NUM_LEDS 9
#define RGB_BYTES 3
#define COLUMN_BYTES (NUM_LEDS * RGB_BYTES) // 27 bytes por columna

#define MAX_FRAME_SIZE 2048  // Tamaño máximo de una trama Ethernet recibida
#define MAX_POV_PACKET_SIZE (512 * 1024) // Límite máximo del mensaje reensamblado

// Header que antecede a cada chunk de datos enviado por mi_driver.c (debe coincidir con
// "struct pov_frame_header" del driver, pero aquí se leen los campos como big-endian explícitos).
struct pov_frame_header {
    uint32_t magic_be;
    uint8_t type;
    uint16_t length_be;
} __attribute__((packed));

// Buffer dinámico (tipo "vector" de bytes) usado para reensamblar el mensaje completo
// a partir de los fragmentos DATA recibidos.
struct dynamic_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

// Modos de operación del receptor (ver parse_config / print_usage)
enum receiver_mode {
    MODE_NORMAL = 1, // Reenvía el mensaje reensamblado a la Pico por TCP
    MODE_TEST_TXT, // Interpreta el mensaje como texto y lo imprime
    MODE_TEST_FRAME // Imprime el mensaje en hexadecimal y valida formato POV
};

// Configuración derivada de los argumentos de línea de comandos
struct receiver_config {
    enum receiver_mode mode;
    const char *interface_name;
    const char *pico_ip;
    uint16_t pico_port;
};

/*
Nombre de funcion: print_usage
Que hace: Imprime por stderr las distintas formas de invocar el programa
(modo normal, txt, frame) con ejemplos.
Que recibe:
  - program_name: nombre del ejecutable (argv[0]), usado en los ejemplos.
Que retorna: Nada (void).
*/
static void print_usage(const char *program_name)
{
    fprintf(stderr, "Uso:\n");
    fprintf(stderr, "  sudo %s normal <IP_PICO> [interfaz_usb] [puerto]\n", program_name);
    fprintf(stderr, "  sudo %s txt [interfaz_usb]\n", program_name);
    fprintf(stderr, "  sudo %s frame [interfaz_usb]\n", program_name);
    fprintf(stderr, "\nEjemplos:\n");
    fprintf(stderr, "  sudo %s normal 192.168.1.80 usb0 5001\n", program_name);
    fprintf(stderr, "  sudo %s txt usb0\n", program_name);
    fprintf(stderr, "  sudo %s frame usb0\n", program_name);
}

/*
Nombre de funcion: mode_to_string
Que hace: Convierte un valor de enum receiver_mode a una descripción
legible en español, para mostrarla en los mensajes de estado.
Que recibe:
  - mode: modo de operación actual.
Que retorna: cadena constante con el nombre del modo ("normal",
"prueba txt", "prueba frame" o "desconocido").
*/
static const char *mode_to_string(enum receiver_mode mode)
{
    switch (mode) {
        case MODE_NORMAL:
            return "normal";
        case MODE_TEST_TXT:
            return "prueba txt";
        case MODE_TEST_FRAME:
            return "prueba frame";
        default:
            return "desconocido";
    }
}

/*
Nombre de funcion: buffer_reset
Que hace: "Vacía" el buffer dinámico poniendo su tamaño en 0, sin liberar
la memoria reservada (capacity se mantiene, para reutilizar el buffer
en el siguiente mensaje).
Que recibe:
  - buffer: buffer dinámico a reiniciar.
Que retorna: Nada (void).
*/
static void buffer_reset(struct dynamic_buffer *buffer)
{
    buffer->size = 0;
}

/*
Nombre de funcion: buffer_free
Que hace: Libera la memoria reservada por el buffer dinámico y reinicia
todos sus campos a 0/NULL.
Que recibe:
  - buffer: buffer dinámico a liberar.
Que retorna: Nada (void).
*/
static void buffer_free(struct dynamic_buffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

/*
Nombre de funcion: buffer_append
Que hace: Agrega "len" bytes de "data" al final del buffer dinámico,
creciendo (duplicando) la capacidad con realloc cuando es necesario.
Rechaza el append si el tamaño resultante excedería MAX_POV_PACKET_SIZE.
Que recibe:
  - buffer: buffer dinámico destino.
  - data: bytes a agregar.
  - len: cantidad de bytes a agregar (si es 0, no hace nada y retorna 0).
Que retorna:
  - 0 si los datos se agregaron correctamente.
  - -1 si el resultado excedería MAX_POV_PACKET_SIZE o si realloc falla.
*/
static int buffer_append(struct dynamic_buffer *buffer, const uint8_t *data, size_t len)
{
    uint8_t *new_data;
    size_t new_capacity;

    if (len == 0)
        return 0;

    if (buffer->size + len > MAX_POV_PACKET_SIZE) {
        fprintf(stderr, "[Error] Paquete excede el máximo permitido: %d bytes\n",
                MAX_POV_PACKET_SIZE);
        return -1;
    }

    // Si ya hay espacio suficiente, solo se copia
    if (buffer->size + len <= buffer->capacity) {
        memcpy(buffer->data + buffer->size, data, len);
        buffer->size += len;
        return 0;
    }

    // Si no hay espacio, se duplica la capacidad (empezando en 4096) hasta que alcance
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

/*
Nombre de funcion: print_hex_dump
Que hace: Imprime "data" en formato hexdump clásico: filas de 16 bytes,
con el offset, los bytes en hex y su representación ASCII (o '.' si no
es imprimible).
Que recibe:
  - data: buffer a mostrar.
  - len: cantidad de bytes a mostrar.
Que retorna: Nada (void). Solo imprime por stdout.
*/
static void print_hex_dump(const uint8_t *data, size_t len)
{
    size_t i;
    size_t j;

    for (i = 0; i < len; i += 16) {
        printf("%08zx  ", i);

        for (j = 0; j < 16; j++) {
            if (i + j < len)
                printf("%02x ", data[i + j]);
            else
                printf("   ");
        }

        printf(" |");

        for (j = 0; j < 16 && i + j < len; j++) {
            unsigned char c = data[i + j];
            printf("%c", isprint(c) ? c : '.');
        }

        printf("|\n");
    }
}

/*
Nombre de funcion: print_text_payload
Que hace: Imprime "data" interpretándolo como texto: los caracteres
imprimibles y '\n','\r','\t' se imprimen tal cual; el resto se muestra
como escape hexadecimal (\xNN). Si el último byte no es '\n', agrega uno
para que el formato de salida quede prolijo.
Que recibe:
  - data: buffer con el texto recibido.
  - len: cantidad de bytes en "data".
Que retorna: Nada (void). Solo imprime por stdout, con encabezado y pie
delimitadores ("----- TEXTO RECIBIDO ... -----").
*/
static void print_text_payload(const uint8_t *data, size_t len)
{
    size_t i;

    printf("----- TEXTO RECIBIDO (%zu bytes) -----\n", len);

    for (i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (c == '\n' || c == '\r' || c == '\t') {
            putchar(c);
        } else if (isprint(c)) {
            putchar(c);
        } else {
            printf("\\x%02x", c);
        }
    }

    if (len == 0 || data[len - 1] != '\n')
        putchar('\n');

    printf("--------------------------------------\n");
}

/*
Nombre de funcion: open_raw_receiver
Que hace: Crea un socket AF_PACKET/SOCK_RAW filtrado por CUSTOM_ETHERTYPE,
obtiene el índice de la interfaz dada por nombre (SIOCGIFINDEX) y lo
enlaza (bind) a esa interfaz para recibir solo tramas de ese EtherType.
Que recibe:
  - interface_name: nombre de la interfaz de red (ej. "usb0").
Que retorna:
  - El descriptor del socket (>= 0) si todo fue exitoso.
  - -1 si falla socket(), ioctl(SIOCGIFINDEX) o bind() (en cada caso se
    imprime el error con perror y se cierra el socket si ya estaba abierto).
*/
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

/*
Nombre de funcion: send_all
Que hace: Envía exactamente "len" bytes por un socket TCP, llamando a
send() repetidamente hasta completar el buffer y reintentando si es
interrumpido por una señal (EINTR).
Que recibe:
  - sockfd: descriptor del socket conectado.
  - data: buffer a enviar.
  - len: cantidad total de bytes a enviar.
Que retorna:
  - 0 si se enviaron todos los bytes correctamente.
  - -1 si send() retornó error (se imprime con perror) o retornó 0
    (conexión cerrada inesperadamente).
*/
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

/*
Nombre de funcion: forward_to_pico
Que hace: Abre una conexión TCP nueva hacia la Pico (ip:puerto), envía el
buffer completo con send_all, hace shutdown de escritura (para que la
Pico detecte fin de datos con recv()==0) y cierra el socket.
Que recibe:
  - pico_ip: dirección IP de la Pico (texto, ej "192.168.1.80").
  - pico_port: puerto TCP de la Pico.
  - data: buffer con el mensaje completo a reenviar.
  - len: tamaño de "data" en bytes.
Que retorna:
  - 0 si el envío fue exitoso.
  - -1 si falla socket(), la IP es inválida (inet_pton), connect() falla,
    o falla send_all.
*/
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

    // shutdown(SHUT_WR) indica "fin de datos" al otro lado sin cerrar todavía el socket
    shutdown(sockfd, SHUT_WR);
    close(sockfd);

    return 0;
}

/*
Nombre de funcion: validate_pico_packet
Que hace: Valida que "data" tenga el formato esperado por el driver de la
Pico (main.py): al menos 2 + COLUMN_BYTES bytes, y que (len - 2) sea
múltiplo de COLUMN_BYTES (27). Si es válido, imprime un resumen
(velocidad, columnas, LEDs por columna, tamaño total).
Que recibe:
  - data: buffer del mensaje reensamblado.
  - len: tamaño de "data" en bytes.
Que retorna:
  - 0 si el paquete tiene un formato válido.
  - -1 si es demasiado pequeño o (len-2) no es múltiplo de COLUMN_BYTES
    (en ambos casos se imprime un mensaje de error).
*/
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

    // Velocidad codificada en los primeros 2 bytes, big-endian (igual que pov_driver_build_packet)
    speed_ms = ((uint16_t)data[0] << 8) | data[1];
    columns = matrix_len / COLUMN_BYTES;

    printf("[OK] Paquete compatible con Pico\n");
    printf("     Velocidad: %u ms\n", speed_ms);
    printf("     Columnas:  %zu\n", columns);
    printf("     LEDs/col:  %d\n", NUM_LEDS);
    printf("     Total:     %zu bytes\n", len);

    return 0;
}

/*
Nombre de funcion: process_complete_message
Que hace: Procesa un mensaje completo ya reensamblado (tras recibir un
frame END), según el modo configurado:
  - MODE_TEST_TXT: lo imprime como texto.
  - MODE_TEST_FRAME: lo imprime en hexadecimal y valida (sin reenviar).
  - MODE_NORMAL: lo valida y, si es válido, lo reenvía a la Pico por TCP.
En todos los casos, al terminar reinicia el buffer (buffer_reset) para
el siguiente mensaje.
Que recibe:
  - config: configuración actual (modo, IP/puerto de la Pico).
  - rx_buffer: buffer dinámico con el mensaje completo reensamblado.
Que retorna: Nada (void).
*/
static void process_complete_message(
    const struct receiver_config *config,
    struct dynamic_buffer *rx_buffer
)
{
    if (config->mode == MODE_TEST_TXT) {
        print_text_payload(rx_buffer->data, rx_buffer->size);
        buffer_reset(rx_buffer);
        return;
    }

    if (config->mode == MODE_TEST_FRAME) {
        printf("----- FRAME BINARIO RECONSTRUIDO (%zu bytes) -----\n", rx_buffer->size);
        print_hex_dump(rx_buffer->data, rx_buffer->size);

        /*
         * En modo prueba frame no se envía a la Pico.
         * Solo se intenta validar para informar si parece paquete POV válido.
         */
        validate_pico_packet(rx_buffer->data, rx_buffer->size);

        printf("--------------------------------------------------\n");

        buffer_reset(rx_buffer);
        return;
    }

    if (config->mode == MODE_NORMAL) {
        if (validate_pico_packet(rx_buffer->data, rx_buffer->size) == 0) {
            if (forward_to_pico(config->pico_ip,
                                config->pico_port,
                                rx_buffer->data,
                                rx_buffer->size) == 0) {
                printf("[OK] Paquete reenviado a Pico %s:%u\n",
                       config->pico_ip,
                       config->pico_port);
            } else {
                fprintf(stderr, "[Error] No se pudo reenviar a la Pico\n");
            }
        } else {
            fprintf(stderr, "[Error] Paquete descartado por formato incompatible\n");
        }

        buffer_reset(rx_buffer);
        return;
    }
}

/*
Nombre de funcion: process_raw_frame
Que hace: Procesa una trama Ethernet cruda recibida por el socket
AF_PACKET: verifica tamaño mínimo y EtherType, extrae y valida el
pov_frame_header (magic, tipo, longitud), y según el tipo:
  - POV_FRAME_TYPE_DATA: agrega el chunk al buffer de reensamblado
    (buffer_append) e imprime el progreso.
  - POV_FRAME_TYPE_END: llama a process_complete_message con lo
    acumulado hasta ahora.
Tramas con magic inválido, EtherType distinto, tamaño insuficiente o
tipo desconocido se descartan (con warning en algunos casos).
Que recibe:
  - frame: buffer con la trama Ethernet completa (incluye header Ethernet).
  - frame_size: tamaño de "frame" en bytes, tal como lo devolvió recvfrom.
  - rx_buffer: buffer dinámico de reensamblado, compartido entre llamadas.
  - config: configuración actual (modo, usado para decidir si se imprime
    hexdump adicional de depuración).
Que retorna:
  - 0 en el caso normal (trama procesada, ignorada o descartada por warning).
  - -1 si buffer_append falla (mensaje demasiado grande o sin memoria);
    en ese caso también se reinicia rx_buffer.
*/
static int process_raw_frame(
    const uint8_t *frame,
    ssize_t frame_size,
    struct dynamic_buffer *rx_buffer,
    const struct receiver_config *config
)
{
    const struct ethhdr *eth;
    const uint8_t *payload;
    size_t payload_len;
    struct pov_frame_header header;
    uint32_t magic;
    uint16_t chunk_len;

    // Trama demasiado pequeña para contener header Ethernet + header POV
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

        if (config->mode == MODE_TEST_FRAME) {
            printf("[Debug] Payload recibido sin magic válido (%zu bytes):\n", payload_len);
            print_hex_dump(payload, payload_len);
        }

        return 0;
    }

    // El chunk declarado en el header no puede ser más grande que lo realmente recibido
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

        printf("[RX] DATA: chunk=%u bytes, acumulado=%zu bytes\n",
               chunk_len,
               rx_buffer->size);

        if (config->mode == MODE_TEST_FRAME) {
            printf("[Debug] Chunk DATA en hexadecimal:\n");
            print_hex_dump(payload, chunk_len);
        }

        return 0;
    }

    if (header.type == POV_FRAME_TYPE_END) {
        printf("[RX] END recibido. Mensaje completo: %zu bytes\n", rx_buffer->size);
        process_complete_message(config, rx_buffer);
        return 0;
    }

    fprintf(stderr, "[Warn] Tipo de frame desconocido: %u\n", header.type);
    return 0;
}

/*
Nombre de funcion: parse_config
Que hace: Interpreta los argumentos de línea de comandos (argv) y llena
"config" según el modo solicitado:
  - "normal" <IP_PICO> [interfaz] [puerto]
  - "txt" [interfaz]
  - "frame" [interfaz]
Aplica valores por defecto (DEFAULT_USB_INTERFACE, DEFAULT_PICO_PORT)
cuando los argumentos opcionales no se proporcionan.
Que recibe:
  - argc, argv: argumentos de línea de comandos del programa.
  - config: [salida] estructura a llenar con la configuración resultante.
Que retorna:
  - 0 si los argumentos son válidos y "config" quedó configurado.
  - -1 si los argumentos son insuficientes, el modo es desconocido, o el
    puerto especificado es inválido (fuera de 1..65535).
*/
static int parse_config(int argc, char **argv, struct receiver_config *config)
{
    memset(config, 0, sizeof(*config));

    config->interface_name = DEFAULT_USB_INTERFACE;
    config->pico_ip = NULL;
    config->pico_port = DEFAULT_PICO_PORT;

    if (argc < 2)
        return -1;

    if (strcmp(argv[1], "normal") == 0) {
        int port;

        if (argc < 3)
            return -1;

        config->mode = MODE_NORMAL;
        config->pico_ip = argv[2];

        if (argc >= 4)
            config->interface_name = argv[3];

        if (argc >= 5) {
            port = atoi(argv[4]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "[Error] Puerto inválido: %s\n", argv[4]);
                return -1;
            }

            config->pico_port = (uint16_t)port;
        }

        return 0;
    }

    if (strcmp(argv[1], "txt") == 0) {
        config->mode = MODE_TEST_TXT;

        if (argc >= 3)
            config->interface_name = argv[2];

        return 0;
    }

    if (strcmp(argv[1], "frame") == 0) {
        config->mode = MODE_TEST_FRAME;

        if (argc >= 3)
            config->interface_name = argv[2];

        return 0;
    }

    return -1;
}

/*
Nombre de funcion: main
Que hace:
  - Parsea los argumentos con parse_config (si fallan, imprime uso y sale).
  - Imprime un resumen de la configuración (modo, interfaz, EtherType,
    destino Pico según el modo).
  - Abre el socket raw (open_raw_receiver) sobre la interfaz indicada.
  - Entra en un loop infinito: recibe tramas con recvfrom y las procesa
    con process_raw_frame, hasta un error de recvfrom distinto de EINTR.
  - Libera el buffer de reensamblado y cierra el socket antes de salir.
Que recibe:
  - argc, argv: argumentos de línea de comandos (ver parse_config).
Que retorna:
  - 1 si los argumentos son inválidos o falla open_raw_receiver.
  - 0 al salir del loop principal (por error de recvfrom).
*/
int main(int argc, char **argv)
{
    struct receiver_config config;
    int raw_sock;
    uint8_t frame[MAX_FRAME_SIZE];
    struct dynamic_buffer rx_buffer = {0};

    if (parse_config(argc, argv, &config) < 0) {
        print_usage(argv[0]);
        return 1;
    }

    printf("==========================================================\n");
    printf("     RASPBERRY PI 4: RECEPTOR USB-ETHERNET POV\n");
    printf("==========================================================\n");
    printf("Modo:         %s\n", mode_to_string(config.mode));
    printf("Interfaz USB: %s\n", config.interface_name);
    printf("EtherType:    0x%04X\n", CUSTOM_ETHERTYPE);

    if (config.mode == MODE_NORMAL) {
        printf("Pico destino: %s:%u\n", config.pico_ip, config.pico_port);
    } else if (config.mode == MODE_TEST_TXT) {
        printf("Destino Pico: deshabilitado\n");
        printf("Salida:       texto reconstruido\n");
    } else if (config.mode == MODE_TEST_FRAME) {
        printf("Destino Pico: deshabilitado\n");
        printf("Salida:       hexadecimal de frames binarios\n");
    }

    raw_sock = open_raw_receiver(config.interface_name);
    if (raw_sock < 0)
        return 1;

    printf("Escuchando frames POV en %s...\n", config.interface_name);

    while (1) {
        ssize_t frame_size = recvfrom(raw_sock, frame, sizeof(frame), 0, NULL, NULL);

        if (frame_size < 0) {
            if (errno == EINTR)
                continue;

            perror("[Error] recvfrom");
            break;
        }

        process_raw_frame(frame, frame_size, &rx_buffer, &config);
    }

    buffer_free(&rx_buffer);
    close(raw_sock);

    return 0;
}