#!/usr/bin/env python3

import math
import sys
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
# ============================================================

NUM_LEDS = 9
BYTES_PER_LED = 3
COLUMN_SIZE = NUM_LEDS * BYTES_PER_LED

WINDOW_WIDTH = 1220
WINDOW_HEIGHT = 780

CANVAS_BG = "#101010"
PANEL_BG = "#1f1f1f"
TEXT_FG = "#eeeeee"

CENTER_X = 420
CENTER_Y = 360

INNER_RADIUS = 24
LED_SPACING = 28
LED_RADIUS = 9

TRAIL_STEPS = 60


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
        self.is_running = False
        self.animation_job = None
        self.speed_override_ms = None

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
            text="Delay x ms:",
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
            width=850,
            height=640,
            highlightthickness=0,
        )
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        side_panel = tk.Frame(body_frame, bg=PANEL_BG, width=320)
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
            width=44,
            height=24,
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
            "- En cada paso se cargan los 9 RGB en pixels[0..8].\n"
            "- Luego la Pico hace pixels.write() y espera x ms.\n"
            "- Al girar rápido, la persistencia visual forma una imagen circular."
        )

        self.explanation_label = tk.Label(
            side_panel,
            text=explanation,
            bg=PANEL_BG,
            fg=TEXT_FG,
            anchor="w",
            justify="left",
            wraplength=320,
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
        self.is_running = False
        self.speed_override_ms = None

        self.play_button.configure(text="▶ Play", state=tk.NORMAL)

        self.speed_entry.delete(0, tk.END)
        self.speed_entry.insert(0, str(self.packet["speed_ms"]))

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

        if self.packet is not None:
            self.draw_current_state()
            self.update_led_text()

    def apply_speed_override(self):
        try:
            value = int(self.speed_entry.get())
        except ValueError:
            messagebox.showerror("Error", "El delay debe ser un entero en milisegundos.")
            return

        if value < 1:
            messagebox.showerror("Error", "El delay debe ser al menos 1 ms.")
            return

        self.speed_override_ms = value
        self.status_label.configure(text=f"Delay de simulación actualizado a {value} ms.")

    def get_current_delay_ms(self):
        if self.speed_override_ms is not None:
            return self.speed_override_ms

        if self.packet is None:
            return 10

        return max(1, int(self.packet["speed_ms"]))

    def schedule_next_frame(self):
        if not self.is_running or self.packet is None:
            return

        self.step_animation()

        delay = self.get_current_delay_ms()
        self.animation_job = self.root.after(delay, self.schedule_next_frame)

    def step_animation(self):
        if self.packet is None:
            return

        total_columns = self.packet["column_count"]
        self.current_column = (self.current_column + 1) % total_columns

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

        info = (
            f"Archivo: {Path(packet['filename']).name}\n"
            f"Velocidad bin: {packet['speed_ms']} ms "
            f"({packet['speed_seconds']:.6f} s) | "
            f"Filas detectadas: {packet['row_count']} LEDs | "
            f"Columnas detectadas: {packet['column_count']} | "
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

        self.column_label.configure(
            text=(
                f"Columna actual: {self.current_column} / "
                f"{self.packet['column_count'] - 1}"
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
        self.leds_text.insert(
            tk.END,
            f"time.sleep({self.get_current_delay_ms() / 1000.0:.6f})\n",
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
            text="Los puntos con rastro representan columnas anteriores retenidas visualmente.",
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

        self.canvas.create_text(
            20,
            70,
            anchor="w",
            fill="#eeeeee",
            text=(
                f"Columna {self.current_column} | "
                f"Ángulo {degrees:.2f}° | "
                f"Delay {self.get_current_delay_ms()} ms"
            ),
        )

    def draw_hardware_labels(self):
        self.canvas.create_text(
            20,
            610,
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