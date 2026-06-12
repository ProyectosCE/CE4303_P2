import socket
import struct

IP_PICO = "192.168.5.192"  # Pon la IP de tu Pico
PUERTO = 5001

# --- CONFIGURACIÓN DE COLORES (Valores de 0 a 255) ---
R = [30, 0, 0]
B = [0, 0, 30]
_ = [0, 0, 0]


# Matriz de 9x9
# matriz_ejemplo = [
#     [R, _, _, _, _, _, _, _, R],
#     [_, R, _, _, _, _, _, R, _],
#     [_, _, R, _, _, _, R, _, _],
#     [_, _, _, R, _, R, _, _, _],
#     [_, _, _, _, R, _, _, _, _],
#     [_, _, _, R, _, R, _, _, _],
#     [_, _, R, _, _, _, R, _, _],
#     [_, R, _, _, _, _, _, R, _],
#     [R, _, _, _, _, _, _, _, R],
# ]

matriz_ejemplo = [
    [B, _, _, _, _, _, _, _, B],
    [_, B, _, _, _, _, _, B, _],
    [_, _, B, _, _, _, B, _, _],
    [_, _, _, B, _, B, _, _, _],
    [_, _, _, _, R, _, _, _, _],
    [_, _, _, B, _, B, _, _, _],
    [_, _, B, _, _, _, B, _, _],
    [_, B, _, _, _, _, _, B, _],
    [B, _, _, _, _, _, _, _, B],
]

velocidad_ms = 4 # 4 milisegundos de delay por columna

# --- CONSTRUCCIÓN DEL PAQUETE BINARIO ---
# 1. Empaquetamos la velocidad en 2 bytes (unsigned short, formato 'H')
paquete_binario = struct.pack("!H", velocidad_ms)

# 2. Empaquetamos los colores uno por uno como bytes independientes ('B')
for columna in matriz_ejemplo:
    for led in columna:
        # Añadimos los 3 bytes del LED (Rojo, Verde, Azul)
        paquete_binario += struct.pack("BBB", led[0], led[1], led[2])

# --- ENVÍO POR WIFI ---
try:
    print(f"Enviando {len(paquete_binario)} bytes por WiFi...")
    cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cliente.settimeout(2.0)
    cliente.connect((IP_PICO, PUERTO))
    
    cliente.sendall(paquete_binario)
    cliente.close()
    print("¡Dibujo enviado con éxito en binario!")
    
except Exception as e:
    print(f"Error: {e}")