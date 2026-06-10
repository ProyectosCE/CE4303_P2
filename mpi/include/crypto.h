#ifndef POV_CRYPTO_H
#define POV_CRYPTO_H

#include <stdint.h>
#include <stddef.h>


// Constante de máscara para operaciones de 32 bits, útil para asegurar que las operaciones se mantengan dentro del rango de 32 bits.
#define MASK32 0xFFFFFFFFu


/*
Función para cifrar o descifrar un segmento de datos usando XTEA en modo CTR.
- `input`: puntero al buffer de entrada (datos a cifrar o descifrar).
- `output`: puntero al buffer de salida (donde se guardará el resultado).
- `length`: longitud del segmento a procesar en bytes.
- `rounds`: número de rondas de XTEA (usualmente 32).
- `nonce0` y `nonce1`: valores de nonce para el modo CTR, que deben ser únicos para cada cifrado.
- `global_block_offset`: número de bloque global (en bloques de 8 bytes) para calcular el contador de CTR, lo que permite cifrar segmentos de manera independiente y en paralelo.
*/

void xtea_ctr_crypt_segment(
    const uint8_t *input,
    uint8_t *output,
    size_t length,
    uint32_t rounds,
    uint32_t nonce0,
    uint32_t nonce1,
    uint64_t global_block_offset
);

#endif