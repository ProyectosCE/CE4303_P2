#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MASK32 0xFFFFFFFF
#define DELTA 0x9E3779B9

// Función interna para cifrar un bloque de 64 bits con XTEA
static void xtea_encrypt_block(uint32_t *v0, uint32_t *v1, const uint32_t key[4], uint32_t rounds) {
    uint32_t total = 0;
    uint32_t y = *v0, z = *v1;

    for (uint32_t i = 0; i < rounds; i++) {
        y = (y + ((((z << 4) ^ (z >> 5)) + z) ^ (total + key[total & 3]))) & MASK32;
        total = (total + DELTA) & MASK32;
        z = (z + ((((y << 4) ^ (y >> 5)) + y) ^ (total + key[(total >> 11) & 3]))) & MASK32;
    }
    *v0 = y;
    *v1 = z;
}

/**
 * Procesa los píxeles, los empaqueta, los alinea y los cifra usando XTEA-CTR.
 * Retorna 0 si todo sale bien, -1 si falla la asignación de memoria.
 */
int process_and_encrypt(
    uint32_t width,
    uint32_t height,
    const uint8_t *pixels,
    const uint32_t key[4],
    uint32_t rounds,
    uint32_t nonce0,
    uint32_t nonce1,
    uint32_t row_stride_bytes,
    uint8_t *output_payload
) {
    // 1. Inicializar el payload con ceros (para asegurar el padding)
    size_t payload_size = (size_t)height * row_stride_bytes;
    memset(output_payload, 0, payload_size);

    // 2. Empaquetar bits por filas de forma nativa
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            uint8_t pixel = pixels[row * width + col];
            if (pixel == 1) {
                size_t byte_index = (size_t)row * row_stride_bytes + (col / 8);
                int bit_offset = 7 - (col % 8);
                output_payload[byte_index] |= (1 << bit_offset);
            }
        }
    }

    // 3. Cifrado XTEA en modo CTR sobre el payload generado
    for (size_t offset = 0; offset < payload_size; offset += 8) {
        size_t block_index = offset / 8;

        uint32_t counter_low = block_index & MASK32;
        uint32_t counter_high = (block_index >> 32) & MASK32;

        uint32_t v0 = nonce0 ^ counter_low;
        uint32_t v1 = nonce1 ^ counter_high;

        xtea_encrypt_block(&v0, &v1, key, rounds);

        // Convertir el keystream resultante a Big Endian para el XOR (equivalente a struct.pack("!II"))
        uint8_t keystream[8];
        keystream[0] = (v0 >> 24) & 0xFF; keystream[1] = (v0 >> 16) & 0xFF;
        keystream[2] = (v0 >> 8)  & 0xFF; keystream[3] = v0 & 0xFF;
        keystream[4] = (v1 >> 24) & 0xFF; keystream[5] = (v1 >> 16) & 0xFF;
        keystream[6] = (v1 >> 8)  & 0xFF; keystream[7] = v1 & 0xFF;

        // Determinar cuántos bytes quedan por procesar en este bloque (por si no es múltiplo estricto de 8)
        size_t chunk_size = (payload_size - offset < 8) ? (payload_size - offset) : 8;

        for (size_t i = 0; i < chunk_size; i++) {
            output_payload[offset + i] ^= keystream[i];
        }
    }

    return 0;
}