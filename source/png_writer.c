#include "png_writer.h"

#include <stdlib.h>
#include <string.h>

#define PNG_ADLER_MOD 65521u

static void put_be32(uint8_t p[4], uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t crc_table[256];
static bool crc_table_ready;

static void crc_init(void)
{
    if (crc_table_ready) return;
    for (uint32_t n = 0; n < 256u; ++n)
    {
        uint32_t c = n;
        for (unsigned k = 0; k < 8u; ++k)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_table_ready = true;
}

static uint32_t crc_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    while (size--)
        crc = crc_table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static bool write_all(FILE *f, const void *data, size_t size)
{
    return size == 0 || fwrite(data, 1, size, f) == size;
}

static bool write_chunk(FILE *f, const char type[4], const void *data, uint32_t size)
{
    uint8_t n[4];
    put_be32(n, size);
    if (!write_all(f, n, sizeof(n)) || !write_all(f, type, 4) ||
        !write_all(f, data, size)) return false;

    uint32_t crc = 0xFFFFFFFFu;
    crc = crc_update(crc, type, 4);
    crc = crc_update(crc, data, size) ^ 0xFFFFFFFFu;
    put_be32(n, crc);
    return write_all(f, n, sizeof(n));
}

static uint32_t adler32_bytes(const uint8_t *data, size_t size)
{
    uint32_t s1 = 1u, s2 = 0u;
    while (size)
    {
        size_t n = size > 5552u ? 5552u : size;
        size -= n;
        while (n--)
        {
            s1 += *data++;
            s2 += s1;
        }
        s1 %= PNG_ADLER_MOD;
        s2 %= PNG_ADLER_MOD;
    }
    return (s2 << 16) | s1;
}

bool png_write_rgb8(FILE *f, unsigned width, unsigned height,
                    const uint8_t *rgb, size_t stride)
{
    if (!f || !rgb || width == 0 || height == 0 ||
        width > 0x1FFFFFFFu || stride < (size_t)width * 3u)
        return false;

    crc_init();
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    if (!write_all(f, sig, sizeof(sig))) return false;

    uint8_t ihdr[13] = {0};
    put_be32(ihdr + 0, width);
    put_be32(ihdr + 4, height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* truecolor RGB */
    if (!write_chunk(f, "IHDR", ihdr, sizeof(ihdr))) return false;

    const size_t row_bytes = (size_t)width * 3u;
    if (height > (SIZE_MAX / (row_bytes + 1u))) return false;
    const size_t raw_size = (row_bytes + 1u) * (size_t)height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) return false;

    uint8_t *dst = raw;
    for (unsigned y = 0; y < height; ++y)
    {
        *dst++ = 0; /* PNG filter: None */
        memcpy(dst, rgb + (size_t)y * stride, row_bytes);
        dst += row_bytes;
    }

    const size_t blocks = (raw_size + 65534u) / 65535u;
    if (raw_size > UINT32_MAX - 6u - blocks * 5u)
    {
        free(raw);
        return false;
    }
    const uint32_t idat_size = (uint32_t)(2u + raw_size + blocks * 5u + 4u);
    uint8_t be[4];
    put_be32(be, idat_size);
    if (!write_all(f, be, 4) || !write_all(f, "IDAT", 4))
    {
        free(raw);
        return false;
    }

    uint32_t crc = 0xFFFFFFFFu;
    crc = crc_update(crc, "IDAT", 4);

#define IDAT_WRITE(ptr_, len_) do { \
        const void *p__ = (ptr_); const size_t n__ = (len_); \
        if (!write_all(f, p__, n__)) { free(raw); return false; } \
        crc = crc_update(crc, p__, n__); \
    } while (0)

    static const uint8_t zlib_header[2] = {0x78u, 0x01u};
    IDAT_WRITE(zlib_header, sizeof(zlib_header));

    size_t pos = 0;
    while (pos < raw_size)
    {
        const size_t left = raw_size - pos;
        const uint16_t len = (uint16_t)(left > 65535u ? 65535u : left);
        const uint16_t nlen = (uint16_t)~len;
        uint8_t header[5];
        header[0] = (pos + len == raw_size) ? 0x01u : 0x00u; /* BFINAL, stored */
        header[1] = (uint8_t)len;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)nlen;
        header[4] = (uint8_t)(nlen >> 8);
        IDAT_WRITE(header, sizeof(header));
        IDAT_WRITE(raw + pos, len);
        pos += len;
    }

    uint32_t adler = adler32_bytes(raw, raw_size);
    free(raw);
    put_be32(be, adler);
    if (!write_all(f, be, 4)) return false;
    crc = crc_update(crc, be, 4) ^ 0xFFFFFFFFu;
    put_be32(be, crc);
    if (!write_all(f, be, 4)) return false;
#undef IDAT_WRITE

    return write_chunk(f, "IEND", NULL, 0);
}
