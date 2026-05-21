#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX 256

int main() {

    FILE *fp = fopen("basico.pbm", "r");

    if (fp == NULL) {
        printf("No se pudo abrir el archivo\n");
        return 1;
    }

    char format[3];
    int width, height;

    // Leer encabezado PBM
    fscanf(fp, "%s", format);

    if (strcmp(format, "P1") != 0) {
        printf("Formato no soportado\n");
        return 1;
    }

    fscanf(fp, "%d %d", &width, &height);

    int img[MAX][MAX];

    // Leer matriz
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            fscanf(fp, "%d", &img[i][j]);
        }
    }

    fclose(fp);

    printf("Imagen cargada: %dx%d\n\n", width, height);

    // Mostrar columnas completas
    printf("COLUMNAS:\n\n");

    for (int col = 0; col < width; col++) {

        printf("col[%d] = ", col);

        for (int row = 0; row < height; row++) {
            printf("%d", img[row][col]);
        }

        printf("\n");
    }

    // División en 3 cargas
    int cols_per_node = width / 3;

    printf("\n====================\n");
    printf("DIVISION DE TRABAJO\n");
    printf("====================\n");

    for (int node = 0; node < 3; node++) {

        int start = node * cols_per_node;

        int end;

        // El último nodo toma el sobrante
        if (node == 2)
            end = width;
        else
            end = start + cols_per_node;

        printf("\nNodo %d:\n", node);

        for (int col = start; col < end; col++) {

            printf("col[%d] = ", col);

            for (int row = 0; row < height; row++) {
                printf("%d", img[row][col]);
            }

            printf("\n");
        }
    }

    return 0;
}