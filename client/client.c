#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <unistd.h>
#include <arpa/inet.h>


#include "../pbm/pbm.h"


#define PORT 8080

int send_all(int socket, void *buffer, size_t length)
{
    size_t total = 0;
    ssize_t bytes;

    while(total < length)
    {
        bytes = send(socket,
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
    int sock;

    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if(sock < 0)
    {
        perror("socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    inet_pton(AF_INET,
              "127.0.0.1",
              &server_addr.sin_addr);

    if(connect(sock,
               (struct sockaddr*)&server_addr,
               sizeof(server_addr)) < 0)
    {
        perror("connect");
        return 1;
    }

    PBM_Image image;

    image.width = 8;
    image.height = 32;

    image.columns = malloc(sizeof(uint32_t) * image.width);

    // ejemplo
    for(int i = 0; i < image.width; i++)
    {
        image.columns[i] = 0xAAAAAAAA ^ i;
    }

    // enviar width
    send_all(sock,
             &image.width,
             sizeof(int));

    // enviar height
    send_all(sock,
             &image.height,
             sizeof(int));

    // enviar columnas
    send_all(sock,
             image.columns,
             sizeof(uint32_t) * image.width);

    printf("Imagen enviada.\n");

    free(image.columns);

    close(sock);

    return 0;
}