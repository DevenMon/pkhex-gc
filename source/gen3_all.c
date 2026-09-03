#include "gen3_all.h"
#include "sha1.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define COLO_SLOT_START 0x6000u
#define COLO_SLOT_SIZE  0x1E000u
#define COLO_SLOT_COUNT 3u
#define COLO_PARTY      0x00A8u
#define COLO_BOX        0x0B90u
#define COLO_BOX_STRIDE 0x24A4u
#define COLO_PK_SIZE    312u

#define XD_SLOT_START   0x6000u
#define XD_SLOT_SIZE    0x28000u
#define XD_SLOT_COUNT   2u
#define XD_PK_SIZE      196u
#define XD_DISPLACEMENT 0x00A8u
#define XD_BOX_STRIDE   ((30u * XD_PK_SIZE) + 0x14u)

#define RSBOX_BLOCK_START 0x2000u
#define RSBOX_BLOCK_SIZE  0x2000u
#define RSBOX_BLOCK_COUNT 23u
#define RSBOX_COPY_SIZE   0x1FF0u
#define RSBOX_SLOT_SIZE   (RSBOX_BLOCK_COUNT * RSBOX_BLOCK_SIZE)
#define RSBOX_PK_SIZE     84u
#define RSBOX_BOXES       50u

static uint16_t rd16be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t rd64be(const uint8_t *p) {
    return ((uint64_t)rd32be(p) << 32) | rd32be(p + 4);
}
static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void wr32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* The markings byte stores square and triangle the other way round from the
 * order the UI uses; the same swap appears in the GBA record. */
static uint8_t swap_bits_u8(uint8_t value, unsigned a, unsigned b) {
    const unsigned bit_a = (value >> a) & 1u, bit_b = (value >> b) & 1u;
    if (bit_a == bit_b) return value;
    return (uint8_t)(value ^ ((1u << a) | (1u << b)));
}

static bool range_ok(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

/* Western Colosseum/XD strings are UTF-16BE-ish. For a compact GameCube
 * reader, ASCII maps directly; unsupported glyphs are represented as '?'. */
static void decode_gc_text(const uint8_t *src, size_t bytes, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return;
    size_t w = 0;
    for (size_t i = 0; i + 1 < bytes && w + 1 < dst_len; i += 2) {
        uint16_t ch = rd16be(src + i);
        if (ch == 0) break;
        if (ch >= 0x20 && ch <= 0x7E)
            dst[w++] = (char)ch;
        else if (ch == 0x00A5)
            dst[w++] = 'Y';
        else
            dst[w++] = '?';
    }
    dst[w] = '\0';
}

static uint64_t be_float_seconds(const uint8_t *p) {
    union { uint32_t u; float f; } v;
    v.u = rd32be(p);
    if (!isfinite(v.f) || v.f < 0.0f) return 0;
    return (uint64_t)v.f;
}

static uint64_t be_double_seconds(const uint8_t *p) {
    union { uint64_t u; double d; } v;
    v.u = rd64be(p);
    if (!isfinite(v.d) || v.d < 0.0) return 0;
    return (uint64_t)v.d;
}

static bool gc_slot_intro_ok(const uint8_t *p) {
    /* PKHeX's SaveUtil detects 01 01 00 00 at the start of GC save slots. */
    return rd32le(p) == 0x00000101u;
}

static int pick_latest_slot(const uint8_t *base, size_t total, size_t start,
                            size_t slot_size, unsigned slot_count) {
    int best = -1;
    uint32_t best_counter = 0;
    for (unsigned i = 0; i < slot_count; ++i) {
        size_t off = start + (size_t)i * slot_size;
        if (!range_ok(off, slot_size, total) || !gc_slot_intro_ok(base + off))
            continue;
        uint32_t counter = rd32be(base + off + 4);
        if (best < 0 || counter > best_counter) {
            best = (int)i;
            best_counter = counter;
        }
    }
    return best;
}

static void colo_decrypt(uint8_t *slot, size_t len) {
    if (len < 0x18u + 20u) return;
    uint8_t digest[20], next[20];
    for (unsigned i = 0; i < 20; ++i)
        digest[i] = (uint8_t)~slot[len - 20u + i];

    uint8_t *p = slot + 0x18u;
    uint8_t *end = slot + len - 20u;
    while ((size_t)(end - p) >= 20u) {
        /* The chain hashes the still-encrypted chunk, then XORs it. */
        pkhexgc_sha1(p, 20u, next);
        for (unsigned i = 0; i < 20; ++i)
            p[i] ^= digest[i];
        memcpy(digest, next, 20u);
        p += 20u;
    }
}

static void colo_encrypt(uint8_t *slot, size_t len) {
    if (len < 0x18u + 20u) return;
    uint8_t digest[20], next[20];
    for (unsigned i = 0; i < 20; ++i)
        digest[i] = (uint8_t)~slot[len - 20u + i];
    uint8_t *p = slot + 0x18u;
    uint8_t *end = slot + len - 20u;
    while ((size_t)(end - p) >= 20u) {
        for (unsigned i = 0; i < 20; ++i)
            p[i] ^= digest[i];
        pkhexgc_sha1(p, 20u, next);
        memcpy(digest, next, 20u);
        p += 20u;
    }
}

/* Colosseum has two independent integrity values in the decrypted slot.
 * PKHeX's ColoCrypto.SetChecksums computes SHA-1 over everything except the
 * final 40 bytes, stores that digest in the final 20 bytes, then computes the
 * signed/wrapping header checksum at 0x0C.  The body digest is also the seed
 * for the chained encryption, so this MUST run before colo_encrypt(). */
static uint32_t colo_header_checksum(const uint8_t *slot, const uint8_t hash[20]) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 0x18u; i += 4u)
        result -= rd32be(slot + i);
    result -= rd32be(slot + 0x18u) ^ ~rd32be(hash + 0u);
    result -= rd32be(slot + 0x1Cu) ^ ~rd32be(hash + 4u);
    return result;
}

static bool colo_set_checksums(uint8_t *slot, size_t len) {
    if (!slot || len != COLO_SLOT_SIZE) return false;
    memset(slot + 0x0Cu, 0, 4u);
    uint8_t hash[20];
    pkhexgc_sha1(slot, len - 40u, hash);
    memcpy(slot + len - 20u, hash, 20u);
    wr32be(slot + 0x0Cu, colo_header_checksum(slot, hash));
    return true;
}

static bool colo_checksums_valid(uint8_t *slot, size_t len) {
    if (!slot || len != COLO_SLOT_SIZE) return false;
    uint32_t old = rd32be(slot + 0x0Cu);
    uint8_t saved[4]; memcpy(saved, slot + 0x0Cu, 4u);
    memset(slot + 0x0Cu, 0, 4u);
    uint8_t hash[20];
    pkhexgc_sha1(slot, len - 40u, hash);
    uint32_t expected = colo_header_checksum(slot, hash);
    memcpy(slot + 0x0Cu, saved, 4u);
    return old == expected && memcmp(slot + len - 20u, hash, 20u) == 0;
}

static void genius_advance_keys(uint16_t k[4]) {
    uint16_t k3 = (uint16_t)(k[3] + 0x13u);
    uint16_t k2 = (uint16_t)(k[2] + 0x17u);
    uint16_t k1 = (uint16_t)(k[1] + 0x29u);
    uint16_t k0 = (uint16_t)(k[0] + 0x43u);
    uint16_t n[4];
    n[3] = (uint16_t)(((k0 >> 12) & 0x000Fu) | ((k1 >> 8) & 0x00F0u) |
                      ((k2 >> 4) & 0x0F00u) | (k3 & 0xF000u));
    n[2] = (uint16_t)(((k0 >> 8) & 0x000Fu) | ((k1 >> 4) & 0x00F0u) |
                      (k2 & 0x0F00u) | ((k3 << 4) & 0xF000u));
    n[1] = (uint16_t)(((k0 >> 4) & 0x000Fu) | (k1 & 0x00F0u) |
                      ((k2 << 4) & 0x0F00u) | ((k3 << 8) & 0xF000u));
    n[0] = (uint16_t)((k0 & 0x000Fu) | ((k1 << 4) & 0x00F0u) |
                      ((k2 << 8) & 0x0F00u) | ((k3 << 12) & 0xF000u));
    memcpy(k, n, sizeof(n));
}

static void xd_decrypt(uint8_t *slot, size_t len) {
    if (len < 0x40u) return;
    uint16_t k[4];
    for (unsigned i = 0; i < 4; ++i)
        k[i] = rd16be(slot + 0x08u + i * 2u);

    /* XDCrypto leaves the header and final 0x28 bytes outside the cipher. */
    const size_t begin = 0x10u;
    const size_t end = len - 0x28u;
    unsigned word = 0;
    for (size_t off = begin; off + 1 < end; off += 2, ++word) {
        uint16_t enc = rd16be(slot + off);
        uint16_t dec = (uint16_t)(enc - k[word & 3u]);
        wr16be(slot + off, dec);
        if ((word & 3u) == 3u)
            genius_advance_keys(k);
    }
}

static void xd_encrypt(uint8_t *slot, size_t len) {
    if (len < 0x40u) return;
    uint16_t k[4];
    for (unsigned i = 0; i < 4; ++i)
        k[i] = rd16be(slot + 0x08u + i * 2u);
    const size_t begin = 0x10u;
    const size_t end = len - 0x28u;
    unsigned word = 0;
    for (size_t off = begin; off + 1 < end; off += 2, ++word) {
        uint16_t dec = rd16be(slot + off);
        wr16be(slot + off, (uint16_t)(dec + k[word & 3u]));
        if ((word & 3u) == 3u)
            genius_advance_keys(k);
    }
}

/*
 * XD keeps two checksums over the decrypted slot: a header sum of the first
 * eight bytes, stored inside the first substructure, and a body sum split
 * into four 32-bit halves that are written back reversed into 0x10-0x1F. The
 * body sum is taken with that region already cleared, so it has to be zeroed
 * before summing and filled in afterwards.
 */
#define XD_BODY_CHUNK 0x9FF4u

static void xd_body_checksums(const uint8_t *slot, uint16_t out[8]) {
    uint32_t part[4];
    size_t at = 8u;
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t val = 0;
        const size_t end = at + XD_BODY_CHUNK;
        for (size_t j = at; j < end; j += 2u) val += rd16be(slot + j);
        at = end;
        part[i] = val;
    }
    uint16_t chk[8];
    for (unsigned i = 0; i < 4u; ++i) {
        chk[i * 2u] = (uint16_t)(part[i] >> 16);
        chk[i * 2u + 1u] = (uint16_t)part[i];
    }
    /* Written back in reverse order, which is what the game expects. */
    for (unsigned i = 0; i < 8u; ++i) out[i] = chk[7u - i];
}

static bool xd_set_checksums(uint8_t *slot, size_t len, uint32_t sub0_offset) {
    if (!slot || len != XD_SLOT_SIZE) return false;
    /* 0x88 + 0x20 puts the header checksum inside the first substructure. */
    const size_t header_at = 0xA8u + (size_t)sub0_offset + 0x38u;
    if (!range_ok(header_at, 4u, len)) return false;
    uint32_t header = 0;
    for (unsigned i = 0; i < 8u; ++i) header += slot[i];
    wr32be(slot + header_at, header);

    memset(slot + 0x10, 0, 0x10);
    uint16_t body[8];
    xd_body_checksums(slot, body);
    for (unsigned i = 0; i < 8u; ++i) wr16be(slot + 0x10u + i * 2u, body[i]);
    return true;
}

static bool xd_checksums_valid(const uint8_t *slot, size_t len, uint32_t sub0_offset) {
    if (!slot || len != XD_SLOT_SIZE) return false;
    const size_t header_at = 0xA8u + (size_t)sub0_offset + 0x38u;
    if (!range_ok(header_at, 4u, len)) return false;
    uint32_t header = 0;
    for (unsigned i = 0; i < 8u; ++i) header += slot[i];
    if (rd32be(slot + header_at) != header) return false;

    /* Summing needs the stored checksum region zeroed, so work on a copy. */
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) return false;
    memcpy(copy, slot, len);
    memset(copy + 0x10, 0, 0x10);
    uint16_t body[8];
    xd_body_checksums(copy, body);
    free(copy);
    for (unsigned i = 0; i < 8u; ++i)
        if (rd16be(slot + 0x10u + i * 2u) != body[i]) return false;
    return true;
}

/*
 * CK3 - Pokemon Colosseum's record. Big-endian, with EVs, IVs and friendship
 * stored as halfwords, contest ribbon levels and the twelve flag ribbons each
 * as their own byte, and the stat order HP/Atk/Def/SpA/SpD/Spe. Offsets follow
 * PKHeX's CK3; see SOURCES.md.
 */
#define CK3_EV(i) (0x98u + (i) * 2u)
#define CK3_IV(i) (0xA4u + (i) * 2u)

/* Same reordering as XD: SpA and SpD come before Speed on disk. */
static const unsigned ck3_stat_order[6] = { 0u, 1u, 2u, 5u, 3u, 4u };

static bool parse_ck3(const uint8_t *src, size_t size, bool japanese, Gen3Pokemon *out) {
    if (!src || !out || size < COLO_PK_SIZE) return false;
    memset(out, 0, sizeof(*out));
    uint16_t species = rd16be(src + 0x00);
    if (species == 0) return true;
    out->present = true;
    out->species_internal = species;
    out->pid = rd32be(src + 0x04);
    out->origin_game = src[0x08];
    out->language = src[0x0B];
    out->met_location = (uint8_t)rd16be(src + 0x0C);
    out->met_level = src[0x0E];
    out->ball = src[0x0F];
    out->ot_gender = src[0x10];
    out->otid = rd32be(src + 0x14);
    /* CK3 stores Secret ID then Trainer ID in the big-endian full OT ID. */
    out->sid = rd16be(src + 0x14);
    out->tid = rd16be(src + 0x16);
    out->experience = rd32be(src + 0x5C);
    out->level = src[0x60];
    for (unsigned i = 0; i < 4; ++i) {
        out->moves[i] = rd16be(src + 0x78u + i * 4u);
        out->pp[i] = src[0x7Au + i * 4u];
        out->pp_ups |= (uint8_t)((src[0x7Bu + i * 4u] & 3u) << (i * 2u));
    }
    out->held_item = rd16be(src + 0x88);

    for (unsigned i = 0; i < 6u; ++i) {
        const unsigned slot = ck3_stat_order[i];
        const uint16_t ev = rd16be(src + CK3_EV(slot));
        out->evs[i] = (uint8_t)(ev > 255u ? 255u : ev);
        const uint16_t iv = rd16be(src + CK3_IV(slot));
        out->ivs[i] = (uint8_t)(iv > 31u ? 31u : iv);
    }
    const uint16_t friendship = rd16be(src + 0xB0);
    out->friendship = (uint8_t)(friendship > 255u ? 255u : friendship);

    for (unsigned i = 0; i < 5u; ++i) out->contest[i] = src[0xB2u + i];
    out->contest[5] = src[0xBC];
    /* Contest ribbon levels are a byte each here, not three bits. */
    for (unsigned i = 0; i < 5u; ++i)
        gen3_set_contest_ribbon(out, i, src[0xB7u + i] > 4u ? 4u : src[0xB7u + i]);
    /* Twelve one-byte flag ribbons, in the same order as the GBA's bit run. */
    for (unsigned i = 0; i < GEN3_RIBBON_FLAG_COUNT; ++i)
        if (src[0xBDu + i] == 1u) out->ribbons |= 1u << (15u + i);

    out->pokerus = (uint8_t)((src[0xCA] & 0x0Fu) << 4);
    if (src[0xD0] != 0xFFu) out->pokerus |= (uint8_t)(src[0xD0] & 0x0Fu);
    out->is_egg = src[0xCB] == 1u;
    out->ability_bit = src[0xCC] == 1u;
    out->checksum_ok = src[0xCD] == 0u;   /* Colosseum's own validity byte */
    out->markings = swap_bits_u8(src[0xCF], 1, 2);
    /* Colosseum keeps the fateful flag in a different byte per region, and
     * reads it as the two disagreeing. PKHeX does the same. */
    {
        const bool jpn = (src[0xC9] >> 4) & 1u;
        const bool intl = src[0xFB] & 1u;
        out->fateful = jpn != intl;
    }
    (void)japanese;

    out->shadow_id = rd16be(src + 0xD8);
    out->purification = (int32_t)rd32be(src + 0xDC);
    out->is_shadow = out->shadow_id != 0 && out->purification != -100;
    decode_gc_text(src + 0x44, 22, out->nickname, sizeof(out->nickname));
    decode_gc_text(src + 0x18, 22, out->ot_name, sizeof(out->ot_name));
    return true;
}

static bool write_ck3(uint8_t *dst, size_t size, bool japanese, const Gen3Pokemon *p) {
    if (!dst || !p || size < COLO_PK_SIZE || !p->present) return false;
    wr16be(dst + 0x00, p->species_internal);
    wr32be(dst + 0x04, p->pid);
    dst[0x08] = p->origin_game;
    dst[0x0B] = p->language;
    wr16be(dst + 0x0C, p->met_location);
    dst[0x0E] = p->met_level;
    dst[0x0F] = p->ball;
    dst[0x10] = (uint8_t)(p->ot_gender ? 1u : 0u);
    wr16be(dst + 0x14, p->sid);
    wr16be(dst + 0x16, p->tid);
    wr32be(dst + 0x5C, p->experience);
    dst[0x60] = p->level;
    for (unsigned i = 0; i < 4; ++i) {
        wr16be(dst + 0x78u + i * 4u, p->moves[i]);
        dst[0x7Au + i * 4u] = p->pp[i];
        dst[0x7Bu + i * 4u] = (uint8_t)((p->pp_ups >> (i * 2u)) & 3u);
    }
    wr16be(dst + 0x88, p->held_item);
    for (unsigned i = 0; i < 6u; ++i) {
        const unsigned slot = ck3_stat_order[i];
        wr16be(dst + CK3_EV(slot), p->evs[i]);
        wr16be(dst + CK3_IV(slot), (uint16_t)(p->ivs[i] & 31u));
    }
    wr16be(dst + 0xB0, p->friendship);
    for (unsigned i = 0; i < 5u; ++i) dst[0xB2u + i] = p->contest[i];
    dst[0xBC] = p->contest[5];
    for (unsigned i = 0; i < 5u; ++i) dst[0xB7u + i] = gen3_contest_ribbon(p, i);
    /*
     * 0xC0-0xC3 hold four of the flag ribbons and, in PKHeX's model, also the
     * shadow experience. That overlap is upstream's; writing the ribbons here
     * keeps this port's behaviour the same as PKHeX's rather than inventing a
     * rule of its own.
     */
    for (unsigned i = 0; i < GEN3_RIBBON_FLAG_COUNT; ++i)
        dst[0xBDu + i] = gen3_ribbon_flag(p, i) ? 1u : 0u;

    dst[0xCA] = (uint8_t)(gen3_pokerus_strain(p) & 0x0Fu);
    dst[0xD0] = gen3_pokerus_days(p) ? (uint8_t)(gen3_pokerus_days(p) & 0x0Fu) : 0xFFu;
    dst[0xCB] = p->is_egg ? 1u : 0u;
    dst[0xCC] = p->ability_bit ? 1u : 0u;
    dst[0xCD] = p->checksum_ok ? 0u : 1u;
    dst[0xCF] = swap_bits_u8(p->markings, 1, 2);
    /* Set the flag in the byte this region reads and clear the other, so the
     * two never disagree by accident. */
    dst[0xC9] = (uint8_t)((dst[0xC9] & (uint8_t)~0x10u) | ((japanese && p->fateful) ? 0x10u : 0u));
    dst[0xFB] = (uint8_t)((dst[0xFB] & (uint8_t)~0x01u) | ((!japanese && p->fateful) ? 0x01u : 0u));
    /* Shadow ID, the heart gauge, both names and everything this port does
     * not model are left exactly as they were. */
    return true;
}

/*
 * XK3 - Pokemon XD's record. Big-endian throughout, with IVs and contest
 * stats as plain bytes rather than the GBA's packed bitfields, and the stat
 * order HP/Atk/Def/SpA/SpD/Spe rather than the GBA's HP/Atk/Def/Spe/SpA/SpD.
 * Offsets follow PKHeX's XK3; see SOURCES.md.
 */
#define XK3_EV(i) (0x9Cu + (i) * 2u)
#define XK3_IV(i) (0xA8u + (i))

/* XD stores SpA and SpD before Speed; the UI order puts Speed third. */
static const unsigned xk3_stat_order[6] = { 0u, 1u, 2u, 5u, 3u, 4u };

static bool parse_xk3(const uint8_t *src, size_t size, Gen3Pokemon *out) {
    if (!src || !out || size < XD_PK_SIZE) return false;
    memset(out, 0, sizeof(*out));
    uint16_t species = rd16be(src + 0x00);
    if (species == 0) return true;
    out->present = true;
    out->species_internal = species;
    out->held_item = rd16be(src + 0x02);
    out->friendship = (uint8_t)rd16be(src + 0x06);
    out->met_location = (uint8_t)rd16be(src + 0x08);
    out->met_level = src[0x0E];
    out->ball = src[0x0F];
    out->ot_gender = src[0x10];
    out->level = src[0x11];
    out->contest[5] = src[0x12];               /* sheen */
    out->pokerus = (uint8_t)((src[0x13] & 0x0Fu) << 4);
    /* The days byte is 0xFF when the strain has run its course. */
    if (src[0x15] != 0xFFu) out->pokerus |= (uint8_t)(src[0x15] & 0x0Fu);
    out->markings = swap_bits_u8(src[0x14], 1, 2);

    const uint8_t flags = src[0x1D];
    out->checksum_ok = (flags & 0x20u) == 0;   /* XD marks invalid records */
    out->ability_bit = (flags & 0x40u) != 0;
    out->is_egg = (flags & 0x80u) != 0;

    out->experience = rd32be(src + 0x20);
    out->otid = rd32be(src + 0x24);
    out->sid = rd16be(src + 0x24);
    out->tid = rd16be(src + 0x26);
    out->pid = rd32be(src + 0x28);
    out->fateful = src[0x33] != 0;
    out->language = src[0x37];
    decode_gc_text(src + 0x38, 22, out->ot_name, sizeof(out->ot_name));
    decode_gc_text(src + 0x64, 22, out->nickname, sizeof(out->nickname));

    /* The ribbon halfword is packed the other way up from the GBA's: XD puts
     * Champion in bit 15 where PK3 has it in bit 15 of the low half, so the
     * flags are reversed relative to gen3_ribbon_flag's numbering. */
    const uint16_t rib = rd16be(src + 0x7C);
    for (unsigned i = 0; i < 12u; ++i)
        if (rib & (uint16_t)(1u << (15u - i))) out->ribbons |= 1u << (15u + i);

    for (unsigned i = 0; i < 4u; ++i) {
        out->moves[i] = rd16be(src + 0x80u + i * 4u);
        out->pp[i] = src[0x82u + i * 4u];
        const uint8_t ups = src[0x83u + i * 4u];
        out->pp_ups |= (uint8_t)((ups & 3u) << (i * 2u));
    }

    for (unsigned i = 0; i < 6u; ++i) {
        const unsigned slot = xk3_stat_order[i];
        const uint16_t ev = rd16be(src + XK3_EV(slot));
        out->evs[i] = (uint8_t)(ev > 255u ? 255u : ev);
        const uint8_t iv = src[XK3_IV(slot)];
        out->ivs[i] = (uint8_t)(iv > 31u ? 31u : iv);
    }
    for (unsigned i = 0; i < 5u; ++i) out->contest[i] = src[0xAEu + i];
    /* Contest ribbon levels are one byte each here, not three bits. */
    for (unsigned i = 0; i < 5u; ++i)
        gen3_set_contest_ribbon(out, i, src[0xB3u + i] > 4u ? 4u : src[0xB3u + i]);

    out->shadow_id = rd16be(src + 0xBA);
    /* XD's exact shadow state lives in the save's shadow table; a nonzero
     * Shadow ID is what the record itself can say. */
    out->is_shadow = out->shadow_id != 0;
    return true;
}

static bool write_xk3(uint8_t *dst, size_t size, const Gen3Pokemon *p) {
    if (!dst || !p || size < XD_PK_SIZE || !p->present) return false;
    wr16be(dst + 0x00, p->species_internal);
    wr16be(dst + 0x02, p->held_item);
    wr16be(dst + 0x06, p->friendship);
    wr16be(dst + 0x08, p->met_location);
    dst[0x0E] = p->met_level;
    dst[0x0F] = p->ball;
    dst[0x10] = (uint8_t)(p->ot_gender ? 1u : 0u);
    dst[0x11] = p->level;
    dst[0x12] = p->contest[5];
    dst[0x13] = (uint8_t)(gen3_pokerus_strain(p) & 0x0Fu);
    dst[0x14] = swap_bits_u8(p->markings, 1, 2);
    dst[0x15] = gen3_pokerus_days(p) ? (uint8_t)(gen3_pokerus_days(p) & 0x0Fu) : 0xFFu;

    /* Keep the flags this port does not model - captured, trade block and the
     * two unused bits - and rewrite only the three it does. */
    uint8_t flags = dst[0x1D];
    flags &= (uint8_t)~0xE0u;
    if (!p->checksum_ok) flags |= 0x20u;
    if (p->ability_bit)  flags |= 0x40u;
    if (p->is_egg)       flags |= 0x80u;
    dst[0x1D] = flags;

    wr32be(dst + 0x20, p->experience);
    wr16be(dst + 0x24, p->sid);
    wr16be(dst + 0x26, p->tid);
    wr32be(dst + 0x28, p->pid);
    dst[0x33] = p->fateful ? 1u : 0u;

    uint16_t rib = 0;
    for (unsigned i = 0; i < 12u; ++i)
        if (p->ribbons & (1u << (15u + i))) rib |= (uint16_t)(1u << (15u - i));
    wr16be(dst + 0x7C, rib);

    for (unsigned i = 0; i < 4u; ++i) {
        wr16be(dst + 0x80u + i * 4u, p->moves[i]);
        dst[0x82u + i * 4u] = p->pp[i];
        dst[0x83u + i * 4u] = (uint8_t)((p->pp_ups >> (i * 2u)) & 3u);
    }
    for (unsigned i = 0; i < 6u; ++i) {
        const unsigned slot = xk3_stat_order[i];
        wr16be(dst + XK3_EV(slot), p->evs[i]);
        dst[XK3_IV(slot)] = (uint8_t)(p->ivs[i] & 31u);
    }
    for (unsigned i = 0; i < 5u; ++i) dst[0xAEu + i] = p->contest[i];
    for (unsigned i = 0; i < 5u; ++i) dst[0xB3u + i] = gen3_contest_ribbon(p, i);
    /* Shadow ID, the nickname and OT name, and every field this port does not
     * model are left exactly as they were. */
    return true;
}

static bool open_colosseum(Gen3AnySave *s, const uint8_t *data, size_t size) {
    if (size != GEN3_COLO_SIZE) return false;
    /* The first byte of the game name in the file header; 0x83 begins the
     * Japanese title. It decides which of the two fateful-encounter bytes a
     * record uses. */
    s->japanese = data[0] == 0x83u;
    int slot = pick_latest_slot(data, size, COLO_SLOT_START, COLO_SLOT_SIZE, COLO_SLOT_COUNT);
    if (slot < 0) return false;
    size_t off = COLO_SLOT_START + (size_t)slot * COLO_SLOT_SIZE;
    s->work = (uint8_t *)malloc(COLO_SLOT_SIZE);
    if (!s->work) return false;
    memcpy(s->work, data + off, COLO_SLOT_SIZE);
    s->work_size = COLO_SLOT_SIZE;
    s->kind = GEN3_KIND_COLOSSEUM;
    s->active_slot = slot;
    s->save_counter = rd32be(data + off + 4);
    colo_decrypt(s->work, s->work_size);

    if (!range_ok(COLO_BOX, COLO_BOX_STRIDE * 3u, s->work_size)) return false;
    s->has_trainer = true;
    decode_gc_text(s->work + 0x78, 20, s->trainer_name, sizeof(s->trainer_name));
    s->sid = rd16be(s->work + 0xA4);
    s->tid = rd16be(s->work + 0xA6);
    s->trainer_gender = s->work[0xAF8];
    s->money = rd32be(s->work + 0xAFC);
    s->played_seconds = be_float_seconds(s->work + 0x28);
    s->party_offset = COLO_PARTY;
    s->box_offset = COLO_BOX;
    s->box_count = 3;
    s->current_box = 0;
    s->integrity_ok = colo_checksums_valid(s->work, s->work_size);
    s->integrity_failures = s->integrity_ok ? 0u : 1u;

    s->party_count = 0;
    for (unsigned i = 0; i < 6; ++i) {
        Gen3Pokemon p;
        size_t po = COLO_PARTY + (size_t)i * COLO_PK_SIZE;
        if (range_ok(po, COLO_PK_SIZE, s->work_size) && parse_ck3(s->work + po, COLO_PK_SIZE, s->japanese, &p) && p.present)
            ++s->party_count;
    }
    return true;
}

static bool open_xd(Gen3AnySave *s, const uint8_t *data, size_t size) {
    if (size != GEN3_XD_SIZE) return false;
    int slot = pick_latest_slot(data, size, XD_SLOT_START, XD_SLOT_SIZE, XD_SLOT_COUNT);
    if (slot < 0) return false;
    size_t off = XD_SLOT_START + (size_t)slot * XD_SLOT_SIZE;
    s->work = (uint8_t *)malloc(XD_SLOT_SIZE);
    if (!s->work) return false;
    memcpy(s->work, data + off, XD_SLOT_SIZE);
    s->work_size = XD_SLOT_SIZE;
    s->kind = GEN3_KIND_XD;
    s->active_slot = slot;
    s->save_counter = rd32be(data + off + 4);
    xd_decrypt(s->work, s->work_size);

    for (unsigned i = 0; i < 16; ++i) {
        s->xd_sub_lengths[i] = rd16be(s->work + 0x20u + i * 2u);
        uint32_t lo = rd16be(s->work + 0x40u + i * 4u);
        uint32_t hi = rd16be(s->work + 0x42u + i * 4u);
        s->xd_sub_offsets[i] = lo | (hi << 16);
    }
    s->japanese = s->xd_sub_lengths[7] == 0x1E00u;
    size_t config = (size_t)s->xd_sub_offsets[0] + XD_DISPLACEMENT;
    size_t trainer = (size_t)s->xd_sub_offsets[1] + XD_DISPLACEMENT;
    size_t boxes = (size_t)s->xd_sub_offsets[2] + XD_DISPLACEMENT;
    if (!range_ok(config, s->japanese ? 0x24u : 0x38u, s->work_size) ||
        !range_ok(trainer, 0x8E8u, s->work_size) ||
        !range_ok(boxes, XD_BOX_STRIDE * 8u, s->work_size))
        return false;

    s->has_trainer = true;
    decode_gc_text(s->work + trainer, 20, s->trainer_name, sizeof(s->trainer_name));
    s->sid = rd16be(s->work + trainer + 0x2C);
    s->tid = rd16be(s->work + trainer + 0x2E);
    s->trainer_gender = s->work[trainer + 0x8E0];
    s->money = rd32be(s->work + trainer + 0x8E4);
    s->played_seconds = s->japanese ? be_float_seconds(s->work + config + 0x20)
                                    : be_double_seconds(s->work + config + 0x30);
    s->party_offset = trainer + 0x30;
    s->box_offset = boxes;
    s->box_count = 8;
    s->current_box = 0;
    s->integrity_ok = xd_checksums_valid(s->work, s->work_size, s->xd_sub_offsets[0]);
    if (!s->integrity_ok) s->integrity_failures = 1;

    s->party_count = 0;
    for (unsigned i = 0; i < 6; ++i) {
        Gen3Pokemon p;
        size_t po = s->party_offset + (size_t)i * XD_PK_SIZE;
        if (range_ok(po, XD_PK_SIZE, s->work_size) && parse_xk3(s->work + po, XD_PK_SIZE, &p) && p.present)
            ++s->party_count;
    }
    return true;
}

static uint16_t rsbox_checksum(const uint8_t *block) {
    uint32_t sum = 0;
    sum += rd16be(block + 0x4);
    sum += rd16be(block + 0x6);
    sum += rd16be(block + 0x8);
    sum += rd16be(block + 0xA);
    for (size_t i = 0xC; i < 0x1FFC; i += 2)
        sum += rd16be(block + i);
    return (uint16_t)sum;
}

static bool rsbox_half_stats(const uint8_t *data, size_t size, unsigned half,
                             uint32_t *max_count, unsigned *bad, bool *all_ids) {
    size_t base = RSBOX_BLOCK_START + (size_t)half * RSBOX_SLOT_SIZE;
    if (!range_ok(base, RSBOX_SLOT_SIZE, size)) return false;
    bool seen[RSBOX_BLOCK_COUNT] = {0};
    *max_count = 0;
    *bad = 0;
    for (unsigned i = 0; i < RSBOX_BLOCK_COUNT; ++i) {
        const uint8_t *b = data + base + (size_t)i * RSBOX_BLOCK_SIZE;
        uint32_t id = rd32be(b + 4);
        uint32_t count = rd32be(b + 8);
        if (id < RSBOX_BLOCK_COUNT) seen[id] = true;
        else ++*bad;
        if (count > *max_count) *max_count = count;
        uint16_t a = rd16be(b + 0);
        uint16_t bb = rd16be(b + 2);
        uint16_t calc = rsbox_checksum(b);
        if (a != calc || bb != (uint16_t)(0xF004u - calc)) ++*bad;
    }
    *all_ids = true;
    for (unsigned i = 0; i < RSBOX_BLOCK_COUNT; ++i)
        if (!seen[i]) { *all_ids = false; break; }
    return true;
}

/*
 * Pokemon Box lays its storage out as 12x5 grids, not the 6x5 the games use,
 * so two consecutive PKHeX boxes share one physical grid: the even box is its
 * left half and the odd box its right. Each slot also carries four extra
 * bytes naming the trainer who deposited it, after the 80-byte record.
 */
static size_t rsbox_slot_offset(unsigned box, unsigned slot) {
    const unsigned row = slot / 6u;
    unsigned col = slot % 6u;
    if (box & 1u) col += 6u;
    const unsigned grid_slot = row * 12u + col;
    return 8u + (size_t)RSBOX_PK_SIZE * (box & ~1u) * 30u + (size_t)grid_slot * RSBOX_PK_SIZE;
}

/* Wallpapers sit immediately after the 25 box names, one per physical grid. */
static size_t rsbox_wallpaper_offset(unsigned box) { return 0x1ED19u + box / 2u; }
static size_t rsbox_name_offset(unsigned box) { return 0x1EC38u + (size_t)(box / 2u) * 9u; }

static bool open_rsbox(Gen3AnySave *s, const uint8_t *data, size_t size) {
    if (size != GEN3_RSBOX_SIZE) return false;
    uint32_t c0, c1; unsigned b0, b1; bool ids0, ids1;
    if (!rsbox_half_stats(data, size, 0, &c0, &b0, &ids0) ||
        !rsbox_half_stats(data, size, 1, &c1, &b1, &ids1)) return false;

    /* Prefer a structurally complete half; otherwise follow latest counter. */
    unsigned half;
    if (ids0 && !ids1) half = 0;
    else if (ids1 && !ids0) half = 1;
    else half = c1 > c0 ? 1u : 0u;

    s->work_size = RSBOX_BLOCK_COUNT * RSBOX_COPY_SIZE;
    s->work = (uint8_t *)calloc(1, s->work_size);
    if (!s->work) return false;
    size_t base = RSBOX_BLOCK_START + (size_t)half * RSBOX_SLOT_SIZE;
    bool seen[RSBOX_BLOCK_COUNT] = {0};
    for (unsigned i = 0; i < RSBOX_BLOCK_COUNT; ++i) {
        const uint8_t *b = data + base + (size_t)i * RSBOX_BLOCK_SIZE;
        uint32_t id = rd32be(b + 4);
        if (id >= RSBOX_BLOCK_COUNT || seen[id]) continue;
        seen[id] = true;
        memcpy(s->work + (size_t)id * RSBOX_COPY_SIZE, b + 0xC, RSBOX_COPY_SIZE);
    }

    s->kind = GEN3_KIND_RSBOX;
    s->active_slot = (int)half;
    s->save_counter = half ? c1 : c0;
    s->japanese = data[0] == 0x83u;
    s->has_trainer = false;
    s->box_count = RSBOX_BOXES;
    s->current_box = s->work[4] * 2u;
    if (s->current_box >= s->box_count) s->current_box = 0;
    s->integrity_failures = half ? b1 : b0;
    s->integrity_ok = s->integrity_failures == 0 && (half ? ids1 : ids0);
    return true;
}

void gen3_any_close(Gen3AnySave *s) {
    if (!s) return;
    free(s->work);
    memset(s, 0, sizeof(*s));
    s->active_slot = -1;
}

static bool try_open_exact(Gen3AnySave *s, const uint8_t *data, size_t size) {
    if (size == GEN3_COLO_SIZE && open_colosseum(s, data, size)) return true;
    if (size == GEN3_XD_SIZE && open_xd(s, data, size)) return true;
    if (size == GEN3_RSBOX_SIZE && open_rsbox(s, data, size)) return true;

    if (size >= GEN3_RAW_SIZE && gen3_open(&s->gba, data, size)) {
        s->kind = GEN3_KIND_GBA;
        s->active_slot = s->gba.active_slot;
        s->japanese = s->gba.japanese;
        s->has_trainer = true;
        gen3_trainer_name(&s->gba, s->trainer_name, sizeof(s->trainer_name));
        s->trainer_gender = gen3_trainer_gender(&s->gba);
        s->tid = gen3_tid(&s->gba);
        s->sid = gen3_sid(&s->gba);
        s->money = gen3_money(&s->gba);
        s->played_seconds = (uint64_t)gen3_played_hours(&s->gba) * 3600u +
                            (uint64_t)gen3_played_minutes(&s->gba) * 60u +
                            gen3_played_seconds(&s->gba);
        s->party_count = gen3_party_count(&s->gba);
        s->box_count = GEN3_BOX_COUNT;
        s->current_box = gen3_current_box(&s->gba);
        s->integrity_ok = s->gba.slots[s->gba.active_slot].checksums_ok;
        s->integrity_failures = s->gba.slots[s->gba.active_slot].checksum_failures;
        return true;
    }
    return false;
}

bool gen3_any_open(Gen3AnySave *s, const uint8_t *data, size_t size) {
    if (!s || !data) return false;
    memset(s, 0, sizeof(*s));
    s->active_slot = -1;
    s->input = data;
    s->input_size = size;

    /* Standalone .gci files have a 64-byte directory header. */
    if ((size == GEN3_COLO_SIZE + GEN3_GCI_HEADER ||
         size == GEN3_XD_SIZE + GEN3_GCI_HEADER ||
         size == GEN3_RSBOX_SIZE + GEN3_GCI_HEADER) &&
        try_open_exact(s, data + GEN3_GCI_HEADER, size - GEN3_GCI_HEADER)) {
        s->data_offset = GEN3_GCI_HEADER;
        s->input = data;
        s->input_size = size;
        return true;
    }
    if (try_open_exact(s, data, size)) {
        s->data_offset = 0;
        s->input = data;
        s->input_size = size;
        return true;
    }
    gen3_any_close(s);
    return false;
}

const char *gen3_any_kind_name(Gen3SaveKind kind) {
    switch (kind) {
        case GEN3_KIND_GBA: return "GBA Gen III";
        case GEN3_KIND_COLOSSEUM: return "GameCube Colosseum";
        case GEN3_KIND_XD: return "GameCube XD";
        case GEN3_KIND_RSBOX: return "GameCube Pokemon Box";
        default: return "Unknown";
    }
}

const char *gen3_any_game_name(const Gen3AnySave *s) {
    if (!s) return "Unknown";
    switch (s->kind) {
        case GEN3_KIND_GBA: return gen3_game_name(s->gba.game);
        case GEN3_KIND_COLOSSEUM: return "Pokemon Colosseum";
        case GEN3_KIND_XD: return "Pokemon XD: Gale of Darkness";
        case GEN3_KIND_RSBOX: return "Pokemon Box: Ruby & Sapphire";
        default: return "Unknown";
    }
}

/*
 * An XD record says which Shadow it is, not whether it still is one. That
 * answer is in the table, so fill it in once the record has been parsed; the
 * heart gauge comes from there too.
 */
static void xd_apply_shadow_state(const Gen3AnySave *s, Gen3Pokemon *p) {
    if (!p || !p->present || !p->shadow_id) return;
    Gen3ShadowEntry entry;
    if (!gen3_any_shadow_entry(s, p->shadow_id, &entry)) return;
    p->is_shadow = !entry.purified;
    p->purification = entry.purification;
}

unsigned gen3_any_party_count(const Gen3AnySave *s) {
    return s ? s->party_count : 0;
}

bool gen3_any_party_pokemon(const Gen3AnySave *s, unsigned slot, Gen3Pokemon *out) {
    if (!s || !out || slot >= 6) return false;
    switch (s->kind) {
        case GEN3_KIND_GBA:
            return gen3_party_pokemon(&s->gba, slot, out);
        case GEN3_KIND_COLOSSEUM: {
            size_t off = s->party_offset + (size_t)slot * COLO_PK_SIZE;
            return range_ok(off, COLO_PK_SIZE, s->work_size) && parse_ck3(s->work + off, COLO_PK_SIZE, s->japanese, out);
        }
        case GEN3_KIND_XD: {
            size_t off = s->party_offset + (size_t)slot * XD_PK_SIZE;
            if (!range_ok(off, XD_PK_SIZE, s->work_size) || !parse_xk3(s->work + off, XD_PK_SIZE, out))
                return false;
            xd_apply_shadow_state(s, out);
            return true;
        }
        default:
            memset(out, 0, sizeof(*out));
            return false;
    }
}

unsigned gen3_any_box_count(const Gen3AnySave *s) { return s ? s->box_count : 0; }
unsigned gen3_any_current_box(const Gen3AnySave *s) { return s ? s->current_box : 0; }

void gen3_any_box_name(const Gen3AnySave *s, unsigned box, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!s || box >= s->box_count) return;
    switch (s->kind) {
        case GEN3_KIND_GBA:
            gen3_box_name(&s->gba, box, out, out_size);
            break;
        case GEN3_KIND_COLOSSEUM:
            decode_gc_text(s->work + COLO_BOX + (size_t)box * COLO_BOX_STRIDE, 16, out, out_size);
            break;
        case GEN3_KIND_XD:
            decode_gc_text(s->work + s->box_offset + (size_t)box * XD_BOX_STRIDE, 16, out, out_size);
            break;
        case GEN3_KIND_RSBOX: {
            const unsigned pair = box / 2u;
            char name[24];
            size_t off = rsbox_name_offset(box);
            if (range_ok(off, 9, s->work_size))
                gen3_decode_text(s->work + off, 9, s->japanese, name, sizeof(name));
            else
                snprintf(name, sizeof(name), "Pair %u", pair + 1u);
            snprintf(out, out_size, "%c %s", (box & 1u) ? 'R' : 'L', name);
            break;
        }
        default:
            break;
    }
    if (out[0] == '\0') snprintf(out, out_size, "Box %u", box + 1u);
}

uint8_t gen3_any_box_wallpaper(const Gen3AnySave *s, unsigned box) {
    if (!s || box >= s->box_count) return 0xFFu;
    if (s->kind == GEN3_KIND_GBA) return gen3_box_wallpaper(&s->gba, box);
    if (s->kind == GEN3_KIND_RSBOX) {
        const size_t off = rsbox_wallpaper_offset(box);
        if (!range_ok(off, 1, s->work_size)) return 0xFFu;
        const uint8_t raw = s->work[off];
        return raw < GEN3_WALLPAPER_COUNT ? raw : (uint8_t)((box / 2u) % GEN3_WALLPAPER_COUNT);
    }
    return 0xFFu;
}

bool gen3_any_set_box_wallpaper(Gen3AnySave *s, unsigned box, uint8_t wallpaper) {
    if (!s || box >= s->box_count || wallpaper >= GEN3_WALLPAPER_COUNT) return false;
    if (s->kind == GEN3_KIND_GBA) return gen3_set_box_wallpaper(&s->gba, box, wallpaper);
    if (s->kind == GEN3_KIND_RSBOX) {
        const size_t off = rsbox_wallpaper_offset(box);
        if (!range_ok(off, 1, s->work_size)) return false;
        /* The two halves of a grid share one wallpaper, as they do in game. */
        s->work[off] = wallpaper;
        return true;
    }
    return false;
}

bool gen3_any_has_pokedex(const Gen3AnySave *s) {
    return s && s->kind == GEN3_KIND_GBA;
}

bool gen3_any_dex_seen(const Gen3AnySave *s, unsigned national) {
    return gen3_any_has_pokedex(s) && gen3_dex_seen(&s->gba, national);
}

bool gen3_any_dex_caught(const Gen3AnySave *s, unsigned national) {
    return gen3_any_has_pokedex(s) && gen3_dex_caught(&s->gba, national);
}

void gen3_any_set_dex_seen(Gen3AnySave *s, unsigned national, bool seen) {
    if (gen3_any_has_pokedex(s)) gen3_set_dex_seen(&s->gba, national, seen);
}

void gen3_any_set_dex_caught(Gen3AnySave *s, unsigned national, bool caught) {
    if (gen3_any_has_pokedex(s)) gen3_set_dex_caught(&s->gba, national, caught);
}

unsigned gen3_any_dex_seen_count(const Gen3AnySave *s) {
    return gen3_any_has_pokedex(s) ? gen3_dex_seen_count(&s->gba) : 0u;
}

unsigned gen3_any_dex_caught_count(const Gen3AnySave *s) {
    return gen3_any_has_pokedex(s) ? gen3_dex_caught_count(&s->gba) : 0u;
}

bool gen3_any_national_dex(const Gen3AnySave *s) {
    return gen3_any_has_pokedex(s) && gen3_national_dex(&s->gba);
}

void gen3_any_set_national_dex(Gen3AnySave *s, bool unlocked) {
    if (gen3_any_has_pokedex(s)) gen3_set_national_dex(&s->gba, unlocked);
}


/* ------------------------------------------------- strategy memo --- */

/*
 * PKHeX's StrategyMemo: a big-endian count, two bytes it does not name, and
 * then twelve-byte entries. The stored species is the low nine bits of the
 * first halfword, in the games' internal numbering; the two flags above it
 * mean different things in the two games, which is why "seen" is asked of the
 * save rather than read straight off the entry.
 */
#define MEMO_COLOSSEUM_OFFSET 0x082B0u
#define XD_MEMO_SUB 5u

static size_t memo_base(const Gen3AnySave *s, size_t *length) {
    if (!s || !s->work) return 0;
    if (s->kind == GEN3_KIND_COLOSSEUM) {
        if (length) *length = s->work_size - MEMO_COLOSSEUM_OFFSET;
        return MEMO_COLOSSEUM_OFFSET;
    }
    if (s->kind == GEN3_KIND_XD) {
        const size_t at = (size_t)s->xd_sub_offsets[XD_MEMO_SUB] + XD_DISPLACEMENT;
        if (length) *length = s->xd_sub_lengths[XD_MEMO_SUB];
        return at;
    }
    return 0;
}

bool gen3_any_has_memo(const Gen3AnySave *s) {
    size_t length = 0;
    const size_t at = memo_base(s, &length);
    return at != 0 && length >= 4u && range_ok(at, 4u, s->work_size);
}

unsigned gen3_any_memo_count(const Gen3AnySave *s) {
    size_t length = 0;
    const size_t at = memo_base(s, &length);
    if (!gen3_any_has_memo(s)) return 0;
    unsigned count = rd16be(s->work + at);
    if (count > GEN3_MEMO_MAX_ENTRIES) count = GEN3_MEMO_MAX_ENTRIES;
    /* Trust the block's own size over the count it declares. */
    const unsigned fits = (unsigned)((length - 4u) / GEN3_MEMO_ENTRY_SIZE);
    if (count > fits) count = fits;
    while (count && !range_ok(at + 4u + (size_t)(count - 1u) * GEN3_MEMO_ENTRY_SIZE,
                              GEN3_MEMO_ENTRY_SIZE, s->work_size))
        --count;
    return count;
}

bool gen3_any_memo_entry(const Gen3AnySave *s, unsigned index, Gen3MemoEntry *out) {
    if (!out || index >= gen3_any_memo_count(s)) return false;
    const size_t at = memo_base(s, NULL) + 4u + (size_t)index * GEN3_MEMO_ENTRY_SIZE;
    const uint8_t *e = s->work + at;

    memset(out, 0, sizeof(*out));
    const uint16_t head = rd16be(e);
    out->species_internal = (uint16_t)(head & 0x1FFu);
    out->species = (uint16_t)gen3_species_national(out->species_internal);
    out->sid = rd16be(e + 4);
    out->tid = rd16be(e + 6);
    out->pid = rd32be(e + 8);

    const bool flag0 = (e[0] & 0x40u) != 0u;   /* PKHeX marks this unused */
    const bool flag1 = (e[0] & 0x80u) != 0u;   /* "complete entry" */
    if (s->kind == GEN3_KIND_XD) {
        out->seen = !flag1;
        out->owned = false;                     /* XD does not record it */
    } else {
        out->seen = out->species_internal != 0u;
        out->owned = flag0 || !flag1;
    }
    return true;
}

bool gen3_any_set_memo_seen(Gen3AnySave *s, unsigned index, bool seen) {
    if (index >= gen3_any_memo_count(s)) return false;
    const size_t at = memo_base(s, NULL) + 4u + (size_t)index * GEN3_MEMO_ENTRY_SIZE;
    uint8_t *e = s->work + at;
    if (s->kind == GEN3_KIND_XD) {
        /* The flag means "not seen", so it is set when seen is cleared. */
        if (seen) e[0] = (uint8_t)(e[0] & 0x7Fu);
        else e[0] = (uint8_t)(e[0] | 0x80u);
        return true;
    }
    /* Colosseum has no seen flag: an entry is seen while it holds a species,
     * so clearing it means clearing the whole entry, as PKHeX does. */
    if (!seen) memset(e, 0, GEN3_MEMO_ENTRY_SIZE);
    return !seen;
}

unsigned gen3_any_event_flag_count(const Gen3AnySave *s) {
    return gen3_any_has_pokedex(s) ? gen3_event_flag_count(&s->gba) : 0u;
}

unsigned gen3_any_event_work_count(const Gen3AnySave *s) {
    return gen3_any_has_pokedex(s) ? gen3_event_work_count(&s->gba) : 0u;
}

bool gen3_any_event_flag(const Gen3AnySave *s, unsigned flag) {
    return gen3_any_has_pokedex(s) && gen3_event_flag(&s->gba, flag);
}

void gen3_any_set_event_flag(Gen3AnySave *s, unsigned flag, bool value) {
    if (gen3_any_has_pokedex(s)) gen3_set_event_flag(&s->gba, flag, value);
}

uint16_t gen3_any_event_work(const Gen3AnySave *s, unsigned index) {
    return gen3_any_has_pokedex(s) ? gen3_event_work(&s->gba, index) : 0u;
}

void gen3_any_set_event_work(Gen3AnySave *s, unsigned index, uint16_t value) {
    if (gen3_any_has_pokedex(s)) gen3_set_event_work(&s->gba, index, value);
}

bool gen3_any_badge(const Gen3AnySave *s, unsigned badge) {
    return gen3_any_has_pokedex(s) && gen3_badge(&s->gba, badge);
}

void gen3_any_set_badge(Gen3AnySave *s, unsigned badge, bool earned) {
    if (gen3_any_has_pokedex(s)) gen3_set_badge(&s->gba, badge, earned);
}

unsigned gen3_any_badge_count(const Gen3AnySave *s) {
    return gen3_any_has_pokedex(s) ? gen3_badge_count(&s->gba) : 0u;
}

bool gen3_any_box_pokemon(const Gen3AnySave *s, unsigned box, unsigned slot, Gen3Pokemon *out) {
    if (!s || !out || box >= s->box_count || slot >= 30) return false;
    switch (s->kind) {
        case GEN3_KIND_GBA:
            return gen3_box_pokemon(&s->gba, box, slot, out);
        case GEN3_KIND_COLOSSEUM: {
            size_t off = COLO_BOX + (size_t)box * COLO_BOX_STRIDE + 0x14u + (size_t)slot * COLO_PK_SIZE;
            return range_ok(off, COLO_PK_SIZE, s->work_size) && parse_ck3(s->work + off, COLO_PK_SIZE, s->japanese, out);
        }
        case GEN3_KIND_XD: {
            size_t off = s->box_offset + (size_t)box * XD_BOX_STRIDE + 0x14u + (size_t)slot * XD_PK_SIZE;
            if (!range_ok(off, XD_PK_SIZE, s->work_size) || !parse_xk3(s->work + off, XD_PK_SIZE, out))
                return false;
            xd_apply_shadow_state(s, out);
            return true;
        }
        case GEN3_KIND_RSBOX: {
            const size_t off = rsbox_slot_offset(box, slot);
            if (!range_ok(off, RSBOX_PK_SIZE, s->work_size)) return false;
            return gen3_parse_pk3_record(s->work + off, GEN3_PK3_STORED_SIZE, s->japanese, out);
        }
        default:
            return false;
    }
}

bool gen3_any_can_edit(const Gen3AnySave *s) {
    return s && s->kind != GEN3_KIND_UNKNOWN;
}

bool gen3_any_has_integrity_check(Gen3SaveKind kind) {
    return kind == GEN3_KIND_COLOSSEUM || kind == GEN3_KIND_XD || kind == GEN3_KIND_RSBOX;
}

bool gen3_any_has_shadow(Gen3SaveKind kind) {
    return kind == GEN3_KIND_COLOSSEUM || kind == GEN3_KIND_XD;
}

/* Where each writable field sits in XD's trainer substructure. */
static bool xd_trainer(const Gen3AnySave *s, size_t *out) {
    if (!s || s->kind != GEN3_KIND_XD || !s->work) return false;
    const size_t trainer = (size_t)s->xd_sub_offsets[1] + XD_DISPLACEMENT;
    if (!range_ok(trainer, 0x8E8u, s->work_size)) return false;
    *out = trainer;
    return true;
}

bool gen3_any_set_tid(Gen3AnySave *s, uint16_t tid) {
    if (!s) return false;
    if (s->kind == GEN3_KIND_GBA) { gen3_set_tid(&s->gba, tid); s->tid = tid; return true; }
    if (s->kind == GEN3_KIND_COLOSSEUM && s->work && s->work_size > 0xA7u) {
        wr16be(s->work + 0xA6, tid); s->tid = tid; return true;
    }
    size_t trainer;
    if (xd_trainer(s, &trainer)) { wr16be(s->work + trainer + 0x2E, tid); s->tid = tid; return true; }
    return false;
}

bool gen3_any_set_sid(Gen3AnySave *s, uint16_t sid) {
    if (!s) return false;
    if (s->kind == GEN3_KIND_GBA) { gen3_set_sid(&s->gba, sid); s->sid = sid; return true; }
    if (s->kind == GEN3_KIND_COLOSSEUM && s->work && s->work_size > 0xA5u) {
        wr16be(s->work + 0xA4, sid); s->sid = sid; return true;
    }
    size_t trainer;
    if (xd_trainer(s, &trainer)) { wr16be(s->work + trainer + 0x2C, sid); s->sid = sid; return true; }
    return false;
}

bool gen3_any_set_trainer_gender(Gen3AnySave *s, uint8_t gender) {
    if (!s) return false;
    gender = gender ? 1u : 0u;
    if (s->kind == GEN3_KIND_GBA) { gen3_set_trainer_gender(&s->gba, gender); s->trainer_gender = gender; return true; }
    if (s->kind == GEN3_KIND_COLOSSEUM && s->work && s->work_size > 0xAF8u) {
        s->work[0xAF8] = gender; s->trainer_gender = gender; return true;
    }
    size_t trainer;
    if (xd_trainer(s, &trainer)) { s->work[trainer + 0x8E0] = gender; s->trainer_gender = gender; return true; }
    return false;
}

bool gen3_any_set_money(Gen3AnySave *s, uint32_t money) {
    if (!s) return false;
    if (s->kind == GEN3_KIND_GBA) { gen3_set_money(&s->gba, money); s->money = money; return true; }
    if (s->kind == GEN3_KIND_COLOSSEUM && s->work && s->work_size > 0xAFFu) {
        wr32be(s->work + 0xAFC, money); s->money = money; return true;
    }
    size_t trainer;
    if (xd_trainer(s, &trainer)) { wr32be(s->work + trainer + 0x8E4, money); s->money = money; return true; }
    return false;
}

bool gen3_any_set_played_seconds(Gen3AnySave *s, uint64_t seconds) {
    if (!s) return false;
    if (s->kind == GEN3_KIND_GBA) {
        uint64_t maxsec = (uint64_t)65535u * 3600u + 59u * 60u + 59u;
        if (seconds > maxsec) seconds = maxsec;
        gen3_set_playtime(&s->gba, (uint16_t)(seconds / 3600u), (uint8_t)((seconds / 60u) % 60u), (uint8_t)(seconds % 60u));
        s->played_seconds = seconds; return true;
    }
    if (s->kind == GEN3_KIND_COLOSSEUM && s->work && s->work_size > 0x2Bu) {
        union { float f; uint32_t u; } v; v.f = (float)seconds; wr32be(s->work + 0x28, v.u); s->played_seconds = seconds; return true;
    }
    return false;
}

/* ----------------------------------------------------------- gc names --- */

bool gen3_any_name_is_utf16(const Gen3AnySave *s) {
    return s && (s->kind == GEN3_KIND_COLOSSEUM || s->kind == GEN3_KIND_XD);
}

/*
 * Writes ASCII as UTF-16 big-endian into a field of the given width in code
 * units, zero-terminating and clearing the rest. The keyboard only produces
 * printable ASCII, each of which is its own code unit.
 */
static void encode_gc_text(uint8_t *dst, size_t units, const char *src) {
    size_t w = 0;
    for (const char *c = src; c && *c && w < units; ++c) {
        if ((unsigned char)*c < 0x20u || (unsigned char)*c > 0x7Eu) continue;
        wr16be(dst + w * 2u, (uint16_t)(unsigned char)*c);
        ++w;
    }
    for (; w < units; ++w) wr16be(dst + w * 2u, 0);
}

unsigned gen3_any_trainer_name_length(const Gen3AnySave *s) {
    if (!s) return 0;
    if (s->kind == GEN3_KIND_GBA) return (unsigned)gen3_trainer_name_length(&s->gba);
    /* Both GameCube games keep ten code units for the trainer's name. */
    if (gen3_any_name_is_utf16(s)) return 10u;
    return 0;
}

bool gen3_any_set_trainer_name_ascii(Gen3AnySave *s, const char *name) {
    if (!s || !name) return false;
    if (s->kind == GEN3_KIND_GBA) {
        if (!gen3_set_trainer_name_ascii(&s->gba, name)) return false;
        gen3_trainer_name(&s->gba, s->trainer_name, sizeof(s->trainer_name));
        return true;
    }
    size_t at;
    if (s->kind == GEN3_KIND_COLOSSEUM) {
        at = 0x78u;
    } else if (s->kind == GEN3_KIND_XD) {
        size_t trainer;
        if (!xd_trainer(s, &trainer)) return false;
        at = trainer;
    } else {
        return false;
    }
    if (!range_ok(at, 20u, s->work_size)) return false;
    encode_gc_text(s->work + at, 10u, name);
    decode_gc_text(s->work + at, 20, s->trainer_name, sizeof(s->trainer_name));
    return true;
}

unsigned gen3_any_box_name_length(const Gen3AnySave *s) {
    if (!s) return 0;
    if (s->kind == GEN3_KIND_GBA) return GEN3_BOX_NAME_LEN - 1u;
    /* Colosseum and XD keep eight code units per box name. */
    if (gen3_any_name_is_utf16(s)) return 8u;
    /* Pokemon Box stores its names in Generation III text, like a cartridge. */
    if (s->kind == GEN3_KIND_RSBOX) return GEN3_BOX_NAME_LEN - 1u;
    return 0;
}

bool gen3_any_set_box_name_ascii(Gen3AnySave *s, unsigned box, const char *name) {
    if (!s || !name || box >= s->box_count) return false;
    if (s->kind == GEN3_KIND_GBA) {
        uint8_t raw[GEN3_BOX_NAME_LEN];
        gen3_encode_text(name, raw, sizeof(raw));
        return gen3_set_box_name(&s->gba, box, raw, sizeof(raw));
    }
    if (s->kind == GEN3_KIND_RSBOX) {
        uint8_t raw[GEN3_BOX_NAME_LEN];
        gen3_encode_text(name, raw, sizeof(raw));
        /* Two consecutive boxes share one stored name, as they share a grid. */
        const size_t ofs = rsbox_name_offset(box);
        if (!range_ok(ofs, GEN3_BOX_NAME_LEN, s->work_size)) return false;
        memcpy(s->work + ofs, raw, GEN3_BOX_NAME_LEN);
        return true;
    }
    size_t at;
    if (s->kind == GEN3_KIND_COLOSSEUM) at = COLO_BOX + (size_t)box * COLO_BOX_STRIDE;
    else if (s->kind == GEN3_KIND_XD) at = s->box_offset + (size_t)box * XD_BOX_STRIDE;
    else return false;
    if (!range_ok(at, 16u, s->work_size)) return false;
    encode_gc_text(s->work + at, 8u, name);
    return true;
}

bool gen3_any_set_party_pokemon(Gen3AnySave *s, unsigned slot, const Gen3Pokemon *p) {
    if (!s || !p || slot >= 6u) return false;
    if (s->kind == GEN3_KIND_GBA) return gen3_set_party_pokemon(&s->gba, slot, p);
    if (s->kind == GEN3_KIND_COLOSSEUM) {
        size_t off = s->party_offset + (size_t)slot * COLO_PK_SIZE;
        return range_ok(off, COLO_PK_SIZE, s->work_size) && write_ck3(s->work + off, COLO_PK_SIZE, s->japanese, p);
    }
    if (s->kind == GEN3_KIND_XD) {
        size_t off = s->party_offset + (size_t)slot * XD_PK_SIZE;
        return range_ok(off, XD_PK_SIZE, s->work_size) && write_xk3(s->work + off, XD_PK_SIZE, p);
    }
    return false;
}

bool gen3_any_set_box_pokemon(Gen3AnySave *s, unsigned box, unsigned slot, const Gen3Pokemon *p) {
    if (!s || !p || slot >= 30u) return false;
    if (s->kind == GEN3_KIND_GBA) return gen3_set_box_pokemon(&s->gba, box, slot, p);
    if (s->kind == GEN3_KIND_COLOSSEUM && box < 3u) {
        size_t off = COLO_BOX + (size_t)box * COLO_BOX_STRIDE + 0x14u + (size_t)slot * COLO_PK_SIZE;
        return range_ok(off, COLO_PK_SIZE, s->work_size) && write_ck3(s->work + off, COLO_PK_SIZE, s->japanese, p);
    }
    if (s->kind == GEN3_KIND_XD && box < s->box_count) {
        size_t off = s->box_offset + (size_t)box * XD_BOX_STRIDE + 0x14u + (size_t)slot * XD_PK_SIZE;
        return range_ok(off, XD_PK_SIZE, s->work_size) && write_xk3(s->work + off, XD_PK_SIZE, p);
    }
    if (s->kind == GEN3_KIND_RSBOX && box < s->box_count) {
        /* Pokemon Box stores ordinary PK3 records, so the GBA writer applies;
         * the four depositor bytes after each record are left alone. */
        const size_t off = rsbox_slot_offset(box, slot);
        return range_ok(off, RSBOX_PK_SIZE, s->work_size) &&
               gen3_write_pk3_record(s->work + off, GEN3_PK3_STORED_SIZE, p);
    }
    return false;
}

/*
 * Pokemon Box keeps its storage split across 23 blocks whose IDs say where
 * each one belongs, so exporting is the reverse of the unpack on open: copy
 * each block's payload back to the block carrying its ID, then re-checksum
 * every block in the half that was edited.
 */
static bool rsbox_export(const Gen3AnySave *s, uint8_t *out, size_t out_size) {
    if (s->work_size != (size_t)RSBOX_BLOCK_COUNT * RSBOX_COPY_SIZE) return false;
    memcpy(out, s->input, s->input_size);
    const size_t base = s->data_offset + RSBOX_BLOCK_START +
                        (size_t)s->active_slot * RSBOX_SLOT_SIZE;
    if (!range_ok(base, RSBOX_SLOT_SIZE, out_size)) return false;

    /*
     * The reader picks a half by save counter, and the two can be equal in a
     * freshly copied file. Stamping the edited half one past the highest
     * counter in the image makes it unambiguously the newer save, which is
     * what the game does when it writes.
     */
    uint32_t newest = 0;
    for (unsigned half = 0; half < 2u; ++half) {
        const size_t hb = s->data_offset + RSBOX_BLOCK_START + (size_t)half * RSBOX_SLOT_SIZE;
        if (!range_ok(hb, RSBOX_SLOT_SIZE, out_size)) return false;
        for (unsigned i = 0; i < RSBOX_BLOCK_COUNT; ++i) {
            const uint32_t c = rd32be(out + hb + (size_t)i * RSBOX_BLOCK_SIZE + 8);
            if (c > newest) newest = c;
        }
    }
    if (newest == 0xFFFFFFFFu) return false;   /* nothing left to count up to */

    for (unsigned i = 0; i < RSBOX_BLOCK_COUNT; ++i) {
        uint8_t *b = out + base + (size_t)i * RSBOX_BLOCK_SIZE;
        const uint32_t id = rd32be(b + 4);
        if (id >= RSBOX_BLOCK_COUNT) return false;
        memcpy(b + 0xC, s->work + (size_t)id * RSBOX_COPY_SIZE, RSBOX_COPY_SIZE);
        wr32be(b + 8, newest + 1u);
        const uint16_t sum = rsbox_checksum(b);
        wr16be(b + 0, sum);
        wr16be(b + 2, (uint16_t)(0xF004u - sum));
    }

    Gen3AnySave verify;
    bool ok = gen3_any_open(&verify, out, out_size) &&
              verify.kind == GEN3_KIND_RSBOX && verify.integrity_ok;
    if (ok) gen3_any_close(&verify);
    return ok;
}

/*
 * Both GameCube formats export the same way: copy the original image through,
 * then re-checksum, re-encrypt and drop in the one slot that was edited. The
 * result is reopened before it is handed back, so a cipher or offset
 * regression cannot reach the card.
 */
/* ------------------------------------------------------------- pouches --- */

/*
 * Colosseum and XD lay their bags out the same way - four bytes per slot,
 * item id then quantity, both big-endian - but at different offsets and with
 * different pouches. Offsets follow PKHeX's PlayerBag3Colosseum and
 * PlayerBag3XD; see SOURCES.md.
 */
typedef struct GcPouch {
    uint16_t offset;
    uint16_t count;
    uint16_t max_quantity;
} GcPouch;

static bool gc_pouch(const Gen3AnySave *s, Gen3Pocket pocket, GcPouch *out) {
    static const GcPouch colo[GEN3_POCKET_COUNT] = {
        [GEN3_POCKET_PC]        = { 0, 0, 0 },       /* Colosseum has no PC box */
        [GEN3_POCKET_ITEMS]     = { 0x000, 20, 99 },
        [GEN3_POCKET_KEY_ITEMS] = { 0x050, 43, 1 },
        [GEN3_POCKET_BALLS]     = { 0x0FC, 16, 99 },
        [GEN3_POCKET_TMS]       = { 0x13C, 64, 99 },
        [GEN3_POCKET_BERRIES]   = { 0x23C, 46, 999 },
        [GEN3_POCKET_COLOGNE]   = { 0x2F4, 3, 99 },
        [GEN3_POCKET_DISCS]     = { 0, 0, 0 },       /* XD only */
    };
    static const GcPouch xd[GEN3_POCKET_COUNT] = {
        [GEN3_POCKET_PC]        = { 0, 0, 0 },
        [GEN3_POCKET_ITEMS]     = { 0x000, 30, 999 },
        [GEN3_POCKET_KEY_ITEMS] = { 0x078, 43, 1 },
        [GEN3_POCKET_BALLS]     = { 0x124, 16, 999 },
        [GEN3_POCKET_TMS]       = { 0x164, 64, 999 },
        [GEN3_POCKET_BERRIES]   = { 0x264, 46, 999 },
        [GEN3_POCKET_COLOGNE]   = { 0x31C, 3, 999 },
        [GEN3_POCKET_DISCS]     = { 0x328, 60, 1 },
    };
    if (!s || !out || pocket >= GEN3_POCKET_COUNT) return false;

    size_t base;
    const GcPouch *table;
    if (s->kind == GEN3_KIND_COLOSSEUM) {
        base = 0x7F8u;
        table = colo;
    } else if (s->kind == GEN3_KIND_XD) {
        size_t trainer;
        if (!xd_trainer(s, &trainer)) return false;
        base = trainer + 0x4C8u;
        table = xd;
    } else {
        return false;
    }
    if (table[pocket].count == 0) return false;

    /* The offset in the table is relative to the bag; the caller adds the
     * base, which gc_pouch_base works out the same way. */
    const size_t at = base + table[pocket].offset;
    if (!range_ok(at, (size_t)table[pocket].count * 4u, s->work_size)) return false;
    *out = table[pocket];
    return true;
}

static bool gc_pouch_base(const Gen3AnySave *s, size_t *base) {
    if (s->kind == GEN3_KIND_COLOSSEUM) { *base = 0x7F8u; return true; }
    if (s->kind == GEN3_KIND_XD) {
        size_t trainer;
        if (!xd_trainer(s, &trainer)) return false;
        *base = trainer + 0x4C8u;
        return true;
    }
    return false;
}

unsigned gen3_any_pocket_capacity(const Gen3AnySave *s, Gen3Pocket pocket) {
    if (!s) return 0;
    if (s->kind == GEN3_KIND_GBA)
        return pocket < GEN3_POCKET_COLOGNE ? gen3_pocket_capacity(&s->gba, pocket) : 0u;
    GcPouch p;
    return gc_pouch(s, pocket, &p) ? p.count : 0u;
}

uint16_t gen3_any_pocket_max_quantity(const Gen3AnySave *s, Gen3Pocket pocket) {
    if (!s) return 0;
    if (s->kind == GEN3_KIND_GBA) return pocket == GEN3_POCKET_KEY_ITEMS ? 1u : 999u;
    GcPouch p;
    return gc_pouch(s, pocket, &p) ? p.max_quantity : 0u;
}

bool gen3_any_get_item_slot(const Gen3AnySave *s, Gen3Pocket pocket, unsigned slot, Gen3ItemSlot *out) {
    if (!s || !out) return false;
    if (s->kind == GEN3_KIND_GBA) return gen3_get_item_slot(&s->gba, pocket, slot, out);
    GcPouch p; size_t base;
    if (!gc_pouch(s, pocket, &p) || slot >= p.count || !gc_pouch_base(s, &base)) return false;
    const uint8_t *entry = s->work + base + p.offset + (size_t)slot * 4u;
    out->item_id = rd16be(entry);
    out->quantity = rd16be(entry + 2);
    return true;
}

bool gen3_any_set_item_slot(Gen3AnySave *s, Gen3Pocket pocket, unsigned slot,
                            uint16_t item_id, uint16_t quantity) {
    if (!s) return false;
    if (s->kind == GEN3_KIND_GBA) return gen3_set_item_slot(&s->gba, pocket, slot, item_id, quantity);
    GcPouch p; size_t base;
    if (!gc_pouch(s, pocket, &p) || slot >= p.count || !gc_pouch_base(s, &base)) return false;
    if (quantity > p.max_quantity) quantity = p.max_quantity;
    uint8_t *entry = s->work + base + p.offset + (size_t)slot * 4u;
    wr16be(entry, item_id);
    wr16be(entry + 2, quantity);
    return true;
}

/* -------------------------------------------------------- shadow table --- */

/*
 * Substructure 7 of an XD save. Each entry is 72 bytes in the international
 * releases and 60 in the Japanese one, which is also how the save's language
 * is detected. Offsets follow PKHeX's ShadowInfoEntryXD.
 */
#define XD_SHADOW_SUB 7u

static size_t xd_shadow_entry_size(const Gen3AnySave *s) {
    return s->japanese ? 60u : 72u;
}

bool gen3_any_has_shadow_table(const Gen3AnySave *s) {
    if (!s || s->kind != GEN3_KIND_XD || !s->work) return false;
    const size_t base = (size_t)s->xd_sub_offsets[XD_SHADOW_SUB] + XD_DISPLACEMENT;
    const size_t len = s->xd_sub_lengths[XD_SHADOW_SUB];
    return len >= xd_shadow_entry_size(s) && range_ok(base, len, s->work_size);
}

unsigned gen3_any_shadow_count(const Gen3AnySave *s) {
    if (!gen3_any_has_shadow_table(s)) return 0;
    const unsigned count = (unsigned)(s->xd_sub_lengths[XD_SHADOW_SUB] / xd_shadow_entry_size(s));
    return count > GEN3_SHADOW_MAX ? GEN3_SHADOW_MAX : count;
}

static uint8_t *xd_shadow_entry(const Gen3AnySave *s, unsigned id) {
    if (id >= gen3_any_shadow_count(s)) return NULL;
    const size_t base = (size_t)s->xd_sub_offsets[XD_SHADOW_SUB] + XD_DISPLACEMENT;
    return s->work + base + (size_t)id * xd_shadow_entry_size(s);
}

bool gen3_any_shadow_entry(const Gen3AnySave *s, unsigned id, Gen3ShadowEntry *out) {
    const uint8_t *e = xd_shadow_entry(s, id);
    if (!e || !out) return false;
    memset(out, 0, sizeof(*out));
    out->snagged = (e[0] >> 6) != 0;
    out->purified = (e[0] >> 7) == 1u;
    /* The experience shares its word with twelve bits of something else. */
    out->experience = rd32be(e + 0x04) >> 12;
    /* Stored HP/Atk/Def/SpA/SpD/Spe; the rest of this port puts Speed third. */
    static const unsigned order[6] = { 0u, 1u, 2u, 5u, 3u, 4u };
    for (unsigned i = 0; i < 6u; ++i) out->ivs[i] = e[0x0B + order[i]];
    out->species = (uint16_t)gen3_species_national(rd16be(e + 0x1A));
    out->pid = rd32be(e + 0x1C);
    out->purification = (int32_t)rd32be(e + 0x24);
    out->index = s->japanese ? e[0x35] : e[0x3F];
    out->present = out->species != 0;
    return true;
}

bool gen3_any_set_shadow_purified(Gen3AnySave *s, unsigned id, bool purified) {
    uint8_t *e = xd_shadow_entry(s, id);
    if (!e) return false;
    e[0] = (uint8_t)((e[0] & 0x7Fu) | (purified ? 0x80u : 0u));
    return true;
}

bool gen3_any_set_shadow_purification(Gen3AnySave *s, unsigned id, int32_t value) {
    uint8_t *e = xd_shadow_entry(s, id);
    if (!e) return false;
    wr32be(e + 0x24, (uint32_t)value);
    return true;
}

size_t gen3_any_record_size(const Gen3AnySave *s) {
    if (!s) return 0;
    switch (s->kind) {
        case GEN3_KIND_GBA:
        case GEN3_KIND_RSBOX:      return GEN3_PK3_STORED_SIZE;
        case GEN3_KIND_COLOSSEUM:  return COLO_PK_SIZE;
        case GEN3_KIND_XD:         return XD_PK_SIZE;
        default:                   return 0;
    }
}

const char *gen3_any_record_extension(const Gen3AnySave *s) {
    if (!s) return "bin";
    switch (s->kind) {
        case GEN3_KIND_COLOSSEUM: return "ck3";
        case GEN3_KIND_XD:        return "xk3";
        default:                  return "pk3";
    }
}

/*
 * Where a box slot's record sits: which buffer, and how far in. Returning an
 * offset rather than a pointer keeps the const and non-const callers honest -
 * the GBA records live in a fixed array inside the save, so a shared pointer
 * helper would have to cast the constness away.
 */
typedef enum RecordBuffer { RECORD_NONE = 0, RECORD_GBA_STORAGE, RECORD_WORK } RecordBuffer;

static RecordBuffer box_record_where(const Gen3AnySave *s, unsigned box, unsigned slot,
                                     size_t *offset, size_t *len) {
    if (!s || box >= s->box_count || slot >= 30u) return RECORD_NONE;
    const size_t size = gen3_any_record_size(s);
    if (!size) return RECORD_NONE;
    *len = size;
    switch (s->kind) {
        case GEN3_KIND_GBA: {
            const size_t off = 4u + ((size_t)box * GEN3_BOX_SLOTS + slot) * GEN3_PK3_STORED_SIZE;
            if (off + size > sizeof s->gba.storage) return RECORD_NONE;
            *offset = off;
            return RECORD_GBA_STORAGE;
        }
        case GEN3_KIND_COLOSSEUM: {
            if (box >= 3u) return RECORD_NONE;
            const size_t off = COLO_BOX + (size_t)box * COLO_BOX_STRIDE + 0x14u + (size_t)slot * COLO_PK_SIZE;
            if (!range_ok(off, size, s->work_size)) return RECORD_NONE;
            *offset = off;
            return RECORD_WORK;
        }
        case GEN3_KIND_XD: {
            const size_t off = s->box_offset + (size_t)box * XD_BOX_STRIDE + 0x14u + (size_t)slot * XD_PK_SIZE;
            if (!range_ok(off, size, s->work_size)) return RECORD_NONE;
            *offset = off;
            return RECORD_WORK;
        }
        case GEN3_KIND_RSBOX: {
            const size_t off = rsbox_slot_offset(box, slot);
            if (!range_ok(off, size, s->work_size)) return RECORD_NONE;
            *offset = off;
            return RECORD_WORK;
        }
        default:
            return RECORD_NONE;
    }
}

bool gen3_any_box_record_raw(const Gen3AnySave *s, unsigned box, unsigned slot,
                             uint8_t *out, size_t out_size) {
    size_t off = 0, len = 0;
    const RecordBuffer buf = box_record_where(s, box, slot, &off, &len);
    if (buf == RECORD_NONE || !out || out_size < len) return false;
    const uint8_t *src = buf == RECORD_GBA_STORAGE ? s->gba.storage : s->work;
    memcpy(out, src + off, len);
    return true;
}

bool gen3_any_set_box_record_raw(Gen3AnySave *s, unsigned box, unsigned slot,
                                 const uint8_t *in, size_t in_size) {
    size_t off = 0, len = 0;
    const RecordBuffer buf = box_record_where(s, box, slot, &off, &len);
    if (buf == RECORD_NONE || !in || in_size != len) return false;

    /*
     * Parse it as this format's record before writing. A file of the right
     * length but the wrong kind would otherwise go straight into the save and
     * only show up as nonsense afterwards.
     */
    Gen3Pokemon check;
    bool ok;
    switch (s->kind) {
        case GEN3_KIND_COLOSSEUM: ok = parse_ck3(in, len, s->japanese, &check); break;
        case GEN3_KIND_XD:        ok = parse_xk3(in, len, &check); break;
        default:                  ok = gen3_parse_pk3_record(in, len, s->japanese, &check); break;
    }
    if (!ok || !check.present || !check.checksum_ok) return false;
    if (gen3_species_national(check.species_internal) == 0) return false;

    uint8_t *dst = buf == RECORD_GBA_STORAGE ? s->gba.storage : s->work;
    memcpy(dst + off, in, len);
    return true;
}

bool gen3_any_clear_box_slot(Gen3AnySave *s, unsigned box, unsigned slot) {
    if (!s || box >= s->box_count || slot >= 30u) return false;
    size_t off, len;
    switch (s->kind) {
        case GEN3_KIND_GBA:
            /* Storage is a flat run of records; the box index is folded in. */
            off = 4u + ((size_t)box * GEN3_BOX_SLOTS + slot) * GEN3_PK3_STORED_SIZE;
            if (off + GEN3_PK3_STORED_SIZE > sizeof s->gba.storage) return false;
            memset(s->gba.storage + off, 0, GEN3_PK3_STORED_SIZE);
            return true;
        case GEN3_KIND_COLOSSEUM:
            if (box >= 3u) return false;
            off = COLO_BOX + (size_t)box * COLO_BOX_STRIDE + 0x14u + (size_t)slot * COLO_PK_SIZE;
            len = COLO_PK_SIZE;
            break;
        case GEN3_KIND_XD:
            off = s->box_offset + (size_t)box * XD_BOX_STRIDE + 0x14u + (size_t)slot * XD_PK_SIZE;
            len = XD_PK_SIZE;
            break;
        case GEN3_KIND_RSBOX:
            /* Only the record itself; the four depositor bytes stay. */
            off = rsbox_slot_offset(box, slot);
            len = GEN3_PK3_STORED_SIZE;
            break;
        default:
            return false;
    }
    if (!range_ok(off, len, s->work_size)) return false;
    memset(s->work + off, 0, len);
    return true;
}

bool gen3_any_export(const Gen3AnySave *s, uint8_t *out, size_t out_size) {
    if (!s || !out || out_size != s->input_size) return false;
    if (s->kind == GEN3_KIND_GBA)
        return s->data_offset == 0u && gen3_export_raw(&s->gba, out, out_size);
    if (!s->input || !s->work) return false;

    if (s->kind == GEN3_KIND_RSBOX) return rsbox_export(s, out, out_size);

    size_t save_size, slot_start, slot_size;
    if (s->kind == GEN3_KIND_COLOSSEUM) {
        save_size = GEN3_COLO_SIZE; slot_start = COLO_SLOT_START; slot_size = COLO_SLOT_SIZE;
    } else if (s->kind == GEN3_KIND_XD) {
        save_size = GEN3_XD_SIZE; slot_start = XD_SLOT_START; slot_size = XD_SLOT_SIZE;
    } else {
        return false;
    }
    if (s->work_size != slot_size) return false;

    memcpy(out, s->input, s->input_size);
    const size_t base = s->data_offset;
    if (!range_ok(base, save_size, out_size)) return false;
    const size_t slot_off = base + slot_start + (size_t)s->active_slot * slot_size;
    if (!range_ok(slot_off, slot_size, out_size)) return false;

    uint8_t *tmp = (uint8_t *)malloc(slot_size);
    if (!tmp) return false;
    memcpy(tmp, s->work, slot_size);
    bool prepared;
    if (s->kind == GEN3_KIND_COLOSSEUM) {
        prepared = colo_set_checksums(tmp, slot_size);
        if (prepared) colo_encrypt(tmp, slot_size);
    } else {
        prepared = xd_set_checksums(tmp, slot_size, s->xd_sub_offsets[0]);
        if (prepared) xd_encrypt(tmp, slot_size);
    }
    if (!prepared) { free(tmp); return false; }
    memcpy(out + slot_off, tmp, slot_size);
    free(tmp);

    Gen3AnySave verify;
    bool ok = gen3_any_open(&verify, out, out_size) && verify.kind == s->kind && verify.integrity_ok;
    if (ok) gen3_any_close(&verify);
    return ok;
}
