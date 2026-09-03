/*
 * GBA JOY-bus multiboot handshake arithmetic.  See include/joyboot.h and
 * SOURCES.md; the sequence itself is the one the GBA BIOS implements.
 */
#include "joyboot.h"

uint32_t joyboot_key(uint32_t send_size)
{
    uint32_t size = (send_size - 0x200u) >> 3;
    uint32_t res1 = (size & 0x3F80u) << 1;
    res1 |= (size & 0x4000u) << 2;
    res1 |= (size & 0x7Fu);
    res1 |= 0x380000u;

    const uint32_t res2 = res1;
    uint32_t res3 = (res2 >> 8) + (res2 >> 16) + res2;
    res3 <<= 24;
    res3 |= res2;
    res3 |= 0x80808080u;

    uint32_t ret = 0;
    if ((res3 & 0x200u) == 0)
    {
        ret |= ((res3 & 0xFFu) ^ 0x4Bu) << 24;
        ret |= (((res3 >> 8) & 0xFFu) ^ 0x61u) << 16;
        ret |= (((res3 >> 16) & 0xFFu) ^ 0x77u) << 8;
        ret |= (((res3 >> 24) & 0xFFu) ^ 0x61u);
    }
    else
    {
        ret |= ((res3 & 0xFFu) ^ 0x73u) << 24;
        ret |= (((res3 >> 8) & 0xFFu) ^ 0x65u) << 16;
        ret |= (((res3 >> 16) & 0xFFu) ^ 0x64u) << 8;
        ret |= (((res3 >> 24) & 0xFFu) ^ 0x6Fu);
    }
    return ret;
}

uint32_t joyboot_crc_step(uint32_t crc, uint32_t plain_word)
{
    for (unsigned i = 0; i < 32u; ++i)
    {
        if ((crc ^ plain_word) & 1u)
        {
            crc >>= 1;
            crc ^= 0xA1C1u;
        }
        else
        {
            crc >>= 1;
        }
        plain_word >>= 1;
    }
    return crc;
}

uint32_t joyboot_session_key(uint32_t raw_word)
{
    const uint32_t x = raw_word ^ 0x7365646Fu;
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}

static uint32_t advance(uint32_t *session_key)
{
    *session_key = (*session_key * 0x6177614Bu) + 1u;
    return *session_key;
}

uint32_t joyboot_encrypt(uint32_t *session_key, uint32_t plain_word,
                         uint32_t offset)
{
    uint32_t enc = plain_word;
    enc ^= advance(session_key);
    enc ^= (~(offset + (0x20u << 20)) + 1u);
    enc ^= 0x20796220u;
    return enc;
}

uint32_t joyboot_finish(uint32_t *session_key, uint32_t crc, uint32_t offset)
{
    return joyboot_encrypt(session_key, crc, offset);
}
