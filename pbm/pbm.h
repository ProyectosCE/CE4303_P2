/*
Nombre archivo: pbm.h
Que hace: Header que define la estructura PBM_Image (imagen en formato
PBM representada como columnas de bits empacadas en uint32_t) y declara
las funciones públicas para cargarla, imprimirla, liberarla, "encriptarla"
(XOR) y enviarla por red a un servidor.
*/

#ifndef PBM_H
#define PBM_H

#include <stdint.h>

// Estructura que representa una imagen PBM:
// - width/height: dimensiones de la imagen
// - columns: arreglo de "width" elementos, donde cada uint32_t guarda
//   los bits de una columna completa (un bit por fila, hasta 32 filas)
typedef struct {

    int width;
    int height;

    uint32_t *columns;

} PBM_Image;

// Carga una imagen PBM (formato P1) desde archivo. Retorna NULL si falla.
PBM_Image *pbm_load(const char *filename);

// Imprime ancho, alto y el contenido bit a bit de cada columna.
void pbm_print_columns(PBM_Image *img);

// Libera toda la memoria asociada a la imagen (columns + struct).
void pbm_free(PBM_Image *img);

// Crea una copia "encriptada" de la imagen aplicando XOR a cada columna.
PBM_Image *pbm_encrypt(PBM_Image *img);

/* networking */
// Se conecta a ip:port y envía la imagen completa (width, height, columns).
int pbm_send_to_server(const char *ip,
                       int port,
                       PBM_Image *img);




#endif