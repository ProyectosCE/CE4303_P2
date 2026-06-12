/*
Nombre archivo: server.c
Que hace: Servidor TCP que escucha en un puerto fijo, acepta clientes
uno a la vez y recibe una imagen en formato PBM (width, height y un
arreglo de columnas como uint32_t), imprimiendo su contenido.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>

#include "../pbm/pbm.h"

#define PORT 8080 // Puerto donde el servidor escucha conexiones entrantes

/*
Nombre de funcion: recv_all
Que hace: Recibe exactamente "length" bytes desde un socket, llamando a
recv() repetidamente hasta completar el buffer (recv() puede entregar
menos bytes de los pedidos en una sola llamada).
Que recibe:
  - socket: descriptor del socket conectado al cliente
  - buffer: puntero donde se van a almacenar los bytes recibidos
  - length: cantidad total de bytes que se esperan recibir
Que retorna:
  - 0 si se recibieron correctamente todos los bytes
  - -1 si el cliente cerró la conexión o hubo un error (recv <= 0)
*/
int recv_all(int socket, void *buffer, size_t length)
{
    size_t total = 0;
    ssize_t bytes;

    while(total < length)
    {
        bytes = recv(socket,
                     (char*)buffer + total,
                     length - total,
                     0);

        if(bytes <= 0)
            return -1; // conexión cerrada o error de lectura

        total += bytes;
    }

    return 0;
}

/*
Nombre de funcion: main
Que hace:
  - Crea un socket TCP, lo enlaza al puerto PORT y queda escuchando.
  - En un loop infinito acepta clientes uno por uno.
  - Por cada cliente recibe width, height y luego "width" columnas
    (cada una un uint32_t) que representan la imagen PBM.
  - Imprime los valores recibidos y libera la memoria usada.
Que recibe: Nada (no usa argc/argv).
Que retorna:
  - 0 en teoría al final del programa (en la práctica el while(1) nunca
    termina de forma normal).
  - 1 si falla la creación del socket o el bind.
*/
int main()
{
    int server_fd;

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // --- Creación del socket TCP/IPv4 ---
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(server_fd < 0)
    {
        perror("socket");
        return 1;
    }

     // --- Configuración de la dirección del servidor ---
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // acepta conexiones en cualquier interfaz
    address.sin_port = htons(PORT); // htons: convierte el puerto a network byte order

    // --- Enlazar (bind) el socket a la IP/puerto configurados ---
    if(bind(server_fd,
            (struct sockaddr*)&address,
            sizeof(address)) < 0)
    {
        perror("bind");
        return 1;
    }

    // Pone el socket en modo escucha, con cola de hasta 5 conexiones pendientes
    listen(server_fd, 5);

    printf("Servidor esperando en puerto %d...\n", PORT);

    while(1)
    {
        int client_socket;

        // Bloquea hasta que llegue una nueva conexión entrante
        client_socket = accept(server_fd,
                               (struct sockaddr*)&address,
                               &addrlen);

        if(client_socket < 0)
        {
            perror("accept");
            continue; // error al aceptar, seguimos esperando otro cliente
        }

        printf("\nCliente conectado\n");

        PBM_Image image;

        // --- Recibir width (4 bytes, tipo int) ---
        if(recv_all(client_socket,
                    &image.width,
                    sizeof(int)) < 0)
        {
            printf("Error recibiendo width\n");
            close(client_socket);
            continue;
        }

        // --- Recibir height (4 bytes, tipo int) ---
        if(recv_all(client_socket,
                    &image.height,
                    sizeof(int)) < 0)
        {
            printf("Error recibiendo height\n");
            close(client_socket);
            continue;
        }

        printf("Imagen recibida:\n");
        printf("Width  = %d\n", image.width);
        printf("Height = %d\n", image.height);

          // --- Reservar memoria para las columnas de la imagen ---
        // Cada columna ocupa 4 bytes (uint32_t), se reservan "width" columnas.
        image.columns = malloc(sizeof(uint32_t) * image.width);

        if(image.columns == NULL)
        {
            printf("Error reservando memoria\n");
            close(client_socket);
            continue;
        }

        // --- Recibir el arreglo completo de columnas ---
        if(recv_all(client_socket,
                    image.columns,
                    sizeof(uint32_t) * image.width) < 0)
        {
            printf("Error recibiendo columnas\n");
            free(image.columns);
            close(client_socket);
            continue;
        }

        printf("\nColumnas:\n");

        // Imprime cada columna recibida en formato hexadecimal (32 bits)
        for(int i = 0; i < image.width; i++)
        {
            printf("Column[%d] = 0x%08X\n",
                   i,
                   image.columns[i]);
        }

        // Liberar la memoria reservada para esta imagen
        free(image.columns);

        printf("Cliente desconectado\n");
        // Cerrar la conexión con este cliente y volvemos a esperar otro 
        close(client_socket);
    }

    close(server_fd);

    return 0;
}