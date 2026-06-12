import argparse
import ctypes
import struct
import time  # <--- Importación necesaria para el temporizador
from array import array
from pathlib import Path

BIN_MAGIC = 0x50424D42  # "PBMB"
BIN_VERSION = 1

XTEA_KEY = [0xA56BABCD, 0x0000FACE, 0xDEADBEEF, 0x01234567]
DEFAULT_ROUNDS = 64
DEFAULT_SPEED_MS = 4


def read_pbm_p1(filename):
    """Lee un PBM ASCII P1 y devuelve las dimensiones y un array nativo de bytes rápidos."""
    tokens = []
    with open(filename, "r", encoding="utf-8") as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
            if "#" in line:
                line = line[: line.index("#")]
            tokens.extend(line.split())

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
            f"Cantidad de píxeles inválida. Esperado={expected_pixels}, recibido={len(pixel_tokens)}"
        )

    pixels = array("B", (int(token) for token in pixel_tokens))
    return width, height, pixels


def compute_row_stride_bytes(width):
    return ((width + 63) // 64) * 8


def main():
    parser = argparse.ArgumentParser(
        description="Cliente Python que procesa y cifra PBM delegando a C."
    )
    parser.add_argument("input_pbm", help="Ruta del archivo PBM P1.")
    parser.add_argument("output_bin", help="Ruta del archivo .bin de salida.")
    parser.add_argument("--speed-ms", type=int, default=DEFAULT_SPEED_MS)
    parser.add_argument("--rounds", type=int, default=DEFAULT_ROUNDS)
    args = parser.parse_args()

    lib_path = Path(__file__).parent / "libencoder.so"
    if not lib_path.exists():
        raise FileNotFoundError(
            f"No se encontró '{lib_path.name}'. Recuerda compilarla con gcc."
        )

    lib = ctypes.CDLL(str(lib_path))

    lib.process_and_encrypt.argtypes = [
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint8),
        (ctypes.c_uint32 * 4),
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint8),
    ]
    lib.process_and_encrypt.restype = ctypes.c_int

    width, height, pixels = read_pbm_p1(args.input_pbm)
    row_stride_bytes = compute_row_stride_bytes(width)
    payload_size = height * row_stride_bytes

    nonce0 = 0x13579BDF
    nonce1 = 0x2468ACE0

    c_pixels = (ctypes.c_uint8 * len(pixels)).from_buffer(pixels)
    c_key = (ctypes.c_uint32 * 4)(*XTEA_KEY)
    output_buffer = (ctypes.c_uint8 * payload_size)()

    # =========================================================================
    # MEDICIÓN DE TIEMPO DE LA BIBLIOTECA C
    # =========================================================================
    print("\n[INFO] Iniciando procesamiento en biblioteca C...")
    start_time = time.perf_counter()

    result = lib.process_and_encrypt(
        width,
        height,
        c_pixels,
        c_key,
        args.rounds,
        nonce0,
        nonce1,
        row_stride_bytes,
        output_buffer,
    )

    end_time = time.perf_counter()
    # =========================================================================

    if result != 0:
        raise RuntimeError("Error crítico durante el procesamiento en C.")

    # Calcular la duración exacta en milisegundos
    elapsed_time_ms = (end_time - start_time) * 1000

    header = struct.pack(
        "!10I",
        BIN_MAGIC,
        BIN_VERSION,
        width,
        height,
        row_stride_bytes,
        args.speed_ms,
        payload_size,
        args.rounds,
        nonce0,
        nonce1,
    )

    with open(args.output_bin, "wb") as file:
        file.write(header)
        file.write(bytes(output_buffer))

    print("✓ BIN generado correctamente vía C (.so)")
    print(f"Payload size       : {payload_size} bytes")
    print(f"Tiempo de C (.so)  : {elapsed_time_ms:.4f} ms\n")


if __name__ == "__main__":
    main()