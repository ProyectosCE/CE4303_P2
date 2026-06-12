#include "pov_driver_lib.h"

#include <stdio.h>

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