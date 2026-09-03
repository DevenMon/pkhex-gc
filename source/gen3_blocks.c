/*
 * Daycare, roamer, mailbox, Hall of Fame and gift ribbons.
 *
 * These blocks all live in the large save section (except the Hall of Fame,
 * which sits past the main save entirely) and every one of them moves between
 * Ruby/Sapphire, Emerald and FireRed/LeafGreen. The offsets come from PKHeX's
 * SaveBlock3LargeRS, SaveBlock3LargeE and SaveBlock3LargeFRLG; see SOURCES.md.
 */
#include <string.h>

#include "gen3_blocks.h"

/* Local little-endian helpers: the ones in gen3.c are private to that file,
 * and these are three lines each. */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

typedef struct BlockLayout {
    size_t mail;
    size_t daycare;
    size_t daycare_slot_size;
    size_t daycare_exp[GEN3_DAYCARE_SLOTS];
    size_t daycare_seed;
    unsigned daycare_seed_bytes;
    unsigned egg_event_flag;
    size_t roamer;
    size_t gift_ribbons;
    size_t records;
    size_t rival_name;   /* 0 where the game has no rival name */
    size_t pokeblocks;   /* 0 in FireRed and LeafGreen */
    size_t secret_bases;
    size_t decorations;
    size_t swarm;
    size_t paintings;
    size_t eberry;
    size_t eberry_size;
    size_t wonder_news;   /* 0 in Ruby and Sapphire, which have no Wonder Cards */
    size_t mystery_event;
    size_t joyful;        /* 0 in Ruby and Sapphire; in the small block */
    size_t trendy_words;  /* Emerald only */
    size_t trainer_hill;  /* Emerald only */
    size_t walda;         /* Emerald only */
    size_t external_events;
} BlockLayout;

static BlockLayout block_layout(const Gen3Save *save) {
    BlockLayout l;
    memset(&l, 0, sizeof(l));
    switch (save->game) {
        case GEN3_GAME_EMERALD:
            l.mail = 0x2BE0u;
            /* Emerald and FRLG keep the held mail and the banked experience
             * inside each slot, so a slot is 0x3C bytes longer than a record. */
            l.daycare = 0x3030u;
            l.daycare_slot_size = (size_t)GEN3_PK3_STORED_SIZE + 0x3Cu;
            l.daycare_seed = 0x3148u;
            l.daycare_seed_bytes = 4u;
            l.egg_event_flag = 0x86u;
            l.roamer = 0x31DCu;
            l.gift_ribbons = 0x31B3u;
            l.records = 0x159Cu;
            l.pokeblocks = 0x848u;
            l.secret_bases = 0x1A9Cu;
            l.decorations = 0x2734u;
            l.swarm = 0x2B90u;
            l.paintings = 0x2F90u;
            l.eberry = 0x31F8u;
            l.eberry_size = 0x34u;
            l.wonder_news = 0x322Cu;
            l.mystery_event = 0x3728u;
            l.joyful = 0x1FCu;
            l.external_events = 0x31B3u;
            l.trendy_words = 0x2E20u;
            l.trainer_hill = 0x3718u;
            l.walda = 0x3D70u;
            break;
        case GEN3_GAME_FRLG:
            l.mail = 0x2CD0u;
            l.daycare = 0x2F80u;
            l.daycare_slot_size = (size_t)GEN3_PK3_STORED_SIZE + 0x3Cu;
            l.daycare_seed = 0x3098u;
            l.daycare_seed_bytes = 2u;
            l.egg_event_flag = 0x266u;
            l.roamer = 0x30D0u;
            l.gift_ribbons = 0x309Cu;
            l.external_events = 0x30A7u;
            l.records = 0x1200u;
            l.rival_name = 0x3A4Cu;
            l.eberry = 0x30ECu;
            l.eberry_size = 0x34u;
            l.wonder_news = 0x3120u;
            l.mystery_event = 0x361Cu;
            l.joyful = 0xB00u;
            break;
        default: /* Ruby / Sapphire */
            l.mail = 0x2B4Cu;
            /* Ruby and Sapphire store the daycare's mail elsewhere, so a slot
             * is exactly one record and the experience follows both slots. */
            l.daycare = 0x2F9Cu;
            l.daycare_slot_size = (size_t)GEN3_PK3_STORED_SIZE;
            l.daycare_seed = 0x30B4u;
            l.daycare_seed_bytes = 2u;
            l.egg_event_flag = 0x86u;
            l.roamer = 0x3144u;
            /* The eleven gift ribbons sit immediately before the external
             * event data, which begins at 0x311B. */
            l.gift_ribbons = 0x311Bu - GEN3_GIFT_RIBBON_COUNT;
            l.external_events = 0x311Bu;
            l.records = 0x1540u;
            l.pokeblocks = 0x7F8u;
            l.secret_bases = 0x1A08u;
            l.decorations = 0x26A0u;
            l.swarm = 0x2AFCu;
            l.paintings = 0x2EFCu;
            /* Ruby and Sapphire reserve far more room for the berry program. */
            l.eberry = 0x3160u;
            l.eberry_size = 0x530u;
            l.mystery_event = 0x3690u;
            break;
    }
    for (unsigned slot = 0; slot < GEN3_DAYCARE_SLOTS; ++slot) {
        l.daycare_exp[slot] = save->game == GEN3_GAME_RS || save->game == GEN3_GAME_UNKNOWN
            /* after both slots and both mail records */
            ? l.daycare + GEN3_DAYCARE_SLOTS * l.daycare_slot_size + 2u * 0x38u + 4u * slot
            /* at the end of each slot */
            : l.daycare + (slot + 1u) * l.daycare_slot_size - 4u;
    }
    return l;
}

#define LARGE_SIZE (4u * GEN3_SECTOR_USED)

/* Every offset here is a compile-time constant from the table above, but a
 * corrupt game id must not be able to walk off the buffer. */
static bool large_fits(size_t offset, size_t length) {
    return offset < LARGE_SIZE && length <= LARGE_SIZE - offset;
}

/* ------------------------------------------------------------- daycare --- */

static size_t daycare_slot_offset(const BlockLayout *l, unsigned slot) {
    return l->daycare + (size_t)slot * l->daycare_slot_size;
}

bool gen3_daycare_pokemon(const Gen3Save *save, unsigned slot, Gen3Pokemon *out) {
    if (!save || !out || slot >= GEN3_DAYCARE_SLOTS) return false;
    const BlockLayout l = block_layout(save);
    const size_t ofs = daycare_slot_offset(&l, slot);
    if (!large_fits(ofs, GEN3_PK3_STORED_SIZE)) return false;
    return gen3_parse_pk3_record(save->large + ofs, GEN3_PK3_STORED_SIZE, save->japanese, out);
}

bool gen3_set_daycare_pokemon(Gen3Save *save, unsigned slot, const Gen3Pokemon *pokemon) {
    if (!save || !pokemon || slot >= GEN3_DAYCARE_SLOTS) return false;
    const BlockLayout l = block_layout(save);
    const size_t ofs = daycare_slot_offset(&l, slot);
    if (!large_fits(ofs, GEN3_PK3_STORED_SIZE)) return false;
    return gen3_write_pk3_record(save->large + ofs, GEN3_PK3_STORED_SIZE, pokemon);
}

uint32_t gen3_daycare_exp(const Gen3Save *save, unsigned slot) {
    if (!save || slot >= GEN3_DAYCARE_SLOTS) return 0;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.daycare_exp[slot], 4u)) return 0;
    return rd32(save->large + l.daycare_exp[slot]);
}

bool gen3_set_daycare_exp(Gen3Save *save, unsigned slot, uint32_t experience) {
    if (!save || slot >= GEN3_DAYCARE_SLOTS) return false;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.daycare_exp[slot], 4u)) return false;
    wr32(save->large + l.daycare_exp[slot], experience);
    return true;
}

uint32_t gen3_daycare_seed(const Gen3Save *save) {
    if (!save) return 0;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.daycare_seed, l.daycare_seed_bytes)) return 0;
    return l.daycare_seed_bytes == 4u ? rd32(save->large + l.daycare_seed)
                                      : rd16(save->large + l.daycare_seed);
}

unsigned gen3_daycare_seed_bits(const Gen3Save *save) {
    return save ? block_layout(save).daycare_seed_bytes * 8u : 0u;
}

bool gen3_set_daycare_seed(Gen3Save *save, uint32_t seed) {
    if (!save) return false;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.daycare_seed, l.daycare_seed_bytes)) return false;
    if (l.daycare_seed_bytes == 4u) wr32(save->large + l.daycare_seed, seed);
    else wr16(save->large + l.daycare_seed, (uint16_t)seed);
    return true;
}

bool gen3_daycare_egg_waiting(const Gen3Save *save) {
    if (!save) return false;
    return gen3_event_flag(save, block_layout(save).egg_event_flag);
}

bool gen3_set_daycare_egg_waiting(Gen3Save *save, bool waiting) {
    if (!save) return false;
    gen3_set_event_flag(save, block_layout(save).egg_event_flag, waiting);
    return true;
}

/* -------------------------------------------------------------- roamer --- */

static void unpack_ivs(uint32_t iv32, uint8_t out[6]) {
    for (unsigned i = 0; i < 6u; ++i) out[i] = (uint8_t)((iv32 >> (i * 5u)) & 31u);
}

bool gen3_roamer(const Gen3Save *save, Gen3Roamer *out) {
    if (!save || !out) return false;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.roamer, 0x14u)) return false;
    const uint8_t *r = save->large + l.roamer;
    memset(out, 0, sizeof(*out));
    out->iv32 = rd32(r);
    unpack_ivs(out->iv32, out->ivs);
    out->pid = rd32(r + 4);
    out->species = (uint16_t)gen3_species_national(rd16(r + 8));
    out->hp_current = rd16(r + 10);
    out->level = r[12];
    out->status = r[13];
    for (unsigned i = 0; i < 5u; ++i) out->contest[i] = r[14 + i];
    out->active = r[0x13] == 1u;
    return true;
}

bool gen3_set_roamer(Gen3Save *save, const Gen3Roamer *roamer) {
    if (!save || !roamer) return false;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.roamer, 0x14u)) return false;
    uint8_t *r = save->large + l.roamer;
    uint32_t iv32 = 0;
    for (unsigned i = 0; i < 6u; ++i) iv32 |= (uint32_t)(roamer->ivs[i] & 31u) << (i * 5u);
    wr32(r, iv32);
    wr32(r + 4, roamer->pid);
    wr16(r + 8, gen3_species_internal_from_national(roamer->species));
    wr16(r + 10, roamer->hp_current);
    r[12] = roamer->level;
    r[13] = roamer->status;
    for (unsigned i = 0; i < 5u; ++i) r[14 + i] = roamer->contest[i];
    r[0x13] = roamer->active ? 1u : 0u;
    return true;
}

bool gen3_roamer_ivs_are_glitched(const Gen3Save *save) {
    /* Emerald fixed the bug; everything else still loads a single IV byte. */
    return save && save->game != GEN3_GAME_EMERALD;
}

void gen3_roamer_encounter_ivs(const Gen3Roamer *roamer, bool glitched, uint8_t out[6]) {
    if (!roamer || !out) return;
    unpack_ivs(glitched ? (roamer->iv32 & 0xFFu) : roamer->iv32, out);
}

/* ---------------------------------------------------------------- mail --- */

bool gen3_item_is_mail(uint16_t item_id) { return item_id >= 121u && item_id <= 132u; }

bool gen3_mail(const Gen3Save *save, unsigned slot, Gen3Mail *out) {
    if (!save || !out || slot >= GEN3_MAIL_SLOTS) return false;
    const BlockLayout l = block_layout(save);
    const size_t ofs = l.mail + (size_t)slot * GEN3_MAIL_SIZE;
    if (!large_fits(ofs, GEN3_MAIL_SIZE)) return false;
    const uint8_t *m = save->large + ofs;
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < GEN3_MAIL_WORDS; ++i) out->words[i] = rd16(m + i * 2u);
    gen3_decode_text(m + 0x12, 7, save->japanese, out->author, sizeof(out->author));
    out->author_tid = rd16(m + 0x1A);
    out->author_sid = rd16(m + 0x1C);
    out->appear_species = (uint16_t)gen3_species_national(rd16(m + 0x1E));
    out->mail_item = rd16(m + 0x20);
    out->present = gen3_item_is_mail(out->mail_item);
    return true;
}

bool gen3_clear_mail(Gen3Save *save, unsigned slot) {
    if (!save || slot >= GEN3_MAIL_SLOTS) return false;
    const BlockLayout l = block_layout(save);
    const size_t ofs = l.mail + (size_t)slot * GEN3_MAIL_SIZE;
    if (!large_fits(ofs, GEN3_MAIL_SIZE)) return false;
    uint8_t *m = save->large + ofs;
    /* A blank sheet: every word unset, no author, and the species the games
     * default to rather than zero. */
    memset(m, 0, GEN3_MAIL_SIZE);
    for (unsigned i = 0; i < GEN3_MAIL_WORDS; ++i) wr16(m + i * 2u, 0xFFFFu);
    memset(m + 0x12, GEN3_TEXT_TERMINATOR, 8);
    wr16(m + 0x1E, 1u);
    wr16(m + 0x20, 0u);
    return true;
}

/* -------------------------------------------------------- hall of fame --- */

#define HOF_MEMBER_SIZE 0x14u
#define HOF_ENTRY_SIZE (GEN3_HOF_TEAM_SIZE * HOF_MEMBER_SIZE)
#define HOF_SECTOR_A 0x1C000u
#define HOF_SECTOR_B 0x1D000u

bool gen3_hof_available(const Gen3Save *save) {
    return save && save->raw && save->raw_size >= GEN3_RAW_SIZE;
}

/*
 * The two sectors are contiguous once the 0x80 bytes of sector footer are
 * dropped, so an entry can straddle the boundary. Copy the member out rather
 * than pointing into either sector.
 */
static bool hof_read_member(const Gen3Save *save, size_t byte_offset, uint8_t out[HOF_MEMBER_SIZE]) {
    for (unsigned i = 0; i < HOF_MEMBER_SIZE; ++i) {
        const size_t at = byte_offset + i;
        if (at >= 2u * GEN3_SECTOR_USED) return false;
        const size_t base = at < GEN3_SECTOR_USED ? HOF_SECTOR_A + at
                                                  : HOF_SECTOR_B + (at - GEN3_SECTOR_USED);
        out[i] = save->raw[base];
    }
    return true;
}

bool gen3_hof_member(const Gen3Save *save, unsigned entry, unsigned index, Gen3HofMember *out) {
    if (!gen3_hof_available(save) || !out) return false;
    if (entry >= GEN3_HOF_ENTRIES || index >= GEN3_HOF_TEAM_SIZE) return false;
    uint8_t m[HOF_MEMBER_SIZE];
    if (!hof_read_member(save, (size_t)entry * HOF_ENTRY_SIZE + (size_t)index * HOF_MEMBER_SIZE, m))
        return false;

    memset(out, 0, sizeof(*out));
    const uint32_t id32 = rd32(m);
    out->tid = (uint16_t)id32;
    out->sid = (uint16_t)(id32 >> 16);
    out->pid = rd32(m + 4);
    /* Species and level share a halfword: nine bits of internal species, then
     * the level in the top seven. */
    const uint16_t spec_level = rd16(m + 8);
    out->species = (uint16_t)gen3_species_national((uint16_t)(spec_level & 0x1FFu));
    out->level = (uint8_t)(spec_level >> 9);
    gen3_decode_text(m + 10, 10, save->japanese, out->nickname, sizeof(out->nickname));
    out->present = out->species != 0 && out->level != 0;
    if (out->present) {
        const uint32_t x = id32 ^ out->pid;
        out->shiny = ((x >> 16) ^ (x & 0xFFFFu)) < 8u;
    }
    return true;
}

unsigned gen3_hof_entry_count(const Gen3Save *save) {
    if (!gen3_hof_available(save)) return 0;
    for (unsigned entry = 0; entry < GEN3_HOF_ENTRIES; ++entry) {
        Gen3HofMember first;
        if (!gen3_hof_member(save, entry, 0, &first) || !first.present)
            return entry;
    }
    return GEN3_HOF_ENTRIES;
}

/* ------------------------------------------------------------- records --- */

uint32_t gen3_record(const Gen3Save *save, unsigned index) {
    if (!save || index >= gen3_record_count(save->game)) return 0;
    const BlockLayout l = block_layout(save);
    const size_t ofs = l.records + (size_t)index * 4u;
    if (!large_fits(ofs, 4u)) return 0;
    return rd32(save->large + ofs);
}

bool gen3_set_record(Gen3Save *save, unsigned index, uint32_t value) {
    if (!save || index >= gen3_record_count(save->game)) return false;
    const BlockLayout l = block_layout(save);
    const size_t ofs = l.records + (size_t)index * 4u;
    if (!large_fits(ofs, 4u)) return false;
    wr32(save->large + ofs, value);
    return true;
}

/* ---------------------------------------------------------- rival name --- */

bool gen3_has_rival_name(const Gen3Save *save) {
    return save && block_layout(save).rival_name != 0;
}

void gen3_rival_name(const Gen3Save *save, char *out, size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    if (!gen3_has_rival_name(save)) return;
    gen3_decode_text(save->large + block_layout(save).rival_name, GEN3_RIVAL_NAME_LEN,
                     save->japanese, out, out_size);
}

void gen3_rival_name_raw(const Gen3Save *save, uint8_t out[GEN3_RIVAL_NAME_LEN]) {
    if (!out) return;
    memset(out, GEN3_TEXT_TERMINATOR, GEN3_RIVAL_NAME_LEN);
    if (!gen3_has_rival_name(save)) return;
    memcpy(out, save->large + block_layout(save).rival_name, GEN3_RIVAL_NAME_LEN);
}

bool gen3_set_rival_name(Gen3Save *save, const uint8_t *raw, size_t len) {
    if (!raw || !gen3_has_rival_name(save) || len > GEN3_RIVAL_NAME_LEN) return false;
    uint8_t *dst = save->large + block_layout(save).rival_name;
    memcpy(dst, raw, len);
    for (size_t i = len; i < GEN3_RIVAL_NAME_LEN; ++i) dst[i] = GEN3_TEXT_TERMINATOR;
    return true;
}

/* ---------------------------------------------------------- pokeblocks --- */

#define POKEBLOCK_SIZE 8u

bool gen3_has_pokeblocks(const Gen3Save *save) {
    return save && block_layout(save).pokeblocks != 0;
}

bool gen3_pokeblock(const Gen3Save *save, unsigned index, Gen3PokeBlock *out) {
    if (!out || !gen3_has_pokeblocks(save) || index >= GEN3_POKEBLOCK_COUNT) return false;
    const size_t ofs = block_layout(save).pokeblocks + (size_t)index * POKEBLOCK_SIZE;
    if (!large_fits(ofs, POKEBLOCK_SIZE)) return false;
    const uint8_t *b = save->large + ofs;
    out->color = b[0]; out->spicy = b[1]; out->dry = b[2]; out->sweet = b[3];
    out->bitter = b[4]; out->sour = b[5]; out->feel = b[6];
    return true;
}

bool gen3_set_pokeblock(Gen3Save *save, unsigned index, const Gen3PokeBlock *block) {
    if (!block || !gen3_has_pokeblocks(save) || index >= GEN3_POKEBLOCK_COUNT) return false;
    if (block->color >= GEN3_POKEBLOCK_COLOR_COUNT) return false;
    const size_t ofs = block_layout(save).pokeblocks + (size_t)index * POKEBLOCK_SIZE;
    if (!large_fits(ofs, POKEBLOCK_SIZE)) return false;
    uint8_t *b = save->large + ofs;
    b[0] = block->color; b[1] = block->spicy; b[2] = block->dry; b[3] = block->sweet;
    b[4] = block->bitter; b[5] = block->sour; b[6] = block->feel;
    b[7] = 0;   /* alignment byte the games leave clear */
    return true;
}

const char *gen3_pokeblock_color_name(uint8_t color) {
    static const char *const names[GEN3_POKEBLOCK_COLOR_COUNT] = {
        "None", "Red", "Blue", "Pink", "Green", "Yellow", "Purple", "Indigo",
        "Brown", "Light Blue", "Olive", "Gray", "Black", "White", "Gold",
    };
    return color < GEN3_POKEBLOCK_COLOR_COUNT ? names[color] : "?";
}

/* ----------------------------------------------------------- game clock --- */

/* The clocks live in the small block, which every game has, but only the
 * Hoenn games keep one. */
#define CLOCK_INITIAL 0x098u
#define CLOCK_ELAPSED 0x0A0u

bool gen3_has_clock(const Gen3Save *save) {
    return save && save->game != GEN3_GAME_FRLG;
}

bool gen3_clock(const Gen3Save *save, bool elapsed, Gen3Clock *out) {
    if (!out || !gen3_has_clock(save)) return false;
    const uint8_t *c = save->small + (elapsed ? CLOCK_ELAPSED : CLOCK_INITIAL);
    out->day = rd16(c);
    out->hour = c[2];
    out->minute = c[3];
    out->second = c[4];
    return true;
}

bool gen3_set_clock(Gen3Save *save, bool elapsed, const Gen3Clock *clock) {
    if (!clock || !gen3_has_clock(save)) return false;
    if (clock->hour > 23u || clock->minute > 59u || clock->second > 59u) return false;
    uint8_t *c = save->small + (elapsed ? CLOCK_ELAPSED : CLOCK_INITIAL);
    wr16(c, clock->day);
    c[2] = clock->hour;
    c[3] = clock->minute;
    c[4] = clock->second;
    return true;
}

/* --------------------------------------------------------- secret bases --- */

#define SECRET_BASE_SIZE 160u

bool gen3_has_secret_bases(const Gen3Save *save) {
    return save && block_layout(save).secret_bases != 0;
}

bool gen3_secret_base(const Gen3Save *save, unsigned index, Gen3SecretBase *out) {
    if (!out || !gen3_has_secret_bases(save) || index >= GEN3_SECRET_BASE_COUNT) return false;
    const size_t ofs = block_layout(save).secret_bases + (size_t)index * SECRET_BASE_SIZE;
    if (!large_fits(ofs, SECRET_BASE_SIZE)) return false;
    const uint8_t *b = save->large + ofs;
    memset(out, 0, sizeof(*out));
    out->location = b[0];
    out->ot_gender = (uint8_t)((b[1] >> 4) & 1u);
    out->battled_today = ((b[1] >> 5) & 1u) != 0;
    out->registry_status = (uint8_t)((b[1] >> 6) & 3u);
    gen3_decode_text(b + 2, 7, save->japanese, out->ot_name, sizeof(out->ot_name));
    /* The trainer class and the ID share bytes 9-12: the class is the low byte
     * modulo five, and the IDs read the same run as halfwords. PKHeX exposes
     * both readings, and so does this. */
    out->ot_class = (uint8_t)(b[9] % 5u);
    out->tid = rd16(b + 9);
    out->sid = rd16(b + 11);
    out->language = b[0x0D];
    out->received = rd16(b + 0x0E);
    out->times_entered = b[0x10];
    /*
     * PKHeX calls a base occupied when its trainer name does not begin with
     * the terminator. That misses one case: a slot the game has never written
     * is all zeroes, and zero is a space rather than a terminator, so it would
     * read as a base belonging to a trainer named "       ". An all-zero
     * record is treated as free too.
     */
    out->present = b[2] != GEN3_TEXT_TERMINATOR;
    if (out->present) {
        bool all_zero = true;
        for (unsigned i = 0; i < SECRET_BASE_SIZE; ++i)
            if (b[i]) { all_zero = false; break; }
        if (all_zero) out->present = false;
    }
    return true;
}

/*
 * The defending team sits 0x34 into the base record: six PIDs, then six sets
 * of four moves, then species, held item, level and the shared EV value, each
 * as its own run rather than six contiguous records.
 */
#define SECRET_TEAM_AT 0x34u

bool gen3_secret_base_mon(const Gen3Save *save, unsigned base, unsigned slot,
                          Gen3SecretBaseMon *out) {
    if (!out || !gen3_has_secret_bases(save)) return false;
    if (base >= GEN3_SECRET_BASE_COUNT || slot >= GEN3_SECRET_BASE_TEAM) return false;
    const size_t ofs = block_layout(save).secret_bases + (size_t)base * SECRET_BASE_SIZE;
    if (!large_fits(ofs, SECRET_BASE_SIZE)) return false;
    const uint8_t *t = save->large + ofs + SECRET_TEAM_AT;
    memset(out, 0, sizeof(*out));
    out->pid = rd32(t + slot * 4u);
    for (unsigned m = 0; m < 4u; ++m)
        out->moves[m] = rd16(t + 0x18u + slot * 8u + m * 2u);
    out->species = (uint16_t)gen3_species_national(rd16(t + 0x48u + slot * 2u));
    out->held_item = rd16(t + 0x54u + slot * 2u);
    out->level = t[0x60u + slot];
    out->ev_all = t[0x66u + slot];
    out->present = out->species != 0;
    return true;
}

bool gen3_clear_secret_base(Gen3Save *save, unsigned index) {
    if (!gen3_has_secret_bases(save) || index >= GEN3_SECRET_BASE_COUNT) return false;
    const size_t ofs = block_layout(save).secret_bases + (size_t)index * SECRET_BASE_SIZE;
    if (!large_fits(ofs, SECRET_BASE_SIZE)) return false;
    memset(save->large + ofs, 0, SECRET_BASE_SIZE);
    return true;
}

const char *gen3_secret_base_class_name(uint8_t trainer_class) {
    static const char *const names[5] = {
        "Rich Boy / Lady", "Youngster / Lass", "Bug Catcher / Schoolkid",
        "Camper / Picnicker", "Ace Trainer",
    };
    return trainer_class < 5u ? names[trainer_class] : "?";
}

/* --------------------------------------------------------- decorations --- */

typedef struct DecoKind { uint8_t offset, count; const char *name; } DecoKind;

static const DecoKind deco_kinds[GEN3_DECO_KIND_COUNT] = {
    {   0, 10, "Desks"     },
    {  10, 10, "Chairs"    },
    {  20, 10, "Plants"    },
    {  30, 30, "Ornaments" },
    {  60, 30, "Mats"      },
    {  90, 10, "Posters"   },
    { 100, 40, "Dolls"     },
    { 140, 10, "Cushions"  },
};

bool gen3_has_decorations(const Gen3Save *save) {
    return save && block_layout(save).decorations != 0;
}

const char *gen3_decoration_kind_name(Gen3DecorationKind kind) {
    return kind < GEN3_DECO_KIND_COUNT ? deco_kinds[kind].name : "?";
}

unsigned gen3_decoration_slot_count(Gen3DecorationKind kind) {
    return kind < GEN3_DECO_KIND_COUNT ? deco_kinds[kind].count : 0u;
}

static bool deco_offset(const Gen3Save *save, Gen3DecorationKind kind, unsigned slot, size_t *out) {
    if (!gen3_has_decorations(save) || kind >= GEN3_DECO_KIND_COUNT) return false;
    if (slot >= deco_kinds[kind].count) return false;
    const size_t ofs = block_layout(save).decorations + deco_kinds[kind].offset + slot;
    if (!large_fits(ofs, 1u)) return false;
    *out = ofs;
    return true;
}

uint8_t gen3_decoration(const Gen3Save *save, Gen3DecorationKind kind, unsigned slot) {
    size_t ofs;
    return deco_offset(save, kind, slot, &ofs) ? save->large[ofs] : 0u;
}

bool gen3_set_decoration(Gen3Save *save, Gen3DecorationKind kind, unsigned slot, uint8_t value) {
    size_t ofs;
    if (!deco_offset(save, kind, slot, &ofs)) return false;
    save->large[ofs] = value;
    return true;
}

/* ------------------------------------------------------ battle frontier --- */

/* The streak block, in the small section. Emerald only. */
#define FRONTIER_BLOCK 0xCDCu
#define FRONTIER_SIZE 0x1DCu
#define FRONTIER_BP 0xEB8u
/* Symbols and the pass are event flags, not part of the block. */
#define FRONTIER_SYMBOL_FLAG(f) (0x860u + 0x64u + (f) * 2u)
#define FRONTIER_PASS_FLAG (0x860u + 0x72u)

bool gen3_has_battle_frontier(const Gen3Save *save) {
    return save && save->game == GEN3_GAME_EMERALD;
}

const char *gen3_facility_name(Gen3Facility facility) {
    static const char *const names[GEN3_FACILITY_COUNT] = {
        "Battle Tower", "Battle Dome", "Battle Palace", "Battle Arena",
        "Battle Factory", "Battle Pike", "Battle Pyramid",
    };
    return facility < GEN3_FACILITY_COUNT ? names[facility] : "?";
}

const char *gen3_frontier_stat_name(Gen3FrontierStat stat) {
    static const char *const names[GEN3_FRONTIER_STAT_COUNT] = {
        "Current streak", "Record streak", "Current swapped", "Record swapped",
        "Championships", "Rooms cleared",
    };
    return stat < GEN3_FRONTIER_STAT_COUNT ? names[stat] : "?";
}

unsigned gen3_facility_mode_count(Gen3Facility facility) {
    switch (facility) {
        case GEN3_FACILITY_TOWER: return 4u;   /* singles, doubles, multi, link */
        case GEN3_FACILITY_DOME:
        case GEN3_FACILITY_PALACE:
        case GEN3_FACILITY_FACTORY: return 2u; /* singles and doubles */
        default: return 1u;                    /* singles only */
    }
}

/*
 * Each facility keeps a different set of statistics at a base offset of its
 * own, striding by mode and then by record type. Offsets follow PKHeX's
 * BattleFrontier3.
 */
static bool frontier_offset(Gen3Facility facility, unsigned mode, unsigned record,
                            Gen3FrontierStat stat, size_t *out) {
    if (facility >= GEN3_FACILITY_COUNT || record > 1u) return false;
    const unsigned modes = gen3_facility_mode_count(facility);
    if (mode >= modes) mode = modes - 1u;

    size_t base;
    bool strides_by_mode = true;
    switch (facility) {
        case GEN3_FACILITY_TOWER:
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x004u;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x014u;
            else return false;
            break;
        case GEN3_FACILITY_DOME:
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x030u;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x038u;
            else if (stat == GEN3_FRONTIER_CHAMPIONSHIPS) base = 0x040u;
            else return false;
            break;
        case GEN3_FACILITY_PALACE:
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x0ECu;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x0F4u;
            else return false;
            break;
        case GEN3_FACILITY_ARENA:
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x0FEu;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x102u;
            else return false;
            strides_by_mode = false;
            break;
        case GEN3_FACILITY_FACTORY:
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x106u;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x10Eu;
            else if (stat == GEN3_FRONTIER_CURRENT_SWAPPED) base = 0x116u;
            else if (stat == GEN3_FRONTIER_RECORD_SWAPPED) base = 0x11Eu;
            else return false;
            break;
        case GEN3_FACILITY_PIKE:
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x128u;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x12Cu;
            else if (stat == GEN3_FRONTIER_RECORD_CLEARED) base = 0x130u;
            else return false;
            strides_by_mode = false;
            break;
        default: /* pyramid */
            if (stat == GEN3_FRONTIER_CURRENT) base = 0x13Eu;
            else if (stat == GEN3_FRONTIER_RECORD) base = 0x142u;
            else return false;
            strides_by_mode = false;
            break;
    }
    const size_t at = base + (strides_by_mode ? 4u * mode : 0u) + 2u * record;
    if (at + 2u > FRONTIER_SIZE) return false;
    *out = FRONTIER_BLOCK + at;
    return true;
}

bool gen3_facility_has_stat(Gen3Facility facility, Gen3FrontierStat stat) {
    size_t ignored;
    return frontier_offset(facility, 0, 0, stat, &ignored);
}

bool gen3_frontier_stat(const Gen3Save *save, Gen3Facility facility, unsigned mode,
                        unsigned record, Gen3FrontierStat stat, uint16_t *out) {
    size_t ofs;
    if (!out || !gen3_has_battle_frontier(save)) return false;
    if (!frontier_offset(facility, mode, record, stat, &ofs)) return false;
    if (ofs + 2u > GEN3_SECTOR_USED) return false;
    *out = rd16(save->small + ofs);
    return true;
}

bool gen3_set_frontier_stat(Gen3Save *save, Gen3Facility facility, unsigned mode,
                            unsigned record, Gen3FrontierStat stat, uint16_t value) {
    size_t ofs;
    if (!gen3_has_battle_frontier(save)) return false;
    if (!frontier_offset(facility, mode, record, stat, &ofs)) return false;
    if (ofs + 2u > GEN3_SECTOR_USED) return false;
    wr16(save->small + ofs, value);
    return true;
}

uint8_t gen3_frontier_symbol(const Gen3Save *save, Gen3Facility facility) {
    if (!gen3_has_battle_frontier(save) || facility >= GEN3_FACILITY_COUNT) return 0;
    const unsigned silver = FRONTIER_SYMBOL_FLAG(facility);
    if (gen3_event_flag(save, silver + 1u)) return 2u;
    return gen3_event_flag(save, silver) ? 1u : 0u;
}

bool gen3_set_frontier_symbol(Gen3Save *save, Gen3Facility facility, uint8_t level) {
    if (!gen3_has_battle_frontier(save) || facility >= GEN3_FACILITY_COUNT || level > 2u) return false;
    const unsigned silver = FRONTIER_SYMBOL_FLAG(facility);
    /* Gold implies silver, which is how the game stores it. */
    gen3_set_event_flag(save, silver, level >= 1u);
    gen3_set_event_flag(save, silver + 1u, level == 2u);
    return true;
}

bool gen3_frontier_pass(const Gen3Save *save) {
    return gen3_has_battle_frontier(save) && gen3_event_flag(save, FRONTIER_PASS_FLAG);
}

bool gen3_set_frontier_pass(Gen3Save *save, bool active) {
    if (!gen3_has_battle_frontier(save)) return false;
    gen3_set_event_flag(save, FRONTIER_PASS_FLAG, active);
    return true;
}

uint16_t gen3_battle_points(const Gen3Save *save) {
    if (!gen3_has_battle_frontier(save)) return 0;
    return rd16(save->small + FRONTIER_BP);
}

bool gen3_set_battle_points(Gen3Save *save, uint16_t points) {
    if (!gen3_has_battle_frontier(save)) return false;
    if (points > 9999u) points = 9999u;
    wr16(save->small + FRONTIER_BP, points);
    return true;
}

/* ---------------------------------------------------------------- swarm --- */

#define SWARM_SIZE 0x14u

bool gen3_has_swarm(const Gen3Save *save) {
    return save && block_layout(save).swarm != 0;
}

bool gen3_swarm(const Gen3Save *save, Gen3Swarm *out) {
    if (!out || !gen3_has_swarm(save)) return false;
    const size_t ofs = block_layout(save).swarm;
    if (!large_fits(ofs, SWARM_SIZE)) return false;
    const uint8_t *b = save->large + ofs;
    memset(out, 0, sizeof(*out));
    out->species = (uint16_t)gen3_species_national(rd16(b));
    out->map_num = b[2];
    out->map_group = b[3];
    out->level = b[4];
    for (unsigned i = 0; i < 4u; ++i) out->moves[i] = rd16(b + 8u + i * 2u);
    out->probability = b[0x11];
    out->days_left = rd16(b + 0x12);
    out->active = out->species != 0 && out->days_left != 0;
    return true;
}

bool gen3_set_swarm(Gen3Save *save, const Gen3Swarm *swarm) {
    if (!swarm || !gen3_has_swarm(save)) return false;
    const size_t ofs = block_layout(save).swarm;
    if (!large_fits(ofs, SWARM_SIZE)) return false;
    uint8_t *b = save->large + ofs;
    wr16(b, gen3_species_internal_from_national(swarm->species));
    b[2] = swarm->map_num;
    b[3] = swarm->map_group;
    b[4] = swarm->level;
    for (unsigned i = 0; i < 4u; ++i) wr16(b + 8u + i * 2u, swarm->moves[i]);
    b[0x11] = swarm->probability > 100u ? 100u : swarm->probability;
    wr16(b + 0x12, swarm->days_left);
    return true;
}

bool gen3_clear_swarm(Gen3Save *save) {
    if (!gen3_has_swarm(save)) return false;
    const size_t ofs = block_layout(save).swarm;
    if (!large_fits(ofs, SWARM_SIZE)) return false;
    memset(save->large + ofs, 0, SWARM_SIZE);
    return true;
}

/* ------------------------------------------------------ e-reader berry --- */

bool gen3_has_eberry(const Gen3Save *save) {
    if (!save) return false;
    const BlockLayout l = block_layout(save);
    return l.eberry != 0 && large_fits(l.eberry, l.eberry_size);
}

void gen3_eberry_name(const Gen3Save *save, char *out, size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    if (!gen3_has_eberry(save)) return;
    gen3_decode_text(save->large + block_layout(save).eberry, 7, save->japanese, out, out_size);
}

bool gen3_eberry_is_enigma(const Gen3Save *save) {
    if (!gen3_has_eberry(save)) return false;
    /* A blank first byte means no berry was ever loaded, and the games then
     * fall back to the Enigma Berry, which they cannot resolve. */
    const uint8_t first = save->large[block_layout(save).eberry];
    return first == 0u || first == GEN3_TEXT_TERMINATOR;
}

/* ------------------------------------------------------------ paintings --- */

#define PAINTING_SIZE 0x20u

bool gen3_has_paintings(const Gen3Save *save) {
    return save && block_layout(save).paintings != 0;
}

bool gen3_painting(const Gen3Save *save, unsigned index, Gen3Painting *out) {
    if (!out || !gen3_has_paintings(save) || index >= GEN3_PAINTING_COUNT) return false;
    const size_t ofs = block_layout(save).paintings + (size_t)index * PAINTING_SIZE;
    if (!large_fits(ofs, PAINTING_SIZE)) return false;
    const uint8_t *b = save->large + ofs;
    memset(out, 0, sizeof(*out));
    out->pid = rd32(b);
    out->tid = rd16(b + 4);
    out->sid = rd16(b + 6);
    out->species = (uint16_t)gen3_species_national(rd16(b + 8));
    out->caption = b[0x0A];
    gen3_decode_text(b + 0x0B, 10, save->japanese, out->nickname, sizeof(out->nickname));
    gen3_decode_text(b + 0x16, 7, save->japanese, out->ot_name, sizeof(out->ot_name));
    out->present = out->species != 0;
    return true;
}

/* ------------------------------------------------------- mystery gift --- */

/*
 * Both blocks are checksummed with the same reflected CRC-16: seed 0x1121,
 * polynomial 0x8408, taken over everything past the stored checksum word and
 * complemented. The table is generated rather than transcribed, which is one
 * fewer 256-entry list to get wrong.
 */
static uint16_t mystery_crc(const uint8_t *data, size_t len) {
    static uint16_t table[256];
    static bool built = false;
    if (!built) {
        for (unsigned i = 0; i < 256u; ++i) {
            uint16_t c = (uint16_t)i;
            for (unsigned bit = 0; bit < 8u; ++bit)
                c = (uint16_t)((c & 1u) ? ((c >> 1) ^ 0x8408u) : (c >> 1));
            table[i] = c;
        }
        built = true;
    }
    uint16_t chk = 0x1121u;
    for (size_t i = 0; i < len; ++i)
        chk = (uint16_t)(table[(uint8_t)(data[i] ^ (uint8_t)chk)] ^ (chk >> 8));
    return (uint16_t)~chk;
}

/* 4 bytes of header plus 332 of card; the Japanese layout is smaller, and
 * Wonder News before it shrinks by the same reasoning. */
#define WONDER_NEWS_SIZE(jp) ((jp) ? (4u + 224u) : (4u + 444u))
#define WONDER_CARD_SIZE(jp) ((jp) ? (4u + 164u) : (4u + 332u))
#define WONDER_CARD_TITLE(jp) ((jp) ? 18u : 40u)
#define MYSTERY_EVENT_SIZE (4u + 1000u)

bool gen3_has_wonder_card(const Gen3Save *save) {
    if (!save) return false;
    const BlockLayout l = block_layout(save);
    if (!l.wonder_news) return false;
    const size_t at = l.wonder_news + WONDER_NEWS_SIZE(save->japanese);
    return large_fits(at, WONDER_CARD_SIZE(save->japanese));
}

static size_t wonder_card_offset(const Gen3Save *save) {
    return block_layout(save).wonder_news + WONDER_NEWS_SIZE(save->japanese);
}

bool gen3_wonder_card(const Gen3Save *save, Gen3WonderCard *out) {
    if (!out || !gen3_has_wonder_card(save)) return false;
    const size_t at = wonder_card_offset(save);
    const size_t size = WONDER_CARD_SIZE(save->japanese);
    const uint8_t *c = save->large + at;
    memset(out, 0, sizeof(*out));
    out->checksum_ok = rd16(c) == mystery_crc(c + 4, size - 4u);
    out->card_id = rd16(c + 4);
    out->icon = rd16(c + 6);
    out->count = rd16(c + 8);
    out->type = (uint8_t)(c[0x0C] & 3u);
    out->color = (uint8_t)((c[0x0C] >> 2) & 0x0Fu);
    gen3_decode_text(c + 0x0E, WONDER_CARD_TITLE(save->japanese), save->japanese,
                     out->title, sizeof(out->title));
    /* An empty slot is all zeroes, which has neither an id nor a valid CRC. */
    out->present = out->card_id != 0 && out->checksum_ok;
    return true;
}


bool gen3_wonder_card_data_valid(const uint8_t *data, size_t length, bool japanese) {
    if (!data || length != WONDER_CARD_SIZE(japanese)) return false;
    /* An all-zero block is an empty slot, not a card. */
    if (rd16(data + 4) == 0u) return false;
    return rd16(data) == mystery_crc(data + 4, length - 4u);
}

bool gen3_set_wonder_card(Gen3Save *save, const uint8_t *data, size_t length) {
    if (!gen3_has_wonder_card(save)) return false;
    if (!gen3_wonder_card_data_valid(data, length, save->japanese)) return false;
    memcpy(save->large + wonder_card_offset(save), data, length);
    return true;
}

bool gen3_clear_wonder_card(Gen3Save *save) {
    if (!gen3_has_wonder_card(save)) return false;
    memset(save->large + wonder_card_offset(save), 0, WONDER_CARD_SIZE(save->japanese));
    return true;
}

bool gen3_has_mystery_event(const Gen3Save *save) {
    if (!save) return false;
    const BlockLayout l = block_layout(save);
    return l.mystery_event != 0 && large_fits(l.mystery_event, MYSTERY_EVENT_SIZE);
}

bool gen3_mystery_event_checksum_ok(const Gen3Save *save) {
    if (!gen3_has_mystery_event(save)) return false;
    const uint8_t *e = save->large + block_layout(save).mystery_event;
    return rd16(e) == mystery_crc(e + 4, MYSTERY_EVENT_SIZE - 4u);
}

bool gen3_mystery_event_present(const Gen3Save *save) {
    if (!gen3_has_mystery_event(save)) return false;
    const uint8_t *e = save->large + block_layout(save).mystery_event;
    for (unsigned i = 0; i < MYSTERY_EVENT_SIZE; ++i)
        if (e[i]) return true;
    return false;
}

bool gen3_clear_mystery_event(Gen3Save *save) {
    if (!gen3_has_mystery_event(save)) return false;
    memset(save->large + block_layout(save).mystery_event, 0, MYSTERY_EVENT_SIZE);
    return true;
}



/* ------------------------------------------- external event data --- */

/*
 * Offsets from PKHeX's SAV3: the block starts at ExternalEventData, the two
 * PokeCoupon totals are three-byte values seven and eleven bytes in (stored as
 * 32-bit little-endian with a flag byte in the low eight bits), and the flag
 * bytes follow at +0x14.
 */
#define EXTERNAL_FLAGS_AT 0x14u

bool gen3_external_events(const Gen3Save *save, Gen3ExternalEvents *out) {
    if (!save || !out) return false;
    const size_t at = block_layout(save).external_events;
    if (!at) return false;
    const uint8_t *p = save->large + at;

    memset(out, 0, sizeof(*out));
    out->coupons = rd32(p + 7) >> 8;
    const uint32_t raw2 = rd32(p + 11);
    out->coupons_total = raw2 >> 8;
    out->title_gold = (raw2 & (1u << 0)) != 0u;
    out->title_silver = (raw2 & (1u << 1)) != 0u;
    out->title_bronze = (raw2 & (1u << 2)) != 0u;
    out->received_celebi = (raw2 & (1u << 3)) != 0u;

    const uint8_t *flags = p + EXTERNAL_FLAGS_AT;
    out->used_rsbox = (flags[0] & 1u) != 0u;
    out->rsbox_eggs = (uint8_t)((flags[0] >> 1) & 3u);
    out->received_jirachi = (flags[2] & 1u) != 0u;
    return true;
}

bool gen3_set_external_event_flags(Gen3Save *save, const Gen3ExternalEvents *in) {
    if (!save || !in) return false;
    const size_t at = block_layout(save).external_events;
    if (!at) return false;
    uint8_t *p = save->large + at;

    /* The four bonus disc bits live in the low byte of the lifetime total, so
     * they are rewritten without touching the count above them. */
    uint32_t raw2 = rd32(p + 11);
    raw2 = (raw2 & 0xFFFFFF00u)
         | (in->title_gold ? 1u << 0 : 0u)
         | (in->title_silver ? 1u << 1 : 0u)
         | (in->title_bronze ? 1u << 2 : 0u)
         | (in->received_celebi ? 1u << 3 : 0u);
    wr32(p + 11, raw2);

    uint8_t *flags = p + EXTERNAL_FLAGS_AT;
    flags[0] = (uint8_t)((flags[0] & ~0x07u)
                         | (in->used_rsbox ? 1u : 0u)
                         | (uint8_t)((in->rsbox_eggs & 3u) << 1));
    if (in->received_jirachi) flags[2] |= 1u;
    else flags[2] = (uint8_t)(flags[2] & ~1u);
    return true;
}

/* ------------------------------------------------- Emerald extras --- */

/*
 * Offsets from PKHeX's SaveBlock3LargeE: the trendy words at 0x2E20 as a bit
 * per word, the four Trainer Hill times at 0x3718, and Walda's wallpaper at
 * 0x3D70. Ruby, Sapphire, FireRed and LeafGreen have none of them.
 */
bool gen3_has_emerald_extras(const Gen3Save *save) {
    return save && block_layout(save).trainer_hill != 0;
}

uint32_t gen3_trainer_hill_record(const Gen3Save *save, unsigned mode) {
    if (!gen3_has_emerald_extras(save) || mode >= GEN3_TRAINER_HILL_MODES) return 0;
    return rd32(save->large + block_layout(save).trainer_hill + mode * 4u);
}

bool gen3_set_trainer_hill_record(Gen3Save *save, unsigned mode, uint32_t frames) {
    if (!gen3_has_emerald_extras(save) || mode >= GEN3_TRAINER_HILL_MODES) return false;
    wr32(save->large + block_layout(save).trainer_hill + mode * 4u, frames);
    return true;
}

bool gen3_trendy_word(const Gen3Save *save, unsigned word) {
    if (!gen3_has_emerald_extras(save) || word >= GEN3_TRENDY_WORD_COUNT) return false;
    const uint8_t *p = save->large + block_layout(save).trendy_words + (word >> 3);
    return (*p & (uint8_t)(1u << (word & 7u))) != 0u;
}

bool gen3_set_trendy_word(Gen3Save *save, unsigned word, bool unlocked) {
    if (!gen3_has_emerald_extras(save) || word >= GEN3_TRENDY_WORD_COUNT) return false;
    uint8_t *p = save->large + block_layout(save).trendy_words + (word >> 3);
    const uint8_t bit = (uint8_t)(1u << (word & 7u));
    if (unlocked) *p |= bit; else *p = (uint8_t)(*p & (uint8_t)~bit);
    return true;
}

bool gen3_walda(const Gen3Save *save, Gen3Walda *out) {
    if (!gen3_has_emerald_extras(save) || !out) return false;
    const uint8_t *p = save->large + block_layout(save).walda;
    out->background = rd16(p + 0x00);
    out->foreground = rd16(p + 0x02);
    out->icon = p[0x14];
    out->pattern = p[0x15];
    out->unlocked = p[0x16] != 0u;
    return true;
}

bool gen3_set_walda(Gen3Save *save, const Gen3Walda *in) {
    if (!gen3_has_emerald_extras(save) || !in) return false;
    uint8_t *p = save->large + block_layout(save).walda;
    wr16(p + 0x00, in->background);
    wr16(p + 0x02, in->foreground);
    p[0x14] = in->icon;
    p[0x15] = in->pattern;
    p[0x16] = in->unlocked ? 1u : 0u;
    return true;
}

/* -------------------------------------------------------- joyful game --- */

bool gen3_has_joyful(const Gen3Save *save) {
    return save && block_layout(save).joyful != 0;
}

const char *gen3_joyful_stat_name(Gen3JoyfulStat stat) {
    static const char *const names[GEN3_JOYFUL_STAT_COUNT] = {
        "Jumps in a row", "5 jumps in a row", "Jump: most players",
        "Jump: best score", "Berries: best score", "Berries in a row",
        "5 berries in a row",
    };
    return stat < GEN3_JOYFUL_STAT_COUNT ? names[stat] : "?";
}

/*
 * The seven values are not evenly spaced and two of them are 32-bit, so the
 * layout is listed rather than computed. Emerald and FireRed/LeafGreen use
 * the same relative layout at different bases, so one table serves both.
 *
 * PKHeX reads the two scores as 16-bit and writes them as 32-bit; both are
 * capped at 99990, which fits either way, so this reads and writes 32 bits
 * consistently rather than reproducing that asymmetry.
 */
typedef struct JoyfulField { uint16_t offset; uint8_t width; uint32_t max; } JoyfulField;

static const JoyfulField joyful_fields[GEN3_JOYFUL_STAT_COUNT] = {
    { 0x00, 2, 9999 },   /* jumps in a row */
    { 0x04, 2, 9999 },   /* five in a row */
    { 0x06, 2, 9999 },   /* most players */
    { 0x0C, 4, 99990 },  /* jump score */
    { 0x10, 4, 99990 },  /* berry score */
    { 0x14, 2, 9999 },   /* berries in a row */
    { 0x16, 2, 9999 },   /* five berries in a row */
};

static bool joyful_field(const Gen3Save *save, Gen3JoyfulStat stat, size_t *out, const JoyfulField **field) {
    if (!gen3_has_joyful(save) || stat >= GEN3_JOYFUL_STAT_COUNT) return false;
    const size_t at = block_layout(save).joyful + joyful_fields[stat].offset;
    if (at + joyful_fields[stat].width > GEN3_SECTOR_USED) return false;
    *out = at;
    *field = &joyful_fields[stat];
    return true;
}

uint32_t gen3_joyful_stat(const Gen3Save *save, Gen3JoyfulStat stat) {
    size_t at; const JoyfulField *f;
    if (!joyful_field(save, stat, &at, &f)) return 0;
    return f->width == 4u ? rd32(save->small + at) : rd16(save->small + at);
}

bool gen3_set_joyful_stat(Gen3Save *save, Gen3JoyfulStat stat, uint32_t value) {
    size_t at; const JoyfulField *f;
    if (!joyful_field(save, stat, &at, &f)) return false;
    if (value > f->max) value = f->max;
    if (f->width == 4u) wr32(save->small + at, value);
    else wr16(save->small + at, (uint16_t)value);
    return true;
}

/* -------------------------------------------------------- gift ribbons --- */

uint8_t gen3_gift_ribbon(const Gen3Save *save, unsigned index) {
    if (!save || index >= GEN3_GIFT_RIBBON_COUNT) return 0;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.gift_ribbons, GEN3_GIFT_RIBBON_COUNT)) return 0;
    return save->large[l.gift_ribbons + index];
}

bool gen3_set_gift_ribbon(Gen3Save *save, unsigned index, uint8_t value) {
    if (!save || index >= GEN3_GIFT_RIBBON_COUNT) return false;
    const BlockLayout l = block_layout(save);
    if (!large_fits(l.gift_ribbons, GEN3_GIFT_RIBBON_COUNT)) return false;
    save->large[l.gift_ribbons + index] = value;
    return true;
}
