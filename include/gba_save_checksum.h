#ifndef PKHEX_GC_GBA_SAVE_CHECKSUM_H
#define PKHEX_GC_GBA_SAVE_CHECKSUM_H

#include <stdint.h>

/*
 * Integrity check for a save image crossing the GBA link cable.
 *
 * Both ends of the link must agree bit-for-bit, so the GameCube host
 * (source/gbalink.c) and the cartridge agent (gba-agent/agent.c) include this
 * one definition rather than keeping two copies in step by hand.  It only has
 * to catch a truncated or bit-rotted transfer, not resist tampering.
 */
static inline uint32_t gba_save_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t sum = 0x9E3779B9u;
    for (uint32_t i = 0; i < size; ++i)
        sum = (sum << 3) ^ (sum >> 29) ^ data[i];
    return sum;
}

#endif
