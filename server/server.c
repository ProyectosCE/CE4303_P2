#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>

#include "../pbm/pbm.h"

#define PORT 8080

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
            return -1;

        total += bytes;
    }

    return 0;
}

int main()
{
    int server_fd;
    int client_socket;

    struct sockaddr_in address;

    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if(bind(server_fd,
            (struct sockaddr*)&address,
            sizeof(address)) < 0)
    {
        perror("bind");
        return 1;
    }

    listen(server_fd, 1);

    printf("Servidor esperando en puerto %d...\n", PORT);

    client_socket = accept(server_fd,
                           (struct sockaddr*)&address,
                           &addrlen);

    if(client_socket < 0)
    {
        perror("accept");
        return 1;
    }

    PBM_Image image;

    // recibir width
    recv_all(client_socket,
             &image.width,
             sizeof(int));

    // recibir height
    recv_all(client_socket,
             &image.height,
             sizeof(int));

    printf("Imagen recibida:\n");
    printf("Width  = %d\n", image.width);
    printf("Height = %d\n", image.height);

    // reservar memoria
    image.columns = malloc(sizeof(uint32_t) * image.width);

    // recibir columnas
    recv_all(client_socket,
             image.columns,
             sizeof(uint32_t) * image.width);

    printf("\nColumnas:\n");

    for(int i = 0; i < image.width; i++)
    {
        printf("Column[%d] = 0x%08X\n",
               i,
               image.columns[i]);
    }

    free(image.columns);

    close(client_socket);
    close(server_fd);

    return 0;
}