#ifndef PKHEX_GC_SHA1_H
#define PKHEX_GC_SHA1_H
#include <stddef.h>
#include <stdint.h>
void pkhexgc_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
#endif
