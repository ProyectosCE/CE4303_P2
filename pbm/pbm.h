#ifndef PBM_H
#define PBM_H
#include <stdint.h>

typedef struct {

    int width;
    int height;

    uint32_t *columns;

} PBM_Image;

PBM_Image *pbm_load(const char *filename);

void pbm_print_columns(PBM_Image *img);

void pbm_free(PBM_Image *img);

PBM_Image *pbm_encrypt(PBM_Image *img);


#endif