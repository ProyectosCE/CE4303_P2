#!/usr/bin/env python3

import tkinter as tk
from tkinter import filedialog, messagebox
from pathlib import Path

NUM_LEDS = 9
BYTES_PER_LED = 3
COLUMN_SIZE = NUM_LEDS * BYTES_PER_LED

CELL_WIDTH = 18
CELL_HEIGHT = 28
LED_LABEL_WIDTH = 60
TOP_MARGIN = 60
LEFT_MARGIN = 80


def parse_driver_packet(filename):
    data = Path(filename).read_bytes()

    if len(data) < 2:
        raise ValueError("El archivo no contiene los 2 bytes iniciales de velocidad.")

    speed_ms = int.from_bytes(data[0:2], byteorder="big")
    matrix_bytes = data[2:]

    remainder = len(matrix_bytes) % COLUMN_SIZE
    valid_matrix_bytes = len(matrix_bytes) - remainder
    column_count = valid_matrix_bytes // COLUMN_SIZE

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
        "trailing_bytes": remainder,
        "column_count": column_count,
        "columns": columns,
    }


def rgb_to_hex(rgb):
    r, g, b = rgb
    return f"#{r:02x}{g:02x}{b:02x}"


def rgb_to_text(rgb):
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


class DriverPacketViewer:
    def __init__(self, root):
        self.root = root
        self.root.title("Driver Packet Viewer - Raspberry Pico POV")
        self.root.geometry("1200x700")

        self.packet = None

        self.build_ui()

    def build_ui(self):
        top_frame = tk.Frame(self.root)
        top_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=8)

        open_button = tk.Button(
            top_frame,
            text="Abrir driver_packet.bin",
            command=self.open_file
        )
        open_button.pack(side=tk.LEFT)

        self.info_label = tk.Label(
            top_frame,
            text="No hay archivo cargado.",
            anchor="w",
            justify="left"
        )
        self.info_label.pack(side=tk.LEFT, padx=15)

        control_frame = tk.Frame(self.root)
        control_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=4)

        tk.Label(control_frame, text="Columna inicial:").pack(side=tk.LEFT)

        self.start_entry = tk.Entry(control_frame, width=8)
        self.start_entry.insert(0, "0")
        self.start_entry.pack(side=tk.LEFT, padx=5)

        tk.Label(control_frame, text="Cantidad de columnas:").pack(side=tk.LEFT)

        self.count_entry = tk.Entry(control_frame, width=8)
        self.count_entry.insert(0, "128")
        self.count_entry.pack(side=tk.LEFT, padx=5)

        draw_button = tk.Button(
            control_frame,
            text="Dibujar rango",
            command=self.draw_packet
        )
        draw_button.pack(side=tk.LEFT, padx=10)

        self.selected_label = tk.Label(
            control_frame,
            text="Seleccione una celda para ver detalle.",
            anchor="w",
            justify="left"
        )
        self.selected_label.pack(side=tk.LEFT, padx=15)

        main_frame = tk.Frame(self.root)
        main_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=10, pady=10)

        self.canvas = tk.Canvas(
            main_frame,
            bg="#202020",
            scrollregion=(0, 0, 2000, 600)
        )

        self.h_scroll = tk.Scrollbar(
            main_frame,
            orient=tk.HORIZONTAL,
            command=self.canvas.xview
        )

        self.v_scroll = tk.Scrollbar(
            main_frame,
            orient=tk.VERTICAL,
            command=self.canvas.yview
        )

        self.canvas.configure(
            xscrollcommand=self.h_scroll.set,
            yscrollcommand=self.v_scroll.set
        )

        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.v_scroll.grid(row=0, column=1, sticky="ns")
        self.h_scroll.grid(row=1, column=0, sticky="ew")

        main_frame.rowconfigure(0, weight=1)
        main_frame.columnconfigure(0, weight=1)

        explanation = (
            "Interpretación hardware: la Pico recorre columnas. "
            "En cada columna asigna pixels[0..8] = RGB, ejecuta pixels.write(), "
            "espera velocidad_ms y pasa a la siguiente columna."
        )

        bottom_label = tk.Label(
            self.root,
            text=explanation,
            anchor="w",
            justify="left"
        )
        bottom_label.pack(side=tk.BOTTOM, fill=tk.X, padx=10, pady=5)

    def open_file(self):
        filename = filedialog.askopenfilename(
            title="Abrir driver_packet.bin",
            filetypes=[
                ("Binary files", "*.bin"),
                ("All files", "*.*")
            ]
        )

        if not filename:
            return

        try:
            self.packet = parse_driver_packet(filename)
        except Exception as exc:
            messagebox.showerror("Error", str(exc))
            return

        self.update_info()
        self.draw_packet()

    def update_info(self):
        if self.packet is None:
            return

        packet = self.packet

        expected_packet_size = 2 + packet["column_count"] * COLUMN_SIZE

        info = (
            f"Archivo: {packet['filename']}\n"
            f"Velocidad: {packet['speed_ms']} ms = {packet['speed_seconds']:.6f} s por columna | "
            f"Columnas: {packet['column_count']} | "
            f"LEDs por columna: {NUM_LEDS} | "
            f"Total: {packet['total_bytes']} bytes | "
            f"Esperado válido: {expected_packet_size} bytes"
        )

        if packet["trailing_bytes"] != 0:
            info += f" | WARNING: {packet['trailing_bytes']} bytes sobrantes ignorados."

        self.info_label.configure(text=info)

    def draw_packet(self):
        if self.packet is None:
            return

        try:
            start_col = int(self.start_entry.get())
            count = int(self.count_entry.get())
        except ValueError:
            messagebox.showerror("Error", "Columna inicial y cantidad deben ser enteros.")
            return

        if start_col < 0:
            start_col = 0

        if count <= 0:
            count = 1

        total_columns = self.packet["column_count"]

        if start_col >= total_columns:
            messagebox.showerror("Error", "La columna inicial está fuera de rango.")
            return

        end_col = min(start_col + count, total_columns)

        self.canvas.delete("all")

        visible_count = end_col - start_col

        canvas_width = LEFT_MARGIN + visible_count * CELL_WIDTH + 80
        canvas_height = TOP_MARGIN + NUM_LEDS * CELL_HEIGHT + 80

        self.canvas.configure(scrollregion=(0, 0, canvas_width, canvas_height))

        self.draw_headers(start_col, end_col)
        self.draw_cells(start_col, end_col)

    def draw_headers(self, start_col, end_col):
        self.canvas.create_text(
            10,
            15,
            anchor="w",
            fill="white",
            text="Vista del paquete que recibe la Raspberry Pico"
        )

        self.canvas.create_text(
            10,
            35,
            anchor="w",
            fill="#cccccc",
            text="Horizontal = columnas en el tiempo | Vertical = LEDs físicos pixels[0..8]"
        )

        for led in range(NUM_LEDS):
            y = TOP_MARGIN + led * CELL_HEIGHT + CELL_HEIGHT / 2

            self.canvas.create_text(
                10,
                y,
                anchor="w",
                fill="white",
                text=f"pixels[{led}]"
            )

        for col in range(start_col, end_col):
            local_x = col - start_col
            x = LEFT_MARGIN + local_x * CELL_WIDTH + CELL_WIDTH / 2

            if col % 10 == 0:
                self.canvas.create_text(
                    x,
                    TOP_MARGIN - 10,
                    anchor="s",
                    fill="#cccccc",
                    text=str(col)
                )

    def draw_cells(self, start_col, end_col):
        columns = self.packet["columns"]

        for col in range(start_col, end_col):
            local_x = col - start_col

            for led in range(NUM_LEDS):
                rgb = columns[col][led]
                color = rgb_to_hex(rgb)

                x1 = LEFT_MARGIN + local_x * CELL_WIDTH
                y1 = TOP_MARGIN + led * CELL_HEIGHT
                x2 = x1 + CELL_WIDTH
                y2 = y1 + CELL_HEIGHT

                outline = "#555555"

                cell_id = self.canvas.create_rectangle(
                    x1,
                    y1,
                    x2,
                    y2,
                    fill=color,
                    outline=outline
                )

                label = rgb_to_text(rgb)

                if CELL_WIDTH >= 18:
                    self.canvas.create_text(
                        x1 + CELL_WIDTH / 2,
                        y1 + CELL_HEIGHT / 2,
                        fill="white" if rgb != (0, 0, 0) else "#777777",
                        text=label,
                        font=("Arial", 7)
                    )

                self.canvas.tag_bind(
                    cell_id,
                    "<Button-1>",
                    lambda event, c=col, l=led: self.show_cell_detail(c, l)
                )

    def show_cell_detail(self, col, led):
        rgb = self.packet["columns"][col][led]
        r, g, b = rgb

        text = (
            f"Columna {col}, LED físico pixels[{led}] -> "
            f"RGB=({r}, {g}, {b})"
        )

        if rgb == (0, 0, 0):
            text += " | Apagado"
        else:
            text += " | Encendido"

        self.selected_label.configure(text=text)


def main():
    root = tk.Tk()
    app = DriverPacketViewer(root)
    root.mainloop()


if __name__ == "__main__":
    main()