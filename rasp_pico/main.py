"""
Archivo: main.py (POV Display - Raspberry Pi Pico 2 W)
Que hace:
  - Conecta la Pico W a una red WiFi.
  - Levanta un servidor socket TCP que recibe paquetes binarios
    con una matriz de colores (columnas x LEDs) y una velocidad.
  - Reproduce esa matriz en una tira NeoPixel para simular un
    efecto de "Persistence of Vision" (POV).
  - Incluye prints de depuración para ver el paquete crudo, la
    velocidad calculada y las columnas decodificadas.
"""

import machine
import neopixel
import network
import socket
import time
import select

# --- CONFIGURACIÓN HARDWARE ---
PIN_NUM = 16 # Pin GPIO donde está conectado el data-in de la tira NeoPixel
NUM_LEDS = 9 # Cantidad de LEDs en la tira
pixels = neopixel.NeoPixel(machine.Pin(PIN_NUM), NUM_LEDS)

# --- CONFIGURACIÓN WIFI ---
WIFI_SSID = "_Casa Campus P5_"
WIFI_PASSWORD = "casacampus"

# Variables de control
grafico_pov = [[(0, 0, 0) for _ in range(NUM_LEDS)]] # lista de columnas, cada columna es una lista de NUM_LEDS tuplas (r,g,b)
velocidad_giro = 0.002  # segundos que se muestra cada columna antes de pasar a la siguiente

"""
Nombre de funcion: apagar_todos
Que hace: Apaga (pone en negro) todos los LEDs de la tira y aplica el cambio.
Que recibe: Nada.
Que retorna: Nada (None). 
Efecto secundario: actualiza el hardware vía pixels.write().
"""
def apagar_todos():
    for i in range(NUM_LEDS):
        pixels[i] = (0, 0, 0)
    pixels.write()

# Conexión WiFi
wlan = network.WLAN(network.STA_IF) # el modo STA_IF conecta la Pico como cliente a una red existente.
wlan.active(True)
wlan.connect(WIFI_SSID, WIFI_PASSWORD)
print("Conectando al WiFi...")
while not wlan.isconnected():
    time.sleep(0.5)
print(f"¡Conectado! IP de la Pico: {wlan.ifconfig()[0]}")

# Servidor Socket
servidor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
servidor.bind(('0.0.0.0', 5001))
servidor.listen(1)
servidor.setblocking(False)
try:
    while True:
        # 1. LEER DATOS BINARIOS DESDE LA RED
        r, w, x = select.select([servidor], [], [], 0)
        if r:
            conexion, direccion = servidor.accept()
            conexion.settimeout(0.5)
            try:
                # Leer todo el paquete binario de golpe (en fragmentos
                # de hasta 1024 bytes) hasta que el cliente cierra el envío.
                datos_binarios = b""
                while True:
                    fragmento = conexion.recv(1024)
                    if not fragmento:
                        break
                    datos_binarios += fragmento

                # --- DEBUG: tamaño y contenido crudo del paquete recibido ---
                print(f"--- Paquete recibido: {len(datos_binarios)} bytes ---")
                print("RAW:", datos_binarios)

                # Procesar si el paquete tiene el tamaño mínimo requerido
                # 2 bytes (velocidad) + al menos una columna (9 LEDs * 3 bytes = 27 bytes)
                if len(datos_binarios) >= 29:
                    # Los primeros 2 bytes determinan la velocidad en milisegundos
                    byte_alto = datos_binarios[0]
                    byte_bajo = datos_binarios[1]
                    ms_velocidad = (byte_alto << 8) | byte_bajo
                    velocidad_giro = ms_velocidad / 1000.0

                    print(f"Bytes velocidad: [{byte_alto}, {byte_bajo}] -> {ms_velocidad} ms -> {velocidad_giro} s")

                    # El resto de los bytes son los colores de la matriz
                    bytes_matriz = datos_binarios[2:]
                    nuevas_columnas = []

                    # Cada columna mide exactamente 27 bytes (9 leds * 3 colores)
                    tamano_columna = NUM_LEDS * 3

                    # Recorrer bytes_matriz en bloques de tamano_columna bytes,
                    # convirtiendo cada bloque en una lista de tuplas (r,g,b)
                    for c in range(0, len(bytes_matriz), tamano_columna):
                        bloque_columna = bytes_matriz[c:c+tamano_columna]
                        if len(bloque_columna) == tamano_columna:
                            columna_leds = []
                            for l in range(0, tamano_columna, 3):
                                r_val = bloque_columna[l]
                                g_val = bloque_columna[l+1]
                                b_val = bloque_columna[l+2]
                                columna_leds.append((r_val, g_val, b_val))
                            nuevas_columnas.append(columna_leds)
                    # Reemplazar el gráfico actual si se logró armar al menos una columna válida
                    if nuevas_columnas:
                        grafico_pov = nuevas_columnas
                        print(f"Matriz actualizada. Columnas: {len(grafico_pov)}, Vel: {velocidad_giro}s")
                        # --- DEBUG: imprime el contenido de cada columna decodificada ---
                        for idx, col in enumerate(grafico_pov):
                            print(f"  Col {idx}: {col}")
                else:
                    # --- DEBUG: paquete incompleto o demasiado corto, se descarta ---
                    print(f"Paquete demasiado pequeño ({len(datos_binarios)} bytes), se ignora")

            except Exception as e:
                print("Error procesando binario:", e)
            finally:
                conexion.close()
        # 2. DIBUJAR EN LOS LEDS
        # Recorrer cada columna del gráfico actual y la mostrar en la tira durante 
        # "velocidad_giro" segundos antes de pasar a la siguiente columna 
        if grafico_pov:
            for columna in grafico_pov:
                for i in range(NUM_LEDS):
                    if i < len(columna):
                        pixels[i] = columna[i]
                pixels.write()
                time.sleep(velocidad_giro)
            
            apagar_todos()
            time.sleep(velocidad_giro) # * 10
except KeyboardInterrupt:
    servidor.close()
    apagar_todos()