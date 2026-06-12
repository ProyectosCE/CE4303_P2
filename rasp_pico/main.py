import machine
import neopixel
import network
import socket
import time
import select

# --- CONFIGURACIÓN HARDWARE ---
PIN_NUM = 16
NUM_LEDS = 9
pixels = neopixel.NeoPixel(machine.Pin(PIN_NUM), NUM_LEDS)

# --- CONFIGURACIÓN WIFI ---
WIFI_SSID = "_Casa Campus P5_"
WIFI_PASSWORD = "casacampus"

# Variables de control
grafico_pov = [[(0, 0, 0) for _ in range(NUM_LEDS)]]
velocidad_giro = 0.002

def apagar_todos():
    for i in range(NUM_LEDS):
        pixels[i] = (0, 0, 0)
    pixels.write()

# Conexión WiFi
wlan = network.WLAN(network.STA_IF)
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
                # Leemos todo el paquete binario de golpe
                datos_binarios = b""
                while True:
                    fragmento = conexion.recv(1024)
                    if not fragmento:
                        break
                    datos_binarios += fragmento
                
                # Procesamos si el paquete tiene el tamaño mínimo requerido
                # 2 bytes (velocidad) + al menos una columna (9 LEDs * 3 bytes = 27 bytes)
                if len(datos_binarios) >= 29:
                    # Los primeros 2 bytes determinan la velocidad en milisegundos
                    # Ejemplo: si llega un entero 4, significa 4ms -> 0.004s
                    ms_velocidad = (datos_binarios[0] << 8) | datos_binarios[1]
                    velocidad_giro = ms_velocidad / 1000.0
                    
                    # El resto de los bytes son los colores de la matriz
                    bytes_matriz = datos_binarios[2:]
                    nuevas_columnas = []
                    
                    # Cada columna mide exactamente 27 bytes (9 leds * 3 colores)
                    tamano_columna = NUM_LEDS * 3
                    
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
                    
                    if nuevas_columnas:
                        grafico_pov = nuevas_columnas
                        print(f"Matriz actualizada. Columnas: {len(grafico_pov)}, Vel: {velocidad_giro}s")
                        
            except Exception as e:
                print("Error procesando binario:", e)
            finally:
                conexion.close()

        # 2. DIBUJAR EN LOS LEDS
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