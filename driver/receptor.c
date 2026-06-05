#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>

#define INTERFAZ_PI "usb0"
#define ARCHIVO_SALIDA "imagen_recibida_otg.png"
#define MAX_BUFFER 150000

int main() {
    int sock_raw;
    struct sockaddr_ll sll;
    struct ifreq ifr;
    unsigned char *buffer = malloc(MAX_BUFFER);
    FILE *fp;
    
    printf("RASPBERRY PI: RECEPTOR \n");

    sock_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_raw < 0) {
        perror("[Error] No se pudo abrir el socket crudo de hardware");
        free(buffer);
        return 1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, INTERFAZ_PI, IFNAMSIZ - 1);
    if (ioctl(sock_raw, SIOCGIFINDEX, &ifr) < 0) {
        perror("[Error] No se encontró la interfaz física USB");
        close(sock_raw);
        free(buffer);
        return 1;
    }

    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock_raw, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("[Error] Falló el enlace de hardware al puerto");
        close(sock_raw);
        free(buffer);
        return 1;
    }

    printf("Escuchando bytes crudos en el puerto USB (%s)...\n", INTERFAZ_PI);

    fp = fopen(ARCHIVO_SALIDA, "wb");
    if (!fp) {
        perror("[Error] No se pudo crear el archivo de imagen");
        close(sock_raw);
        free(buffer);
        return 1;
    }

    size_t total_bytes_recibidos = 0;
    int esperando = 1;
    int cabecera_encontrada = 0;

    while (esperando) {
        ssize_t packet_size = recvfrom(sock_raw, buffer, MAX_BUFFER, 0, NULL, NULL);
        if (packet_size < 0) {
            perror("[Error] Falló la lectura de ráfaga física");
            break;
        }

        if (packet_size > 0) {
            
            // VALIDACIÓN EXPLÍCITA DE CIERRE
            int encontrado_eof = 0;
            for (int i = 0; i <= packet_size - 15; i++) {
                if (memcmp(buffer + i, "FIN_TRANSMISION", 15) == 0) {
                    encontrado_eof = 1;
                    break;
                }
            }

            if (encontrado_eof) {
                printf("-> ¡Marcador de cierre explícito detectado! Finalizando escucha.\n");
                fflush(stdout);
                esperando = 0;
                continue; 
            }

            // FILTRO Y ALINEACIÓN DE DATOS DE IMAGEN
            if (packet_size >= 1000) {
                unsigned char *inicio_datos = buffer;
                ssize_t tamano_datos = packet_size;

                // Si es el primer paquete, buscamos la firma mágica del PNG (\x89PNG)
                if (!cabecera_encontrada) {
                    for (int i = 0; i <= packet_size - 4; i++) {
                        if (buffer[i] == 0x89 && buffer[i+1] == 0x50 && buffer[i+2] == 0x4E && buffer[i+3] == 0x47) {
                            inicio_datos = buffer + i;
                            tamano_datos = packet_size - i;
                            cabecera_encontrada = 1;
                            printf("-> ¡Cabecera PNG válida detectada en el byte offset %d!\n", i);
                            break;
                        }
                    }
                } else {
                    // Para los siguientes paquetes intermedios, nos saltamos los 14 bytes fijos de la interfaz de red
                    inicio_datos = buffer + 14;
                    tamano_datos = packet_size - 14;
                }

                if (cabecera_encontrada && tamano_datos > 0) {
                    printf("-> Ráfaga física capturada: %ld bytes escritos en disco.\n", tamano_datos);
                    fflush(stdout); 
                    
                    fwrite(inicio_datos, 1, tamano_datos, fp);
                    total_bytes_recibidos += tamano_datos;
                }
            }
        }
    }

    fclose(fp);
    close(sock_raw);
    free(buffer);

    printf("¡ÉXITO! Imagen reconstruida en C de forma cruda por USB.\n");
    printf("Archivo guardado en la Pi como: %s\n", ARCHIVO_SALIDA);
    printf("Tamaño total guardado en disco: %zu bytes\n", total_bytes_recibidos);

    return 0;
}
