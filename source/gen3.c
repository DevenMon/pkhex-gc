#include "gen3.h"

#include <stdio.h>
#include <string.h>

#define GEN3_SIGNATURE 0x08012025u

static uint16_t rd16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Gen III checksums are over a section-ID-specific data length. Most sections
 * use all 0xF80 data bytes, but Trainer Info (0), Rival/Game Specific (4),
 * and the final PC buffer (13) are shorter. Using the exact lengths matters
 * for safe write-back because unused tail bytes in a real save are not a
 * contractually-zero part of the checksum. */
static size_t sector_data_length(unsigned id) {
    if (id == 0) return 0xF2Cu;
    if (id == 4) return 0xF08u;
    if (id == 13) return 0x7D0u;
    return GEN3_SECTOR_USED;
}

static uint16_t sector_checksum(const uint8_t *p, unsigned id) {
    uint32_t sum = 0;
    size_t n = sector_data_length(id);
    for (size_t i = 0; i < n; i += 4)
        sum += rd32le(p + i);
    return (uint16_t)((sum & 0xFFFFu) + (sum >> 16));
}

static void analyze_slot(const uint8_t *data, int slot, Gen3SlotInfo *out) {
    memset(out, 0, sizeof(*out));
    const size_t base = (size_t)slot * GEN3_HALF_SIZE;
    uint16_t seen = 0;
    bool signatures = true;
    unsigned bad_checksums = 0;
    uint32_t section0_index = 0;

    for (unsigned physical = 0; physical < GEN3_MAIN_SECTORS; ++physical) {
        const size_t ofs = base + (size_t)physical * GEN3_SECTOR_SIZE;
        const uint8_t *sector = data + ofs;
        const uint16_t id = rd16le(sector + 0xFF4);
        if (id >= GEN3_MAIN_SECTORS)
            continue;
        seen |= (uint16_t)(1u << id);

        if (rd32le(sector + 0xFF8) != GEN3_SIGNATURE)
            signatures = false;
        if (sector_checksum(sector, id) != rd16le(sector + 0xFF6))
            ++bad_checksums;

        if (id == 0) {
            out->section0_offset = ofs;
            section0_index = rd32le(sector + 0xFFC);
        }
    }

    out->all_sections = seen == 0x3FFFu;
    out->signatures_ok = signatures && out->all_sections;
    out->checksum_failures = bad_checksums;
    out->checksums_ok = out->all_sections && bad_checksums == 0;
    out->save_index = section0_index;
}

static bool index_newer(uint32_t a, uint32_t b) {
    /* Save counter rollover-safe for normal adjacent save generations. */
    return (int32_t)(a - b) > 0;
}

static int choose_active_slot(const Gen3SlotInfo *a, const Gen3SlotInfo *b) {
    const bool av = a->all_sections && a->signatures_ok && a->checksums_ok;
    const bool bv = b->all_sections && b->signatures_ok && b->checksums_ok;

    if (av && !bv) return 0;
    if (bv && !av) return 1;
    if (av && bv) {
        if (a->save_index == b->save_index)
            return 0; /* Match PKHeX SAV3BlockDetection: equal counters prefer primary. */
        return index_newer(a->save_index, b->save_index) ? 0 : 1;
    }

    /* Damaged save: still allow read-only inspection if the sector map exists. */
    if (a->all_sections && !b->all_sections) return 0;
    if (b->all_sections && !a->all_sections) return 1;
    if (a->all_sections && b->all_sections) {
        if (a->save_index == b->save_index)
            return a->checksum_failures <= b->checksum_failures ? 0 : 1;
        return index_newer(a->save_index, b->save_index) ? 0 : 1;
    }
    return -1;
}

static bool any_nonzero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (p[i] != 0)
            return true;
    return false;
}

static Gen3Game detect_game(const Gen3Save *save) {
    const uint32_t v = rd32le(save->small + 0xAC);
    if (v == 1)
        return GEN3_GAME_FRLG;
    if (v == 0)
        return GEN3_GAME_RS;
    if (any_nonzero(save->small + 0x890, 0xF2C - 0x890))
        return GEN3_GAME_EMERALD;
    return GEN3_GAME_RS;
}

bool gen3_open(Gen3Save *save, const uint8_t *data, size_t size) {
    if (!save || !data || size < GEN3_RAW_SIZE)
        return false;

    memset(save, 0, sizeof(*save));
    save->raw = data;
    save->raw_size = size;

    analyze_slot(data, 0, &save->slots[0]);
    analyze_slot(data, 1, &save->slots[1]);
    save->active_slot = choose_active_slot(&save->slots[0], &save->slots[1]);
    if (save->active_slot < 0)
        return false;

    const size_t base = (size_t)save->active_slot * GEN3_HALF_SIZE;
    for (unsigned physical = 0; physical < GEN3_MAIN_SECTORS; ++physical) {
        const uint8_t *sector = data + base + (size_t)physical * GEN3_SECTOR_SIZE;
        const uint16_t id = rd16le(sector + 0xFF4);
        if (id >= GEN3_MAIN_SECTORS)
            return false;

        if (id == 0) {
            memcpy(save->small, sector, GEN3_SECTOR_USED);
        } else if (id <= 4) {
            memcpy(save->large + (size_t)(id - 1) * GEN3_SECTOR_USED,
                   sector, GEN3_SECTOR_USED);
        } else {
            memcpy(save->storage + (size_t)(id - 5) * GEN3_SECTOR_USED,
                   sector, GEN3_SECTOR_USED);
        }
    }

    save->japanese = rd16le(save->small + 6) == 0;
    save->game = detect_game(save);
    return true;
}

const char *gen3_game_name(Gen3Game game) {
    switch (game) {
        case GEN3_GAME_RS: return "Ruby / Sapphire";
        case GEN3_GAME_EMERALD: return "Emerald";
        case GEN3_GAME_FRLG: return "FireRed / LeafGreen";
        default: return "Unknown Gen III";
    }
}

/* Compact ASCII rendering of the Gen III western character set. The console
 * reader intentionally degrades unsupported accented/Japanese glyphs to '?'. */
static char western_char(uint8_t c) {
    if (c >= 0xA1 && c <= 0xAA) return (char)('0' + (c - 0xA1));
    if (c >= 0xBB && c <= 0xD4) return (char)('A' + (c - 0xBB));
    if (c >= 0xD5 && c <= 0xEE) return (char)('a' + (c - 0xD5));
    switch (c) {
        case 0x00: return ' ';
        case 0x2D: return '&';
        case 0x2E: return '+';
        case 0x5B: return '%';
        case 0x5C: return '(';
        case 0x5D: return ')';
        case 0xAB: return '!';
        case 0xAC: return '?';
        case 0xAD: return '.';
        case 0xAE: return '-';
        case 0xB4: return '\'';
        case 0xB5: return 'M'; /* male symbol fallback */
        case 0xB6: return 'F'; /* female symbol fallback */
        case 0xB7: return '$';
        case 0xB8: return ',';
        case 0xBA: return '/';
        case 0xF0: return ':';
        default: return '?';
    }
}

char gen3_decode_char(uint8_t c) { return western_char(c); }

/*
 * The inverse of western_char, for the on-screen keyboard. Only characters
 * that round-trip are mapped: the gender symbols decode to 'M' and 'F' for
 * display but must not encode back from them, since those are also letters.
 * Anything unmapped returns the terminator so callers can reject it.
 */
uint8_t gen3_encode_char(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(0xA1u + (unsigned)(c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0xBBu + (unsigned)(c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0xD5u + (unsigned)(c - 'a'));
    switch (c) {
        case ' ':  return 0x00;
        case '&':  return 0x2D;
        case '+':  return 0x2E;
        case '%':  return 0x5B;
        case '(':  return 0x5C;
        case ')':  return 0x5D;
        case '!':  return 0xAB;
        case '?':  return 0xAC;
        case '.':  return 0xAD;
        case '-':  return 0xAE;
        case '\'': return 0xB4;
        case '$':  return 0xB7;
        case ',':  return 0xB8;
        case '/':  return 0xBA;
        case ':':  return 0xF0;
        default:   return GEN3_TEXT_TERMINATOR;
    }
}

void gen3_encode_text(const char *src, uint8_t *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return;
    size_t w = 0;
    for (const char *p = src; p && *p && w < dst_len; ++p) {
        const uint8_t enc = gen3_encode_char(*p);
        if (enc == GEN3_TEXT_TERMINATOR) continue;   /* skip what Gen III cannot hold */
        dst[w++] = enc;
    }
    /* Names are terminated, then padded with the terminator to the full field
     * width, which is what the games themselves write. */
    while (w < dst_len) dst[w++] = GEN3_TEXT_TERMINATOR;
}

void gen3_decode_text(const uint8_t *src, size_t src_len, bool japanese, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0)
        return;
    size_t w = 0;
    for (size_t i = 0; i < src_len && w + 1 < dst_len; ++i) {
        const uint8_t c = src[i];
        if (c == 0xFF)
            break;
        /* 0x00 is a space, not padding: dropping it turned "SAM B" into
         * "SAMB". Trailing padding is handled by the trim below instead. */
        char out = japanese ? '?' : western_char(c);
        dst[w++] = out;
    }
    while (w > 0 && dst[w - 1] == ' ')
        --w;
    dst[w] = '\0';
    if (w == 0 && dst_len > 1) {
        dst[0] = '-';
        dst[1] = '\0';
    }
}

void gen3_trainer_name(const Gen3Save *save, char *out, size_t out_size) {
    gen3_decode_text(save->small, save->japanese ? 5 : 7, save->japanese, out, out_size);
}

size_t gen3_trainer_name_length(const Gen3Save *save) {
    return (save && save->japanese) ? 5u : 7u;
}

bool gen3_set_trainer_name(Gen3Save *save, const uint8_t *raw, size_t len) {
    if (!save || !raw) return false;
    const size_t field = gen3_trainer_name_length(save);
    if (len > field) return false;
    memcpy(save->small, raw, len);
    for (size_t i = len; i < field; ++i) save->small[i] = GEN3_TEXT_TERMINATOR;
    /* The byte after the name is the terminator the games rely on. */
    save->small[field] = GEN3_TEXT_TERMINATOR;
    return true;
}

uint8_t gen3_trainer_gender(const Gen3Save *save) { return save->small[8]; }
uint16_t gen3_tid(const Gen3Save *save) { return rd16le(save->small + 0x0A); }
uint16_t gen3_sid(const Gen3Save *save) { return rd16le(save->small + 0x0C); }
uint16_t gen3_played_hours(const Gen3Save *save) { return rd16le(save->small + 0x0E); }
uint8_t gen3_played_minutes(const Gen3Save *save) { return save->small[0x10]; }
uint8_t gen3_played_seconds(const Gen3Save *save) { return save->small[0x11]; }


bool gen3_is_pokedex_corrupt(const Gen3Save *save) {
    if (!save) return false;
    for (unsigned i = 0; i < 8u; ++i)
        if (save->small[0xAC + i] != 0xFFu) return false;
    return true;
}

uint32_t gen3_security_key(const Gen3Save *save) {
    if (!save) return 0;
    if (save->game == GEN3_GAME_EMERALD) return rd32le(save->small + 0xAC);
    if (save->game == GEN3_GAME_FRLG) return rd32le(save->small + 0xAF8);
    return 0; /* Ruby/Sapphire do not mask money/item quantities. */
}

static size_t money_offset(const Gen3Save *save) {
    return save && save->game == GEN3_GAME_FRLG ? 0x290u : 0x490u;
}

uint32_t gen3_money(const Gen3Save *save) {
    if (!save) return 0;
    return rd32le(save->large + money_offset(save)) ^ gen3_security_key(save);
}

uint16_t gen3_coins(const Gen3Save *save) {
    if (!save) return 0;
    return (uint16_t)(rd16le(save->large + money_offset(save) + 4u) ^ (uint16_t)gen3_security_key(save));
}

typedef struct PocketLayout { size_t offset; unsigned count; bool encrypted_qty; } PocketLayout;

static bool pocket_layout(const Gen3Save *save, Gen3Pocket pocket, PocketLayout *out) {
    if (!save || !out || pocket >= GEN3_POCKET_COUNT) return false;
    const bool f = save->game == GEN3_GAME_FRLG;
    const bool e = save->game == GEN3_GAME_EMERALD;
    PocketLayout p = {0};
    switch (pocket) {
        case GEN3_POCKET_PC:        p.offset=f?0x298u:0x498u; p.count=f?30u:50u; p.encrypted_qty=false; break;
        case GEN3_POCKET_ITEMS:     p.offset=f?0x310u:0x560u; p.count=f?42u:(e?30u:20u); p.encrypted_qty=true; break;
        case GEN3_POCKET_KEY_ITEMS: p.offset=f?0x3B8u:(e?0x5D8u:0x5B0u); p.count=f?30u:(e?30u:20u); p.encrypted_qty=true; break;
        case GEN3_POCKET_BALLS:     p.offset=f?0x430u:(e?0x650u:0x600u); p.count=f?13u:16u; p.encrypted_qty=true; break;
        case GEN3_POCKET_TMS:       p.offset=f?0x464u:(e?0x690u:0x640u); p.count=f?58u:64u; p.encrypted_qty=true; break;
        case GEN3_POCKET_BERRIES:   p.offset=f?0x54Cu:(e?0x790u:0x740u); p.count=f?43u:46u; p.encrypted_qty=true; break;
        default: return false;
    }
    *out=p; return true;
}

unsigned gen3_pocket_capacity(const Gen3Save *save, Gen3Pocket pocket) {
    PocketLayout p; return pocket_layout(save,pocket,&p)?p.count:0u;
}

const char *gen3_pocket_name(Gen3Pocket pocket) {
    switch (pocket) {
        case GEN3_POCKET_PC: return "PC Items";
        case GEN3_POCKET_ITEMS: return "Items";
        case GEN3_POCKET_KEY_ITEMS: return "Key Items";
        case GEN3_POCKET_BALLS: return "Poke Balls";
        case GEN3_POCKET_TMS: return "TM / HM";
        case GEN3_POCKET_BERRIES: return "Berries";
        case GEN3_POCKET_COLOGNE: return "Colognes";
        case GEN3_POCKET_DISCS: return "Battle Discs";
        default: return "Unknown";
    }
}

bool gen3_get_item_slot(const Gen3Save *save, Gen3Pocket pocket, unsigned slot, Gen3ItemSlot *out) {
    PocketLayout p;
    if (!out || !pocket_layout(save,pocket,&p) || slot>=p.count) return false;
    const uint8_t *entry=save->large+p.offset+(size_t)slot*4u;
    out->item_id=rd16le(entry);
    uint16_t q=rd16le(entry+2);
    if (p.encrypted_qty) q ^= (uint16_t)gen3_security_key(save);
    out->quantity=q;
    return true;
}

static const char *const block_orders[24] = {
    "GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA",
    "AGEM", "AGME", "AEGM", "AEMG", "AMGE", "AMEG",
    "EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG",
    "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"
};

static int block_index(char c) {
    switch (c) {
        case 'G': return 0;
        case 'A': return 1;
        case 'E': return 2;
        case 'M': return 3;
        default: return -1;
    }
}

static uint8_t swap_bits(uint8_t value, unsigned a, unsigned b) {
    const unsigned bit_a = (value >> a) & 1u;
    const unsigned bit_b = (value >> b) & 1u;
    value = (uint8_t)(value & ~((1u << a) | (1u << b)));
    return (uint8_t)(value | (bit_a << b) | (bit_b << a));
}

static bool pk3_unpack_canonical(const uint8_t *src, uint8_t canonical[48]) {
    uint32_t pid = rd32le(src + 0x00);
    uint32_t otid = rd32le(src + 0x04);
    uint8_t decrypted[48];
    memcpy(decrypted, src + 0x20, sizeof(decrypted));
    uint32_t key = pid ^ otid;
    for (size_t i = 0; i < sizeof(decrypted); i += 4)
        wr32le(decrypted + i, rd32le(decrypted + i) ^ key);

    const char *order = block_orders[pid % 24u];
    for (int stored = 0; stored < 4; ++stored) {
        int canonical_index = block_index(order[stored]);
        if (canonical_index < 0) return false;
        memcpy(canonical + canonical_index * 12, decrypted + stored * 12, 12);
    }
    return true;
}

static void pk3_pack_canonical(uint8_t *dst, const uint8_t canonical[48], uint32_t pid, uint32_t otid) {
    uint32_t sum = 0;
    for (size_t i = 0; i < 48; i += 2) sum += rd16le(canonical + i);
    wr16le(dst + 0x1C, (uint16_t)sum);

    uint8_t stored_data[48];
    const char *order = block_orders[pid % 24u];
    for (int stored = 0; stored < 4; ++stored) {
        int canonical_index = block_index(order[stored]);
        memcpy(stored_data + stored * 12, canonical + canonical_index * 12, 12);
    }
    uint32_t key = pid ^ otid;
    for (size_t i = 0; i < sizeof(stored_data); i += 4)
        wr32le(stored_data + i, rd32le(stored_data + i) ^ key);
    memcpy(dst + 0x20, stored_data, sizeof(stored_data));
    wr32le(dst + 0x00, pid);
    wr32le(dst + 0x04, otid);
}

bool gen3_parse_pk3_record(const uint8_t *src, size_t record_size, bool japanese, Gen3Pokemon *out) {
    if (!src || !out || record_size < GEN3_PK3_STORED_SIZE)
        return false;
    memset(out, 0, sizeof(*out));

    /*
     * A slot is occupied only when the plaintext flag byte says so. Byte 0x13
     * carries Bad Egg (bit 0), Has Species (bit 1) and Is Egg (bit 2), and
     * PKHeX's rule is that everything but the egg bit must leave exactly Has
     * Species set.
     *
     * The obvious alternative - "the record is not all zeroes" - is wrong, and
     * wrong in a way that shows up on real saves: withdrawing a Pokemon clears
     * the flag byte and the species but leaves the PID, the trainer ID and the
     * nickname behind. A save with two Pokemon in it can hold a hundred and
     * fifty slots of that residue, and reading them as occupied produces a box
     * full of nameless entries with real nicknames attached.
     */
    if ((src[0x13] & (uint8_t)~GEN3_PK3_FLAG_IS_EGG) != GEN3_PK3_FLAG_HAS_SPECIES)
        return true;

    out->present = true;
    out->pid = rd32le(src + 0x00);
    out->otid = rd32le(src + 0x04);
    out->tid = (uint16_t)out->otid;
    out->sid = (uint16_t)(out->otid >> 16);
    gen3_decode_text(src + 0x08, 10, japanese, out->nickname, sizeof(out->nickname));
    gen3_decode_text(src + 0x14, 7, japanese, out->ot_name, sizeof(out->ot_name));
    /* Kept raw as well: editing goes through the stored bytes so characters
     * this build renders as '?' survive a round trip untouched. */
    memcpy(out->nickname_raw, src + 0x08, sizeof(out->nickname_raw));
    memcpy(out->ot_raw, src + 0x14, sizeof(out->ot_raw));

    uint8_t canonical[48];
    if (!pk3_unpack_canonical(src, canonical)) return false;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(canonical); i += 2)
        sum += rd16le(canonical + i);
    out->checksum_ok = (uint16_t)sum == rd16le(src + 0x1C);

    const uint8_t *growth = canonical + 0;
    const uint8_t *attacks = canonical + 12;
    const uint8_t *evs = canonical + 24;
    const uint8_t *misc = canonical + 36;
    out->species_internal = rd16le(growth + 0);
    out->held_item = rd16le(growth + 2);
    out->experience = rd32le(growth + 4);
    out->friendship = growth[9];
    for (unsigned i = 0; i < 4; ++i) {
        out->moves[i] = rd16le(attacks + i * 2u);
        out->pp[i] = attacks[8u + i];
    }
    memcpy(out->evs, evs, 6);
    memcpy(out->contest, evs + 6, 6);
    out->pp_ups = growth[8];

    const uint32_t iv_word = rd32le(misc + 4);
    for (unsigned i = 0; i < 6; ++i) out->ivs[i] = (uint8_t)((iv_word >> (i * 5u)) & 31u);
    out->is_egg = (iv_word & (1u << 30)) != 0;
    out->ability_bit = (iv_word & (1u << 31)) != 0;

    out->language = src[0x12];
    /* PKHeX stores markings with bits 1 and 2 exchanged relative to the order
     * the games display them (circle, square, triangle, heart). */
    out->markings = swap_bits(src[0x1B], 1, 2);

    out->pokerus = misc[0];
    out->met_location = misc[1];
    const uint16_t origins = rd16le(misc + 2);
    out->met_level = (uint8_t)(origins & 0x7Fu);
    out->origin_game = (uint8_t)((origins >> 7) & 0x0Fu);
    out->ball = (uint8_t)((origins >> 11) & 0x0Fu);
    out->ot_gender = (uint8_t)((origins >> 15) & 1u);

    out->ribbons = rd32le(misc + 8);
    out->fateful = (out->ribbons & (1u << 31)) != 0;

    if (record_size >= GEN3_PK3_PARTY_SIZE)
        out->level = src[0x54];
    return true;
}

/* ------------------------------------------- derived Generation III values */

static const char *const NATURE_NAMES[GEN3_NATURE_COUNT] = {
    "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
    "Bold", "Docile", "Relaxed", "Impish", "Lax",
    "Timid", "Hasty", "Serious", "Jolly", "Naive",
    "Modest", "Mild", "Quiet", "Bashful", "Rash",
    "Calm", "Gentle", "Sassy", "Careful", "Quirky",
};

/* Hidden Power's type index order, which is not the games' type-chart order. */
static const char *const HP_TYPE_NAMES[16] = {
    "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel",
    "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark",
};

static const char *const BALL_NAMES[13] = {
    "(none)", "Master Ball", "Ultra Ball", "Great Ball", "Poke Ball",
    "Safari Ball", "Net Ball", "Dive Ball", "Nest Ball", "Repeat Ball",
    "Timer Ball", "Luxury Ball", "Premier Ball",
};

const char *gen3_nature_name(uint8_t nature) {
    return nature < GEN3_NATURE_COUNT ? NATURE_NAMES[nature] : "?";
}

const char *gen3_type_name(uint8_t type) {
    return type < 16u ? HP_TYPE_NAMES[type] : "?";
}

const char *gen3_language_name(uint8_t language) {
    switch (language) {
        case 1: return "Japanese";
        case 2: return "English";
        case 3: return "French";
        case 4: return "Italian";
        case 5: return "German";
        case 6: return "Korean";
        case 7: return "Spanish";
        default: return "?";
    }
}

const char *gen3_ball_name(uint8_t ball) {
    return ball < 13u ? BALL_NAMES[ball] : "?";
}

const char *gen3_origin_game_name(uint8_t version) {
    switch (version) {
        case 1: return "Sapphire";
        case 2: return "Ruby";
        case 3: return "Emerald";
        case 4: return "FireRed";
        case 5: return "LeafGreen";
        case 15: return "Colosseum/XD";
        default: return "?";
    }
}

uint8_t gen3_nature(const Gen3Pokemon *p) {
    return p ? (uint8_t)(p->pid % GEN3_NATURE_COUNT) : 0u;
}

bool gen3_is_shiny(const Gen3Pokemon *p) {
    if (!p) return false;
    const uint16_t pid_hi = (uint16_t)(p->pid >> 16);
    const uint16_t pid_lo = (uint16_t)p->pid;
    return (uint16_t)(p->tid ^ p->sid ^ pid_hi ^ pid_lo) < 8u;
}

/* Hidden Power reads the low bit of each IV for the type and the second bit
 * for the power, both in HP/Atk/Def/Spe/SpA/SpD order. */
uint8_t gen3_hidden_power_type(const Gen3Pokemon *p) {
    if (!p) return 0;
    unsigned acc = 0;
    for (unsigned i = 0; i < 6; ++i) acc |= (unsigned)(p->ivs[i] & 1u) << i;
    return (uint8_t)((acc * 15u) / 63u);
}

uint8_t gen3_hidden_power_power(const Gen3Pokemon *p) {
    if (!p) return 0;
    unsigned acc = 0;
    for (unsigned i = 0; i < 6; ++i) acc |= (unsigned)((p->ivs[i] >> 1) & 1u) << i;
    return (uint8_t)(((acc * 40u) / 63u) + 30u);
}

uint8_t gen3_pokerus_days(const Gen3Pokemon *p) { return p ? (uint8_t)(p->pokerus & 0x0Fu) : 0u; }

/* --------------------------------------------------------------- ribbons ---
 *
 * Bits 0-14 hold the five contest ribbons, three bits each, as a level from 0
 * (not won) to 4 (Master). Bits 15-26 are one ribbon each. Bit 31 is Fateful
 * Encounter, which the record keeps separately, so it is not listed here.
 */
static const char *const contest_ribbon_names[GEN3_CONTEST_RIBBON_COUNT] = {
    "Cool", "Beauty", "Cute", "Smart", "Tough",
};

static const char *const ribbon_flag_names[GEN3_RIBBON_FLAG_COUNT] = {
    "Champion", "Winning", "Victory", "Artist", "Effort", "Battle Champion",
    "Regional Champion", "National Champion", "Country", "National", "Earth", "World",
};

uint8_t gen3_contest_ribbon(const Gen3Pokemon *p, unsigned contest) {
    if (!p || contest >= GEN3_CONTEST_RIBBON_COUNT) return 0;
    return (uint8_t)((p->ribbons >> (contest * 3u)) & 7u);
}

bool gen3_set_contest_ribbon(Gen3Pokemon *p, unsigned contest, uint8_t level) {
    if (!p || contest >= GEN3_CONTEST_RIBBON_COUNT || level > GEN3_CONTEST_RIBBON_MAX) return false;
    const unsigned shift = contest * 3u;
    p->ribbons = (p->ribbons & ~(7u << shift)) | ((uint32_t)level << shift);
    return true;
}

const char *gen3_contest_ribbon_name(unsigned contest) {
    return contest < GEN3_CONTEST_RIBBON_COUNT ? contest_ribbon_names[contest] : "?";
}

const char *gen3_contest_ribbon_level_name(uint8_t level) {
    static const char *const levels[GEN3_CONTEST_RIBBON_MAX + 1u] = {
        "None", "Normal", "Super", "Hyper", "Master",
    };
    return level <= GEN3_CONTEST_RIBBON_MAX ? levels[level] : "?";
}

bool gen3_ribbon_flag(const Gen3Pokemon *p, unsigned index) {
    if (!p || index >= GEN3_RIBBON_FLAG_COUNT) return false;
    return (p->ribbons & (1u << (15u + index))) != 0;
}

bool gen3_set_ribbon_flag(Gen3Pokemon *p, unsigned index, bool on) {
    if (!p || index >= GEN3_RIBBON_FLAG_COUNT) return false;
    const uint32_t bit = 1u << (15u + index);
    if (on) p->ribbons |= bit; else p->ribbons &= ~bit;
    return true;
}

const char *gen3_ribbon_flag_name(unsigned index) {
    return index < GEN3_RIBBON_FLAG_COUNT ? ribbon_flag_names[index] : "?";
}

unsigned gen3_ribbon_count(const Gen3Pokemon *p) {
    if (!p) return 0;
    unsigned total = 0;
    for (unsigned i = 0; i < GEN3_CONTEST_RIBBON_COUNT; ++i) {
        const uint8_t level = gen3_contest_ribbon(p, i);
        /* A Master ribbon means the four below it were won as well. */
        total += level > GEN3_CONTEST_RIBBON_MAX ? GEN3_CONTEST_RIBBON_MAX : level;
    }
    for (unsigned i = 0; i < GEN3_RIBBON_FLAG_COUNT; ++i)
        if (gen3_ribbon_flag(p, i)) ++total;
    return total;
}
uint8_t gen3_pokerus_strain(const Gen3Pokemon *p) { return p ? (uint8_t)(p->pokerus >> 4) : 0u; }

unsigned gen3_pp_up_count(const Gen3Pokemon *p, unsigned move_slot) {
    if (!p || move_slot >= 4u) return 0u;
    return (unsigned)((p->pp_ups >> (move_slot * 2u)) & 3u);
}

void gen3_set_pp_up_count(Gen3Pokemon *p, unsigned move_slot, unsigned ups) {
    if (!p || move_slot >= 4u) return;
    if (ups > 3u) ups = 3u;
    const uint8_t shift = (uint8_t)(move_slot * 2u);
    p->pp_ups = (uint8_t)((p->pp_ups & ~(3u << shift)) | (ups << shift));
}

/*
 * Nature is PID modulo 25, and so are shininess and the games' gender roll, so
 * changing it means finding a different PID rather than writing a field.  Walk
 * PIDs with a plain LCG until one has the requested nature and the same
 * shininess as before, which keeps a legitimate Pokemon legitimate and does
 * not accidentally mint a shiny.
 */
bool gen3_set_nature(Gen3Pokemon *p, uint8_t nature) {
    if (!p || nature >= GEN3_NATURE_COUNT) return false;
    if (gen3_nature(p) == nature) return true;

    const uint32_t original = p->pid;

    if (gen3_is_shiny(p)) {
        /*
         * A shiny needs (TID ^ SID ^ PIDhi ^ PIDlo) < 8, which is far too rare
         * to stumble into by walking PIDs.  Construct one instead: pick a high
         * half, derive the low half that keeps the XOR inside the shiny range,
         * and accept the first that also lands on the requested nature.
         */
        const uint16_t xor_base = (uint16_t)(p->tid ^ p->sid);
        for (uint32_t step = 0; step < 0x10000u; ++step) {
            const uint16_t hi = (uint16_t)((original >> 16) + step);
            for (uint32_t k = 0; k < 8u; ++k) {
                const uint16_t lo = (uint16_t)(xor_base ^ hi ^ k);
                p->pid = ((uint32_t)hi << 16) | lo;
                if (gen3_nature(p) == nature) return true;
            }
        }
    } else {
        uint32_t candidate = original;
        for (unsigned tries = 0; tries < 100000u; ++tries) {
            candidate = candidate * 0x41C64E6Du + 0x00006073u; /* the games' own LCG */
            p->pid = candidate;
            if (gen3_nature(p) == nature && !gen3_is_shiny(p)) return true;
        }
    }

    p->pid = original;
    return false;
}


unsigned gen3_party_count(const Gen3Save *save) {
    size_t count_ofs = save->game == GEN3_GAME_FRLG ? 0x34 : 0x234;
    unsigned count = save->large[count_ofs];
    return count > 6 ? 6 : count;
}

bool gen3_party_pokemon(const Gen3Save *save, unsigned slot, Gen3Pokemon *out) {
    if (slot >= 6 || !out)
        return false;
    size_t party_ofs = save->game == GEN3_GAME_FRLG ? 0x38 : 0x238;
    return gen3_parse_pk3_record(save->large + party_ofs + (size_t)slot * GEN3_PK3_PARTY_SIZE,
                     GEN3_PK3_PARTY_SIZE, save->japanese, out);
}

unsigned gen3_current_box(const Gen3Save *save) {
    unsigned box = save->storage[0];
    return box < GEN3_BOX_COUNT ? box : 0;
}

static size_t box_name_offset(unsigned box);

void gen3_box_name(const Gen3Save *save, unsigned box, char *out, size_t out_size) {
    if (box >= GEN3_BOX_COUNT) {
        if (out && out_size) out[0] = '\0';
        return;
    }
    gen3_decode_text(save->storage + box_name_offset(box), GEN3_BOX_NAME_LEN,
                     save->japanese, out, out_size);
}

static size_t box_name_offset(unsigned box) {
    const size_t names = 4 + (size_t)GEN3_BOX_COUNT * GEN3_BOX_SLOTS * GEN3_PK3_STORED_SIZE;
    return names + (size_t)box * 9u;
}

void gen3_box_name_raw(const Gen3Save *save, unsigned box, uint8_t out[GEN3_BOX_NAME_LEN]) {
    if (!save || !out) return;
    if (box >= GEN3_BOX_COUNT) { memset(out, GEN3_TEXT_TERMINATOR, GEN3_BOX_NAME_LEN); return; }
    memcpy(out, save->storage + box_name_offset(box), GEN3_BOX_NAME_LEN);
}

bool gen3_set_box_name(Gen3Save *save, unsigned box, const uint8_t *raw, size_t len) {
    if (!save || !raw || box >= GEN3_BOX_COUNT || len > GEN3_BOX_NAME_LEN) return false;
    uint8_t *dst = save->storage + box_name_offset(box);
    memcpy(dst, raw, len);
    for (size_t i = len; i < GEN3_BOX_NAME_LEN; ++i) dst[i] = GEN3_TEXT_TERMINATOR;
    return true;
}

/*
 * Box names are followed by one wallpaper byte per box, which is how the games
 * pick which background art to draw. PKHeX reads the same two runs:
 * GetBoxOffset(14) for the names, plus 14*9 for the wallpapers.
 */
static size_t box_wallpaper_offset(unsigned box) {
    const size_t names = 4 + (size_t)GEN3_BOX_COUNT * GEN3_BOX_SLOTS * GEN3_PK3_STORED_SIZE;
    return names + (size_t)GEN3_BOX_COUNT * 9u + box;
}

uint8_t gen3_box_wallpaper(const Gen3Save *save, unsigned box) {
    if (!save || box >= GEN3_BOX_COUNT) return 0;
    const uint8_t raw = save->storage[box_wallpaper_offset(box)];
    /* A corrupt or unwritten byte must not index past the name table. */
    return raw < GEN3_WALLPAPER_COUNT ? raw : (uint8_t)(box % GEN3_WALLPAPER_COUNT);
}

bool gen3_set_box_wallpaper(Gen3Save *save, unsigned box, uint8_t wallpaper) {
    if (!save || box >= GEN3_BOX_COUNT || wallpaper >= GEN3_WALLPAPER_COUNT) return false;
    save->storage[box_wallpaper_offset(box)] = wallpaper;
    return true;
}

/*
 * Deliberately not taken from PKHeX. It keeps one wallpaper list and uses it
 * for every generation, and that list is the Generation IV one: it has "Poke
 * Center" at 12 and "Checks" at 14, where Generation III has "Polka-dot" at 12
 * and no Checks at all. These are the names Ruby, Sapphire, Emerald, FireRed
 * and LeafGreen actually show.
 */
const char *gen3_wallpaper_name(uint8_t wallpaper) {
    static const char *const names[GEN3_WALLPAPER_COUNT] = {
        "Forest", "City", "Desert", "Savanna", "Crag", "Volcano", "Snow", "Cave",
        "Beach", "Seafloor", "River", "Sky", "Polka-dot", "Pokecenter", "Machine", "Simple",
    };
    return wallpaper < GEN3_WALLPAPER_COUNT ? names[wallpaper] : "?";
}

bool gen3_box_pokemon(const Gen3Save *save, unsigned box, unsigned slot, Gen3Pokemon *out) {
    if (box >= GEN3_BOX_COUNT || slot >= GEN3_BOX_SLOTS || !out)
        return false;
    const size_t index = (size_t)box * GEN3_BOX_SLOTS + slot;
    const size_t ofs = 4 + index * GEN3_PK3_STORED_SIZE;
    return gen3_parse_pk3_record(save->storage + ofs, GEN3_PK3_STORED_SIZE, save->japanese, out);
}


bool gen3_set_trainer_name_ascii(Gen3Save *save, const char *name) {
    if (!save || !name || save->japanese) return false;
    gen3_encode_text(name, save->small, 7);
    return true;
}
void gen3_set_trainer_gender(Gen3Save *save, uint8_t gender) { if (save) save->small[8] = gender ? 1u : 0u; }
void gen3_set_tid(Gen3Save *save, uint16_t tid) { if (save) wr16le(save->small + 0x0A, tid); }
void gen3_set_sid(Gen3Save *save, uint16_t sid) { if (save) wr16le(save->small + 0x0C, sid); }
void gen3_set_playtime(Gen3Save *save, uint16_t hours, uint8_t minutes, uint8_t seconds) {
    if (!save) return;
    wr16le(save->small + 0x0E, hours);
    save->small[0x10] = minutes > 59 ? 59 : minutes;
    save->small[0x11] = seconds > 59 ? 59 : seconds;
}


void gen3_set_money(Gen3Save *save, uint32_t money) {
    if (!save) return;
    if (money > 999999u) money = 999999u;
    wr32le(save->large + money_offset(save), money ^ gen3_security_key(save));
}

void gen3_set_coins(Gen3Save *save, uint16_t coins) {
    if (!save) return;
    if (coins > 9999u) coins = 9999u;
    wr16le(save->large + money_offset(save) + 4u, (uint16_t)(coins ^ (uint16_t)gen3_security_key(save)));
}

bool gen3_set_item_slot(Gen3Save *save, Gen3Pocket pocket, unsigned slot, uint16_t item_id, uint16_t quantity) {
    PocketLayout p;
    if (!pocket_layout(save,pocket,&p) || slot>=p.count) return false;
    if (quantity > 999u) quantity=999u;
    uint8_t *entry=save->large+p.offset+(size_t)slot*4u;
    wr16le(entry,item_id);
    uint16_t stored=quantity;
    if (p.encrypted_qty) stored ^= (uint16_t)gen3_security_key(save);
    wr16le(entry+2,stored);
    return true;
}

/* Writes a Gen3Pokemon back over an existing stored or party record, keeping
 * whatever bytes the struct does not carry. Public so the daycare - which
 * holds ordinary records outside the party and boxes - can use it too. */
bool gen3_write_pk3_record(uint8_t *record, size_t record_size, const Gen3Pokemon *p) {
    if (!record || !p || record_size < GEN3_PK3_STORED_SIZE || !p->present) return false;
    uint8_t canonical[48];
    if (!pk3_unpack_canonical(record, canonical)) return false;
    uint8_t *growth = canonical + 0;
    uint8_t *attacks = canonical + 12;
    uint8_t *evs = canonical + 24;
    uint8_t *misc = canonical + 36;
    wr16le(growth + 0, p->species_internal);
    wr16le(growth + 2, p->held_item);
    wr32le(growth + 4, p->experience);
    growth[9] = p->friendship;
    for (unsigned i = 0; i < 4; ++i) {
        wr16le(attacks + i * 2u, p->moves[i]);
        attacks[8u + i] = p->pp[i];
    }
    memcpy(evs, p->evs, 6);
    memcpy(evs + 6, p->contest, 6);
    growth[8] = p->pp_ups;

    uint32_t iv_word = 0;
    for (unsigned i = 0; i < 6; ++i) iv_word |= ((uint32_t)(p->ivs[i] & 31u)) << (i * 5u);
    if (p->is_egg) iv_word |= 1u << 30;
    if (p->ability_bit) iv_word |= 1u << 31;
    wr32le(misc + 4, iv_word);

    record[0x12] = p->language;
    record[0x1B] = swap_bits(p->markings, 1, 2);
    memcpy(record + 0x08, p->nickname_raw, sizeof(p->nickname_raw));
    memcpy(record + 0x14, p->ot_raw, sizeof(p->ot_raw));

    misc[0] = p->pokerus;
    misc[1] = p->met_location;
    uint16_t origins = (uint16_t)(p->met_level & 0x7Fu);
    origins |= (uint16_t)((p->origin_game & 0x0Fu) << 7);
    origins |= (uint16_t)((p->ball & 0x0Fu) << 11);
    origins |= (uint16_t)((p->ot_gender & 1u) << 15);
    wr16le(misc + 2, origins);

    uint32_t ribbons = p->ribbons & 0x7FFFFFFFu;
    if (p->fateful) ribbons |= 1u << 31;
    wr32le(misc + 8, ribbons);

    /*
     * The flag byte is what marks the slot occupied, so writing a record has
     * to set Has Species; leaving it alone would write a Pokemon into a slot
     * that still reads as empty. Bad Egg is cleared for the same reason it is
     * in PKHeX: this is a record we just built, not a corrupt one.
     */
    record[0x13] = (uint8_t)(GEN3_PK3_FLAG_HAS_SPECIES | (p->is_egg ? GEN3_PK3_FLAG_IS_EGG : 0u));

    uint32_t otid = ((uint32_t)p->sid << 16) | p->tid;
    pk3_pack_canonical(record, canonical, p->pid, otid);
    if (record_size >= GEN3_PK3_PARTY_SIZE && p->level) record[0x54] = p->level;
    return true;
}

bool gen3_set_party_pokemon(Gen3Save *save, unsigned slot, const Gen3Pokemon *pokemon) {
    if (!save || slot >= 6) return false;
    size_t party_ofs = save->game == GEN3_GAME_FRLG ? 0x38 : 0x238;
    return gen3_write_pk3_record(save->large + party_ofs + (size_t)slot * GEN3_PK3_PARTY_SIZE,
                             GEN3_PK3_PARTY_SIZE, pokemon);
}

bool gen3_set_box_pokemon(Gen3Save *save, unsigned box, unsigned slot, const Gen3Pokemon *pokemon) {
    if (!save || box >= GEN3_BOX_COUNT || slot >= GEN3_BOX_SLOTS) return false;
    size_t index = (size_t)box * GEN3_BOX_SLOTS + slot;
    size_t ofs = 4 + index * GEN3_PK3_STORED_SIZE;
    return gen3_write_pk3_record(save->storage + ofs, GEN3_PK3_STORED_SIZE, pokemon);
}

static const uint8_t *logical_section(const Gen3Save *save, unsigned id) {
    if (id == 0) return save->small;
    if (id <= 4) return save->large + (size_t)(id - 1u) * GEN3_SECTOR_USED;
    if (id < GEN3_MAIN_SECTORS) return save->storage + (size_t)(id - 5u) * GEN3_SECTOR_USED;
    return NULL;
}

bool gen3_export_raw(const Gen3Save *save, uint8_t *out, size_t out_size) {
    if (!save || !out || !save->raw || out_size < GEN3_RAW_SIZE || save->raw_size < GEN3_RAW_SIZE || save->active_slot < 0)
        return false;
    memcpy(out, save->raw, GEN3_RAW_SIZE);
    size_t base = (size_t)save->active_slot * GEN3_HALF_SIZE;
    for (unsigned physical = 0; physical < GEN3_MAIN_SECTORS; ++physical) {
        uint8_t *sector = out + base + (size_t)physical * GEN3_SECTOR_SIZE;
        unsigned id = rd16le(sector + 0xFF4);
        const uint8_t *logical = logical_section(save, id);
        if (!logical) return false;
        memcpy(sector, logical, GEN3_SECTOR_USED);
        wr16le(sector + 0xFF6, sector_checksum(sector, id));
    }
    Gen3Save verify;
    if (!gen3_open(&verify, out, GEN3_RAW_SIZE)) return false;
    return verify.slots[verify.active_slot].checksums_ok;
}


/* Generation III stores Hoenn species in the order the games added them,
 * not National Dex order, so the mapping needs a table. Generated from
 * PKHeX's SpeciesConverter Table3InternalToNational; see SOURCES.md. */
#define GEN3_INTERNAL_FIRST_UNALIGNED 277u
#define GEN3_NATIONAL_FIRST_UNALIGNED 252u

static const int8_t gen3_internal_to_national_delta[135] = {
     -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,
     -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,  -25,
     -11,  -11,  -11,  -28,  -28,  -21,  -21,   19,  -31,  -31,  -28,  -28,
       7,    7,  -15,  -15,   35,   25,   25,  -21,    3,  -20,   16,   16,
      45,   15,   15,   21,   21,  -12,  -12,   -4,   -4,   -4,  -39,  -39,
     -28,  -28,  -17,  -17,   22,   22,   22,  -13,  -13,   15,   15,  -11,
     -11,  -52,  -26,  -26,  -42,  -42,  -52,  -49,  -49,  -25,  -25,    0,
      -6,   -6,  -48,  -77,  -77,  -77,  -51,  -51,  -12,  -77,  -77,  -77,
      -7,   -7,   -7,  -17,  -24,  -24,  -43,  -45,  -12,  -78,  -78,  -78,
     -34,  -73,  -73,  -43,  -43,  -43,  -43, -112, -112, -112,  -24,  -24,
     -24,  -24,  -24,  -24,  -24,  -24,  -24,  -22,  -22,  -22,  -27,  -27,
     -24,  -24,  -53,
};

static const int8_t gen3_national_to_internal_delta[135] = {
      25,   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,
      25,   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,
      28,   28,   31,   31,  112,  112,  112,   28,   28,   21,   21,   77,
      77,   77,   11,   11,   11,   77,   77,   77,   39,   39,   52,   21,
      15,   15,   20,   52,   78,   78,   78,   49,   49,   28,   28,   42,
      42,   73,   73,   48,   51,   51,   12,   12,   -7,   -7,   17,   17,
      -3,   26,   26,  -19,    4,    4,    4,   13,   13,   25,   25,   45,
      43,   11,   11,  -16,  -16,  -15,  -15,  -25,  -25,   43,   43,   43,
      43,  -21,  -21,   34,  -35,   24,   24,    6,    6,   12,   53,   17,
       0,  -15,  -15,  -22,  -22,  -22,    7,    7,    7,   12,  -45,   24,
      24,   24,   24,   24,   24,   24,   24,   24,   27,   27,   22,   22,
      22,   24,   24,
};

/* --------------------------------------------------- Event flags / work --- */

/*
 * Story progress. Offsets and counts are per game, from PKHeX's
 * SaveBlock3Large{RS,E,FRLG}. Emerald has twelve more flag bytes than the
 * others, and every game keeps 0x100 sixteen-bit work values.
 */
typedef struct Gen3EventLayout {
    size_t flag_offset;
    size_t work_offset;
    unsigned flag_count;
    unsigned work_count;
} Gen3EventLayout;

static Gen3EventLayout event_layout(const Gen3Save *save) {
    Gen3EventLayout l;
    l.work_count = 0x100u;
    switch (save->game) {
        case GEN3_GAME_EMERALD:
            l.flag_offset = 0x1270u; l.work_offset = 0x139Cu; l.flag_count = 8u * 300u; break;
        case GEN3_GAME_FRLG:
            l.flag_offset = 0x0EE0u; l.work_offset = 0x1000u; l.flag_count = 8u * 288u; break;
        default: /* Ruby / Sapphire */
            l.flag_offset = 0x1220u; l.work_offset = 0x1340u; l.flag_count = 8u * 288u; break;
    }
    return l;
}

unsigned gen3_event_flag_count(const Gen3Save *save) {
    return save ? event_layout(save).flag_count : 0u;
}

unsigned gen3_event_work_count(const Gen3Save *save) {
    return save ? event_layout(save).work_count : 0u;
}

bool gen3_event_flag(const Gen3Save *save, unsigned flag) {
    if (!save) return false;
    const Gen3EventLayout l = event_layout(save);
    if (flag >= l.flag_count) return false;
    return (save->large[l.flag_offset + (flag >> 3)] >> (flag & 7u)) & 1u;
}

void gen3_set_event_flag(Gen3Save *save, unsigned flag, bool value) {
    if (!save) return;
    const Gen3EventLayout l = event_layout(save);
    if (flag >= l.flag_count) return;
    uint8_t *byte = &save->large[l.flag_offset + (flag >> 3)];
    const uint8_t mask = (uint8_t)(1u << (flag & 7u));
    if (value) *byte |= mask; else *byte &= (uint8_t)~mask;
}

uint16_t gen3_event_work(const Gen3Save *save, unsigned index) {
    if (!save) return 0;
    const Gen3EventLayout l = event_layout(save);
    if (index >= l.work_count) return 0;
    return rd16le(save->large + l.work_offset + index * 2u);
}

void gen3_set_event_work(Gen3Save *save, unsigned index, uint16_t value) {
    if (!save) return;
    const Gen3EventLayout l = event_layout(save);
    if (index >= l.work_count) return;
    wr16le(save->large + l.work_offset + index * 2u, value);
}

unsigned gen3_event_flags_set(const Gen3Save *save) {
    if (!save) return 0;
    const Gen3EventLayout l = event_layout(save);
    unsigned count = 0;
    for (unsigned f = 0; f < l.flag_count; ++f)
        if ((save->large[l.flag_offset + (f >> 3)] >> (f & 7u)) & 1u) ++count;
    return count;
}

/* Badges are eight consecutive event flags; only the base differs per game. */
static unsigned badge_flag_start(const Gen3Save *save) {
    switch (save->game) {
        case GEN3_GAME_EMERALD: return 0x867u;
        case GEN3_GAME_FRLG:    return 0x820u;
        default:                return 0x807u; /* Ruby / Sapphire */
    }
}

bool gen3_badge(const Gen3Save *save, unsigned badge) {
    if (!save || badge >= GEN3_BADGE_COUNT) return false;
    return gen3_event_flag(save, badge_flag_start(save) + badge);
}

void gen3_set_badge(Gen3Save *save, unsigned badge, bool earned) {
    if (!save || badge >= GEN3_BADGE_COUNT) return;
    gen3_set_event_flag(save, badge_flag_start(save) + badge, earned);
}

unsigned gen3_badge_count(const Gen3Save *save) {
    unsigned n = 0;
    for (unsigned b = 0; b < GEN3_BADGE_COUNT; ++b) if (gen3_badge(save, b)) ++n;
    return n;
}

/* ------------------------------------------------------------- Pokedex --- */

/*
 * Layout from PKHeX's SAV3: the caught bits live at small block 0x18+0x10 and
 * the seen bits at 0x18+0x44, both indexed by National Dex number minus one.
 * The seen bits are additionally mirrored twice inside the large block, at
 * per-game offsets, and the games validate the three copies against each
 * other - so a write has to update all of them.
 */
#define GEN3_DEX_BASE        0x18u
#define GEN3_DEX_CAUGHT      (GEN3_DEX_BASE + 0x10u)
#define GEN3_DEX_SEEN        (GEN3_DEX_BASE + 0x44u)
#define GEN3_DEX_BYTES       ((GEN3_DEX_SPECIES + 7u) / 8u)

static void dex_seen_mirrors(const Gen3Save *save, size_t out[2]) {
    switch (save->game) {
        case GEN3_GAME_EMERALD: out[0] = 0x988u;  out[1] = 0x3B24u; break;
        case GEN3_GAME_FRLG:    out[0] = 0x5F8u;  out[1] = 0x3A18u; break;
        default:                out[0] = 0x938u;  out[1] = 0x3A8Cu; break; /* R/S */
    }
}

static bool flag_get(const uint8_t *base, unsigned bit) {
    return (base[bit >> 3] >> (bit & 7u)) & 1u;
}

static void flag_set(uint8_t *base, unsigned bit, bool value) {
    const uint8_t mask = (uint8_t)(1u << (bit & 7u));
    if (value) base[bit >> 3] |= mask;
    else       base[bit >> 3] &= (uint8_t)~mask;
}

bool gen3_dex_caught(const Gen3Save *save, unsigned national) {
    if (!save || national < 1u || national > GEN3_DEX_SPECIES) return false;
    return flag_get(save->small + GEN3_DEX_CAUGHT, national - 1u);
}

bool gen3_dex_seen(const Gen3Save *save, unsigned national) {
    if (!save || national < 1u || national > GEN3_DEX_SPECIES) return false;
    return flag_get(save->small + GEN3_DEX_SEEN, national - 1u);
}

void gen3_set_dex_caught(Gen3Save *save, unsigned national, bool caught) {
    if (!save || national < 1u || national > GEN3_DEX_SPECIES) return;
    flag_set(save->small + GEN3_DEX_CAUGHT, national - 1u, caught);
}

void gen3_set_dex_seen(Gen3Save *save, unsigned national, bool seen) {
    if (!save || national < 1u || national > GEN3_DEX_SPECIES) return;
    const unsigned bit = national - 1u;
    size_t mirror[2];
    dex_seen_mirrors(save, mirror);
    flag_set(save->small + GEN3_DEX_SEEN, bit, seen);
    flag_set(save->large + mirror[0], bit, seen);
    flag_set(save->large + mirror[1], bit, seen);
}

void gen3_mirror_dex_seen(Gen3Save *save) {
    if (!save) return;
    for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n)
        gen3_set_dex_seen(save, n, gen3_dex_seen(save, n));
}

bool gen3_dex_seen_consistent(const Gen3Save *save) {
    if (!save) return false;
    size_t mirror[2];
    dex_seen_mirrors(save, mirror);
    for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n) {
        const unsigned bit = n - 1u;
        const bool primary = flag_get(save->small + GEN3_DEX_SEEN, bit);
        if (flag_get(save->large + mirror[0], bit) != primary) return false;
        if (flag_get(save->large + mirror[1], bit) != primary) return false;
    }
    return true;
}

unsigned gen3_dex_seen_count(const Gen3Save *save) {
    unsigned count = 0;
    for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n) if (gen3_dex_seen(save, n)) ++count;
    return count;
}

unsigned gen3_dex_caught_count(const Gen3Save *save) {
    unsigned count = 0;
    for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n) if (gen3_dex_caught(save, n)) ++count;
    return count;
}

/*
 * The National Dex unlock is three things at once: a magic byte in the small
 * block, a story event flag, and a work value. PKHeX writes all three, and so
 * must this - the games check more than the byte.
 */
bool gen3_national_dex(const Gen3Save *save) {
    if (!save) return false;
    /* The magic byte and its value both differ per family: FireRed/LeafGreen
     * use 0x1B/0xB9, Ruby/Sapphire/Emerald use 0x1A/0xDA. */
    if (save->game == GEN3_GAME_FRLG) return save->small[0x1B] == 0xB9u;
    return save->small[0x1A] == 0xDAu;
}

void gen3_set_national_dex(Gen3Save *save, bool unlocked) {
    if (!save) return;
    if (save->game == GEN3_GAME_FRLG) {
        save->small[0x1B] = unlocked ? 0xB9u : 0u;
        gen3_set_event_flag(save, 0x840u, unlocked);
        gen3_set_event_work(save, 0x4Eu, unlocked ? 0x6258u : 0u);
    } else {
        save->small[0x19] = unlocked ? 1u : 0u;  /* Pokedex mode */
        save->small[0x1A] = unlocked ? 0xDAu : 0u;
        gen3_set_event_flag(save, 0x836u, unlocked);
        gen3_set_event_work(save, 0x46u, unlocked ? 0x0302u : 0u);
    }
}

unsigned gen3_species_national(uint16_t internal_species) {
    if (internal_species < GEN3_NATIONAL_FIRST_UNALIGNED)
        return internal_species;
    const unsigned shift = (unsigned)internal_species - GEN3_INTERNAL_FIRST_UNALIGNED;
    if (shift >= 135u) return 0; /* also catches the 252-276 dummy range */
    return (unsigned)((int)internal_species + gen3_internal_to_national_delta[shift]);
}

uint16_t gen3_species_internal_from_national(unsigned national_species) {
    if (national_species < GEN3_NATIONAL_FIRST_UNALIGNED)
        return (uint16_t)national_species;
    const unsigned shift = national_species - GEN3_NATIONAL_FIRST_UNALIGNED;
    if (shift >= 135u) return 0;
    return (uint16_t)((int)national_species + gen3_national_to_internal_delta[shift]);
}


