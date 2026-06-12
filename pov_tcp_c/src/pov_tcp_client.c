#define _POSIX_C_SOURCE 200809L

#include "pov_tcp_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define XTEA_DELTA 0x9E3779B9u
#define CHUNK_SIZE 65536u

typedef struct {
    const char *server_ip;
    const char *server_port;
    uint32_t width;
    uint32_t height;
    uint32_t pattern_id;
    const char *pattern_name;
    uint32_t rounds;
    uint32_t speed_ms;
    uint32_t nonce0;
    uint32_t nonce1;
    const char *output_bin;
    const char *output_pbm;
    int save_pbm;
    int run_mpi;
    int return_pbm;
    int return_packet;
    int wait_response;
    const char *results_dir;
} ClientConfig;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t row_stride_bytes;
    uint64_t payload_size;
    uint64_t total_bin_size;
} BinStats;

static void print_usage(const char *program) {
    printf("Uso:\n");
    printf("  %s --server-ip IP --port PORT [opciones]\n", program);
    printf("\nOpciones principales:\n");
    printf("  --width N                 Ancho del PBM. Default: 1024\n");
    printf("  --height N                Alto del PBM. Default: 1024\n");
    printf("  --pattern NAME            circle | checker | diagonal | waves. Default: waves\n");
    printf("  --rounds N                Rondas XTEA. Default: 64\n");
    printf("  --speed-ms N              Velocidad en ms. Default: 4\n");
    printf("  --output-bin FILE         BIN local generado. Default: payload.bin\n");
    printf("  --output-pbm FILE         PBM local generado. Default: generated_input.pbm\n");
    printf("  --no-save-pbm             No guardar PBM P1 local, solo generar BIN\n");
    printf("\nEjecución remota:\n");
    printf("  --no-run                  El servidor solo guarda el BIN, no ejecuta MPI\n");
    printf("  --return-pbm              Pedir resultado_mpi_driver.pbm. Default: activado\n");
    printf("  --no-return-pbm           No pedir PBM final\n");
    printf("  --return-packet           Pedir driver_packet.bin\n");
    printf("  --no-wait-response        Enviar y cerrar sin esperar respuesta\n");
    printf("  --results-dir DIR         Directorio para resultados. Default: client_results\n");
    printf("\nEjemplo:\n");
    printf("  %s --server-ip 192.168.1.139 --port 5050 --width 1024 --height 1024 --pattern waves --rounds 128 --speed-ms 4 --return-packet\n", program);
}

static uint32_t parse_u32(const char *text, const char *name) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);

    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        fprintf(stderr, "ERROR: valor inválido para %s: %s\n", name, text);
        exit(1);
    }

    return (uint32_t)value;
}

static uint32_t pattern_id_from_name(const char *name) {
    if (strcmp(name, "circle") == 0) {
        return POV_PATTERN_CIRCLE;
    }
    if (strcmp(name, "checker") == 0) {
        return POV_PATTERN_CHECKER;
    }
    if (strcmp(name, "diagonal") == 0) {
        return POV_PATTERN_DIAGONAL;
    }
    if (strcmp(name, "waves") == 0) {
        return POV_PATTERN_WAVES;
    }

    fprintf(stderr, "ERROR: patrón inválido: %s\n", name);
    fprintf(stderr, "Use: circle, checker, diagonal o waves.\n");
    exit(1);
}

static void parse_args(int argc, char **argv, ClientConfig *config) {
    memset(config, 0, sizeof(*config));

    config->server_ip = NULL;
    config->server_port = NULL;
    config->width = 1024;
    config->height = 1024;
    config->pattern_name = "waves";
    config->pattern_id = POV_PATTERN_WAVES;
    config->rounds = POV_DEFAULT_ROUNDS;
    config->speed_ms = POV_DEFAULT_SPEED_MS;
    config->nonce0 = POV_DEFAULT_NONCE0;
    config->nonce1 = POV_DEFAULT_NONCE1;
    config->output_bin = "payload.bin";
    config->output_pbm = "generated_input.pbm";
    config->save_pbm = 1;
    config->run_mpi = 1;
    config->return_pbm = 1;
    config->return_packet = 0;
    config->wait_response = 1;
    config->results_dir = "client_results";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--server-ip") == 0 && i + 1 < argc) {
            config->server_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config->server_port = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            config->width = parse_u32(argv[++i], "--width");
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            config->height = parse_u32(argv[++i], "--height");
        } else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
            config->pattern_name = argv[++i];
            config->pattern_id = pattern_id_from_name(config->pattern_name);
        } else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            config->rounds = parse_u32(argv[++i], "--rounds");
        } else if (strcmp(argv[i], "--speed-ms") == 0 && i + 1 < argc) {
            config->speed_ms = parse_u32(argv[++i], "--speed-ms");
        } else if (strcmp(argv[i], "--nonce0") == 0 && i + 1 < argc) {
            config->nonce0 = parse_u32(argv[++i], "--nonce0");
        } else if (strcmp(argv[i], "--nonce1") == 0 && i + 1 < argc) {
            config->nonce1 = parse_u32(argv[++i], "--nonce1");
        } else if (strcmp(argv[i], "--output-bin") == 0 && i + 1 < argc) {
            config->output_bin = argv[++i];
        } else if (strcmp(argv[i], "--output-pbm") == 0 && i + 1 < argc) {
            config->output_pbm = argv[++i];
            config->save_pbm = 1;
        } else if (strcmp(argv[i], "--no-save-pbm") == 0) {
            config->save_pbm = 0;
        } else if (strcmp(argv[i], "--no-run") == 0) {
            config->run_mpi = 0;
        } else if (strcmp(argv[i], "--return-pbm") == 0) {
            config->return_pbm = 1;
        } else if (strcmp(argv[i], "--no-return-pbm") == 0) {
            config->return_pbm = 0;
        } else if (strcmp(argv[i], "--return-packet") == 0) {
            config->return_packet = 1;
        } else if (strcmp(argv[i], "--no-wait-response") == 0) {
            config->wait_response = 0;
        } else if (strcmp(argv[i], "--results-dir") == 0 && i + 1 < argc) {
            config->results_dir = argv[++i];
        } else {
            fprintf(stderr, "ERROR: argumento desconocido o incompleto: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }

    if (config->server_ip == NULL || config->server_port == NULL) {
        fprintf(stderr, "ERROR: --server-ip y --port son obligatorios.\n");
        print_usage(argv[0]);
        exit(1);
    }

    if (config->width == 0 || config->height == 0) {
        fprintf(stderr, "ERROR: width y height deben ser mayores que 0.\n");
        exit(1);
    }

    if (config->rounds == 0) {
        fprintf(stderr, "ERROR: rounds debe ser mayor que 0.\n");
        exit(1);
    }
}

static uint32_t compute_row_stride_bytes(uint32_t width) {
    return ((width + 63u) / 64u) * 8u;
}

static uint8_t pattern_value(uint32_t x, uint32_t y, const ClientConfig *config) {
    double width = (double)config->width;
    double height = (double)config->height;

    switch (config->pattern_id) {
        case POV_PATTERN_CIRCLE: {
            double cx = width / 2.0;
            double cy = height / 2.0;
            double radius = fmin(width, height) * 0.40;
            double dx = (double)x - cx;
            double dy = (double)y - cy;
            return (dx * dx + dy * dy <= radius * radius) ? 1u : 0u;
        }

        case POV_PATTERN_CHECKER: {
            uint32_t block = 16u;
            return (((x / block) + (y / block)) % 2u == 0u) ? 1u : 0u;
        }

        case POV_PATTERN_DIAGONAL: {
            int32_t sx = (int32_t)x;
            int32_t sy = (int32_t)y;
            int32_t sw = (int32_t)config->width;
            int32_t d1 = sx - sy;
            int32_t d2 = (sw - sx) - sy;
            if (d1 < 0) d1 = -d1;
            if (d2 < 0) d2 = -d2;
            return (d1 < 8 || d2 < 8) ? 1u : 0u;
        }

        case POV_PATTERN_WAVES: {
            double wave = sin((double)x * 0.05) + cos((double)y * 0.05);
            return (wave > 0.0) ? 1u : 0u;
        }

        default:
            return (((x + y) % 2u) == 0u) ? 1u : 0u;
    }
}

static void xtea_encrypt_block(uint32_t *v0, uint32_t *v1, const uint32_t key[4], uint32_t rounds) {
    uint32_t sum = 0;
    uint32_t y = *v0;
    uint32_t z = *v1;

    for (uint32_t i = 0; i < rounds; i++) {
        y += ((((z << 4) ^ (z >> 5)) + z) ^ (sum + key[sum & 3u]));
        sum += XTEA_DELTA;
        z += ((((y << 4) ^ (y >> 5)) + y) ^ (sum + key[(sum >> 11) & 3u]));
    }

    *v0 = y;
    *v1 = z;
}

static void encrypt_row_ctr(
    const uint8_t *plain_row,
    uint8_t *cipher_row,
    uint32_t row_stride_bytes,
    uint64_t global_payload_offset,
    const ClientConfig *config
) {
    const uint32_t key[4] = {
        POV_XTEA_KEY0,
        POV_XTEA_KEY1,
        POV_XTEA_KEY2,
        POV_XTEA_KEY3
    };

    for (uint32_t offset = 0; offset < row_stride_bytes; offset += 8u) {
        uint64_t block_index = (global_payload_offset + (uint64_t)offset) / 8u;

        uint32_t counter_low = (uint32_t)(block_index & 0xFFFFFFFFu);
        uint32_t counter_high = (uint32_t)((block_index >> 32) & 0xFFFFFFFFu);

        uint32_t v0 = config->nonce0 ^ counter_low;
        uint32_t v1 = config->nonce1 ^ counter_high;

        xtea_encrypt_block(&v0, &v1, key, config->rounds);

        uint8_t keystream[8];
        keystream[0] = (uint8_t)((v0 >> 24) & 0xFFu);
        keystream[1] = (uint8_t)((v0 >> 16) & 0xFFu);
        keystream[2] = (uint8_t)((v0 >> 8) & 0xFFu);
        keystream[3] = (uint8_t)(v0 & 0xFFu);
        keystream[4] = (uint8_t)((v1 >> 24) & 0xFFu);
        keystream[5] = (uint8_t)((v1 >> 16) & 0xFFu);
        keystream[6] = (uint8_t)((v1 >> 8) & 0xFFu);
        keystream[7] = (uint8_t)(v1 & 0xFFu);

        for (uint32_t i = 0; i < 8u; i++) {
            cipher_row[offset + i] = plain_row[offset + i] ^ keystream[i];
        }
    }
}

static int generate_pbm_and_bin(const ClientConfig *config, BinStats *stats) {
    uint32_t row_stride_bytes = compute_row_stride_bytes(config->width);
    uint64_t payload_size = (uint64_t)config->height * (uint64_t)row_stride_bytes;

    if (payload_size > UINT32_MAX) {
        fprintf(stderr, "ERROR: payload_size=%llu excede uint32_t del contrato binario.\n",
                (unsigned long long)payload_size);
        return -1;
    }

    FILE *bin = fopen(config->output_bin, "wb");
    if (bin == NULL) {
        perror("fopen output_bin");
        return -1;
    }

    FILE *pbm = NULL;
    if (config->save_pbm) {
        pbm = fopen(config->output_pbm, "w");
        if (pbm == NULL) {
            perror("fopen output_pbm");
            fclose(bin);
            return -1;
        }
        fprintf(pbm, "P1\n%u %u\n", config->width, config->height);
    }

    uint8_t header[40];
    pov_write_u32_be(header + 0, POV_BIN_MAGIC);
    pov_write_u32_be(header + 4, POV_BIN_VERSION);
    pov_write_u32_be(header + 8, config->width);
    pov_write_u32_be(header + 12, config->height);
    pov_write_u32_be(header + 16, row_stride_bytes);
    pov_write_u32_be(header + 20, config->speed_ms);
    pov_write_u32_be(header + 24, (uint32_t)payload_size);
    pov_write_u32_be(header + 28, config->rounds);
    pov_write_u32_be(header + 32, config->nonce0);
    pov_write_u32_be(header + 36, config->nonce1);

    if (fwrite(header, 1, sizeof(header), bin) != sizeof(header)) {
        perror("fwrite header");
        if (pbm != NULL) fclose(pbm);
        fclose(bin);
        return -1;
    }

    uint8_t *plain_row = calloc(1, row_stride_bytes);
    uint8_t *cipher_row = malloc(row_stride_bytes);

    if (plain_row == NULL || cipher_row == NULL) {
        fprintf(stderr, "ERROR: malloc filas falló.\n");
        free(plain_row);
        free(cipher_row);
        if (pbm != NULL) fclose(pbm);
        fclose(bin);
        return -1;
    }

    for (uint32_t row = 0; row < config->height; row++) {
        memset(plain_row, 0, row_stride_bytes);

        for (uint32_t col = 0; col < config->width; col++) {
            uint8_t bit = pattern_value(col, row, config);

            if (pbm != NULL) {
                fprintf(pbm, "%u", bit);
                if (col + 1 < config->width) {
                    fputc(' ', pbm);
                }
            }

            if (bit == 1u) {
                uint64_t byte_index = (uint64_t)(col / 8u);
                uint32_t bit_offset = 7u - (col % 8u);
                plain_row[byte_index] |= (uint8_t)(1u << bit_offset);
            }
        }

        if (pbm != NULL) {
            fputc('\n', pbm);
        }

        uint64_t global_offset = (uint64_t)row * (uint64_t)row_stride_bytes;

        encrypt_row_ctr(
            plain_row,
            cipher_row,
            row_stride_bytes,
            global_offset,
            config
        );

        if (fwrite(cipher_row, 1, row_stride_bytes, bin) != row_stride_bytes) {
            perror("fwrite payload row");
            free(plain_row);
            free(cipher_row);
            if (pbm != NULL) fclose(pbm);
            fclose(bin);
            return -1;
        }
    }

    free(plain_row);
    free(cipher_row);

    if (pbm != NULL) {
        fclose(pbm);
    }

    fclose(bin);

    stats->width = config->width;
    stats->height = config->height;
    stats->row_stride_bytes = row_stride_bytes;
    stats->payload_size = payload_size;
    stats->total_bin_size = payload_size + 40u;

    return 0;
}

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port, &hints, &result);
    if (gai != 0) {
        fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(gai));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

static int get_file_size(const char *path, uint64_t *size_out) {
    struct stat st;
    if (stat(path, &st) < 0) {
        return -1;
    }
    if (st.st_size < 0) {
        return -1;
    }
    *size_out = (uint64_t)st.st_size;
    return 0;
}

static uint32_t build_flags(const ClientConfig *config) {
    uint32_t flags = 0;
    if (config->run_mpi) {
        flags |= POV_TCP_FLAG_RUN_MPI;
    }
    if (config->return_pbm) {
        flags |= POV_TCP_FLAG_RETURN_PBM;
    }
    if (config->return_packet) {
        flags |= POV_TCP_FLAG_RETURN_PACKET;
    }
    return flags;
}

static int send_generated_bin(const ClientConfig *config, const BinStats *stats) {
    uint64_t file_size = 0;
    if (get_file_size(config->output_bin, &file_size) < 0) {
        perror("stat output_bin");
        return -1;
    }

    FILE *fp = fopen(config->output_bin, "rb");
    if (fp == NULL) {
        perror("fopen output_bin for send");
        return -1;
    }

    int fd = connect_to_server(config->server_ip, config->server_port);
    if (fd < 0) {
        fprintf(stderr, "ERROR: no se pudo conectar a %s:%s\n",
                config->server_ip, config->server_port);
        fclose(fp);
        return -1;
    }

    uint8_t header[POV_TCP_REQUEST_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    pov_write_u32_be(header + 0, POV_TCP_REQUEST_MAGIC);
    pov_write_u32_be(header + 4, POV_TCP_VERSION);
    pov_write_u32_be(header + 8, build_flags(config));
    pov_write_u32_be(header + 12, stats->width);
    pov_write_u32_be(header + 16, stats->height);
    pov_write_u32_be(header + 20, config->speed_ms);
    pov_write_u32_be(header + 24, config->rounds);
    pov_write_u32_be(header + 28, config->nonce0);
    pov_write_u32_be(header + 32, config->nonce1);
    pov_write_u32_be(header + 36, config->pattern_id);
    pov_write_u64_be(header + 40, file_size);

    printf("\n[TCP CLIENT]\n");
    printf("Server        : %s:%s\n", config->server_ip, config->server_port);
    printf("BIN local     : %s\n", config->output_bin);
    printf("BIN bytes     : %llu\n", (unsigned long long)file_size);
    printf("Flags         : 0x%08X\n", build_flags(config));

    if (send_all(fd, header, sizeof(header)) < 0) {
        perror("send request header");
        close(fd);
        fclose(fp);
        return -1;
    }

    uint8_t buffer[CHUNK_SIZE];
    uint64_t sent_total = 0;

    while (sent_total < file_size) {
        size_t to_read = CHUNK_SIZE;
        if (file_size - sent_total < CHUNK_SIZE) {
            to_read = (size_t)(file_size - sent_total);
        }

        size_t read_count = fread(buffer, 1, to_read, fp);
        if (read_count != to_read) {
            perror("fread output_bin");
            close(fd);
            fclose(fp);
            return -1;
        }

        if (send_all(fd, buffer, read_count) < 0) {
            perror("send bin payload");
            close(fd);
            fclose(fp);
            return -1;
        }

        sent_total += read_count;
    }

    fclose(fp);

    printf("Archivo enviado al servidor correctamente.\n");

    if (!config->wait_response) {
        printf("No se esperará respuesta (--no-wait-response).\n");
        close(fd);
        return 0;
    }

    uint8_t response_header[POV_TCP_RESPONSE_HEADER_SIZE];
    if (recv_all(fd, response_header, sizeof(response_header)) < 0) {
        perror("recv response header");
        close(fd);
        return -1;
    }

    uint32_t magic = pov_read_u32_be(response_header + 0);
    uint32_t version = pov_read_u32_be(response_header + 4);
    int32_t status = (int32_t)pov_read_u32_be(response_header + 8);
    uint64_t message_size = pov_read_u64_be(response_header + 16);
    uint64_t pbm_size = pov_read_u64_be(response_header + 24);
    uint64_t packet_size = pov_read_u64_be(response_header + 32);

    if (magic != POV_TCP_RESPONSE_MAGIC || version != POV_TCP_VERSION) {
        fprintf(stderr, "ERROR: respuesta TCP inválida.\n");
        close(fd);
        return -1;
    }

    char *message = calloc(1, (size_t)message_size + 1u);
    if (message == NULL) {
        fprintf(stderr, "ERROR: malloc message falló.\n");
        close(fd);
        return -1;
    }

    if (message_size > 0 && recv_all(fd, message, (size_t)message_size) < 0) {
        perror("recv message");
        free(message);
        close(fd);
        return -1;
    }

    printf("\n[TCP SERVER RESPONSE]\n");
    printf("Status        : %d\n", status);
    printf("Message bytes : %llu\n", (unsigned long long)message_size);
    printf("PBM bytes     : %llu\n", (unsigned long long)pbm_size);
    printf("Packet bytes  : %llu\n", (unsigned long long)packet_size);

    if (message_size > 0) {
        printf("\n[SERVER MESSAGE]\n%s\n", message);
    }

    free(message);

    char mkdir_cmd[1024];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", config->results_dir);
    system(mkdir_cmd);

    if (pbm_size > 0) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", config->results_dir, POV_RESULT_PBM_NAME);
        FILE *out = fopen(path, "wb");
        if (out == NULL) {
            perror("fopen result pbm");
            close(fd);
            return -1;
        }
        uint64_t remaining = pbm_size;
        while (remaining > 0) {
            size_t chunk = remaining < CHUNK_SIZE ? (size_t)remaining : CHUNK_SIZE;
            if (recv_all(fd, buffer, chunk) < 0) {
                perror("recv pbm");
                fclose(out);
                close(fd);
                return -1;
            }
            fwrite(buffer, 1, chunk, out);
            remaining -= chunk;
        }
        fclose(out);
        printf("PBM final guardado en: %s\n", path);
    }

    if (packet_size > 0) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", config->results_dir, POV_RESULT_PACKET_NAME);
        FILE *out = fopen(path, "wb");
        if (out == NULL) {
            perror("fopen result packet");
            close(fd);
            return -1;
        }
        uint64_t remaining = packet_size;
        while (remaining > 0) {
            size_t chunk = remaining < CHUNK_SIZE ? (size_t)remaining : CHUNK_SIZE;
            if (recv_all(fd, buffer, chunk) < 0) {
                perror("recv packet");
                fclose(out);
                close(fd);
                return -1;
            }
            fwrite(buffer, 1, chunk, out);
            remaining -= chunk;
        }
        fclose(out);
        printf("Driver packet guardado en: %s\n", path);
    }

    close(fd);

    return status == 0 ? 0 : -1;
}

int main(int argc, char **argv) {
    ClientConfig config;
    BinStats stats;

    parse_args(argc, argv, &config);

    printf("\n[GENERADOR / CIFRADOR]\n");
    printf("Dimensiones   : %u x %u\n", config.width, config.height);
    printf("Patrón        : %s\n", config.pattern_name);
    printf("Rounds XTEA   : %u\n", config.rounds);
    printf("Speed ms      : %u\n", config.speed_ms);
    printf("Output BIN    : %s\n", config.output_bin);
    printf("Guardar PBM   : %s\n", config.save_pbm ? config.output_pbm : "no");

    if (generate_pbm_and_bin(&config, &stats) < 0) {
        return 1;
    }

    printf("\n[BIN GENERADO]\n");
    printf("Width            : %u\n", stats.width);
    printf("Height           : %u\n", stats.height);
    printf("Row stride bytes : %u\n", stats.row_stride_bytes);
    printf("Payload size     : %llu bytes\n", (unsigned long long)stats.payload_size);
    printf("Total BIN size   : %llu bytes\n", (unsigned long long)stats.total_bin_size);

    if (send_generated_bin(&config, &stats) < 0) {
        return 1;
    }

    printf("\nCliente TCP finalizado correctamente.\n");

    return 0;
}
