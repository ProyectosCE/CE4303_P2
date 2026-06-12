#!/usr/bin/env python3

import argparse
import ctypes
import math
import shlex
import shutil
import struct
import subprocess
import sys
import time
from array import array
from pathlib import Path


BIN_MAGIC = 0x50424D42  # "PBMB"
BIN_VERSION = 1

XTEA_KEY = [0xA56BABCD, 0x0000FACE, 0xDEADBEEF, 0x01234567]

DEFAULT_ROUNDS = 64
DEFAULT_SPEED_MS = 4

DEFAULT_NONCE0 = 0x13579BDF
DEFAULT_NONCE1 = 0x2468ACE0


# ============================================================
# PBM GENERATOR
# ============================================================

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
    filename = Path(filename)
    filename.parent.mkdir(parents=True, exist_ok=True)

    with filename.open("w", encoding="utf-8") as file:
        file.write("P1\n")
        file.write(f"{width} {height}\n")

        for y in range(height):
            file.write(" ".join(str(bit) for bit in pixels[y]))
            file.write("\n")


def generate_pbm_file(output, width, height, pattern):
    print("\n[PBM GENERATOR]")
    print(f"Output  : {output}")
    print(f"Width   : {width}")
    print(f"Height  : {height}")
    print(f"Pattern : {pattern}")

    start = time.perf_counter()

    pixels = generate_pattern(width, height, pattern)
    write_pbm(output, width, height, pixels)

    elapsed_ms = (time.perf_counter() - start) * 1000.0

    print(f"PBM generado correctamente en {elapsed_ms:.4f} ms")


# ============================================================
# PBM READER + ENCODER
# ============================================================

def read_pbm_p1(filename):
    tokens = []

    with open(filename, "r", encoding="utf-8") as file:
        for line in file:
            line = line.strip()

            if not line:
                continue

            if "#" in line:
                line = line[:line.index("#")]

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
            f"Cantidad de píxeles inválida. "
            f"Esperado={expected_pixels}, recibido={len(pixel_tokens)}"
        )

    pixels = array("B", (int(token) for token in pixel_tokens))

    return width, height, pixels


def compute_row_stride_bytes(width):
    return ((width + 63) // 64) * 8


def load_encoder_library(lib_path):
    lib_path = Path(lib_path)

    if not lib_path.exists():
        raise FileNotFoundError(
            f"No se encontró '{lib_path}'. "
            "Compile la biblioteca C como libencoder.so."
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

    return lib


def encode_pbm_to_bin(
    input_pbm,
    output_bin,
    lib_path,
    speed_ms,
    rounds,
    nonce0,
    nonce1
):
    print("\n[PBM -> BIN ENCRYPTED]")
    print(f"Input PBM : {input_pbm}")
    print(f"Output BIN: {output_bin}")
    print(f"Speed ms  : {speed_ms}")
    print(f"Rounds    : {rounds}")
    print(f"Nonce0    : 0x{nonce0:08X}")
    print(f"Nonce1    : 0x{nonce1:08X}")

    if speed_ms < 0 or speed_ms > 0xFFFFFFFF:
        raise ValueError("speed_ms debe estar entre 0 y 4294967295.")

    if rounds < 1 or rounds > 0xFFFFFFFF:
        raise ValueError("rounds debe estar entre 1 y 4294967295.")

    lib = load_encoder_library(lib_path)

    width, height, pixels = read_pbm_p1(input_pbm)
    row_stride_bytes = compute_row_stride_bytes(width)
    payload_size = height * row_stride_bytes

    if payload_size > 0xFFFFFFFF:
        raise ValueError("payload_size excede uint32_t. Reduzca el tamaño de imagen.")

    c_pixels = (ctypes.c_uint8 * len(pixels)).from_buffer(pixels)
    c_key = (ctypes.c_uint32 * 4)(*XTEA_KEY)
    output_buffer = (ctypes.c_uint8 * payload_size)()

    print("\n[INFO] Iniciando procesamiento en biblioteca C...")
    start_time = time.perf_counter()

    result = lib.process_and_encrypt(
        width,
        height,
        c_pixels,
        c_key,
        rounds,
        nonce0,
        nonce1,
        row_stride_bytes,
        output_buffer,
    )

    elapsed_time_ms = (time.perf_counter() - start_time) * 1000.0

    if result != 0:
        raise RuntimeError("Error crítico durante el procesamiento en C.")

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
        nonce1,
    )

    output_bin = Path(output_bin)
    output_bin.parent.mkdir(parents=True, exist_ok=True)

    with output_bin.open("wb") as file:
        file.write(header)
        file.write(bytes(output_buffer))

    print("\n✓ BIN generado correctamente vía C (.so)")
    print(f"Width             : {width}")
    print(f"Height            : {height}")
    print(f"Row stride bytes  : {row_stride_bytes}")
    print(f"Payload size      : {payload_size} bytes")
    print(f"Total file size   : {40 + payload_size} bytes")
    print(f"Tiempo de C (.so) : {elapsed_time_ms:.4f} ms")

    return {
        "width": width,
        "height": height,
        "row_stride_bytes": row_stride_bytes,
        "payload_size": payload_size,
        "total_file_size": 40 + payload_size,
    }


# ============================================================
# SSH / SCP TRANSPORT
# ============================================================

def ensure_command_exists(command):
    if shutil.which(command) is None:
        raise RuntimeError(f"No se encontró el comando requerido: {command}")


def command_to_string(command):
    return " ".join(shlex.quote(str(part)) for part in command)


def run_command(command, description):
    print(f"\n[{description}]")
    print("$ " + command_to_string(command))

    process = subprocess.run(
        command,
        text=True,
    )

    if process.returncode != 0:
        raise RuntimeError(
            f"Comando falló con código {process.returncode}: {description}"
        )


def run_command_capture(command, description):
    print(f"\n[{description}]")
    print("$ " + command_to_string(command))

    process = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    if process.stdout:
        print(process.stdout)

    if process.returncode != 0:
        raise RuntimeError(
            f"Comando falló con código {process.returncode}: {description}"
        )

    return process.stdout


def scp_upload(local_file, server, remote_file):
    run_command(
        ["scp", str(local_file), f"{server}:{remote_file}"],
        "SCP upload payload"
    )


def scp_download(server, remote_file, local_file):
    local_file = Path(local_file)
    local_file.parent.mkdir(parents=True, exist_ok=True)

    run_command(
        ["scp", f"{server}:{remote_file}", str(local_file)],
        "SCP download result"
    )


def ssh_run_remote_script(server, remote_dir, remote_script):
    inner_command = (
        f"cd {shlex.quote(remote_dir)} && "
        f"chmod +x {shlex.quote(remote_script)} && "
        f"{shlex.quote(remote_script)}"
    )

    remote_command = (
        "bash -lc "
        + shlex.quote(inner_command)
    )

    return run_command_capture(
        ["ssh", server, remote_command],
        "SSH ejecutar script MPI remoto"
    )

def transport_ssh_scp(
    local_bin,
    server,
    remote_dir,
    remote_name,
    remote_script,
    fetch_results,
    results_dir
):
    ensure_command_exists("ssh")
    ensure_command_exists("scp")

    local_bin = Path(local_bin)

    if not local_bin.exists():
        raise FileNotFoundError(f"No existe el BIN local: {local_bin}")

    # Tu run_mpi_host.sh espera normalmente:
    #
    #   mpirun -hostfile=hosts.txt ./mpi_proyectosce imagen_payload.bin --send
    #
    # Entonces este cliente sube el archivo directamente a remote_dir
    # con el nombre remote_name, que por defecto es imagen_payload.bin.
    remote_payload_path = f"{remote_dir.rstrip('/')}/{remote_name}"

    print("\n[SSH/SCP TRANSPORT]")
    print(f"Server             : {server}")
    print(f"Remote dir         : {remote_dir}")
    print(f"Remote payload     : {remote_payload_path}")
    print(f"Remote script      : {remote_script}")
    print(f"Fetch results      : {fetch_results}")

    scp_upload(
        local_bin,
        server,
        remote_payload_path
    )

    ssh_run_remote_script(
        server=server,
        remote_dir=remote_dir,
        remote_script=remote_script
    )

    if fetch_results:
        results_dir = Path(results_dir)
        results_dir.mkdir(parents=True, exist_ok=True)

        remote_pbm = f"{remote_dir.rstrip('/')}/resultado_mpi_driver.pbm"
        remote_packet = f"{remote_dir.rstrip('/')}/driver_packet.bin"

        local_pbm = results_dir / "resultado_mpi_driver.pbm"
        local_packet = results_dir / "driver_packet.bin"

        scp_download(server, remote_pbm, local_pbm)
        scp_download(server, remote_packet, local_packet)

        print("\n[RESULTADOS DESCARGADOS]")
        print(f"PBM final      : {local_pbm}")
        print(f"Driver packet  : {local_packet}")


# ============================================================
# MAIN
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description=(
            "Cliente externo: genera PBM, cifra a .bin y ejecuta MPI "
            "en servidor usando SSH/SCP y un script remoto."
        )
    )

    input_group = parser.add_argument_group("Entrada PBM")
    input_group.add_argument(
        "--input-pbm",
        help="PBM P1 existente que se va a cifrar."
    )
    input_group.add_argument(
        "--generate-pbm",
        help="Ruta donde generar un PBM P1 antes de cifrar."
    )
    input_group.add_argument("--width", type=int, default=1024)
    input_group.add_argument("--height", type=int, default=1024)
    input_group.add_argument(
        "--pattern",
        choices=["circle", "checker", "diagonal", "waves"],
        default="waves"
    )

    encode_group = parser.add_argument_group("Cifrado / BIN")
    encode_group.add_argument(
        "--output-bin",
        default="payload.bin",
        help="Archivo .bin local generado."
    )
    encode_group.add_argument(
        "--libencoder",
        default=None,
        help="Ruta a libencoder.so. Por defecto: junto a este script."
    )
    encode_group.add_argument("--speed-ms", type=int, default=DEFAULT_SPEED_MS)
    encode_group.add_argument("--rounds", type=int, default=DEFAULT_ROUNDS)
    encode_group.add_argument("--nonce0", type=lambda x: int(x, 0), default=DEFAULT_NONCE0)
    encode_group.add_argument("--nonce1", type=lambda x: int(x, 0), default=DEFAULT_NONCE1)

    ssh_group = parser.add_argument_group("SSH/SCP")
    ssh_group.add_argument(
        "--ssh",
        action="store_true",
        help="Activar envío por SSH/SCP y ejecución remota del script MPI."
    )
    ssh_group.add_argument(
        "--server",
        help="Servidor SSH. Ejemplo: alex@192.168.101.43"
    )
    ssh_group.add_argument(
        "--remote-dir",
        help="Directorio remoto donde está run_mpi_host.sh, hosts.txt y mpi_proyectosce."
    )
    ssh_group.add_argument(
        "--remote-name",
        default="imagen_payload.bin",
        help="Nombre remoto del payload. Debe coincidir con run_mpi_host.sh."
    )
    ssh_group.add_argument(
        "--remote-script",
        default="./run_mpi_host.sh",
        help="Script remoto que ejecuta MPI. Ejemplo: ./run_mpi_host.sh"
    )
    ssh_group.add_argument(
        "--fetch-results",
        action="store_true",
        help="Descargar resultado_mpi_driver.pbm y driver_packet.bin."
    )
    ssh_group.add_argument(
        "--results-dir",
        default="client_results",
        help="Directorio local donde guardar resultados descargados."
    )

    args = parser.parse_args()

    if args.libencoder is None:
        script_dir = Path(__file__).resolve().parent
        args.libencoder = str(script_dir / "libencoder.so")

    if args.generate_pbm and args.input_pbm:
        print("ERROR: use --generate-pbm o --input-pbm, pero no ambos.")
        return 1

    if not args.generate_pbm and not args.input_pbm:
        print("ERROR: debe usar --generate-pbm o --input-pbm.")
        return 1

    if args.width <= 0 or args.height <= 0:
        print("ERROR: width y height deben ser mayores que cero.")
        return 1

    if args.generate_pbm:
        generate_pbm_file(
            args.generate_pbm,
            args.width,
            args.height,
            args.pattern
        )
        input_pbm = args.generate_pbm
    else:
        input_pbm = args.input_pbm

    encode_pbm_to_bin(
        input_pbm=input_pbm,
        output_bin=args.output_bin,
        lib_path=args.libencoder,
        speed_ms=args.speed_ms,
        rounds=args.rounds,
        nonce0=args.nonce0,
        nonce1=args.nonce1,
    )

    if args.ssh:
        if not args.server:
            print("ERROR: --server es requerido cuando se usa --ssh.")
            return 1

        if not args.remote_dir:
            print("ERROR: --remote-dir es requerido cuando se usa --ssh.")
            return 1

        transport_ssh_scp(
            local_bin=args.output_bin,
            server=args.server,
            remote_dir=args.remote_dir,
            remote_name=args.remote_name,
            remote_script=args.remote_script,
            fetch_results=args.fetch_results,
            results_dir=args.results_dir,
        )
    else:
        print("\n[LOCAL ONLY]")
        print("No se usó --ssh. Solo se generó el archivo .bin local.")

    print("\n✓ Cliente finalizó correctamente.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        sys.exit(1)