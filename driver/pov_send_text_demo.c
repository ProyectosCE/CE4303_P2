c/*
Nombre archivo: pov_send_text_demo.c
Que hace: Programa de demostración mínimo que abre el driver POV y le
escribe directamente una cadena de texto (sin formato de paquete POV),
usando pov_driver_send_text. Sirve para probar el "modo txt" del receptor.
*/

#include "pov_driver_lib.h"

#include <stdio.h>

/*
Nombre de funcion: main
Que hace: Abre el dispositivo del driver, envía un texto fijo de prueba
mediante pov_driver_send_text, y cierra el dispositivo.
Que recibe: Nada (no usa argc/argv).
Que retorna:
  - 0 si el texto se envió correctamente.
  - 1 si falla la apertura del driver o el envío del texto.
*/
int main(void)
{
    int fd;
    int ret;

    fd = pov_driver_open(NULL);
    if (fd < 0) {
        fprintf(stderr, "No se pudo abrir el driver. errno=%d\n", -fd);
        return 1;
    }

    ret = pov_driver_send_text(fd, "hola waos desde libpovdriver.a\n");

    pov_driver_close(fd);

    if (ret < 0) {
        fprintf(stderr, "Error enviando texto. errno=%d\n", -ret);
        return 1;
    }

    printf("Texto enviado correctamente.\n");
    return 0;
}