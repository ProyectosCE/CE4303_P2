/*
Nombre archivo: pov_driver_lib.c
Que hace: Implementación de la librería de espacio de usuario para
construir paquetes en formato POV (velocidad + columnas RGB) y escribirlos
al character device del driver (/dev/raspberry_usb), manejando escrituras
parciales (write_all).
*/

#include "pov_driver_lib.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
Nombre de funcion: write_all
Que hace: Escribe exactamente "len" bytes al descriptor "fd", llamando a
write() repetidamente hasta completar el buffer. Reintenta si write()
es interrumpido por una señal (EINTR).
Que recibe:
  - fd: descriptor de archivo/dispositivo abierto para escritura.
  - data: puntero a los datos a escribir.
  - len: cantidad total de bytes a escribir.
Que retorna:
  - 0 si se escribieron todos los bytes correctamente.
  - -errno si write() falló (excepto EINTR, que se reintenta).
  - -EIO si write() retornó 0 (no se pudo escribir nada, fin inesperado).
Nota: static -> función interna, no expuesta en el header.
*/
static int write_all(int fd, const uint8_t *data, size_t len)
{
    size_t written = 0;

    while (written < len) {
        ssize_t n;
        
        // write() puede no aceptar más de SSIZE_MAX bytes en una sola llamada
        if (len - written > SSIZE_MAX)
            n = write(fd, data + written, SSIZE_MAX);
        else
            n = write(fd, data + written, len - written);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -errno;
        }

        if (n == 0)
            return -EIO;

        written += (size_t)n;
    }

    return 0;
}

/*
Nombre de funcion: pov_driver_open
Que hace: Abre el dispositivo del driver POV en modo solo escritura.
Que recibe:
  - device_path: ruta del dispositivo, o NULL para usar
    POV_DRIVER_DEFAULT_DEVICE.
Que retorna:
  - El descriptor de archivo (>= 0) si se abrió correctamente.
  - -errno si open() falló.
*/
int pov_driver_open(const char *device_path)
{
    int fd;

    if (!device_path)
        device_path = POV_DRIVER_DEFAULT_DEVICE;

    fd = open(device_path, O_WRONLY);
    if (fd < 0)
        return -errno;

    return fd;
}

/*
Nombre de funcion: pov_driver_close
Que hace: Cierra el descriptor del dispositivo si es válido.
Que recibe:
  - fd: descriptor a cerrar.
Que retorna: Nada (void). Si fd < 0, no hace nada.
*/
void pov_driver_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

/*
Nombre de funcion: pov_driver_packet_size
Que hace: Calcula el tamaño total en bytes que tendría un paquete POV
con "columns" columnas (2 bytes de cabecera de velocidad + columnas * 27).
Que recibe:
  - columns: cantidad de columnas.
Que retorna: tamaño total del paquete en bytes.
Nota: no valida overflow; para eso ver pov_driver_build_packet.
*/
size_t pov_driver_packet_size(size_t columns)
{
    return 2 + columns * POV_COLUMN_BYTES;
}

/*
Nombre de funcion: pov_driver_build_packet
Que hace: Construye en memoria (malloc) un paquete binario en el formato
esperado por el driver/Pico: 2 bytes de velocidad (big-endian) seguidos
por "columns" columnas de POV_COLUMN_BYTES bytes cada una (copiadas desde "rgb").
Que recibe:
  - velocidad_ms: velocidad en milisegundos, se codifica en 2 bytes big-endian.
  - rgb: buffer de entrada con columns * POV_COLUMN_BYTES bytes de datos RGB.
  - columns: cantidad de columnas (debe ser > 0).
  - out_packet: [salida] puntero donde se guarda el buffer recién reservado.
  - out_len: [salida] puntero donde se guarda el tamaño total del paquete.
Que retorna:
  - 0 si el paquete se construyó correctamente (el llamador debe hacer free(*out_packet)).
  - -EINVAL si rgb/out_packet/out_len son NULL o columns == 0.
  - -EOVERFLOW si columns es tan grande que el cálculo de tamaño desbordaría.
  - -ENOMEM si malloc falla.
*/
int pov_driver_build_packet(
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns,
    uint8_t **out_packet,
    size_t *out_len
)
{
    size_t rgb_len;
    size_t packet_len;
    uint8_t *packet;

    if (!rgb || !out_packet || !out_len)
        return -EINVAL;

    if (columns == 0)
        return -EINVAL;
    
    // Chequeo de overflow antes de multiplicar/sumar para el tamaño total
    if (columns > (SIZE_MAX - 2) / POV_COLUMN_BYTES)
        return -EOVERFLOW;

    rgb_len = columns * POV_COLUMN_BYTES;
    packet_len = 2 + rgb_len;

    packet = malloc(packet_len);
    if (!packet)
        return -ENOMEM;

    // Velocidad en big-endian: byte alto primero, luego byte bajo
    packet[0] = (uint8_t)((velocidad_ms >> 8) & 0xFF);
    packet[1] = (uint8_t)(velocidad_ms & 0xFF);

    memcpy(packet + 2, rgb, rgb_len);

    *out_packet = packet;
    *out_len = packet_len;

    return 0;
}

/*
Nombre de funcion: pov_driver_send_raw
Que hace: Envía un buffer ya construido (paquete completo o cualquier
dato crudo) directamente al driver, usando write_all.
Que recibe:
  - fd: descriptor del dispositivo (debe ser >= 0).
  - packet: buffer a enviar (no debe ser NULL).
  - packet_len: cantidad de bytes a enviar (no debe ser 0).
Que retorna:
  - 0 si se escribió todo correctamente.
  - -EINVAL si fd < 0, packet es NULL o packet_len == 0.
  - El valor de error de write_all en caso de fallo de escritura.
*/
int pov_driver_send_raw(
    int fd,
    const uint8_t *packet,
    size_t packet_len
)
{
    if (fd < 0 || !packet || packet_len == 0)
        return -EINVAL;

    return write_all(fd, packet, packet_len);
}

/*
Nombre de funcion: pov_driver_send_packet
Que hace: Construye un paquete POV (velocidad + columnas RGB) con
pov_driver_build_packet y lo envía al driver con pov_driver_send_raw.
Libera la memoria del paquete temporal antes de retornar.
Que recibe:
  - fd: descriptor del dispositivo.
  - velocidad_ms: velocidad en milisegundos.
  - rgb: buffer de columnas RGB (columns * POV_COLUMN_BYTES bytes).
  - columns: cantidad de columnas.
Que retorna:
  - 0 si todo el paquete se envió correctamente.
  - Código de error negativo de pov_driver_build_packet o pov_driver_send_raw
    si alguno de los pasos falla.
*/
int pov_driver_send_packet(
    int fd,
    uint16_t velocidad_ms,
    const uint8_t *rgb,
    size_t columns
)
{
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    int ret;

    ret = pov_driver_build_packet(
        velocidad_ms,
        rgb,
        columns,
        &packet,
        &packet_len
    );

    if (ret < 0)
        return ret;

    ret = pov_driver_send_raw(fd, packet, packet_len);

    free(packet);
    return ret;
}

/*
Nombre de funcion: pov_driver_send_text
Que hace: Envía una cadena de texto (sin el formato de paquete POV,
es decir SIN los 2 bytes de velocidad ni el empaquetado por columnas)
directamente al driver. Útil para pruebas (ver pov_send_text_demo.c y
el modo "txt" del receptor).
Que recibe:
  - fd: descriptor del dispositivo.
  - text: cadena terminada en NUL a enviar (no incluye el '\0').
Que retorna:
  - 0 si el texto se envió correctamente.
  - -EINVAL si fd < 0 o text es NULL.
  - Código de error de pov_driver_send_raw si falla la escritura.
*/
int pov_driver_send_text(int fd, const char *text)
{
    if (fd < 0 || !text)
        return -EINVAL;

    return pov_driver_send_raw(
        fd,
        (const uint8_t *)text,
        strlen(text)
    );
}