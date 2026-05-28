#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>


#include "pbm.h"

PBM_Image *pbm_load(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    char format[3];

    PBM_Image *img = malloc(sizeof(PBM_Image));

    fscanf(fp, "%2s", format);

    if (strcmp(format, "P1") != 0) {
        fclose(fp);
        free(img);
        return NULL;
    }

    fscanf(fp, "%d %d",
           &img->width,
           &img->height);

    img->columns =
        calloc(img->width,
               sizeof(uint32_t));

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

void pbm_free(PBM_Image *img)
{
    free(img->columns);
    free(img);
}



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
