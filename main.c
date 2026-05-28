#include "pbm/pbm.h"

#include <stdio.h>

int main(){

    PBM_Image *encrypted;

    PBM_Image *img;

    img = pbm_load("basico.pbm");

    
    if (!img) {
        return 1;
    }

    pbm_print_columns(img);

    encrypted = pbm_encrypt(img);

    pbm_print_columns(encrypted);


    pbm_free(img);

    pbm_free(encrypted);


    return 0;
}