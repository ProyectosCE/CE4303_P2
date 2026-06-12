#include "pbm/pbm.h"

int main()
{
    PBM_Image *img =
        pbm_load("basico.pbm");

    if(!img)
        return 1;

    PBM_Image *encrypted =
        pbm_encrypt(img);

    if(pbm_send_to_server("127.0.0.1",
                          8080,
                          img) < 0)
    {
        return 1;
    }

    pbm_free(img);

    pbm_free(encrypted);

    return 0;
}