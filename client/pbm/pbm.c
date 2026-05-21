#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pbm.h"

PBM_Image *pbm_load(const char *filename)
{
    FILE *fp = fopen(filename, "r");

    if (!fp)
        return NULL;

    char format[3];

    PBM_Image *img = malloc(sizeof(PBM_Image));

    fscanf(fp, "%s", format);

    if (strcmp(format, "P1") != 0) {
        fclose(fp);
        free(img);
        return NULL;
    }

    fscanf(fp, "%d %d", &img->width, &img->height);

    int temp[512][512];

    // Leer imagen
    for (int row = 0; row < img->height; row++) {

        for (int col = 0; col < img->width; col++) {

            fscanf(fp, "%d", &temp[row][col]);
        }
    }

    fclose(fp);

    // Crear columnas
    img->columns = malloc(img->width * sizeof(int *));

    for (int col = 0; col < img->width; col++) {

        img->columns[col] = malloc(img->height * sizeof(int));

        for (int row = 0; row < img->height; row++) {

            img->columns[col][row] = temp[row][col];
        }
    }

    return img;
}

void pbm_print_columns(PBM_Image *img)
{
    for (int col = 0; col < img->width; col++) {

        printf("columns[%d] = ", col);

        for (int row = 0; row < img->height; row++) {

            printf("%d", img->columns[col][row]);
        }

        printf("\n");
    }
}

void pbm_free(PBM_Image *img)
{
    for (int col = 0; col < img->width; col++) {

        free(img->columns[col]);
    }

    free(img->columns);

    free(img);
}