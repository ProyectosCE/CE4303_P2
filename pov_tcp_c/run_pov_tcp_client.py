#!/usr/bin/env python3
"""
Wrapper Python para ejecutar pov_tcp_client con configuración sencilla.

Este script NO reemplaza al cliente C. Solo construye el comando, permite
habilitar/deshabilitar parámetros fácilmente, ejecuta pov_tcp_client y muestra
una notificación al terminar.

Uso típico:
  python3 run_pov_tcp_client.py

También se pueden sobrescribir parámetros por consola:
  python3 run_pov_tcp_client.py --server-ip 192.168.1.139 --width 2048 --height 2048
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


# ============================================================
# CONFIGURACIÓN RÁPIDA
# ============================================================
# Cambie estos valores según su entorno.
# Para deshabilitar opciones booleanas, use False.

CONFIG = {
    # Ejecutable C
    "client_bin": "./pov_tcp_client",

    # Servidor TCP
    "server_ip": "192.168.1.139",
    "port": 5050,

    # Imagen PBM a generar
    "width": 1024,
    "height": 1024,
    "pattern": "waves",  # circle, checker, diagonal, waves

    # Cifrado / contrato binario
    "rounds": 128,
    "speed_ms": 4,

    # Archivos locales generados por el cliente C
    "output_bin": "payload_1024.bin",
    "output_pbm": "grande_1024.pbm",
    "results_dir": "client_results",

    # Flags opcionales del cliente C
    "save_pbm": True,       # False => agrega --no-save-pbm
    "run_remote": True,     # False => agrega --no-run
    "return_pbm": True,     # True  => agrega --return-pbm
    "return_packet": True,  # True  => agrega --return-packet

    # Wrapper
    "show_desktop_notification": True,
    "save_log": True,
    "logs_dir": "client_logs",
}


VALID_PATTERNS = {"circle", "checker", "diagonal", "waves"}


# ============================================================
# UTILIDADES
# ============================================================

def q(value: object) -> str:
    return shlex.quote(str(value))


def make_timestamp() -> str:
    return _dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def notify(title: str, message: str, enabled: bool = True) -> None:
    """Muestra notificación de escritorio si es posible; si no, imprime."""
    print(f"\n[{title}]\n{message}\n")

    if not enabled:
        return

    # Linux: notify-send
    if shutil.which("notify-send") is not None:
        subprocess.run(
            ["notify-send", title, message],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        return

    # macOS: osascript
    if sys.platform == "darwin" and shutil.which("osascript") is not None:
        script = f'display notification {message!r} with title {title!r}'
        subprocess.run(
            ["osascript", "-e", script],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        return

    # Fallback simple: campana en terminal
    print("\a", end="")


def apply_cli_overrides(config: dict, args: argparse.Namespace) -> dict:
    cfg = dict(config)

    for key in [
        "client_bin",
        "server_ip",
        "port",
        "width",
        "height",
        "pattern",
        "rounds",
        "speed_ms",
        "output_bin",
        "output_pbm",
        "results_dir",
    ]:
        value = getattr(args, key, None)
        if value is not None:
            cfg[key] = value

    # Flags booleanos con override explícito.
    if args.save_pbm is not None:
        cfg["save_pbm"] = args.save_pbm
    if args.run_remote is not None:
        cfg["run_remote"] = args.run_remote
    if args.return_pbm is not None:
        cfg["return_pbm"] = args.return_pbm
    if args.return_packet is not None:
        cfg["return_packet"] = args.return_packet
    if args.notify is not None:
        cfg["show_desktop_notification"] = args.notify

    return cfg


def validate_config(cfg: dict) -> None:
    if cfg["pattern"] not in VALID_PATTERNS:
        raise ValueError(
            f"pattern inválido: {cfg['pattern']}. "
            f"Use uno de: {', '.join(sorted(VALID_PATTERNS))}"
        )

    if int(cfg["width"]) <= 0 or int(cfg["height"]) <= 0:
        raise ValueError("width y height deben ser mayores que cero.")

    if int(cfg["port"]) <= 0 or int(cfg["port"]) > 65535:
        raise ValueError("port debe estar entre 1 y 65535.")

    if int(cfg["rounds"]) <= 0:
        raise ValueError("rounds debe ser mayor que cero.")

    if int(cfg["speed_ms"]) < 0:
        raise ValueError("speed_ms no puede ser negativo.")

    client_bin = Path(cfg["client_bin"])
    if not client_bin.exists():
        raise FileNotFoundError(
            f"No existe el ejecutable C: {client_bin}. "
            "Compile primero pov_tcp_client o ajuste CONFIG['client_bin']."
        )


def build_command(cfg: dict) -> list[str]:
    cmd = [
        str(cfg["client_bin"]),
        "--server-ip", str(cfg["server_ip"]),
        "--port", str(cfg["port"]),
        "--width", str(cfg["width"]),
        "--height", str(cfg["height"]),
        "--pattern", str(cfg["pattern"]),
        "--rounds", str(cfg["rounds"]),
        "--speed-ms", str(cfg["speed_ms"]),
        "--output-bin", str(cfg["output_bin"]),
        "--output-pbm", str(cfg["output_pbm"]),
        "--results-dir", str(cfg["results_dir"]),
    ]

    if not cfg.get("save_pbm", True):
        cmd.append("--no-save-pbm")

    if not cfg.get("run_remote", True):
        cmd.append("--no-run")

    if cfg.get("return_pbm", False):
        cmd.append("--return-pbm")
    else:
        # Solo agregue este flag si su cliente C lo soporta.
        # En la versión generada antes, --return-pbm habilita retorno;
        # no hace falta enviar --no-return-pbm si no lo quiere.
        pass

    if cfg.get("return_packet", False):
        cmd.append("--return-packet")

    return cmd


def expected_outputs(cfg: dict) -> list[Path]:
    outputs: list[Path] = []

    outputs.append(Path(cfg["output_bin"]))

    if cfg.get("save_pbm", True):
        outputs.append(Path(cfg["output_pbm"]))

    results_dir = Path(cfg["results_dir"])

    if cfg.get("return_pbm", False):
        outputs.append(results_dir / "resultado_mpi_driver.pbm")

    if cfg.get("return_packet", False):
        outputs.append(results_dir / "driver_packet.bin")

    return outputs


def save_execution_log(cfg: dict, command: list[str], output: str, returncode: int) -> Path | None:
    if not cfg.get("save_log", True):
        return None

    logs_dir = Path(cfg.get("logs_dir", "client_logs"))
    logs_dir.mkdir(parents=True, exist_ok=True)

    log_path = logs_dir / f"pov_tcp_client_{make_timestamp()}.log"

    with log_path.open("w", encoding="utf-8") as f:
        f.write("[COMMAND]\n")
        f.write(" ".join(q(part) for part in command))
        f.write("\n\n")
        f.write("[CONFIG]\n")
        for key in sorted(cfg.keys()):
            f.write(f"{key} = {cfg[key]}\n")
        f.write("\n")
        f.write(f"[RETURNCODE]\n{returncode}\n\n")
        f.write("[OUTPUT]\n")
        f.write(output)

    return log_path


def run_client(cfg: dict) -> int:
    validate_config(cfg)

    command = build_command(cfg)

    print("\n[POV TCP CLIENT WRAPPER]")
    print(f"Servidor     : {cfg['server_ip']}:{cfg['port']}")
    print(f"Imagen       : {cfg['width']}x{cfg['height']} pattern={cfg['pattern']}")
    print(f"Rounds       : {cfg['rounds']}")
    print(f"Speed ms     : {cfg['speed_ms']}")
    print(f"Output BIN   : {cfg['output_bin']}")
    print(f"Output PBM   : {cfg['output_pbm'] if cfg.get('save_pbm', True) else '(deshabilitado)'}")
    print(f"Results dir  : {cfg['results_dir']}")
    print(f"Run remote   : {cfg.get('run_remote', True)}")
    print(f"Return PBM   : {cfg.get('return_pbm', False)}")
    print(f"Return packet: {cfg.get('return_packet', False)}")

    print("\n[COMMAND]")
    print("$ " + " ".join(q(part) for part in command))

    start = _dt.datetime.now()
    process = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    end = _dt.datetime.now()

    output = process.stdout or ""
    if output:
        print("\n[CLIENT OUTPUT]")
        print(output)

    elapsed = end - start
    log_path = save_execution_log(cfg, command, output, process.returncode)

    existing_outputs = [path for path in expected_outputs(cfg) if path.exists()]

    if process.returncode == 0:
        message_lines = [
            "Proceso terminado correctamente.",
            f"Duración: {elapsed}",
        ]

        if existing_outputs:
            message_lines.append("Archivos generados/recibidos:")
            for path in existing_outputs:
                try:
                    size = path.stat().st_size
                    message_lines.append(f"- {path} ({size} bytes)")
                except OSError:
                    message_lines.append(f"- {path}")
        else:
            message_lines.append("No se detectaron archivos de salida esperados.")

        if log_path is not None:
            message_lines.append(f"Log: {log_path}")

        notify(
            "POV TCP terminado",
            "\n".join(message_lines),
            enabled=cfg.get("show_desktop_notification", True),
        )

        return 0

    message_lines = [
        "El proceso terminó con error.",
        f"Código de salida: {process.returncode}",
        f"Duración: {elapsed}",
    ]

    if log_path is not None:
        message_lines.append(f"Log: {log_path}")

    notify(
        "POV TCP falló",
        "\n".join(message_lines),
        enabled=cfg.get("show_desktop_notification", True),
    )

    return process.returncode


# ============================================================
# CLI DEL WRAPPER
# ============================================================

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Wrapper Python para ejecutar pov_tcp_client. "
            "El procesamiento real lo hace el cliente C."
        )
    )

    parser.add_argument("--client-bin", help="Ruta a ./pov_tcp_client")
    parser.add_argument("--server-ip", help="IP del servidor TCP")
    parser.add_argument("--port", type=int, help="Puerto TCP del servidor")

    parser.add_argument("--width", type=int, help="Ancho PBM")
    parser.add_argument("--height", type=int, help="Alto PBM")
    parser.add_argument("--pattern", choices=sorted(VALID_PATTERNS), help="Patrón PBM")

    parser.add_argument("--rounds", type=int, help="Rondas XTEA")
    parser.add_argument("--speed-ms", type=int, help="Velocidad en ms")

    parser.add_argument("--output-bin", help="Archivo .bin local")
    parser.add_argument("--output-pbm", help="Archivo PBM local generado")
    parser.add_argument("--results-dir", help="Directorio de resultados")

    save_pbm_group = parser.add_mutually_exclusive_group()
    save_pbm_group.add_argument("--save-pbm", dest="save_pbm", action="store_true")
    save_pbm_group.add_argument("--no-save-pbm", dest="save_pbm", action="store_false")
    parser.set_defaults(save_pbm=None)

    run_group = parser.add_mutually_exclusive_group()
    run_group.add_argument("--run", dest="run_remote", action="store_true")
    run_group.add_argument("--no-run", dest="run_remote", action="store_false")
    parser.set_defaults(run_remote=None)

    return_pbm_group = parser.add_mutually_exclusive_group()
    return_pbm_group.add_argument("--return-pbm", dest="return_pbm", action="store_true")
    return_pbm_group.add_argument("--no-return-pbm", dest="return_pbm", action="store_false")
    parser.set_defaults(return_pbm=None)

    return_packet_group = parser.add_mutually_exclusive_group()
    return_packet_group.add_argument("--return-packet", dest="return_packet", action="store_true")
    return_packet_group.add_argument("--no-return-packet", dest="return_packet", action="store_false")
    parser.set_defaults(return_packet=None)

    notify_group = parser.add_mutually_exclusive_group()
    notify_group.add_argument("--notify", dest="notify", action="store_true")
    notify_group.add_argument("--no-notify", dest="notify", action="store_false")
    parser.set_defaults(notify=None)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cfg = apply_cli_overrides(CONFIG, args)
    return run_client(cfg)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        notify(
            "POV TCP wrapper error",
            str(exc),
            enabled=CONFIG.get("show_desktop_notification", True),
        )
        sys.exit(1)
