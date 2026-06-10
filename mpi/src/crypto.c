#include "crypto.h"

/*
 * Misma clave que se usa en el cliente Python.
 */
static const uint32_t XTEA_KEY[4] = {
    0xA56BABCDu,
    0x0000FACEu,
    0xDEADBEEFu,
    0x01234567u
};

static void xtea_encrypt_block(
    uint32_t *v0,
    uint32_t *v1,
    uint32_t rounds
) {
    uint32_t sum = 0;
    uint32_t delta = 0x9E3779B9u;

    for (uint32_t i = 0; i < rounds; i++) {

        *v0 = *v0 + (
            (
                (((*v1 << 4) ^ (*v1 >> 5)) + *v1)
            ) ^
            (
                sum + XTEA_KEY[sum & 3u]
            )
        );

        sum = sum + delta;

        *v1 = *v1 + (
            (
                (((*v0 << 4) ^ (*v0 >> 5)) + *v0)
            ) ^
            (
                sum + XTEA_KEY[(sum >> 11) & 3u]
            )
        );
    }
}

static void put_u32_be(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)((value >> 24) & 0xFF);
    buffer[1] = (uint8_t)((value >> 16) & 0xFF);
    buffer[2] = (uint8_t)((value >> 8) & 0xFF);
    buffer[3] = (uint8_t)(value & 0xFF);
}

/*
 * XTEA en modo CTR.
 *
 * Importante:
 * - CTR cifra y descifra con la misma función.
 * - global_block_offset permite descifrar segmentos repartidos por MPI.
 * - Como cada fila se alinea a 8 bytes, cada worker empieza en un bloque CTR válido.
 */
void xtea_ctr_crypt_segment(
    const uint8_t *input,
    uint8_t *output,
    size_t length,
    uint32_t rounds,
    uint32_t nonce0,
    uint32_t nonce1,
    uint64_t global_block_offset
) {
    uint8_t keystream[8];

    for (size_t offset = 0; offset < length; offset += 8) {

        uint64_t block_index =
            global_block_offset + (offset / 8);

        uint32_t counter_low =
            (uint32_t)(block_index & 0xFFFFFFFFu);

        uint32_t counter_high =
            (uint32_t)((block_index >> 32) & 0xFFFFFFFFu);

        uint32_t v0 =
            nonce0 ^ counter_low;

        uint32_t v1 =
            nonce1 ^ counter_high;

        xtea_encrypt_block(
            &v0,
            &v1,
            rounds
        );

        put_u32_be(&keystream[0], v0);
        put_u32_be(&keystream[4], v1);

        size_t remaining = length - offset;
        size_t chunk = remaining < 8 ? remaining : 8;

        for (size_t i = 0; i < chunk; i++) {
            output[offset + i] =
                input[offset + i] ^ keystream[i];
        }
    }
}