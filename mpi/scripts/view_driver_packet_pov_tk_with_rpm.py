#!/usr/bin/env python3

import math
import sys
import time
import tkinter as tk
from tkinter import filedialog, messagebox
from pathlib import Path

# ============================================================
# SIMULADOR POV PARA driver_packet.bin
# ============================================================
#
# Compatible con el receptor de la Raspberry Pico:
#
#   NUM_LEDS = 9
#   Primeros 2 bytes: velocidad en milisegundos, big-endian
#   Resto del paquete: columnas
#   Cada columna: 9 LEDs * 3 bytes RGB = 27 bytes
#
# La Pico interpreta el paquete así:
#
#   for columna in grafico_pov:
#       for i in range(NUM_LEDS):
#           pixels[i] = columna[i]
#       pixels.write()
#       time.sleep(velocidad_giro)
#
# Este visor simula esa lógica como un aspa rotando sobre un eje.
# También permite controlar la velocidad angular en RPM.
# ============================================================

NUM_LEDS = 9
BYTES_PER_LED = 3
COLUMN_SIZE = NUM_LEDS * BYTES_PER_LED

WINDOW_WIDTH = 1320
WINDOW_HEIGHT = 800

CANVAS_BG = "#101010"
PANEL_BG = "#1f1f1f"
TEXT_FG = "#eeeeee"

CENTER_X = 420
CENTER_Y = 370

INNER_RADIUS = 24
LED_SPACING = 28
LED_RADIUS = 9

TRAIL_STEPS = 60
ANIMATION_FRAME_MS = 16  # ~60 FPS para modo RPM.


def parse_driver_packet(filename):
    data = Path(filename).read_bytes()

    if len(data) < 2:
        raise ValueError("El archivo no contiene los 2 bytes iniciales de velocidad.")

    speed_ms = int.from_bytes(data[0:2], byteorder="big")
    matrix_bytes = data[2:]

    trailing_bytes = len(matrix_bytes) % COLUMN_SIZE
    valid_matrix_bytes = len(matrix_bytes) - trailing_bytes
    column_count = valid_matrix_bytes // COLUMN_SIZE
    row_count = NUM_LEDS

    if column_count == 0:
        raise ValueError(
            "El archivo no contiene ninguna columna válida. "
            "Se necesitan al menos 2 + 27 bytes."
        )

    columns = []
    offset = 0

    for _ in range(column_count):
        column = []

        for _ in range(NUM_LEDS):
            r = matrix_bytes[offset]
            g = matrix_bytes[offset + 1]
            b = matrix_bytes[offset + 2]
            column.append((r, g, b))
            offset += BYTES_PER_LED

        columns.append(column)

    return {
        "filename": str(filename),
        "total_bytes": len(data),
        "speed_ms": speed_ms,
        "speed_seconds": speed_ms / 1000.0,
        "matrix_bytes": len(matrix_bytes),
        "valid_matrix_bytes": valid_matrix_bytes,
        "trailing_bytes": trailing_bytes,
        "column_count": column_count,
        "row_count": row_count,
        "columns": columns,
    }


def clamp_color(value):
    return max(0, min(255, int(value)))


def rgb_to_hex(rgb):
    r, g, b = rgb
    return f"#{clamp_color(r):02x}{clamp_color(g):02x}{clamp_color(b):02x}"


def dim_rgb(rgb, factor):
    r, g, b = rgb
    return (
        clamp_color(r * factor),
        clamp_color(g * factor),
        clamp_color(b * factor),
    )


def rgb_is_on(rgb):
    return rgb != (0, 0, 0)


def rgb_to_label(rgb):
    r, g, b = rgb

    if (r, g, b) == (0, 0, 0):
        return "OFF"

    if r >= g and r >= b:
        return "R"

    if g >= r and g >= b:
        return "G"

    if b >= r and b >= g:
        return "B"

    return "ON"


class PovRotatingViewer:
    def __init__(self, root, initial_file=None):
        self.root = root
        self.root.title("POV Rotating Blade Simulator - Raspberry Pico")
        self.root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        self.root.configure(bg=PANEL_BG)

        self.packet = None
        self.current_column = 0
        self.column_phase = 0.0
        self.is_running = False
        self.animation_job = None

        # Modo 1: delay por columna, equivalente al time.sleep() de la Pico.
        self.speed_override_ms = None

        # Modo 2: velocidad angular física en revoluciones por minuto.
        # Cuando se activa, se calcula cuántas columnas avanzan por frame.
        self.angular_velocity_rpm_override = None
        self.last_frame_time = None

        self.build_ui()

        if initial_file is not None:
            self.load_file(initial_file)

    # ========================================================
    # UI
    # ========================================================

    def build_ui(self):
        top_frame = tk.Frame(self.root, bg=PANEL_BG)
        top_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=8)

        open_button = tk.Button(
            top_frame,
            text="Abrir driver_packet.bin",
            command=self.open_file,
        )
        open_button.pack(side=tk.LEFT, padx=4)

        self.play_button = tk.Button(
            top_frame,
            text="▶ Play",
            command=self.toggle_play,
            state=tk.DISABLED,
        )
        self.play_button.pack(side=tk.LEFT, padx=4)

        reset_button = tk.Button(
            top_frame,
            text="Reiniciar",
            command=self.reset_animation,
        )
        reset_button.pack(side=tk.LEFT, padx=4)

        tk.Label(
            top_frame,
            text="Delay columna ms:",
            bg=PANEL_BG,
            fg=TEXT_FG,
        ).pack(side=tk.LEFT, padx=(18, 4))

        self.speed_entry = tk.Entry(top_frame, width=8)
        self.speed_entry.pack(side=tk.LEFT, padx=4)

        apply_speed_button = tk.Button(
            top_frame,
            text="Aplicar delay",
            command=self.apply_speed_override,
        )
        apply_speed_button.pack(side=tk.LEFT, padx=4)

        tk.Label(
            top_frame,
            text="Vel angular RPM:",
            bg=PANEL_BG,
            fg=TEXT_FG,
        ).pack(side=tk.LEFT, padx=(18, 4))

        self.angular_entry = tk.Entry(top_frame, width=9)
        self.angular_entry.pack(side=tk.LEFT, padx=4)

        apply_angular_button = tk.Button(
            top_frame,
            text="Aplicar RPM",
            command=self.apply_angular_velocity_override,
        )
        apply_angular_button.pack(side=tk.LEFT, padx=4)

        use_bin_button = tk.Button(
            top_frame,
            text="Usar velocidad del bin",
            command=self.use_packet_speed,
        )
        use_bin_button.pack(side=tk.LEFT, padx=4)

        self.info_label = tk.Label(
            top_frame,
            text="No hay archivo cargado.",
            anchor="w",
            justify="left",
            bg=PANEL_BG,
            fg=TEXT_FG,
        )
        self.info_label.pack(side=tk.LEFT, padx=15)

        body_frame = tk.Frame(self.root, bg=PANEL_BG)
        body_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=10, pady=8)

        self.canvas = tk.Canvas(
            body_frame,
            bg=CANVAS_BG,
            width=880,
            height=660,
            highlightthickness=0,
        )
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        side_panel = tk.Frame(body_frame, bg=PANEL_BG, width=360)
        side_panel.pack(side=tk.RIGHT, fill=tk.Y, padx=(10, 0))

        self.column_label = tk.Label(
            side_panel,
            text="Columna actual: -",
            bg=PANEL_BG,
            fg=TEXT_FG,
            anchor="w",
            justify="left",
            font=("Consolas", 11, "bold"),
        )
        self.column_label.pack(side=tk.TOP, fill=tk.X, pady=(0, 8))

        self.leds_text = tk.Text(
            side_panel,
            width=48,
            height=25,
            bg="#151515",
            fg="#eeeeee",
            insertbackground="#eeeeee",
            font=("Consolas", 10),
        )
        self.leds_text.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        explanation = (
            "Simulación:\n"
            "- Cada columna del bin equivale a un instante angular.\n"
            "- La barra/aspa tiene 9 LEDs físicos.\n"
            "- Delay columna ms: simula time.sleep() por columna.\n"
            "- Vel angular RPM: simula la rotación física del aspa.\n"
            "- RPM calcula el avance angular usando las columnas detectadas.\n"
            "- Al girar rápido, la persistencia visual forma una imagen circular."
        )

        self.explanation_label = tk.Label(
            side_panel,
            text=explanation,
            bg=PANEL_BG,
            fg=TEXT_FG,
            anchor="w",
            justify="left",
            wraplength=350,
        )
        self.explanation_label.pack(side=tk.BOTTOM, fill=tk.X, pady=8)

        bottom_frame = tk.Frame(self.root, bg=PANEL_BG)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=10, pady=6)

        self.status_label = tk.Label(
            bottom_frame,
            text="Abra un driver_packet.bin para iniciar.",
            bg=PANEL_BG,
            fg="#bbbbbb",
            anchor="w",
            justify="left",
        )
        self.status_label.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.draw_empty_scene()

    # ========================================================
    # FILE
    # ========================================================

    def open_file(self):
        filename = filedialog.askopenfilename(
            title="Abrir driver_packet.bin",
            filetypes=[
                ("Binary files", "*.bin"),
                ("All files", "*.*"),
            ],
        )

        if filename:
            self.load_file(filename)

    def load_file(self, filename):
        try:
            self.packet = parse_driver_packet(filename)
        except Exception as exc:
            messagebox.showerror("Error", str(exc))
            return

        self.current_column = 0
        self.column_phase = 0.0
        self.is_running = False
        self.speed_override_ms = None
        self.angular_velocity_rpm_override = None
        self.last_frame_time = None
        self.cancel_animation_job()

        self.play_button.configure(text="▶ Play", state=tk.NORMAL)

        self.speed_entry.delete(0, tk.END)
        self.speed_entry.insert(0, str(self.packet["speed_ms"]))

        self.angular_entry.delete(0, tk.END)
        self.angular_entry.insert(0, f"{self.compute_rpm_from_delay(self.packet['speed_ms']):.3f}")

        self.update_info()
        self.draw_current_state()
        self.update_led_text()

    # ========================================================
    # CONTROL
    # ========================================================

    def toggle_play(self):
        if self.packet is None:
            return

        self.is_running = not self.is_running

        if self.is_running:
            self.play_button.configure(text="⏸ Pausa")
            self.last_frame_time = None
            self.schedule_next_frame()
        else:
            self.play_button.configure(text="▶ Play")
            self.cancel_animation_job()

    def cancel_animation_job(self):
        if self.animation_job is not None:
            self.root.after_cancel(self.animation_job)
            self.animation_job = None

    def reset_animation(self):
        self.current_column = 0
        self.column_phase = 0.0
        self.last_frame_time = None

        if self.packet is not None:
            self.draw_current_state()
            self.update_led_text()

    def apply_speed_override(self):
        if self.packet is None:
            messagebox.showerror("Error", "Primero abra un driver_packet.bin.")
            return

        try:
            value = float(self.speed_entry.get())
        except ValueError:
            messagebox.showerror("Error", "El delay debe ser un número en milisegundos.")
            return

        if value <= 0:
            messagebox.showerror("Error", "El delay debe ser mayor que 0 ms.")
            return

        self.speed_override_ms = value
        self.angular_velocity_rpm_override = None
        self.last_frame_time = None

        rpm = self.compute_rpm_from_delay(value)
        self.angular_entry.delete(0, tk.END)
        self.angular_entry.insert(0, f"{rpm:.3f}")

        self.status_label.configure(
            text=(
                f"Modo delay por columna: {value:.3f} ms. "
                f"Equivale a {rpm:.3f} RPM con {self.packet['column_count']} columnas."
            )
        )

        self.draw_current_state()
        self.update_led_text()

    def apply_angular_velocity_override(self):
        if self.packet is None:
            messagebox.showerror("Error", "Primero abra un driver_packet.bin.")
            return

        try:
            rpm = float(self.angular_entry.get())
        except ValueError:
            messagebox.showerror("Error", "La velocidad angular debe ser un número en RPM.")
            return

        if rpm <= 0:
            messagebox.showerror("Error", "La velocidad angular debe ser mayor que 0 RPM.")
            return

        self.angular_velocity_rpm_override = rpm
        self.speed_override_ms = None
        self.last_frame_time = None

        delay_ms = self.compute_delay_from_rpm(rpm)
        self.speed_entry.delete(0, tk.END)
        self.speed_entry.insert(0, f"{delay_ms:.6f}")

        self.status_label.configure(
            text=(
                f"Modo velocidad angular: {rpm:.3f} RPM. "
                f"Delay equivalente por columna: {delay_ms:.6f} ms."
            )
        )

        self.draw_current_state()
        self.update_led_text()

    def use_packet_speed(self):
        if self.packet is None:
            return

        self.speed_override_ms = None
        self.angular_velocity_rpm_override = None
        self.last_frame_time = None

        self.speed_entry.delete(0, tk.END)
        self.speed_entry.insert(0, str(self.packet["speed_ms"]))

        rpm = self.compute_rpm_from_delay(self.packet["speed_ms"])
        self.angular_entry.delete(0, tk.END)
        self.angular_entry.insert(0, f"{rpm:.3f}")

        self.status_label.configure(
            text="Usando velocidad guardada en el driver_packet.bin."
        )

        self.draw_current_state()
        self.update_led_text()

    def is_angular_mode(self):
        return self.angular_velocity_rpm_override is not None

    def compute_rpm_from_delay(self, delay_ms):
        if self.packet is None:
            return 0.0

        column_count = self.packet["column_count"]

        if delay_ms <= 0 or column_count <= 0:
            return 0.0

        return 60000.0 / (delay_ms * column_count)

    def compute_delay_from_rpm(self, rpm):
        if self.packet is None:
            return 0.0

        column_count = self.packet["column_count"]

        if rpm <= 0 or column_count <= 0:
            return 0.0

        return 60000.0 / (rpm * column_count)

    def get_current_column_delay_ms(self):
        if self.packet is None:
            return 10.0

        if self.angular_velocity_rpm_override is not None:
            return self.compute_delay_from_rpm(self.angular_velocity_rpm_override)

        if self.speed_override_ms is not None:
            return float(self.speed_override_ms)

        return max(0.001, float(self.packet["speed_ms"]))

    def get_current_rpm(self):
        if self.packet is None:
            return 0.0

        if self.angular_velocity_rpm_override is not None:
            return self.angular_velocity_rpm_override

        return self.compute_rpm_from_delay(self.get_current_column_delay_ms())

    def get_columns_per_second(self):
        if self.packet is None:
            return 0.0

        rpm = self.get_current_rpm()
        return (rpm / 60.0) * self.packet["column_count"]

    def schedule_next_frame(self):
        if not self.is_running or self.packet is None:
            return

        if self.is_angular_mode():
            self.step_animation_by_rpm()
            self.animation_job = self.root.after(
                ANIMATION_FRAME_MS,
                self.schedule_next_frame,
            )
        else:
            self.step_animation_by_delay()
            delay = max(1, int(round(self.get_current_column_delay_ms())))
            self.animation_job = self.root.after(delay, self.schedule_next_frame)

    def step_animation_by_delay(self):
        if self.packet is None:
            return

        total_columns = self.packet["column_count"]
        self.current_column = (self.current_column + 1) % total_columns
        self.column_phase = float(self.current_column)

        self.draw_current_state()
        self.update_led_text()

    def step_animation_by_rpm(self):
        if self.packet is None:
            return

        now = time.perf_counter()

        if self.last_frame_time is None:
            delta_seconds = ANIMATION_FRAME_MS / 1000.0
        else:
            delta_seconds = now - self.last_frame_time

        self.last_frame_time = now

        total_columns = self.packet["column_count"]
        columns_per_second = self.get_columns_per_second()
        self.column_phase = (self.column_phase + columns_per_second * delta_seconds) % total_columns
        self.current_column = int(self.column_phase) % total_columns

        self.draw_current_state()
        self.update_led_text()

    # ========================================================
    # INFO
    # ========================================================

    def update_info(self):
        if self.packet is None:
            return

        packet = self.packet
        expected_packet_size = 2 + packet["column_count"] * COLUMN_SIZE
        rpm_from_bin = self.compute_rpm_from_delay(packet["speed_ms"])

        info = (
            f"Archivo: {Path(packet['filename']).name}\n"
            f"Velocidad bin: {packet['speed_ms']} ms "
            f"({packet['speed_seconds']:.6f} s) | "
            f"RPM bin: {rpm_from_bin:.3f} | "
            f"Filas: {packet['row_count']} LEDs | "
            f"Columnas: {packet['column_count']} | "
            f"Total: {packet['total_bytes']} bytes | "
            f"Esperado: {expected_packet_size} bytes"
        )

        if packet["trailing_bytes"] != 0:
            info += (
                f" | WARNING: {packet['trailing_bytes']} bytes sobrantes "
                "serían ignorados por la Pico."
            )

        self.info_label.configure(text=info)

        self.status_label.configure(
            text=(
                "Formato detectado automáticamente: "
                "2 bytes velocidad + columnas de 27 bytes "
                "(9 LEDs * 3 RGB)."
            )
        )

    def update_led_text(self):
        if self.packet is None:
            return

        column = self.packet["columns"][self.current_column]
        delay_ms = self.get_current_column_delay_ms()
        rpm = self.get_current_rpm()
        mode = "RPM" if self.is_angular_mode() else "delay"

        self.column_label.configure(
            text=(
                f"Columna actual: {self.current_column} / "
                f"{self.packet['column_count'] - 1}\n"
                f"Modo: {mode} | RPM: {rpm:.3f} | "
                f"delay/col: {delay_ms:.6f} ms"
            )
        )

        self.leds_text.delete("1.0", tk.END)

        self.leds_text.insert(tk.END, "Lo que la Pico carga en hardware:\n\n")
        self.leds_text.insert(tk.END, "for i in range(NUM_LEDS):\n")

        for led_index, rgb in enumerate(column):
            r, g, b = rgb
            status = "ENCENDIDO" if rgb_is_on(rgb) else "apagado"
            symbol = rgb_to_label(rgb)

            self.leds_text.insert(
                tk.END,
                (
                    f"  pixels[{led_index}] = "
                    f"({r:3d}, {g:3d}, {b:3d}) "
                    f"{symbol:>3s} {status}\n"
                ),
            )

        self.leds_text.insert(tk.END, "\npixels.write()\n")
        self.leds_text.insert(tk.END, f"time.sleep({delay_ms / 1000.0:.9f})\n")
        self.leds_text.insert(
            tk.END,
            (
                "\nRelación POV:\n"
                f"  columnas/seg = {self.get_columns_per_second():.3f}\n"
                f"  RPM = {rpm:.3f}\n"
                f"  grados/col = {360.0 / self.packet['column_count']:.6f}\n"
            ),
        )

    # ========================================================
    # DRAWING
    # ========================================================

    def draw_empty_scene(self):
        self.canvas.delete("all")

        self.canvas.create_text(
            CENTER_X,
            CENTER_Y,
            fill="#bbbbbb",
            text="Abra un driver_packet.bin para simular el aspa POV",
            font=("Arial", 16, "bold"),
        )

    def draw_current_state(self):
        if self.packet is None:
            self.draw_empty_scene()
            return

        self.canvas.delete("all")
        self.draw_static_reference()
        self.draw_persistence_trail()
        self.draw_blade()
        self.draw_hardware_labels()

    def draw_static_reference(self):
        max_radius = INNER_RADIUS + (NUM_LEDS - 1) * LED_SPACING + 26

        self.canvas.create_oval(
            CENTER_X - max_radius,
            CENTER_Y - max_radius,
            CENTER_X + max_radius,
            CENTER_Y + max_radius,
            outline="#303030",
            width=2,
        )

        for led in range(NUM_LEDS):
            radius = INNER_RADIUS + led * LED_SPACING

            self.canvas.create_oval(
                CENTER_X - radius,
                CENTER_Y - radius,
                CENTER_X + radius,
                CENTER_Y + radius,
                outline="#1f1f1f",
            )

        self.canvas.create_oval(
            CENTER_X - 6,
            CENTER_Y - 6,
            CENTER_X + 6,
            CENTER_Y + 6,
            fill="#aaaaaa",
            outline="",
        )

        self.canvas.create_text(
            20,
            20,
            anchor="w",
            fill="#eeeeee",
            text="Simulación POV: aspa de 9 LEDs girando sobre un eje",
            font=("Arial", 14, "bold"),
        )

        self.canvas.create_text(
            20,
            45,
            anchor="w",
            fill="#bbbbbb",
            text=(
                "Rastro = persistencia visual. "
                "RPM controla la velocidad angular física simulada."
            ),
        )

    def column_angle(self, column_index):
        total = self.packet["column_count"]
        return (2.0 * math.pi * column_index) / total

    def led_position(self, angle, led_index):
        radius = INNER_RADIUS + led_index * LED_SPACING
        x = CENTER_X + math.cos(angle) * radius
        y = CENTER_Y + math.sin(angle) * radius
        return x, y

    def draw_persistence_trail(self):
        total_columns = self.packet["column_count"]
        columns = self.packet["columns"]

        steps = min(TRAIL_STEPS, total_columns)

        for age in range(steps, 0, -1):
            col_index = (self.current_column - age) % total_columns
            angle = self.column_angle(col_index)
            column = columns[col_index]
            factor = 0.10 + 0.70 * ((steps - age) / steps)

            for led_index, rgb in enumerate(column):
                if not rgb_is_on(rgb):
                    continue

                x, y = self.led_position(angle, led_index)
                dimmed = dim_rgb(rgb, factor)
                color = rgb_to_hex(dimmed)
                radius = max(2, int(LED_RADIUS * factor))

                self.canvas.create_oval(
                    x - radius,
                    y - radius,
                    x + radius,
                    y + radius,
                    fill=color,
                    outline="",
                )

    def draw_blade(self):
        columns = self.packet["columns"]
        angle = self.column_angle(self.current_column)
        end_x, end_y = self.led_position(angle, NUM_LEDS - 1)

        self.canvas.create_line(
            CENTER_X,
            CENTER_Y,
            end_x,
            end_y,
            fill="#666666",
            width=4,
        )

        column = columns[self.current_column]

        for led_index, rgb in enumerate(column):
            x, y = self.led_position(angle, led_index)
            color = rgb_to_hex(rgb)
            outline = "#ffffff" if rgb_is_on(rgb) else "#444444"

            self.canvas.create_oval(
                x - LED_RADIUS,
                y - LED_RADIUS,
                x + LED_RADIUS,
                y + LED_RADIUS,
                fill=color,
                outline=outline,
                width=2,
            )

            self.canvas.create_text(
                x,
                y - 18,
                fill="#dddddd",
                text=str(led_index),
                font=("Arial", 8),
            )

        degrees = math.degrees(angle)
        rpm = self.get_current_rpm()
        delay_ms = self.get_current_column_delay_ms()
        mode = "velocidad angular" if self.is_angular_mode() else "delay por columna"

        self.canvas.create_text(
            20,
            70,
            anchor="w",
            fill="#eeeeee",
            text=(
                f"Columna {self.current_column} | "
                f"Ángulo {degrees:.2f}° | "
                f"Modo {mode} | "
                f"RPM {rpm:.3f} | "
                f"delay/col {delay_ms:.6f} ms"
            ),
        )

    def draw_hardware_labels(self):
        self.canvas.create_text(
            20,
            630,
            anchor="w",
            fill="#bbbbbb",
            text=(
                "Traducción directa: columna actual -> "
                "pixels[0], pixels[1], ..., pixels[8] -> pixels.write()"
            ),
        )


def main():
    initial_file = sys.argv[1] if len(sys.argv) >= 2 else None

    root = tk.Tk()
    PovRotatingViewer(root, initial_file=initial_file)
    root.mainloop()


if __name__ == "__main__":
    main()