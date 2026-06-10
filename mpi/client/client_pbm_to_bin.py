'''
1. Lee un PBM P1
2. Lo convierte a bits por filas
3. Empaqueta 8 píxeles por byte
4. Alinea cada fila a múltiplos de 8 bytes
5. Cifra el payload usando XTEA en modo CTR
6. Escribe imagen_payload.bin
'''

import argparse
import struct
from pathlib import Path

BIN_MAGIC = 0x50424D42 # "PBMB" en ASCII
BIN_VERSION = 1 # Versión del formato .bin, para futuras extensiones.

# Clave simétrica de 128 bits para pruebas.
# Debe ser exactamente la misma en C cuando hagamos el worker.
XTEA_KEY = [
    0xA56BABCD,
    0x0000FACE,
    0xDEADBEEF,
    0x01234567,
]

DEFAULT_ROUNDS = 64
DEFAULT_SPEED_MS = 4

MASK32 = 0xFFFFFFFF


def read_pbm_p1(filename):
    """
    Lee una imagen PBM ASCII P1.
    Soporta comentarios con #.
    Retorna: width, height, pixels
    donde pixels es una lista plana row-major con valores 0/1.
    """

    tokens = []

    with open(filename, "r", encoding="utf-8") as file:
        for line in file:
            line = line.strip()

            if not line:
                continue

            if "#" in line:
                line = line[:line.index("#")]

            parts = line.split()
            tokens.extend(parts)

    if len(tokens) < 3:
        raise ValueError("Archivo PBM inválido o incompleto.")

    magic = tokens[0]

    if magic != "P1":
        raise ValueError("Solo se soporta PBM ASCII P1.")

    width = int(tokens[1])
    height = int(tokens[2])

    expected_pixels = width * height
    pixel_tokens = tokens[3:]

    if len(pixel_tokens) != expected_pixels:
        raise ValueError(
            f"Cantidad de píxeles inválida. "
            f"Esperado={expected_pixels}, recibido={len(pixel_tokens)}"
        )

    pixels = []

    for token in pixel_tokens:
        value = int(token)

        if value not in (0, 1):
            raise ValueError("PBM solo puede contener píxeles 0 o 1.")

        pixels.append(value)

    return width, height, pixels


def compute_row_stride_bytes(width):
    """
    Calcula cuántos bytes ocupa cada fila.

    La fila se alinea a 64 bits = 8 bytes.
    Esto permite dividir por filas y descifrar por bloques.
    """

    bits_per_row = width
    blocks_64 = (bits_per_row + 63) // 64
    return blocks_64 * 8


def pack_bits_by_rows(width, height, pixels):
    """
    Empaqueta los píxeles PBM en bits, por filas.

    Orden:
    fila 0, fila 1, fila 2...

    Dentro de cada byte:
    bit más significativo primero.

    Ejemplo:
    pixels: 1 0 1 1 0 0 0 1
    byte:   0b10110001
    """

    row_stride_bytes = compute_row_stride_bytes(width)
    payload = bytearray(height * row_stride_bytes)

    for row in range(height):
        for col in range(width):
            pixel = pixels[row * width + col]

            if pixel == 1:
                byte_index = row * row_stride_bytes + (col // 8)
                bit_offset = 7 - (col % 8)

                payload[byte_index] |= (1 << bit_offset)

    return bytes(payload), row_stride_bytes


def xtea_encrypt_block(v0, v1, key, rounds):
    """
    Cifra un bloque de 64 bits usando XTEA.

    Este bloque se usará como generador de flujo en modo CTR.
    En modo CTR, la misma operación sirve para cifrar y descifrar
    porque el payload se combina con XOR.
    """

    delta = 0x9E3779B9
    total = 0

    for _ in range(rounds):
        v0 = (
            v0 + (
                (((v1 << 4) & MASK32) ^ (v1 >> 5)) + v1
            ) ^ (total + key[total & 3])
        ) & MASK32

        total = (total + delta) & MASK32

        v1 = (
            v1 + (
                (((v0 << 4) & MASK32) ^ (v0 >> 5)) + v0
            ) ^ (total + key[(total >> 11) & 3])
        ) & MASK32

    return v0, v1


def xtea_ctr_crypt(data, key, rounds, nonce0, nonce1):
    """
    Cifra o descifra usando XTEA en modo CTR.

    Cada bloque de 8 bytes genera un keystream.
    Luego se aplica XOR con el payload.

    Como CTR usa XOR:
    cifrar(data) == descifrar(data)
    """

    output = bytearray(len(data))

    for offset in range(0, len(data), 8):
        block_index = offset // 8

        counter_low = block_index & MASK32
        counter_high = (block_index >> 32) & MASK32

        v0 = (nonce0 ^ counter_low) & MASK32
        v1 = (nonce1 ^ counter_high) & MASK32

        ks0, ks1 = xtea_encrypt_block(v0, v1, key, rounds)

        keystream = struct.pack("!II", ks0, ks1)

        chunk = data[offset:offset + 8]

        for i in range(len(chunk)):
            output[offset + i] = chunk[i] ^ keystream[i]

    return bytes(output)


def write_bin_file(
    output_filename,
    width,
    height,
    row_stride_bytes,
    speed_ms,
    encrypted_payload,
    rounds,
    nonce0,
    nonce1
):
    """
    Escribe el archivo .bin final.

    Header:
    10 campos uint32_t, todos big-endian.
    """

    payload_size = len(encrypted_payload)

    header = struct.pack(
        "!10I",
        BIN_MAGIC,
        BIN_VERSION,
        width,
        height,
        row_stride_bytes,
        speed_ms,
        payload_size,
        rounds,
        nonce0,
        nonce1
    )

    with open(output_filename, "wb") as file:
        file.write(header)
        file.write(encrypted_payload)


def main():
    parser = argparse.ArgumentParser(
        description="Convierte una imagen PBM P1 en un payload binario cifrado."
    )

    parser.add_argument(
        "input_pbm",
        help="Ruta del archivo PBM P1 de entrada."
    )

    parser.add_argument(
        "output_bin",
        help="Ruta del archivo .bin de salida."
    )

    parser.add_argument(
        "--speed-ms",
        type=int,
        default=DEFAULT_SPEED_MS,
        help="Delay por fila/columna para el paquete final."
    )

    parser.add_argument(
        "--rounds",
        type=int,
        default=DEFAULT_ROUNDS,
        help="Cantidad de rondas XTEA."
    )

    args = parser.parse_args()

    input_path = Path(args.input_pbm)
    output_path = Path(args.output_bin)

    width, height, pixels = read_pbm_p1(input_path)

    plain_payload, row_stride_bytes = pack_bits_by_rows(
        width,
        height,
        pixels
    )

    nonce0 = 0x13579BDF
    nonce1 = 0x2468ACE0

    encrypted_payload = xtea_ctr_crypt(
        plain_payload,
        XTEA_KEY,
        args.rounds,
        nonce0,
        nonce1
    )

    write_bin_file(
        output_path,
        width,
        height,
        row_stride_bytes,
        args.speed_ms,
        encrypted_payload,
        args.rounds,
        nonce0,
        nonce1
    )

    print("BIN generado correctamente")
    print(f"Input PBM          : {input_path}")
    print(f"Output BIN         : {output_path}")
    print(f"Width              : {width}")
    print(f"Height             : {height}")
    print(f"Row stride bytes   : {row_stride_bytes}")
    print(f"Payload size       : {len(encrypted_payload)} bytes")
    print(f"Speed ms           : {args.speed_ms}")
    print(f"Cipher rounds      : {args.rounds}")
    print(f"Magic              : 0x{BIN_MAGIC:08X}")


if __name__ == "__main__":
    main()