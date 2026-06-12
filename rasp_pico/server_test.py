import socket
import struct
import math

IP_PICO = "192.168.0.93"  # Pon la IP de tu Pico
PUERTO = 5001

# --- CONFIGURACIÓN POV ---
NUM_LEDS = 9
NUM_COLUMNAS = 48            # resolución angular (ángulos por vuelta)
GAP_OFFSET = 1.0             # distancia del primer LED al centro (en px de la grilla 18x18)
ESPACIADO_LED = (9 - GAP_OFFSET) / (NUM_LEDS - 1)  # cubre hasta el borde (radio 9)

velocidad_ms = 4  # delay por columna en ms

# --- COLORES ---
R = (30, 0, 0)
B = (0, 0, 30)
_ = (0, 0, 0)

# --- MATRIZ 18x18 ---
imagen_rgb = [
    [R, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, R],
    [_, R, _, _, _, _, _, _, _, _, _, _, _, _, _, _, R, _],
    [_, _, R, _, _, _, _, _, _, _, _, _, _, _, _, R, _, _],
    [_, _, _, R, _, _, _, _, _, _, _, _, _, _, R, _, _, _],
    [_, _, _, _, R, _, _, _, _, _, _, _, _, R, _, _, _, _],
    [_, _, _, _, _, R, _, _, _, _, _, _, R, _, _, _, _, _],
    [_, _, _, _, _, _, R, _, _, _, _, R, _, _, _, _, _, _],
    [_, _, _, _, _, _, _, R, _, _, R, _, _, _, _, _, _, _],
    [_, _, _, _, _, _, _, _, B, B, _, _, _, _, _, _, _, _],
    [_, _, _, _, _, _, _, _, B, B, _, _, _, _, _, _, _, _],
    [_, _, _, _, _, _, _, R, _, _, R, _, _, _, _, _, _, _],
    [_, _, _, _, _, _, R, _, _, _, _, R, _, _, _, _, _, _],
    [_, _, _, _, _, R, _, _, _, _, _, _, R, _, _, _, _, _],
    [_, _, _, _, R, _, _, _, _, _, _, _, _, R, _, _, _, _],
    [_, _, _, R, _, _, _, _, _, _, _, _, _, _, R, _, _, _],
    [_, _, R, _, _, _, _, _, _, _, _, _, _, _, _, R, _, _],
    [_, R, _, _, _, _, _, _, _, _, _, _, _, _, _, _, R, _],
    [R, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, R],
]


def matriz_a_columnas_pov(imagen_rgb, num_columnas, num_leds=9,
                           gap_offset=1.0, espaciado_led=1.0):
    alto = len(imagen_rgb)
    ancho = len(imagen_rgb[0])
    cx, cy = (ancho - 1) / 2.0, (alto - 1) / 2.0

    columnas = []
    for j in range(num_columnas):
        theta = 2 * math.pi * j / num_columnas
        columna = []
        for i in range(num_leds):
            r = gap_offset + i * espaciado_led
            x = cx + r * math.cos(theta)
            y = cy + r * math.sin(theta)
            xi, yi = int(round(x)), int(round(y))
            if 0 <= xi < ancho and 0 <= yi < alto:
                px = imagen_rgb[yi][xi]
            else:
                px = (0, 0, 0)
            columna.append(tuple(int(c) for c in px))
        columnas.append(columna)
    return columnas


# --- CONSTRUCCIÓN DEL PAQUETE BINARIO ---
columnas = matriz_a_columnas_pov(
    imagen_rgb,
    num_columnas=NUM_COLUMNAS,
    num_leds=NUM_LEDS,
    gap_offset=GAP_OFFSET,
    espaciado_led=ESPACIADO_LED,
)

paquete_binario = struct.pack("!H", velocidad_ms)

for columna in columnas:
    for led in columna:
        paquete_binario += struct.pack("BBB", led[0], led[1], led[2])

# --- ENVÍO POR WIFI ---
try:
    print(f"Enviando {len(paquete_binario)} bytes por WiFi "
          f"({NUM_COLUMNAS} columnas x {NUM_LEDS} LEDs)...")
    cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cliente.settimeout(2.0)
    cliente.connect((IP_PICO, PUERTO))

    cliente.sendall(paquete_binario)
    cliente.close()
    print("¡Dibujo enviado con éxito en binario!")

except Exception as e:
    print(f"Error: {e}")