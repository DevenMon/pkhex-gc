/*
 * Pins the GameCube <-> GBA JOY-bus byte order.
 *
 * The GameCube is big-endian and the GBA is little-endian, so every value that
 * crosses this link has exactly one correct orientation.  These tests model
 * the GBA side independently of include/gba_link_wire.h and check that a word
 * written by one end is the word the other end reads.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gba_link_wire.h"
#include "gba_save_checksum.h"

/* Model of the GBA staging a word in REG_JOY_TRANS: least significant byte
 * first onto the wire. */
static void gba_stages(uint8_t wire[4], uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) wire[i] = (uint8_t)(value >> (8 * i));
}

/* Model of the GBA reading REG_JOY_RECV back out. */
static uint32_t gba_receives(const uint8_t wire[4])
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)wire[i] << (8 * i);
    return v;
}

static void test_scalars_survive_both_directions(void)
{
    static const uint32_t values[] = {
        0u, 1u, 0xFFu, 0x100u, 0x12345678u, 0x504B4147u /* 'PKAG' */,
        0x49444E54u /* 'IDNT' */, 0x57524954u /* 'WRIT' */, 0xB5B5B5B5u,
        0xFFFFFFFFu, 0x20000u,
    };
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        const uint32_t v = values[i];
        uint8_t wire[4];

        /* Host sends a command word; the agent must see the same value. */
        gba_scalar_to_wire(wire, v);
        assert(gba_receives(wire) == v);

        /* Agent publishes a status word; the host must see the same value. */
        gba_stages(wire, v);
        assert(gba_wire_to_scalar(wire) == v);
    }
    printf("  scalars round-trip in both directions\n");
}

static void test_save_payload_keeps_cartridge_order(void)
{
    /*
     * The agent moves save bytes with *(uint32_t *)(buf + i), so a word is the
     * little-endian read of four consecutive cartridge bytes.  A dump has to
     * come out in cartridge address order, byte for byte.
     */
    uint8_t cart[64], host[64];
    for (unsigned i = 0; i < sizeof(cart); ++i) cart[i] = (uint8_t)(i * 7u + 3u);

    for (unsigned i = 0; i < sizeof(cart); i += 4) {
        uint32_t word = 0;
        for (unsigned b = 0; b < 4; ++b) word |= (uint32_t)cart[i + b] << (8 * b);

        uint8_t wire[4];
        gba_stages(wire, word);
        memcpy(host + i, wire, 4); /* what source/gbalink.c does on a read */
    }
    assert(memcmp(cart, host, sizeof(cart)) == 0);

    /* And the same in the write direction. */
    uint8_t back[64];
    for (unsigned i = 0; i < sizeof(host); i += 4) {
        uint8_t wire[4];
        memcpy(wire, host + i, 4); /* what source/gbalink.c does on a write */
        const uint32_t word = gba_receives(wire);
        for (unsigned b = 0; b < 4; ++b) back[i + b] = (uint8_t)(word >> (8 * b));
    }
    assert(memcmp(cart, back, sizeof(cart)) == 0);
    printf("  save payload keeps cartridge byte order\n");
}

static void test_checksum_detects_transfer_damage(void)
{
    uint8_t data[512];
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 31u + 11u);

    const uint32_t base = gba_save_checksum(data, sizeof(data));
    assert(gba_save_checksum(data, sizeof(data)) == base);

    /* Every single-bit flip must change the checksum. */
    for (unsigned i = 0; i < sizeof(data); ++i) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            data[i] ^= (uint8_t)(1u << bit);
            assert(gba_save_checksum(data, sizeof(data)) != base);
            data[i] ^= (uint8_t)(1u << bit);
        }
    }

    /* A truncated transfer must not look like a complete one. */
    assert(gba_save_checksum(data, sizeof(data) - 4) != base);
    printf("  transfer checksum catches flips and truncation\n");
}

int main(void)
{
    test_scalars_survive_both_directions();
    test_save_payload_keeps_cartridge_order();
    test_checksum_detects_transfer_damage();
    printf("GBA link wire-format tests: PASS\n");
    return 0;
}
