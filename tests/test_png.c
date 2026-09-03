#include <stdio.h>
#include <stdint.h>
#include "png_writer.h"

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    static const uint8_t rgb[2 * 2 * 3] = {
        255,0,0,   0,255,0,
        0,0,255,   255,255,255
    };
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 3;
    int ok = png_write_rgb8(f, 2, 2, rgb, 6) ? 0 : 4;
    if (fclose(f) != 0) return 5;
    return ok;
}
