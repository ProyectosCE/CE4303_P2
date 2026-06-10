import argparse


def read_pbm_p1(filename):
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
        raise ValueError(f"{filename}: PBM inválido o incompleto.")

    if tokens[0] != "P1":
        raise ValueError(f"{filename}: solo se soporta formato P1.")

    width = int(tokens[1])
    height = int(tokens[2])

    expected = width * height
    pixel_tokens = tokens[3:]

    if len(pixel_tokens) != expected:
        raise ValueError(
            f"{filename}: cantidad de píxeles inválida. "
            f"Esperado={expected}, recibido={len(pixel_tokens)}"
        )

    pixels = []

    for token in pixel_tokens:
        value = int(token)

        if value not in (0, 1):
            raise ValueError(f"{filename}: pixel inválido {value}")

        pixels.append(value)

    return width, height, pixels


def main():
    parser = argparse.ArgumentParser(
        description="Compara dos archivos PBM P1 pixel por pixel."
    )

    parser.add_argument("original", help="PBM original.")
    parser.add_argument("decoded", help="PBM reconstruido/descifrado.")

    args = parser.parse_args()

    width_a, height_a, pixels_a = read_pbm_p1(args.original)
    width_b, height_b, pixels_b = read_pbm_p1(args.decoded)

    print("Comparando PBM...")
    print(f"Original : {args.original}")
    print(f"Decoded  : {args.decoded}")
    print(f"Size A   : {width_a} x {height_a}")
    print(f"Size B   : {width_b} x {height_b}")

    if width_a != width_b or height_a != height_b:
        print("ERROR: dimensiones diferentes.")
        return 1

    differences = 0
    first_differences = []

    for index, (a, b) in enumerate(zip(pixels_a, pixels_b)):
        if a != b:
            differences += 1

            if len(first_differences) < 10:
                row = index // width_a
                col = index % width_a
                first_differences.append((row, col, a, b))

    if differences == 0:
        print("OK: Los archivos PBM son idénticos pixel por pixel.")
        return 0

    print(f"ERROR: Se encontraron {differences} diferencias.")

    print("Primeras diferencias:")
    for row, col, a, b in first_differences:
        print(f"  row={row}, col={col}, original={a}, decoded={b}")

    return 1


if __name__ == "__main__":
    raise SystemExit(main())