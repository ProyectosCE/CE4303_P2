/*
Nombre archivo: pbm.c
Que hace: Implementa la carga de imágenes PBM (formato P1) desde archivo,
su impresión en binario por columna, una función de "encriptación" simple
por XOR, y el envío de la imagen completa por socket TCP a un servidor.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>


#include "pbm.h"

/*
Nombre de funcion: pbm_load
Que hace: Abre un archivo en formato PBM ASCII (P1), lee su ancho y alto,
y construye el arreglo "columns" donde cada columna se representa como un
uint32_t (un bit por fila). El bit row de columns[col] indica si el pixel
(col, row) está encendido (1) o apagado (0).
Que recibe:
  - filename: ruta del archivo PBM a cargar.
Que retorna:
  - Puntero a una PBM_Image cargada en memoria (debe liberarse con pbm_free).
  - NULL si el archivo no existe o no tiene el formato "P1" esperado.
*/
PBM_Image *pbm_load(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    char format[3];

    PBM_Image *img = malloc(sizeof(PBM_Image));

    fscanf(fp, "%2s", format);

    // Solo se soporta el formato PBM ASCII "P1"
    if (strcmp(format, "P1") != 0) {
        fclose(fp);
        free(img);
        return NULL;
    }

    fscanf(fp, "%d %d",
           &img->width,
           &img->height);
    
    // Reserva un uint32_t por columna, inicializado en 0 (calloc)
    img->columns =
        calloc(img->width,
               sizeof(uint32_t));

    // Recorre la matriz de bits del PBM y la "empaqueta" por columna:
    // el bit de la fila "row" se guarda en la posición "row" del
    // entero columns[col]
    for (int row = 0; row < img->height; row++) {
        for (int col = 0; col < img->width; col++) {

            int bit;
            fscanf(fp, "%d", &bit);

            if (bit) {
                img->columns[col] |=
                    (1U << row);
            }
        }
    }

    fclose(fp);

    return img;
}

/*
Nombre de funcion: pbm_print_columns
Que hace: Imprime el ancho, el alto y el contenido de cada columna de la
imagen, mostrando los bits (0/1) de cada fila para esa columna.
Que recibe:
  - img: puntero a la imagen ya cargada.
Que retorna: Nada (void). Solo imprime por stdout.
*/
void pbm_print_columns(PBM_Image *img)
{
    printf("width=%d\n", img->width);
    printf("height=%d\n", img->height);

    for (int col = 0; col < img->width; col++) {

        printf("columns[%d] = ", col);

        for (int row = 0; row < img->height; row++) {

            int bit =
                (img->columns[col] >> row) & 1;

            printf("%d", bit);
        }

        printf("\n");
    }
}

/*
Nombre de funcion: send_all
Que hace: Envía exactamente "length" bytes por el socket, llamando a
send() repetidamente hasta enviar todo el buffer (send() puede enviar
menos bytes de los pedidos en una sola llamada).
Que recibe:
  - socket: descriptor del socket conectado.
  - buffer: puntero a los datos a enviar.
  - length: cantidad total de bytes a enviar.
Que retorna:
  - 0 si se enviaron todos los bytes correctamente.
  - -1 si hubo un error o la conexión se cerró (send <= 0).
Nota: función static -> uso interno de este archivo (no es parte de la API pública).
*/
static int send_all(int socket,
                    const void *buffer,
                    size_t length)
{
    size_t total = 0;

    while(total < length)
    {
        ssize_t sent =
            send(socket,
                 (char*)buffer + total,
                 length - total,
                 0);

        if(sent <= 0)
            return -1;

        total += sent;
    }

    return 0;
}

/*
Nombre de funcion: recv_all
Que hace: Recibe exactamente "length" bytes desde el socket, llamando a
recv() repetidamente hasta completar el buffer (igual que send_all, pero
para lectura). No se usa en este archivo, pero queda disponible/static.
Que recibe:
  - socket: descriptor del socket conectado.
  - buffer: puntero donde se almacenan los bytes recibidos.
  - length: cantidad total de bytes esperados.
Que retorna:
  - 0 si se recibieron todos los bytes correctamente.
  - -1 si hubo un error o la conexión se cerró (recv <= 0).
*/
static int recv_all(int socket,
                    void *buffer,
                    size_t length)
{
    size_t total = 0;

    while(total < length)
    {
        ssize_t received =
            recv(socket,
                 (char*)buffer + total,
                 length - total,
                 0);

        if(received <= 0)
            return -1;

        total += received;
    }

    return 0;
}

/*
Nombre de funcion: pbm_send
Que hace: Envía la imagen completa por un socket ya conectado, en el
orden: width, height y luego el arreglo de columnas. Este es el formato
que el servidor (server.c) espera recibir.
Que recibe:
  - socket: descriptor del socket ya conectado al servidor.
  - img: imagen a enviar.
Que retorna:
  - 0 si toda la información se envió correctamente.
  - -1 si falló el envío de width, height o columns.
Nota: no es static, pero tampoco está declarada en pbm.h; se usa
internamente desde pbm_send_to_server.
*/
int pbm_send(int socket, PBM_Image *img)
{
    if(send_all(socket,
                &img->width,
                sizeof(int)) < 0)
        return -1;

    if(send_all(socket,
                &img->height,
                sizeof(int)) < 0)
        return -1;

    if(send_all(socket,
                img->columns,
                sizeof(uint32_t)
                * img->width) < 0)
        return -1;

    return 0;
}

/*
Nombre de funcion: pbm_send_to_server
Que hace: Crea un socket TCP, se conecta a la IP/puerto indicados y envía
la imagen completa usando pbm_send. Cierra el socket al finalizar (con
éxito o con error).
Que recibe:
  - ip: dirección IP (texto, ej. "127.0.0.1") del servidor destino.
  - port: puerto TCP del servidor destino.
  - img: imagen a enviar.
Que retorna:
  - 0 si la imagen se envió correctamente.
  - -1 si falla la creación del socket, la conversión de la IP (inet_pton),
    la conexión (connect) o el envío de datos (pbm_send).
*/
int pbm_send_to_server(const char *ip,
                       int port,
                       PBM_Image *img)
{
    int sock;

    struct sockaddr_in server_addr;

    sock = socket(AF_INET,
                  SOCK_STREAM,
                  0);

    if(sock < 0)
    {
        return -1;
    }

    server_addr.sin_family = AF_INET;

    server_addr.sin_port =
        htons(port);

    if(inet_pton(AF_INET,
                 ip,
                 &server_addr.sin_addr) <= 0)
    {
        close(sock);
        return -1;
    }

    if(connect(sock,
               (struct sockaddr*)&server_addr,
               sizeof(server_addr)) < 0)
    {
        close(sock);
        return -1;
    }

    if(pbm_send(sock, img) < 0)
    {
        close(sock);
        return -1;
    }

    close(sock);

    return 0;
}

/*
Nombre de funcion: pbm_free
Que hace: Libera toda la memoria dinámica asociada a una PBM_Image:
primero el arreglo de columnas y luego la estructura en sí.
Que recibe:
  - img: puntero a la imagen a liberar.
Que retorna: Nada (void).
Nota: tras llamar esta función, el puntero "img" queda inválido (dangling);
no debe usarse de nuevo.
*/
void pbm_free(PBM_Image *img)
{
    free(img->columns);
    free(img);
}

/*
Nombre de funcion: pbm_encrypt
Que hace: Crea una NUEVA imagen (no modifica "img") con las mismas
dimensiones, donde cada columna es el resultado de aplicar XOR entre la
columna original y la clave (0xDEADBEEF + i), siendo "i" el índice de la
columna. Esto sirve como una "encriptación" simple y reversible (aplicar
XOR de nuevo con la misma clave recupera la imagen original).
Que recibe:
  - img: imagen original (no se modifica).
Que retorna:
  - Puntero a una nueva PBM_Image "encriptada". El llamador es responsable
    de liberarla con pbm_free.
Nota: no se valida que malloc haya tenido éxito (out o out->columns podrían
ser NULL si la memoria se agota).
*/
PBM_Image *pbm_encrypt(PBM_Image *img)
{
    PBM_Image *out =
        malloc(sizeof(PBM_Image));

    out->width  = img->width;
    out->height = img->height;

    out->columns =
        malloc(sizeof(uint32_t)
               * img->width);

    for (int i = 0; i < img->width; i++) {

        out->columns[i] =
            img->columns[i] ^ (0xDEADBEEF+i);
    }

    return out;
}
