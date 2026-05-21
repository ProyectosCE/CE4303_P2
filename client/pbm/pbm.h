#ifndef PBM_H
#define PBM_H

typedef struct {

    int width;
    int height;

    int **columns;

} PBM_Image;

PBM_Image *pbm_load(const char *filename);

void pbm_print_columns(PBM_Image *img);

void pbm_free(PBM_Image *img);

#endif