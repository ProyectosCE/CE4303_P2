#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
