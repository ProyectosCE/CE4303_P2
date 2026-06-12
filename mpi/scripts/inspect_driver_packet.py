#!/usr/bin/env python3

import argparse
from pathlib import Path

NUM_LEDS = 9
BYTES_PER_LED = 3
COLUMN_SIZE = NUM_LEDS * BYTES_PER_LED


def rgb_to_symbol(rgb):
    r, g, b = rgb

    if (r, g, b) == (0, 0, 0):
        return "."

    if r >= g and r >= b:
        return "R"

    if g >= r and g >= b:
        return "G"

    if b >= r and b >= g:
        return "B"

    return "#"


def rgb_to_status(rgb):
    r, g, b = rgb

    if (r, g, b) == (0, 0, 0):
        return "OFF"

    return "ON"


def parse_driver_packet(filename):
    data = Path(filename).read_bytes()

    if len(data) < 2:
        raise ValueError("El archivo no tiene ni siquiera los 2 bytes de velocidad.")

    speed_ms = int.from_bytes(data[0:2], byteorder="big")
    matrix_bytes = data[2:]

    remainder = len(matrix_bytes) % COLUMN_SIZE

    valid_matrix_bytes = len(matrix_bytes) - remainder
    column_count = valid_matrix_bytes // COLUMN_SIZE

    columns = []

    offset = 0

    for col_index in range(column_count):
        column = []

        for led_index in range(NUM_LEDS):
            r = matrix_bytes[offset]
            g = matrix_bytes[offset + 1]
            b = matrix_bytes[offset + 2]

            column.append((r, g, b))

            offset += BYTES_PER_LED

        columns.append(column)

    return {
        "filename": filename,
        "total_bytes": len(data),
        "speed_ms": speed_ms,
        "speed_seconds": speed_ms / 1000.0,
        "matrix_bytes": len(matrix_bytes),
        "valid_matrix_bytes": valid_matrix_bytes,
        "trailing_bytes": remainder,
        "column_count": column_count,
        "columns": columns,
    }


def print_summary(packet):
    expected_total = 2 + packet["column_count"] * COLUMN_SIZE

    print("\n=== DRIVER PACKET INSPECTOR ===\n")

    print(f"Archivo                  : {packet['filename']}")
    print(f"Tamaño total             : {packet['total_bytes']} bytes")
    print(f"Velocidad raw            : {packet['speed_ms']} ms")
    print(f"Velocidad interpretada   : {packet['speed_seconds']:.6f} s por columna")
    print(f"Bytes de matriz          : {packet['matrix_bytes']} bytes")
    print(f"Columnas válidas         : {packet['column_count']}")
    print(f"LEDs por columna         : {NUM_LEDS}")
    print(f"Bytes por LED            : {BYTES_PER_LED}")
    print(f"Bytes por columna        : {COLUMN_SIZE}")
    print(f"Tamaño esperado válido   : {expected_total} bytes")

    if packet["trailing_bytes"] != 0:
        print(f"WARNING: sobran {packet['trailing_bytes']} bytes al final.")
        print("La Pico los ignoraría porque no completan una columna de 27 bytes.")

    print("\nInterpretación en la Pico:")
    print("1. Lee los primeros 2 bytes como velocidad.")
    print("2. Divide el resto en columnas de 27 bytes.")
    print("3. Cada columna contiene 9 LEDs.")
    print("4. Cada LED recibe 3 bytes: R, G, B.")
    print("5. Para cada columna, ejecuta pixels[i] = (R,G,B), pixels.write(), sleep(velocidad).")


def print_columns(packet, max_columns):
    columns = packet["columns"]

    if not columns:
        print("\nNo hay columnas válidas para mostrar.")
        return

    limit = min(max_columns, len(columns))

    print(f"\n=== PRIMERAS {limit} COLUMNAS ===\n")

    for col_index in range(limit):
        column = columns[col_index]

        print(f"Columna {col_index}:")
        print("  Esta columna se escribe completa en la tira NeoPixel:")

        for led_index, rgb in enumerate(column):
            r, g, b = rgb
            symbol = rgb_to_symbol(rgb)
            status = rgb_to_status(rgb)

            print(
                f"    LED {led_index}: "
                f"RGB=({r:3d}, {g:3d}, {b:3d}) "
                f"Estado={status:3s} "
                f"Símbolo={symbol}"
            )

        print()


def print_ascii_preview(packet, max_columns):
    columns = packet["columns"]

    if not columns:
        return

    limit = min(max_columns, len(columns))

    print(f"\n=== VISTA REPRESENTATIVA {limit} COLUMNAS x {NUM_LEDS} LEDS ===\n")

    print("Cada carácter representa un LED en una columna:")
    print("  . = apagado")
    print("  R = rojo dominante")
    print("  G = verde dominante")
    print("  B = azul dominante")
    print("  # = otro color encendido")
    print()

    print("La Pico recorre de izquierda a derecha:")
    print("columna 0, columna 1, columna 2, ...")
    print()

    header = "LED\\COL "
    for col in range(limit):
        header += f"{col % 10}"

    print(header)

    for led_index in range(NUM_LEDS):
        row = f"LED {led_index:02d}  "

        for col_index in range(limit):
            rgb = columns[col_index][led_index]
            row += rgb_to_symbol(rgb)

        print(row)

    print("\nTraducción a hardware:")
    print("Para cada columna mostrada arriba:")
    print("  pixels[0] recibe el color de LED 0")
    print("  pixels[1] recibe el color de LED 1")
    print("  ...")
    print("  pixels[8] recibe el color de LED 8")
    print("Luego la Pico ejecuta pixels.write() y espera velocidad_ms.")


def main():
    parser = argparse.ArgumentParser(
        description="Inspecciona driver_packet.bin y muestra cómo lo interpretaría la Raspberry Pico."
    )

    parser.add_argument(
        "packet",
        help="Ruta del archivo driver_packet.bin."
    )

    parser.add_argument(
        "--columns",
        type=int,
        default=16,
        help="Cantidad de columnas a imprimir como vista representativa."
    )

    parser.add_argument(
        "--details",
        type=int,
        default=4,
        help="Cantidad de columnas a imprimir con detalle LED por LED."
    )

    args = parser.parse_args()

    packet = parse_driver_packet(args.packet)

    print_summary(packet)
    print_columns(packet, args.details)
    print_ascii_preview(packet, args.columns)


if __name__ == "__main__":
    main()