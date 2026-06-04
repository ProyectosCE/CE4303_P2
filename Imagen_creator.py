import tkinter as tk
from tkinter import filedialog
import math

# ==========================================
# CONFIGURACIÓN
# ==========================================

GRID_SIZE = 32
PIXEL_SIZE = 22

CENTER = GRID_SIZE // 2
RADIUS = GRID_SIZE // 2

# Colores
BG_COLOR = "#1e1e1e"
CANVAS_BG = "#2b2b2b"
PIXEL_OFF = "#f2f2f2"
PIXEL_ON = "#00ff9c"
GRID_COLOR = "#404040"
OUTSIDE_COLOR = "#252525"

BUTTON_BG = "#3a3a3a"
BUTTON_FG = "white"

# Matriz de pixeles
pixels = [[0 for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)]

# ==========================================
# VENTANA
# ==========================================

root = tk.Tk()
root.title("POV Circular Editor")
root.configure(bg=BG_COLOR)

# Frame principal
main_frame = tk.Frame(root, bg=BG_COLOR)
main_frame.pack(padx=15, pady=15)

# Título
title = tk.Label(
    main_frame,
    text="POV DISPLAY EDITOR",
    font=("Consolas", 20, "bold"),
    fg="#00ff9c",
    bg=BG_COLOR
)
title.pack(pady=(0, 10))

# Canvas
canvas = tk.Canvas(
    main_frame,
    width=GRID_SIZE * PIXEL_SIZE,
    height=GRID_SIZE * PIXEL_SIZE,
    bg=CANVAS_BG,
    highlightthickness=0
)
canvas.pack()

# ==========================================
# FUNCIONES
# ==========================================

# Verifica si el pixel está dentro del círculo
def inside_circle(x, y):

    dx = x - CENTER
    dy = y - CENTER

    return dx * dx + dy * dy <= RADIUS * RADIUS


# Dibujar grid circular
def draw_grid():

    canvas.delete("all")

    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):

            x1 = x * PIXEL_SIZE
            y1 = y * PIXEL_SIZE

            x2 = x1 + PIXEL_SIZE
            y2 = y1 + PIXEL_SIZE

            if inside_circle(x, y):

                color = PIXEL_ON if pixels[y][x] == 1 else PIXEL_OFF

                canvas.create_rectangle(
                    x1,
                    y1,
                    x2,
                    y2,
                    fill=color,
                    outline=GRID_COLOR,
                    width=1
                )

            else:

                canvas.create_rectangle(
                    x1,
                    y1,
                    x2,
                    y2,
                    fill=OUTSIDE_COLOR,
                    outline=OUTSIDE_COLOR
                )

    # Contorno exterior circular
    margin = 2

    canvas.create_oval(
        margin,
        margin,
        GRID_SIZE * PIXEL_SIZE - margin,
        GRID_SIZE * PIXEL_SIZE - margin,
        outline="#00ff9c",
        width=3
    )


# ==========================================
# DIBUJO
# ==========================================

mouse_down = False
paint_value = 1


def paint_pixel(event):

    x = event.x // PIXEL_SIZE
    y = event.y // PIXEL_SIZE

    if (
        0 <= x < GRID_SIZE and
        0 <= y < GRID_SIZE and
        inside_circle(x, y)
    ):
        pixels[y][x] = paint_value
        draw_grid()


def start_paint(event):

    global mouse_down
    global paint_value

    mouse_down = True

    x = event.x // PIXEL_SIZE
    y = event.y // PIXEL_SIZE

    if (
        0 <= x < GRID_SIZE and
        0 <= y < GRID_SIZE and
        inside_circle(x, y)
    ):

        paint_value = 0 if pixels[y][x] == 1 else 1

        pixels[y][x] = paint_value
        draw_grid()


def stop_paint(event):

    global mouse_down
    mouse_down = False


def drag_paint(event):

    if mouse_down:
        paint_pixel(event)


canvas.bind("<Button-1>", start_paint)
canvas.bind("<B1-Motion>", drag_paint)
canvas.bind("<ButtonRelease-1>", stop_paint)

# ==========================================
# UTILIDADES
# ==========================================

def clear_canvas():

    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            pixels[y][x] = 0

    draw_grid()


def save_pbm():

    filename = filedialog.asksaveasfilename(
        defaultextension=".pbm",
        filetypes=[("PBM files", "*.pbm")]
    )

    if not filename:
        return

    with open(filename, "w") as f:

        f.write("P1\n")
        f.write(f"{GRID_SIZE} {GRID_SIZE}\n")

        for y in range(GRID_SIZE):

            row_data = []

            for x in range(GRID_SIZE):

                if inside_circle(x, y):
                    row_data.append(str(pixels[y][x]))
                else:
                    row_data.append("0")

            f.write(" ".join(row_data) + "\n")


# ==========================================
# BOTONES
# ==========================================

button_frame = tk.Frame(main_frame, bg=BG_COLOR)
button_frame.pack(pady=12)

save_button = tk.Button(
    button_frame,
    text="Guardar PBM",
    command=save_pbm,
    bg=BUTTON_BG,
    fg=BUTTON_FG,
    activebackground="#00aa66",
    activeforeground="white",
    relief="flat",
    font=("Consolas", 12, "bold"),
    padx=20,
    pady=8,
    cursor="hand2"
)

save_button.pack(side="left", padx=10)

clear_button = tk.Button(
    button_frame,
    text="Limpiar",
    command=clear_canvas,
    bg=BUTTON_BG,
    fg=BUTTON_FG,
    activebackground="#aa3333",
    activeforeground="white",
    relief="flat",
    font=("Consolas", 12, "bold"),
    padx=20,
    pady=8,
    cursor="hand2"
)

clear_button.pack(side="left", padx=10)

# ==========================================
# FOOTER
# ==========================================

footer = tk.Label(
    main_frame,
    text="Click y arrastre para dibujar",
    font=("Consolas", 10),
    fg="#aaaaaa",
    bg=BG_COLOR
)

footer.pack(pady=(5, 0))

# ==========================================
# INICIO
# ==========================================

draw_grid()

root.mainloop()