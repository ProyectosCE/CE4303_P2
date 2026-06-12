#define _POSIX_C_SOURCE 200809L

#include "pov_tcp_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHUNK_SIZE 65536u

typedef struct {
    const char *port;
    const char *mpi_dir;
    const char *payload_name;
    const char *remote_script;
    int once;
    int verbose;
} ServerConfig;

typedef struct {
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t speed_ms;
    uint32_t rounds;
    uint32_t nonce0;
    uint32_t nonce1;
    uint32_t pattern_id;
    uint64_t bin_size;
} RequestInfo;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TextBuffer;

static void print_usage(const char *program) {
    printf("Uso:\n");
    printf("  %s --port PORT --mpi-dir DIR [opciones]\n", program);
    printf("\nOpciones:\n");
    printf("  --payload-name FILE       Nombre donde guardar el BIN. Default: imagen_payload.bin\n");
    printf("  --remote-script SCRIPT    Script que ejecuta MPI. Default: ./run_mpi_host.sh\n");
    printf("  --once                    Atiende un cliente y termina\n");
    printf("  --quiet                   Menos salida en consola\n");
    printf("\nEjemplo:\n");
    printf("  %s --port 5050 --mpi-dir /home/alex/Documents/OPERATIVOS/CE4303_P2/mpi --once\n", program);
}

static void parse_args(int argc, char **argv, ServerConfig *config) {
    memset(config, 0, sizeof(*config));

    config->port = NULL;
    config->mpi_dir = NULL;
    config->payload_name = "imagen_payload.bin";
    config->remote_script = "./run_mpi_host.sh";
    config->once = 0;
    config->verbose = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config->port = argv[++i];
        } else if (strcmp(argv[i], "--mpi-dir") == 0 && i + 1 < argc) {
            config->mpi_dir = argv[++i];
        } else if (strcmp(argv[i], "--payload-name") == 0 && i + 1 < argc) {
            config->payload_name = argv[++i];
        } else if (strcmp(argv[i], "--remote-script") == 0 && i + 1 < argc) {
            config->remote_script = argv[++i];
        } else if (strcmp(argv[i], "--once") == 0) {
            config->once = 1;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            config->verbose = 0;
        } else {
            fprintf(stderr, "ERROR: argumento desconocido o incompleto: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }

    if (config->port == NULL || config->mpi_dir == NULL) {
        fprintf(stderr, "ERROR: --port y --mpi-dir son obligatorios.\n");
        print_usage(argv[0]);
        exit(1);
    }
}

static int path_join(char *out, size_t out_size, const char *dir, const char *name) {
    int written = snprintf(out, out_size, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}

static void text_buffer_init(TextBuffer *buffer) {
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

static void text_buffer_free(TextBuffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

static int text_buffer_append(TextBuffer *buffer, const char *text, size_t text_len) {
    if (text_len == 0) {
        return 0;
    }

    size_t needed = buffer->len + text_len + 1u;
    if (needed > buffer->cap) {
        size_t new_cap = buffer->cap == 0 ? 4096u : buffer->cap;
        while (new_cap < needed) {
            new_cap *= 2u;
        }

        char *new_data = realloc(buffer->data, new_cap);
        if (new_data == NULL) {
            return -1;
        }

        buffer->data = new_data;
        buffer->cap = new_cap;
    }

    memcpy(buffer->data + buffer->len, text, text_len);
    buffer->len += text_len;
    buffer->data[buffer->len] = '\0';

    return 0;
}

static char *shell_quote(const char *text) {
    size_t len = strlen(text);
    size_t cap = len * 4u + 3u;
    char *out = malloc(cap);
    if (out == NULL) {
        return NULL;
    }

    size_t pos = 0;
    out[pos++] = '\'';

    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\'') {
            if (pos + 4 >= cap) {
                free(out);
                return NULL;
            }
            out[pos++] = '\'';
            out[pos++] = '\\';
            out[pos++] = '\'';
            out[pos++] = '\'';
        } else {
            if (pos + 1 >= cap) {
                free(out);
                return NULL;
            }
            out[pos++] = text[i];
        }
    }

    out[pos++] = '\'';
    out[pos] = '\0';

    return out;
}

static int run_mpi_script(const ServerConfig *config, TextBuffer *output) {
    char *quoted_dir = shell_quote(config->mpi_dir);
    char *quoted_script = shell_quote(config->remote_script);

    if (quoted_dir == NULL || quoted_script == NULL) {
        free(quoted_dir);
        free(quoted_script);
        text_buffer_append(output, "ERROR: no se pudo reservar memoria para comando MPI.\n", 53u);
        return -1;
    }

    size_t command_size = strlen(quoted_dir) + strlen(quoted_script) * 2u + 128u;
    char *command = malloc(command_size);

    if (command == NULL) {
        free(quoted_dir);
        free(quoted_script);
        text_buffer_append(output, "ERROR: malloc command falló.\n", 29u);
        return -1;
    }

    snprintf(
        command,
        command_size,
        "cd %s && chmod +x %s && %s 2>&1",
        quoted_dir,
        quoted_script,
        quoted_script
    );

    text_buffer_append(output, "[TCP SERVER] Ejecutando MPI:\n", 28u);
    text_buffer_append(output, command, strlen(command));
    text_buffer_append(output, "\n\n", 2u);

    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        text_buffer_append(output, "ERROR: popen falló al ejecutar MPI.\n", 36u);
        free(command);
        free(quoted_dir);
        free(quoted_script);
        return -1;
    }

    char read_buffer[4096];
    while (fgets(read_buffer, sizeof(read_buffer), pipe) != NULL) {
        text_buffer_append(output, read_buffer, strlen(read_buffer));
    }

    int status = pclose(pipe);
    int exit_code = -1;

    if (status == -1) {
        text_buffer_append(output, "ERROR: pclose falló.\n", 21u);
        exit_code = -1;
    } else if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    } else {
        exit_code = -1;
    }

    char summary[128];
    snprintf(summary, sizeof(summary), "\n[TCP SERVER] Código de salida MPI: %d\n", exit_code);
    text_buffer_append(output, summary, strlen(summary));

    free(command);
    free(quoted_dir);
    free(quoted_script);

    return exit_code;
}

static int create_listen_socket(const char *port) {
    int port_number = atoi(port);
    if (port_number <= 0 || port_number > 65535) {
        fprintf(stderr, "ERROR: puerto inválido: %s\n", port);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port_number);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 8) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

static int parse_request_header(const uint8_t *header, RequestInfo *request) {
    uint32_t magic = pov_read_u32_be(header + 0);
    uint32_t version = pov_read_u32_be(header + 4);

    if (magic != POV_TCP_REQUEST_MAGIC) {
        fprintf(stderr, "ERROR: magic TCP inválido: 0x%08X\n", magic);
        return -1;
    }

    if (version != POV_TCP_VERSION) {
        fprintf(stderr, "ERROR: versión TCP inválida: %u\n", version);
        return -1;
    }

    request->flags = pov_read_u32_be(header + 8);
    request->width = pov_read_u32_be(header + 12);
    request->height = pov_read_u32_be(header + 16);
    request->speed_ms = pov_read_u32_be(header + 20);
    request->rounds = pov_read_u32_be(header + 24);
    request->nonce0 = pov_read_u32_be(header + 28);
    request->nonce1 = pov_read_u32_be(header + 32);
    request->pattern_id = pov_read_u32_be(header + 36);
    request->bin_size = pov_read_u64_be(header + 40);

    if (request->bin_size < 40u) {
        fprintf(stderr, "ERROR: bin_size inválido: %llu\n", (unsigned long long)request->bin_size);
        return -1;
    }

    return 0;
}

static int receive_bin_file(int client_fd, const char *output_path, uint64_t bin_size) {
    FILE *fp = fopen(output_path, "wb");
    if (fp == NULL) {
        perror("fopen payload output");
        return -1;
    }

    uint8_t buffer[CHUNK_SIZE];
    uint64_t remaining = bin_size;

    while (remaining > 0) {
        size_t chunk = remaining < CHUNK_SIZE ? (size_t)remaining : CHUNK_SIZE;

        if (recv_all(client_fd, buffer, chunk) < 0) {
            perror("recv bin payload");
            fclose(fp);
            return -1;
        }

        if (fwrite(buffer, 1, chunk, fp) != chunk) {
            perror("fwrite payload output");
            fclose(fp);
            return -1;
        }

        remaining -= chunk;
    }

    fclose(fp);
    return 0;
}

static int read_file_all(const char *path, uint8_t **data_out, uint64_t *size_out) {
    *data_out = NULL;
    *size_out = 0;

    struct stat st;
    if (stat(path, &st) < 0) {
        return -1;
    }

    if (st.st_size < 0) {
        return -1;
    }

    uint64_t size = (uint64_t)st.st_size;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    uint8_t *data = NULL;
    if (size > 0) {
        data = malloc((size_t)size);
        if (data == NULL) {
            fclose(fp);
            return -1;
        }

        if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
            free(data);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    *data_out = data;
    *size_out = size;
    return 0;
}

static int send_response(
    int client_fd,
    int32_t status_code,
    const TextBuffer *message,
    const uint8_t *pbm_data,
    uint64_t pbm_size,
    const uint8_t *packet_data,
    uint64_t packet_size
) {
    uint8_t header[POV_TCP_RESPONSE_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    pov_write_u32_be(header + 0, POV_TCP_RESPONSE_MAGIC);
    pov_write_u32_be(header + 4, POV_TCP_VERSION);
    pov_write_u32_be(header + 8, (uint32_t)status_code);
    pov_write_u32_be(header + 12, 0u);
    pov_write_u64_be(header + 16, (uint64_t)message->len);
    pov_write_u64_be(header + 24, pbm_size);
    pov_write_u64_be(header + 32, packet_size);
    pov_write_u64_be(header + 40, 0u);

    if (send_all(client_fd, header, sizeof(header)) < 0) {
        return -1;
    }

    if (message->len > 0 && send_all(client_fd, message->data, message->len) < 0) {
        return -1;
    }

    if (pbm_size > 0 && send_all(client_fd, pbm_data, (size_t)pbm_size) < 0) {
        return -1;
    }

    if (packet_size > 0 && send_all(client_fd, packet_data, (size_t)packet_size) < 0) {
        return -1;
    }

    return 0;
}

static void append_request_summary(TextBuffer *message, const RequestInfo *request, const char *payload_path) {
    char line[2048];

    snprintf(line, sizeof(line), "[TCP SERVER] Payload recibido: %s\n", payload_path);
    text_buffer_append(message, line, strlen(line));

    snprintf(line, sizeof(line), "Width: %u\nHeight: %u\nSpeed ms: %u\nRounds: %u\nBIN bytes: %llu\nFlags: 0x%08X\n\n",
             request->width,
             request->height,
             request->speed_ms,
             request->rounds,
             (unsigned long long)request->bin_size,
             request->flags);
    text_buffer_append(message, line, strlen(line));
}

static int handle_client(int client_fd, const ServerConfig *config) {
    uint8_t request_header[POV_TCP_REQUEST_HEADER_SIZE];
    RequestInfo request;
    TextBuffer message;
    int32_t status_code = 0;

    text_buffer_init(&message);

    if (recv_all(client_fd, request_header, sizeof(request_header)) < 0) {
        perror("recv request header");
        text_buffer_free(&message);
        return -1;
    }

    if (parse_request_header(request_header, &request) < 0) {
        text_buffer_free(&message);
        return -1;
    }

    char payload_path[1024];
    if (path_join(payload_path, sizeof(payload_path), config->mpi_dir, config->payload_name) < 0) {
        fprintf(stderr, "ERROR: ruta de payload demasiado larga.\n");
        text_buffer_free(&message);
        return -1;
    }

    if (config->verbose) {
        printf("\n[REQUEST]\n");
        printf("Width       : %u\n", request.width);
        printf("Height      : %u\n", request.height);
        printf("Speed ms    : %u\n", request.speed_ms);
        printf("Rounds      : %u\n", request.rounds);
        printf("BIN bytes   : %llu\n", (unsigned long long)request.bin_size);
        printf("Flags       : 0x%08X\n", request.flags);
        printf("Save path   : %s\n", payload_path);
        fflush(stdout);
    }

    if (receive_bin_file(client_fd, payload_path, request.bin_size) < 0) {
        text_buffer_free(&message);
        return -1;
    }

    append_request_summary(&message, &request, payload_path);

    if ((request.flags & POV_TCP_FLAG_RUN_MPI) != 0u) {
        int mpi_exit = run_mpi_script(config, &message);
        status_code = mpi_exit == 0 ? 0 : mpi_exit;
    } else {
        text_buffer_append(&message, "[TCP SERVER] RUN_MPI desactivado. Solo se guardó el BIN.\n", 59u);
        status_code = 0;
    }

    uint8_t *pbm_data = NULL;
    uint8_t *packet_data = NULL;
    uint64_t pbm_size = 0;
    uint64_t packet_size = 0;

    if ((request.flags & POV_TCP_FLAG_RETURN_PBM) != 0u) {
        char pbm_path[1024];
        if (path_join(pbm_path, sizeof(pbm_path), config->mpi_dir, POV_RESULT_PBM_NAME) == 0 &&
            read_file_all(pbm_path, &pbm_data, &pbm_size) == 0) {
            char line[2048];
            snprintf(line, sizeof(line), "[TCP SERVER] Adjuntando PBM final: %s (%llu bytes)\n",
                     pbm_path, (unsigned long long)pbm_size);
            text_buffer_append(&message, line, strlen(line));
        } else {
            text_buffer_append(&message, "[TCP SERVER] WARNING: no se pudo leer resultado_mpi_driver.pbm.\n", 67u);
        }
    }

    if ((request.flags & POV_TCP_FLAG_RETURN_PACKET) != 0u) {
        char packet_path[1024];
        if (path_join(packet_path, sizeof(packet_path), config->mpi_dir, POV_RESULT_PACKET_NAME) == 0 &&
            read_file_all(packet_path, &packet_data, &packet_size) == 0) {
            char line[2048];
            snprintf(line, sizeof(line), "[TCP SERVER] Adjuntando driver packet: %s (%llu bytes)\n",
                     packet_path, (unsigned long long)packet_size);
            text_buffer_append(&message, line, strlen(line));
        } else {
            text_buffer_append(&message, "[TCP SERVER] WARNING: no se pudo leer driver_packet.bin.\n", 59u);
        }
    }

    if (send_response(
            client_fd,
            status_code,
            &message,
            pbm_data,
            pbm_size,
            packet_data,
            packet_size
        ) < 0) {
        if (config->verbose) {
            perror("send response");
        }
    }

    if (config->verbose) {
        printf("[DONE] status=%d pbm=%llu packet=%llu\n",
               status_code,
               (unsigned long long)pbm_size,
               (unsigned long long)packet_size);
        fflush(stdout);
    }

    free(pbm_data);
    free(packet_data);
    text_buffer_free(&message);

    return 0;
}

int main(int argc, char **argv) {
    ServerConfig config;
    parse_args(argc, argv, &config);

    signal(SIGPIPE, SIG_IGN);

    int listen_fd = create_listen_socket(config.port);
    if (listen_fd < 0) {
        return 1;
    }

    printf("\n[POV TCP SERVER]\n");
    printf("Port          : %s\n", config.port);
    printf("MPI dir       : %s\n", config.mpi_dir);
    printf("Payload name  : %s\n", config.payload_name);
    printf("Remote script : %s\n", config.remote_script);
    printf("Mode          : %s\n", config.once ? "once" : "loop");
    printf("Waiting for clients...\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            close(listen_fd);
            return 1;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("\n[CLIENT] %s:%u\n", client_ip, ntohs(client_addr.sin_port));
        fflush(stdout);

        handle_client(client_fd, &config);
        close(client_fd);

        if (config.once) {
            break;
        }
    }

    close(listen_fd);
    return 0;
}
