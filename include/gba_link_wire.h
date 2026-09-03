#ifndef PKHEX_GC_GBA_LINK_WIRE_H
#define PKHEX_GC_GBA_LINK_WIRE_H

#include <stdint.h>

/*
 * Byte order on the GameCube <-> GBA JOY bus.
 *
 * A JOY-bus word is four bytes.  The GBA's REG_JOY_RECV / REG_JOY_TRANS put
 * the LEAST significant byte first, so a value the agent sends or receives as
 * a uint32_t is little-endian on the wire.  This host is big-endian, which is
 * exactly the sort of asymmetry that hides bugs, so the conversion lives here
 * in one place, is documented, and is covered by tests/test_gbalink_wire.c.
 *
 * Two consequences worth stating plainly:
 *   - Save payload bytes need no conversion at all.  The agent reads them out
 *     of cartridge memory in address order and they arrive in address order,
 *     so a dump is byte-identical to the cartridge.
 *   - The single exception on this link is the JoyBoot session key, which the
 *     GBA BIOS - not the agent - consumes, most significant byte first.
 *     source/gbalink.c spells that one out at the call site.
 */

static inline uint32_t gba_wire_to_scalar(const uint8_t raw[4])
{
    return (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
           ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
}

static inline void gba_scalar_to_wire(uint8_t raw[4], uint32_t value)
{
    raw[0] = (uint8_t)value;
    raw[1] = (uint8_t)(value >> 8);
    raw[2] = (uint8_t)(value >> 16);
    raw[3] = (uint8_t)(value >> 24);
}

#endif
