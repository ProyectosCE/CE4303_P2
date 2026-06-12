/*
Nombre archivo: pov_driver_lib.h
Que hace: Header de la librería de espacio de usuario para hablar con el
driver POV (/dev/raspberry_usb). Define constantes del formato de paquete
(número de LEDs, bytes por columna) y declara las funciones para abrir/
cerrar el dispositivo, construir paquetes y enviarlos.
*/

#ifndef POV_DRIVER_H
#define POV_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#define POV_DRIVER_DEFAULT_DEVICE "/dev/raspberry_usb" // Ruta por defecto del char device

#define POV_NUM_LEDS 9 // Cantidad de LEDs por columna
#define POV_RGB_BYTES 3 // Bytes por LED (R,G,B)
#define POV_COLUMN_BYTES (POV_NUM_LEDS * POV_RGB_BYTES) // Bytes totales por columna (27)


#ifdef __cplusplus
extern "C" {
#endif

// Abre el dispositivo (device_path o el default si es NULL). Retorna fd >=0 o -errno.
int pov_driver_open(const char *device_path);

// Cierra el fd del dispositivo si es válido (>= 0).
void pov_driver_close(int fd);

// Calcula el tamaño total en bytes de un paquete con "columns" columnas (2 + columns*27).
size_t pov_driver_packet_size(size_t columns);

// Construye en memoria un paquete (velocidad + columnas RGB)
int pov_driver_build_packet(
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns,
    uint8_t **out_packet,
    size_t *out_len
);

// Escribe "packet_len" bytes ya construidos directamente al driver.
int pov_driver_send_raw(
    int fd,
    const uint8_t *packet,
    size_t packet_len
);

// Construye y envía un paquete POV (velocidad + columnas) en un solo paso.
int pov_driver_send_packet(
    int fd,
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns
);

// Envía una cadena de texto cruda (sin formato de paquete POV) al driver.
int pov_driver_send_text(
    int fd,
    const char *text
);

#ifdef __cplusplus
}
#endif

#endif