import argparse
import math


def generate_pattern(width, height, pattern):
    pixels = []

    cx = width / 2.0
    cy = height / 2.0
    radius = min(width, height) * 0.40

    for y in range(height):
        row = []

        for x in range(width):

            if pattern == "circle":
                dx = x - cx
                dy = y - cy
                value = 1 if dx * dx + dy * dy <= radius * radius else 0

            elif pattern == "checker":
                block = 16
                value = 1 if ((x // block) + (y // block)) % 2 == 0 else 0

            elif pattern == "diagonal":
                value = 1 if abs(x - y) < 8 or abs((width - x) - y) < 8 else 0

            elif pattern == "waves":
                wave = math.sin(x * 0.05) + math.cos(y * 0.05)
                value = 1 if wave > 0 else 0

            else:
                value = 1 if (x + y) % 2 == 0 else 0

            row.append(value)

        pixels.append(row)

    return pixels


def write_pbm(filename, width, height, pixels):
    with open(filename, "w", encoding="utf-8") as file:
        file.write("P1\n")
        file.write(f"{width} {height}\n")

        for y in range(height):
            file.write(" ".join(str(bit) for bit in pixels[y]))
            file.write("\n")


def main():
    parser = argparse.ArgumentParser(
        description="Genera una imagen PBM grande para pruebas MPI."
    )

    parser.add_argument("output", help="Archivo PBM de salida.")
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=1024)
    parser.add_argument(
        "--pattern",
        choices=["circle", "checker", "diagonal", "waves"],
        default="waves"
    )

    args = parser.parse_args()

    pixels = generate_pattern(
        args.width,
        args.height,
        args.pattern
    )

    write_pbm(
        args.output,
        args.width,
        args.height,
        pixels
    )

    print("PBM generado correctamente")
    print(f"Output  : {args.output}")
    print(f"Width   : {args.width}")
    print(f"Height  : {args.height}")
    print(f"Pattern : {args.pattern}")


if __name__ == "__main__":
    main()