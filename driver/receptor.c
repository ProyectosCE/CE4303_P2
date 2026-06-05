/**
 * @file receptor.c
 * @brief Programa receptor para captura de imagen transmitida por red USB-Ethernet.
 *
 * @details
 * Este programa actúa como servidor receptor en una Raspberry Pi, capturando tráfico
 * a nivel de enlace (raw packets) desde un socket AF_PACKET. Escucha paquetes crudos
 * en la interfaz USB, valida marcadores de inicio/fin de transmisión (PNG magic header
 * y marcador explícito "FIN_TRANSMISION"), filtra los encabezados de enlace y reconstruye
 * el archivo de imagen original en disco.
 *
 * @details
 * Flujo de operación:
 * - Crea un socket raw AF_PACKET para captura a nivel de enlace (ETH_P_ALL).
 * - Se vincula a la interfaz USB especificada (usb0).
 * - Entra en bucle de escucha bloqueante (recvfrom).
 * - Por cada paquete recibido: valida EOF, busca firma PNG, elimina headers L2 (14 bytes),
 *   escribe datos al archivo.
 * - Al recibir el marcador "FIN_TRANSMISION", termina la captura.
 *
 * @warning
 * Requiere permisos de root para crear sockets AF_PACKET.
 * Los paquetes se asumen fragmentados en ráfagas de ~1024 bytes del lado transmisor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>       /**< Definiciones de orden de bytes de red. */
#include <sys/socket.h>      /**< API de sockets (socket(), bind(), recvfrom()). */
#include <linux/if_packet.h> /**< Definiciones AF_PACKET para captura L2. */
#include <net/ethernet.h>    /**< Constantes de Ethernet. */
#include <net/if.h>          /**< Estructuras y helpers de interfaces de red. */
#include <sys/ioctl.h>       /**< Llamadas ioctl() para configuración de dispositivos. */

/**
 * @def INTERFAZ_PI
 * @brief Nombre de la interfaz de red USB donde se capturará el tráfico.
 */
#define INTERFAZ_PI "usb0"

/**
 * @def ARCHIVO_SALIDA
 * @brief Ruta relativa del archivo donde se guardará la imagen reconstruida.
 */
#define ARCHIVO_SALIDA "imagen_recibida_otg.png"

/**
 * @def MAX_BUFFER
 * @brief Tamaño máximo del buffer para almacenar un paquete capturado.
 */
#define MAX_BUFFER 150000

/**
 * @brief Programa principal: inicializa socket raw, captura tráfico y reconstruye imagen.
 *
 * @details
 * Implementa el flujo completo de recepción:
 * 1. Crear un socket AF_PACKET en modo raw (SOCK_RAW con ETH_P_ALL).
 * 2. Obtener el índice de interfaz de la estructura ifreq usando ioctl().
 * 3. Vincular (bind) el socket a dicha interfaz mediante sockaddr_ll.
 * 4. Abrir archivo de salida en modo binario ("wb") para escritura.
 * 5. Bucle de captura (recvfrom):
 *    - Detecta marcador EOF explícito ("FIN_TRANSMISION").
 *    - En el primer paquete válido (>=1000 bytes): busca firma PNG (0x89 0x50 0x4E 0x47).
 *    - En paquetes subsecuentes: salta 14 bytes de header Ethernet y escribe datos.
 * 6. Cierra archivo, socket y libera memoria.
 * 7. Reporta estadísticas (bytes totales, ruta de archivo).
 *
 * @return 0 en éxito, 1 en error (inicialización o fallo I/O).
 */
int main() {
    int sock_raw;                         /**< Descriptor de socket raw AF_PACKET. */
    struct sockaddr_ll sll;               /**< Dirección de socket a nivel de enlace. */
    struct ifreq ifr;                     /**< Estructura para ioctl() de interfaces. */
    unsigned char *buffer = malloc(MAX_BUFFER); /**< Buffer de recepción para paquetes crudos. */
    FILE *fp;                             /**< Descriptor de archivo para guardar imagen. */
    
    printf("RASPBERRY PI: RECEPTOR \n");

    /** @brief Crear socket raw para captura a nivel de enlace (Ethernet). */
    sock_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_raw < 0) {
        perror("[Error] No se pudo abrir el socket crudo de hardware");
        free(buffer);
        return 1;
    }

    /** @brief Obtener índice de interfaz mediante ioctl(SIOCGIFINDEX). */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, INTERFAZ_PI, IFNAMSIZ - 1);
    if (ioctl(sock_raw, SIOCGIFINDEX, &ifr) < 0) {
        perror("[Error] No se encontró la interfaz física USB");
        close(sock_raw);
        free(buffer);
        return 1;
    }

    /** @brief Configurar estructura de dirección y vincular socket a la interfaz. */
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

    /** @brief Abrir archivo de salida en modo binario escritura. */
    fp = fopen(ARCHIVO_SALIDA, "wb");
    if (!fp) {
        perror("[Error] No se pudo crear el archivo de imagen");
        close(sock_raw);
        free(buffer);
        return 1;
    }

    size_t total_bytes_recibidos = 0;  /**< Acumulador de bytes válidos guardados en disco. */
    int esperando = 1;                  /**< Flag de control del bucle principal. */
    int cabecera_encontrada = 0;        /**< Flag: se localizó la firma PNG válida. */

    /** @brief Bucle de captura bloqueante: recvfrom() + procesamiento de paquetes. */
    while (esperando) {
        ssize_t packet_size = recvfrom(sock_raw, buffer, MAX_BUFFER, 0, NULL, NULL);
        if (packet_size < 0) {
            perror("[Error] Falló la lectura de ráfaga física");
            break;
        }

        if (packet_size > 0) {
            
            /**
             * @brief Validación explícita de marcador de fin de transmisión.
             *
             * Itera sobre el buffer buscando la cadena ASCII "FIN_TRANSMISION" (15 bytes).
             * Si se encuentra, marca fin de bucle.
             */
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

            /**
             * @brief Filtro de paquetes: validación por tamaño y detección de firma PNG.
             *
             * Solo procesa paquetes con payload >= 1000 bytes (fragmentos significativos).
             * En el primer paquete válido: busca la firma mágica PNG (0x89 0x50 0x4E 0x47).
             * En paquetes subsecuentes: salta los 14 bytes de header Ethernet y escribe datos.
             */
            if (packet_size >= 1000) {
                unsigned char *inicio_datos = buffer; /**< Puntero de inicio del payload útil. */
                ssize_t tamano_datos = packet_size;   /**< Tamaño del payload a escribir. */

                /** @brief Búsqueda de firma PNG en primer paquete válido. */
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
                    /** @brief Paquetes posteriores: omitir encabezado Ethernet (14 bytes fijos). */
                    inicio_datos = buffer + 14;
                    tamano_datos = packet_size - 14;
                }

                if (cabecera_encontrada && tamano_datos > 0) {
                    printf("-> Ráfaga física capturada: %ld bytes escritos en disco.\n", tamano_datos);
                    fflush(stdout); 
                    
                    /** @brief Escribir fragmento validado al archivo de imagen. */
                    fwrite(inicio_datos, 1, tamano_datos, fp);
                    total_bytes_recibidos += tamano_datos;
                }
            }
        }
    }

    /** @brief Cierre limpio de recursos: archivo, socket y memoria dinámica. */
    fclose(fp);
    close(sock_raw);
    free(buffer);

    printf("¡ÉXITO! Imagen reconstruida en C de forma cruda por USB.\n");
    printf("Archivo guardado en la Pi como: %s\n", ARCHIVO_SALIDA);
    printf("Tamaño total guardado en disco: %zu bytes\n", total_bytes_recibidos);

    return 0;
}
