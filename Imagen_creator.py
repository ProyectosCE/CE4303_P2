
import tkinter as tk
from tkinter import filedialog

# Configuración
GRID_SIZE = 16
PIXEL_SIZE = 70

# Matriz de pixeles (0 = blanco, 1 = negro)
pixels = [[0 for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)]

# Crear ventana
root = tk.Tk()
root.title("Editor PBM")

canvas = tk.Canvas(
    root,
    width=GRID_SIZE * PIXEL_SIZE,
    height=GRID_SIZE * PIXEL_SIZE,
    bg="white"
)
canvas.pack()

# Dibujar cuadricula
def draw_grid():
    canvas.delete("all")

    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            color = "black" if pixels[y][x] == 1 else "white"

            canvas.create_rectangle(
                x * PIXEL_SIZE,
                y * PIXEL_SIZE,
                (x + 1) * PIXEL_SIZE,
                (y + 1) * PIXEL_SIZE,
                fill=color,
                outline="gray"
            )

# Estado del mouse
mouse_down = False
paint_value = 1
rectangle_mode = False
start_rect = None

# Dibujar rectángulo completo
def fill_rectangle(x1, y1, x2, y2, value):
    xmin = min(x1, x2)
    xmax = max(x1, x2)
    ymin = min(y1, y2)
    ymax = max(y1, y2)

    for y in range(ymin, ymax + 1):
        for x in range(xmin, xmax + 1):
            if 0 <= x < GRID_SIZE and 0 <= y < GRID_SIZE:
                pixels[y][x] = value

    draw_grid()

# Activar/desactivar modo rectángulo
def toggle_rectangle_mode():
    global rectangle_mode
    rectangle_mode = not rectangle_mode

    if rectangle_mode:
        mode_button.config(text="Modo: Rectángulo")
    else:
        mode_button.config(text="Modo: Dibujo")

# Pintar pixel
def paint_pixel(event):
    x = event.x // PIXEL_SIZE
    y = event.y // PIXEL_SIZE

    if 0 <= x < GRID_SIZE and 0 <= y < GRID_SIZE:
        pixels[y][x] = paint_value
        draw_grid()

# Mouse presionado
def start_paint(event):
    global mouse_down, paint_value, start_rect

    mouse_down = True

    x = event.x // PIXEL_SIZE
    y = event.y // PIXEL_SIZE

    if 0 <= x < GRID_SIZE and 0 <= y < GRID_SIZE:
        # Si el pixel ya está negro, borrar
        # Si está blanco, pintar
        paint_value = 0 if pixels[y][x] == 1 else 1

        if rectangle_mode:
            start_rect = (x, y)
        else:
            pixels[y][x] = paint_value
            draw_grid()

# Mouse soltado
def stop_paint(event):
    global mouse_down, start_rect

    if rectangle_mode and start_rect is not None:
        x2 = event.x // PIXEL_SIZE
        y2 = event.y // PIXEL_SIZE

        fill_rectangle(start_rect[0], start_rect[1], x2, y2, paint_value)
        start_rect = None

    mouse_down = False

# Arrastrar mouse
def drag_paint(event):
    if mouse_down:
        paint_pixel(event)

canvas.bind("<Button-1>", start_paint)
canvas.bind("<B1-Motion>", drag_paint)
canvas.bind("<ButtonRelease-1>", stop_paint)

# Guardar archivo PBM
def save_pbm():
    filename = filedialog.asksaveasfilename(
        defaultextension=".pbm",
        filetypes=[("PBM files", "*.pbm")]
    )

    if not filename:
        return

    with open(filename, "w") as f:
        # Encabezado PBM ASCII
        f.write("P1\n")
        f.write(f"{GRID_SIZE} {GRID_SIZE}\n")

        # Datos de pixeles
        for row in pixels:
            f.write(" ".join(str(pixel) for pixel in row) + "\n")

    print(f"Imagen guardada en: {filename}")


# Botón guardar
save_button = tk.Button(root, text="Guardar PBM", command=save_pbm)
save_button.pack(pady=5)

# Botón modo rectángulo
mode_button = tk.Button(root, text="Modo: Dibujo", command=toggle_rectangle_mode)
mode_button.pack(pady=5)


# Dibujar inicial
draw_grid()

# Ejecutar interfaz
root.mainloop()

