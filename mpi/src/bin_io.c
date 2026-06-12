#include "bin_io.h"

#include <stdio.h>
#include <stdlib.h>


/*
 * Lee el archivo binario de entrada y llena la estructura BinHeader y el payload cifrado.
 *
 * El caller es responsable de liberar el payload con free() cuando ya no se necesite.
 */



 // Lee un uint32_t big-endian de un buffer de 4 bytes.
static uint32_t read_u32_be(const uint8_t *buffer) {
    return
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8)  |
        ((uint32_t)buffer[3]);
}

/*
bin_read_file
Lee el archivo binario de entrada y llena la estructura BinHeader y el payload cifrado.
recibe: - filename: ruta al archivo binario de entrada.
         - header: puntero a BinHeader donde se llenarán los campos del header.
         - payload_out: puntero a uint8_t* donde se asignará la dirección del payload cifrado leído del archivo. El caller es responsable de liberar este buffer con free() cuando ya no se necesite.
retorna: 0 si la lectura fue exitosa, -1 si hubo un error (ej. archivo no encontrado, formato inválido, etc.). En caso de error, no se asigna nada a payload_out.
*/
int bin_read_file(
    const char *filename,
    BinHeader *header,
    uint8_t **payload_out
) {
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    // Leer el header de 40 bytes.
    // El header tiene el siguiente formato (todos los campos son big-endian):
    // Offset  Tamaño  Descripción
    // 0       4       Magic (debe ser BIN_MAGIC)
    // 4       4       Version (debe ser BIN_VERSION)
    // 8       4       Width (ancho de la imagen en píxeles)
    // 12      4       Height (alto de la imagen en píxeles)
    // 16      4       Row stride bytes (número de bytes por fila, incluyendo padding)
    // 20      4       Speed ms (velocidad en milisegundos para el driver)
    // 24      4       Payload size (tamaño del payload cifrado en bytes, debe ser igual a height * row_stride_bytes)
    // 28      4       Cipher rounds (número de rondas de cifrado XTEA)
    // 32      4       Nonce0 (parte baja del nonce para CTR)
    // 36      4       Nonce1 (parte alta del nonce para CTR)
    uint8_t raw_header[HEADER_SIZE_BYTES];

    if (fread(raw_header, 1, HEADER_SIZE_BYTES, fp)
        != HEADER_SIZE_BYTES) {
        printf("ERROR: no se pudo leer header completo.\n");
        fclose(fp);
        return -1;
    }

    header->magic =
        read_u32_be(&raw_header[0]);

    header->version =
        read_u32_be(&raw_header[4]);

    header->width =
        read_u32_be(&raw_header[8]);

    header->height =
        read_u32_be(&raw_header[12]);

    header->row_stride_bytes =
        read_u32_be(&raw_header[16]);

    header->speed_ms =
        read_u32_be(&raw_header[20]);

    header->payload_size =
        read_u32_be(&raw_header[24]);

    header->cipher_rounds =
        read_u32_be(&raw_header[28]);

    header->nonce0 =
        read_u32_be(&raw_header[32]);

    header->nonce1 =
        read_u32_be(&raw_header[36]);

    if (header->magic != BIN_MAGIC) {
        printf("ERROR: magic inválido. Recibido=0x%08X\n",
               header->magic);
        fclose(fp);
        return -1;
    }

    if (header->version != BIN_VERSION) {
        printf("ERROR: versión inválida. Recibida=%u\n",
               header->version);
        fclose(fp);
        return -1;
    }

    if (header->width == 0 ||
        header->height == 0 ||
        header->row_stride_bytes == 0 ||
        header->payload_size == 0) {
        printf("ERROR: header contiene dimensiones inválidas.\n");
        fclose(fp);
        return -1;
    }

    uint64_t expected_size =
        (uint64_t)header->height *
        (uint64_t)header->row_stride_bytes;

    if (expected_size != header->payload_size) {
        printf("ERROR: payload_size inválido.\n");
        printf("Esperado=%llu, recibido=%u\n",
               (unsigned long long)expected_size,
               header->payload_size);
        fclose(fp);
        return -1;
    }

    uint8_t *payload =
        malloc(header->payload_size);

    if (payload == NULL) {
        printf("ERROR: malloc payload falló.\n");
        fclose(fp);
        return -1;
    }

    if (fread(payload, 1, header->payload_size, fp)
        != header->payload_size) {
        printf("ERROR: no se pudo leer payload completo.\n");
        free(payload);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    *payload_out = payload;

    return 0;
}


/*
bin_print_header
Imprime el contenido del BinHeader de forma legible para el usuario.
recibe: - header: puntero al BinHeader que se desea imprimir.
retorna: void. La función solo imprime en consola.
*/
void bin_print_header(const BinHeader *header) {
    printf("\n[BIN HEADER]\n");
    printf("Magic            : 0x%08X\n", header->magic);
    printf("Version          : %u\n", header->version);
    printf("Width            : %u\n", header->width);
    printf("Height           : %u\n", header->height);
    printf("Row stride bytes : %u\n", header->row_stride_bytes);
    printf("Speed ms         : %u\n", header->speed_ms);
    printf("Payload size     : %u bytes\n", header->payload_size);
    printf("Cipher rounds    : %u\n", header->cipher_rounds);
    printf("Nonce0           : 0x%08X\n", header->nonce0);
    printf("Nonce1           : 0x%08X\n\n", header->nonce1);
}