#ifndef PKHEX_GC_PNG_WRITER_H
#define PKHEX_GC_PNG_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Write an 8-bit RGB PNG without external compression libraries. The encoder
 * uses valid zlib/DEFLATE stored blocks: screenshots are larger than compressed
 * PNGs, but the implementation is deterministic and keeps the GameCube build
 * independent of libpng/zlib. */
bool png_write_rgb8(FILE *f, unsigned width, unsigned height,
                    const uint8_t *rgb, size_t stride);

#endif
