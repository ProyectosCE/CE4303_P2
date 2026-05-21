#include "pbm/pbm.h"

int main()
{
    PBM_Image *img = pbm_load("basico.pbm");

    if (!img) {
        return 1;
    }

    pbm_print_columns(img);

    pbm_free(img);

    return 0;
}