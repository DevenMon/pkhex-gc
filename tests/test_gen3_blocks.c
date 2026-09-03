/*
 * Daycare, roamer, mail, Hall of Fame and gift ribbons.
 *
 * Every one of these blocks sits at a different offset in Ruby/Sapphire,
 * Emerald and FireRed/LeafGreen, so the tests build a save for each game and
 * check that a value written through the accessors lands on the byte PKHeX's
 * SaveBlock3Large* names — the offsets are asserted directly, not just
 * round-tripped, since a consistent pair of wrong offsets would round-trip
 * perfectly and still corrupt a real save.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen3.h"
#include "gen3_blocks.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct Expected {
    Gen3Game game;
    const char *name;
    size_t mail, daycare, daycare_slot, exp0, exp1, seed, roamer, gift, records;
    unsigned seed_bytes, egg_flag, record_count;
    size_t pokeblocks, secret_bases, decorations;   /* 0 where the game has none */
    size_t swarm, paintings, eberry, eberry_size;
    size_t wonder_news, mystery_event;   /* wonder_news 0 in Ruby and Sapphire */
    size_t joyful;                       /* 0 in Ruby and Sapphire */
    size_t external_events;              /* what the GameCube games write back */
} Expected;

/* Straight from PKHeX's SaveBlock3LargeRS / E / FRLG. */
static const Expected games[] = {
    { GEN3_GAME_RS, "Ruby / Sapphire",
      0x2B4C, 0x2F9C, 0x50, 0x30AC, 0x30B0, 0x30B4, 0x3144, 0x3110, 0x1540, 2, 0x86, 50,
      0x7F8, 0x1A08, 0x26A0, 0x2AFC, 0x2EFC, 0x3160, 0x530, 0, 0x3690, 0, 0x311B },
    { GEN3_GAME_EMERALD, "Emerald",
      0x2BE0, 0x3030, 0x8C, 0x30B8, 0x3144, 0x3148, 0x31DC, 0x31B3, 0x159C, 4, 0x86, 64,
      0x848, 0x1A9C, 0x2734, 0x2B90, 0x2F90, 0x31F8, 0x34, 0x322C, 0x3728, 0x1FC, 0x31B3 },
    { GEN3_GAME_FRLG, "FireRed / LeafGreen",
      0x2CD0, 0x2F80, 0x8C, 0x3008, 0x3094, 0x3098, 0x30D0, 0x309C, 0x1200, 2, 0x266, 64,
      0, 0, 0, 0, 0, 0x30EC, 0x34, 0x3120, 0x361C, 0xB00, 0x30A7 },
};

static void test_daycare(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    /* A record deposited in slot 1 has to land one slot stride in. */
    Gen3Pokemon in; memset(&in, 0, sizeof(in));
    in.present = true;
    in.pid = 0x1234ABCDu;
    in.tid = 4242; in.sid = 2424;
    in.species_internal = gen3_species_internal_from_national(172); /* Pichu */
    in.experience = 1000;
    for (unsigned i = 0; i < 6u; ++i) in.ivs[i] = (uint8_t)(20u + i);
    memset(in.nickname_raw, GEN3_TEXT_TERMINATOR, sizeof(in.nickname_raw));
    memset(in.ot_raw, GEN3_TEXT_TERMINATOR, sizeof(in.ot_raw));

    assert(gen3_set_daycare_pokemon(&save, 1, &in));
    assert(rd32(save.large + e->daycare + e->daycare_slot) == in.pid);

    Gen3Pokemon out;
    assert(gen3_daycare_pokemon(&save, 1, &out));
    assert(out.present && out.checksum_ok);
    assert(out.pid == in.pid);
    assert(gen3_species_national(out.species_internal) == 172u);
    assert(memcmp(out.ivs, in.ivs, sizeof(in.ivs)) == 0);
    /* Slot 0 is untouched by a write to slot 1. */
    assert(gen3_daycare_pokemon(&save, 0, &out) && !out.present);
    assert(!gen3_set_daycare_pokemon(&save, GEN3_DAYCARE_SLOTS, &in));

    assert(gen3_set_daycare_exp(&save, 0, 0x00ABCDEFu));
    assert(gen3_set_daycare_exp(&save, 1, 0x00FEDCBAu));
    assert(rd32(save.large + e->exp0) == 0x00ABCDEFu);
    assert(rd32(save.large + e->exp1) == 0x00FEDCBAu);
    assert(gen3_daycare_exp(&save, 0) == 0x00ABCDEFu);
    assert(gen3_daycare_exp(&save, 1) == 0x00FEDCBAu);

    /* Emerald widened the seed to 32 bits; the others still store 16. */
    assert(gen3_daycare_seed_bits(&save) == e->seed_bytes * 8u);
    assert(gen3_set_daycare_seed(&save, 0x89ABCDEFu));
    if (e->seed_bytes == 4u) {
        assert(rd32(save.large + e->seed) == 0x89ABCDEFu);
        assert(gen3_daycare_seed(&save) == 0x89ABCDEFu);
    } else {
        assert(rd16(save.large + e->seed) == 0xCDEFu);
        assert(gen3_daycare_seed(&save) == 0xCDEFu);
    }

    assert(!gen3_daycare_egg_waiting(&save));
    assert(gen3_set_daycare_egg_waiting(&save, true));
    assert(gen3_event_flag(&save, e->egg_flag));
    assert(gen3_daycare_egg_waiting(&save));
    assert(gen3_set_daycare_egg_waiting(&save, false));
    assert(!gen3_daycare_egg_waiting(&save));
}

static void test_roamer(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    Gen3Roamer in; memset(&in, 0, sizeof(in));
    in.active = true;
    in.species = 243;           /* Raikou */
    in.pid = 0xDEADBEEFu;
    in.level = 40;
    in.hp_current = 123;
    in.status = 0;
    for (unsigned i = 0; i < 6u; ++i) in.ivs[i] = (uint8_t)(31u - i * 2u);
    for (unsigned i = 0; i < 5u; ++i) in.contest[i] = (uint8_t)(i * 11u);
    assert(gen3_set_roamer(&save, &in));

    const uint8_t *r = save.large + e->roamer;
    assert(rd32(r + 4) == in.pid);
    assert(gen3_species_national(rd16(r + 8)) == 243u);
    assert(rd16(r + 10) == 123u);
    assert(r[12] == 40u && r[0x13] == 1u);

    Gen3Roamer out;
    assert(gen3_roamer(&save, &out));
    assert(out.active && out.species == 243u && out.pid == in.pid);
    assert(out.level == 40u && out.hp_current == 123u);
    assert(memcmp(out.ivs, in.ivs, sizeof(in.ivs)) == 0);
    assert(memcmp(out.contest, in.contest, sizeof(in.contest)) == 0);

    /* Outside Emerald only the low IV byte is loaded when the roamer is met,
     * so the IVs the player sees are not the ones stored. */
    const bool glitched = gen3_roamer_ivs_are_glitched(&save);
    assert(glitched == (e->game != GEN3_GAME_EMERALD));
    uint8_t encounter[6];
    gen3_roamer_encounter_ivs(&out, glitched, encounter);
    if (glitched) {
        /* Only the low byte is loaded: HP keeps all five bits, Attack keeps
         * its bottom three, and everything above bit 7 reads as zero. */
        assert(encounter[0] == (out.iv32 & 31u));
        assert(encounter[1] == ((out.iv32 >> 5) & 7u));
        assert(encounter[2] == 0 && encounter[3] == 0);
        assert(encounter[4] == 0 && encounter[5] == 0);
    } else {
        assert(memcmp(encounter, in.ivs, sizeof(in.ivs)) == 0);
    }
}

static void test_mail(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    uint8_t *m = save.large + e->mail + 3u * GEN3_MAIL_SIZE;
    for (unsigned i = 0; i < GEN3_MAIL_WORDS; ++i) { m[i * 2u] = (uint8_t)i; m[i * 2u + 1u] = 0; }
    gen3_encode_text("SAM", m + 0x12, 7);
    m[0x1A] = 0x39; m[0x1B] = 0x30;   /* TID 12345 */
    m[0x1C] = 0x31; m[0x1D] = 0xD4;   /* SID 54321 */
    m[0x1E] = 25; m[0x1F] = 0;        /* Pikachu, same index in both numberings */
    m[0x20] = 121; m[0x21] = 0;       /* Orange Mail */

    Gen3Mail mail;
    assert(gen3_mail(&save, 3, &mail));
    assert(mail.present);
    assert(mail.mail_item == 121u);
    assert(strcmp(mail.author, "SAM") == 0);
    assert(mail.author_tid == 12345u && mail.author_sid == 54321u);
    assert(mail.appear_species == 25u);
    for (unsigned i = 0; i < GEN3_MAIL_WORDS; ++i) assert(mail.words[i] == i);

    /* A slot with no mail item in it is reported empty, not garbage. */
    assert(gen3_mail(&save, 4, &mail) && !mail.present);
    assert(!gen3_mail(&save, GEN3_MAIL_SLOTS, &mail));

    assert(gen3_clear_mail(&save, 3));
    assert(gen3_mail(&save, 3, &mail) && !mail.present);
    for (unsigned i = 0; i < GEN3_MAIL_WORDS; ++i) assert(mail.words[i] == 0xFFFFu);
    /* Clearing one sheet must not reach the next. */
    assert(save.large[e->mail + 4u * GEN3_MAIL_SIZE] == 0);

    assert(gen3_item_is_mail(121) && gen3_item_is_mail(132));
    assert(!gen3_item_is_mail(120) && !gen3_item_is_mail(133) && !gen3_item_is_mail(0));
}

static void test_gift_ribbons(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;
    for (unsigned i = 0; i < GEN3_GIFT_RIBBON_COUNT; ++i)
        assert(gen3_set_gift_ribbon(&save, i, (uint8_t)(i + 1u)));
    for (unsigned i = 0; i < GEN3_GIFT_RIBBON_COUNT; ++i) {
        assert(save.large[e->gift + i] == i + 1u);
        assert(gen3_gift_ribbon(&save, i) == i + 1u);
    }
    assert(!gen3_set_gift_ribbon(&save, GEN3_GIFT_RIBBON_COUNT, 1));
    assert(gen3_gift_ribbon(&save, GEN3_GIFT_RIBBON_COUNT) == 0);
}

/*
 * The Hall of Fame lives in the two sectors past the main save, outside the
 * rotating block layout, so it is read straight from the raw image.
 */
static void test_hall_of_fame(void)
{
    static uint8_t raw[GEN3_RAW_SIZE];
    memset(raw, 0, sizeof(raw));

    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_EMERALD;
    save.raw = raw;
    save.raw_size = sizeof(raw);
    assert(gen3_hof_available(&save));

    /* Entry 0, member 0, at the start of the first sector. */
    uint8_t *m = raw + 0x1C000u;
    m[0] = 0x39; m[1] = 0x30; m[2] = 0x31; m[3] = 0xD4;   /* TID 12345 / SID 54321 */
    m[4] = 0xEF; m[5] = 0xBE; m[6] = 0xAD; m[7] = 0xDE;   /* PID */
    const uint16_t spec_level = (uint16_t)(gen3_species_internal_from_national(6) | (50u << 9));
    m[8] = (uint8_t)spec_level; m[9] = (uint8_t)(spec_level >> 8);
    gen3_encode_text("CHARRY", m + 10, 10);

    Gen3HofMember member;
    assert(gen3_hof_member(&save, 0, 0, &member));
    assert(member.present);
    assert(member.species == 6u && member.level == 50u);
    assert(member.tid == 12345u && member.sid == 54321u);
    assert(member.pid == 0xDEADBEEFu);
    assert(strcmp(member.nickname, "CHARRY") == 0);

    /* An entry that straddles the sector boundary has to read across it.
     * Each entry is 0x78 bytes, so 0xF80 / 0x78 puts entry 33 across. */
    const size_t boundary_entry = GEN3_SECTOR_USED / (6u * 0x14u);
    const size_t split = boundary_entry * (6u * 0x14u);
    assert(split < GEN3_SECTOR_USED && split + (6u * 0x14u) > GEN3_SECTOR_USED);
    /* Member 5 of that entry starts past the first sector. */
    const size_t member5 = split + 5u * 0x14u;
    assert(member5 >= GEN3_SECTOR_USED);
    uint8_t *b = raw + 0x1D000u + (member5 - GEN3_SECTOR_USED);
    const uint16_t sl2 = (uint16_t)(gen3_species_internal_from_national(384) | (70u << 9));
    b[8] = (uint8_t)sl2; b[9] = (uint8_t)(sl2 >> 8);
    assert(gen3_hof_member(&save, (unsigned)boundary_entry, 5, &member));
    assert(member.species == 384u && member.level == 70u);

    /* Counting stops at the first entry with no team. */
    assert(gen3_hof_entry_count(&save) == 1u);
    assert(!gen3_hof_member(&save, GEN3_HOF_ENTRIES, 0, &member));
    assert(!gen3_hof_member(&save, 0, GEN3_HOF_TEAM_SIZE, &member));

    /* Without the raw image there is nothing to read, and saying so beats
     * returning zeroes that look like an empty Hall of Fame. */
    Gen3Save partial; memset(&partial, 0, sizeof(partial));
    partial.game = GEN3_GAME_EMERALD;
    assert(!gen3_hof_available(&partial));
    assert(!gen3_hof_member(&partial, 0, 0, &member));
}

static void test_records(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    assert(gen3_record_count(e->game) == e->record_count);
    for (unsigned i = 0; i < e->record_count; ++i) {
        assert(gen3_set_record(&save, i, 0x01000000u + i));
        assert(rd32(save.large + e->records + i * 4u) == 0x01000000u + i);
        assert(gen3_record(&save, i) == 0x01000000u + i);
    }
    assert(!gen3_set_record(&save, e->record_count, 1));
    assert(gen3_record(&save, e->record_count) == 0);

    /* Names come from the per-game table; index 5 is Steps in every game. */
    assert(strcmp(gen3_record_name(e->game, 0), "Saved Game") == 0);
    assert(strcmp(gen3_record_name(e->game, 5), "Steps") == 0);
    assert(strcmp(gen3_record_name(e->game, e->record_count), "?") == 0);
}

static void test_rival_name(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    /* FireRed and LeafGreen are the only games that store one. */
    const bool expected = e->game == GEN3_GAME_FRLG;
    assert(gen3_has_rival_name(&save) == expected);

    uint8_t typed[GEN3_RIVAL_NAME_LEN];
    gen3_encode_text("GARY", typed, sizeof(typed));
    if (!expected) {
        assert(!gen3_set_rival_name(&save, typed, 4));
        char nothing[16] = "x";
        gen3_rival_name(&save, nothing, sizeof(nothing));
        assert(nothing[0] == '\0');
        return;
    }
    assert(gen3_set_rival_name(&save, typed, 4));
    assert(save.large[0x3A4C] == 0xC1);  /* G */
    char name[16];
    gen3_rival_name(&save, name, sizeof(name));
    assert(strcmp(name, "GARY") == 0);
    uint8_t back[GEN3_RIVAL_NAME_LEN];
    gen3_rival_name_raw(&save, back);
    assert(memcmp(back, typed, sizeof(typed)) == 0);
    assert(!gen3_set_rival_name(&save, typed, GEN3_RIVAL_NAME_LEN + 1u));
}

/* Met location names are indexed straight by the met location byte, and the
 * cartridge games and the GameCube games use different tables for it. */
static void test_met_locations(void)
{
    assert(strcmp(gen3_met_location_name(0, false), "Littleroot Town") == 0);
    assert(strcmp(gen3_met_location_name(1, false), "Oldale Town") == 0);
    assert(strcmp(gen3_met_location_name(255, false), "(fateful encounter)") == 0);
    assert(strcmp(gen3_met_location_name(254, false), "(in-game trade)") == 0);
    /* The GameCube table is a different set of places at the same indices;
     * index 0 is unused there, and blank entries read as "-" rather than "". */
    assert(strcmp(gen3_met_location_name(3, true), "Phenac City (C)") == 0);
    assert(strcmp(gen3_met_location_name(0, true), "-") == 0);
    assert(strcmp(gen3_met_location_name(0, false), gen3_met_location_name(0, true)) != 0);
    /* Every index has to answer with something printable. */
    for (unsigned i = 0; i < 256u; ++i) {
        for (unsigned gc = 0; gc < 2u; ++gc) {
            const char *n = gen3_met_location_name((uint8_t)i, gc != 0);
            assert(n && n[0]);
        }
    }
}

/* PokeBlocks, secret bases and the decoration inventory are Hoenn-only. */
static void test_hoenn_blocks(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;
    const bool hoenn = e->pokeblocks != 0;

    assert(gen3_has_pokeblocks(&save) == hoenn);
    assert(gen3_has_secret_bases(&save) == hoenn);
    assert(gen3_has_decorations(&save) == hoenn);
    assert(gen3_has_clock(&save) == (e->game != GEN3_GAME_FRLG));

    if (!hoenn) {
        Gen3PokeBlock blk;
        assert(!gen3_pokeblock(&save, 0, &blk));
        Gen3SecretBase base;
        assert(!gen3_secret_base(&save, 0, &base));
        assert(!gen3_set_decoration(&save, GEN3_DECO_DESK, 0, 1));
        return;
    }

    /* PokeBlocks: 40 slots of 8 bytes, colour first then the five boosts. */
    Gen3PokeBlock blk = { .color = 14, .spicy = 255, .dry = 1, .sweet = 2,
                          .bitter = 3, .sour = 4, .feel = 5 };
    assert(gen3_set_pokeblock(&save, 39, &blk));
    const uint8_t *raw = save.large + e->pokeblocks + 39u * 8u;
    assert(raw[0] == 14 && raw[1] == 255 && raw[6] == 5 && raw[7] == 0);
    Gen3PokeBlock back;
    assert(gen3_pokeblock(&save, 39, &back));
    assert(back.color == 14 && back.spicy == 255 && back.sour == 4 && back.feel == 5);
    assert(!gen3_set_pokeblock(&save, GEN3_POKEBLOCK_COUNT, &blk));
    blk.color = GEN3_POKEBLOCK_COLOR_COUNT;
    assert(!gen3_set_pokeblock(&save, 0, &blk));
    assert(strcmp(gen3_pokeblock_color_name(0), "None") == 0);
    assert(strcmp(gen3_pokeblock_color_name(14), "Gold") == 0);

    /* Secret bases: 20 of 160 bytes, the trainer name marking a used slot. */
    Gen3SecretBase base;
    assert(gen3_secret_base(&save, 0, &base) && !base.present);
    uint8_t *b = save.large + e->secret_bases + 3u * 160u;
    b[0] = 12;
    b[1] = (uint8_t)((1u << 4) | (1u << 5) | (2u << 6));
    gen3_encode_text("SAM", b + 2, 7);
    b[9] = 9;                       /* class 4, and the low byte of the TID */
    b[0x0D] = 2;
    b[0x0E] = 0x22; b[0x0F] = 0x11; /* 0x1122 bases received */
    b[0x10] = 7;
    assert(gen3_secret_base(&save, 3, &base));
    assert(base.present && base.location == 12);
    assert(base.ot_gender == 1 && base.battled_today && base.registry_status == 2);
    assert(strcmp(base.ot_name, "SAM") == 0);
    assert(base.ot_class == 4 && base.language == 2);
    assert(base.received == 0x1122 && base.times_entered == 7);
    /* The defending team: six of each field, each in its own run rather than
     * six records laid end to end. */
    uint8_t *team = b + 0x34u;
    const uint16_t mon = gen3_species_internal_from_national(202);   /* Wobbuffet */
    team[4 * 4 + 0] = 0x78; team[4 * 4 + 1] = 0x56;                  /* slot 4 PID low */
    for (unsigned m = 0; m < 4u; ++m) {
        const uint16_t move = (uint16_t)(219 + m);
        team[0x18 + 4 * 8 + m * 2] = (uint8_t)move;
        team[0x19 + 4 * 8 + m * 2] = (uint8_t)(move >> 8);
    }
    team[0x48 + 4 * 2] = (uint8_t)mon; team[0x49 + 4 * 2] = (uint8_t)(mon >> 8);
    team[0x54 + 4 * 2] = 0xB3;                                       /* held item 179 */
    team[0x60 + 4] = 42;
    team[0x66 + 4] = 25;
    Gen3SecretBaseMon tm;
    assert(gen3_secret_base_mon(&save, 3, 4, &tm));
    assert(tm.present && tm.species == 202 && tm.level == 42 && tm.ev_all == 25);
    assert(tm.held_item == 179 && (tm.pid & 0xFFFFu) == 0x5678u);
    for (unsigned m = 0; m < 4u; ++m) assert(tm.moves[m] == 219 + m);
    /* The other slots share the runs but must not pick up slot 4's values. */
    assert(gen3_secret_base_mon(&save, 3, 3, &tm) && !tm.present);
    assert(gen3_secret_base_mon(&save, 3, 5, &tm) && !tm.present);
    assert(!gen3_secret_base_mon(&save, 3, GEN3_SECRET_BASE_TEAM, &tm));

    assert(gen3_clear_secret_base(&save, 3));
    assert(gen3_secret_base(&save, 3, &base) && !base.present);
    assert(!gen3_secret_base(&save, GEN3_SECRET_BASE_COUNT, &base));
    assert(strcmp(gen3_secret_base_class_name(4), "Ace Trainer") == 0);

    /* Decorations: eight categories in one 150-byte run. */
    static const unsigned expect_offsets[GEN3_DECO_KIND_COUNT] = { 0, 10, 20, 30, 60, 90, 100, 140 };
    static const unsigned expect_counts[GEN3_DECO_KIND_COUNT] = { 10, 10, 10, 30, 30, 10, 40, 10 };
    unsigned total = 0;
    for (unsigned k = 0; k < GEN3_DECO_KIND_COUNT; ++k) {
        assert(gen3_decoration_slot_count((Gen3DecorationKind)k) == expect_counts[k]);
        total += expect_counts[k];
        assert(gen3_set_decoration(&save, (Gen3DecorationKind)k, 0, (uint8_t)(k + 1u)));
        assert(save.large[e->decorations + expect_offsets[k]] == k + 1u);
        assert(gen3_decoration(&save, (Gen3DecorationKind)k, 0) == k + 1u);
        assert(!gen3_set_decoration(&save, (Gen3DecorationKind)k, expect_counts[k], 1));
    }
    assert(total == 150u);
    assert(!gen3_set_decoration(&save, GEN3_DECO_KIND_COUNT, 0, 1));
}

/* The two clocks are in the small block, at fixed offsets, little-endian. */
static void test_clock(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;
    Gen3Clock c = { .day = 300, .hour = 23, .minute = 59, .second = 58 };
    if (e->game == GEN3_GAME_FRLG) {
        assert(!gen3_set_clock(&save, false, &c));
        assert(!gen3_clock(&save, false, &c));
        return;
    }
    assert(gen3_set_clock(&save, false, &c));
    assert(save.small[0x098] == 44 && save.small[0x099] == 1);  /* 300 little-endian */
    assert(save.small[0x09A] == 23 && save.small[0x09B] == 59 && save.small[0x09C] == 58);
    c.day = 7; c.hour = 1; c.minute = 2; c.second = 3;
    assert(gen3_set_clock(&save, true, &c));
    assert(save.small[0x0A0] == 7 && save.small[0x0A2] == 1);
    /* The two clocks are separate: writing one must not move the other. */
    Gen3Clock back;
    assert(gen3_clock(&save, false, &back));
    assert(back.day == 300 && back.hour == 23 && back.minute == 59 && back.second == 58);
    assert(gen3_clock(&save, true, &back) && back.day == 7 && back.second == 3);
    /* Out-of-range times are refused rather than wrapped. */
    c.hour = 24;
    assert(!gen3_set_clock(&save, false, &c));
}

/* The Battle Frontier block is Emerald's alone. */
static void test_battle_frontier(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;
    const bool emerald = e->game == GEN3_GAME_EMERALD;
    assert(gen3_has_battle_frontier(&save) == emerald);
    if (!emerald) {
        uint16_t v;
        assert(!gen3_frontier_stat(&save, GEN3_FACILITY_TOWER, 0, 0, GEN3_FRONTIER_CURRENT, &v));
        assert(!gen3_set_battle_points(&save, 100));
        return;
    }

    /* Modes and statistics differ per facility. */
    assert(gen3_facility_mode_count(GEN3_FACILITY_TOWER) == 4);
    assert(gen3_facility_mode_count(GEN3_FACILITY_DOME) == 2);
    assert(gen3_facility_mode_count(GEN3_FACILITY_ARENA) == 1);
    assert(gen3_facility_has_stat(GEN3_FACILITY_DOME, GEN3_FRONTIER_CHAMPIONSHIPS));
    assert(!gen3_facility_has_stat(GEN3_FACILITY_TOWER, GEN3_FRONTIER_CHAMPIONSHIPS));
    assert(gen3_facility_has_stat(GEN3_FACILITY_FACTORY, GEN3_FRONTIER_RECORD_SWAPPED));
    assert(gen3_facility_has_stat(GEN3_FACILITY_PIKE, GEN3_FRONTIER_RECORD_CLEARED));
    assert(!gen3_facility_has_stat(GEN3_FACILITY_PYRAMID, GEN3_FRONTIER_RECORD_CLEARED));

    /* Tower current streak, singles, Level 50 is the first halfword after the
     * continue flags at the head of the block. */
    assert(gen3_set_frontier_stat(&save, GEN3_FACILITY_TOWER, 0, 0, GEN3_FRONTIER_CURRENT, 0x1234));
    assert(save.small[0xCDC + 0x004] == 0x34 && save.small[0xCDC + 0x005] == 0x12);
    /* Doubles is one mode over: four bytes further in. */
    assert(gen3_set_frontier_stat(&save, GEN3_FACILITY_TOWER, 1, 1, GEN3_FRONTIER_CURRENT, 0x4321));
    assert(save.small[0xCDC + 0x004 + 4 + 2] == 0x21);
    uint16_t v = 0;
    assert(gen3_frontier_stat(&save, GEN3_FACILITY_TOWER, 0, 0, GEN3_FRONTIER_CURRENT, &v) && v == 0x1234);
    assert(gen3_frontier_stat(&save, GEN3_FACILITY_TOWER, 1, 1, GEN3_FRONTIER_CURRENT, &v) && v == 0x4321);
    /* The Pyramid keeps one mode, so mode 3 folds back onto mode 0. */
    assert(gen3_set_frontier_stat(&save, GEN3_FACILITY_PYRAMID, 3, 0, GEN3_FRONTIER_RECORD, 99));
    assert(gen3_frontier_stat(&save, GEN3_FACILITY_PYRAMID, 0, 0, GEN3_FRONTIER_RECORD, &v) && v == 99);
    assert(!gen3_set_frontier_stat(&save, GEN3_FACILITY_ARENA, 0, 2, GEN3_FRONTIER_CURRENT, 1));

    /* Symbols are event flag pairs, and gold implies silver. */
    assert(gen3_frontier_symbol(&save, GEN3_FACILITY_DOME) == 0);
    assert(gen3_set_frontier_symbol(&save, GEN3_FACILITY_DOME, 1));
    assert(gen3_event_flag(&save, 0x860 + 0x64 + 2) && !gen3_event_flag(&save, 0x860 + 0x64 + 3));
    assert(gen3_frontier_symbol(&save, GEN3_FACILITY_DOME) == 1);
    assert(gen3_set_frontier_symbol(&save, GEN3_FACILITY_DOME, 2));
    assert(gen3_event_flag(&save, 0x860 + 0x64 + 2) && gen3_event_flag(&save, 0x860 + 0x64 + 3));
    assert(gen3_frontier_symbol(&save, GEN3_FACILITY_DOME) == 2);
    /* Setting one facility's symbol must not touch its neighbours. */
    assert(gen3_frontier_symbol(&save, GEN3_FACILITY_TOWER) == 0);
    assert(gen3_frontier_symbol(&save, GEN3_FACILITY_PALACE) == 0);
    assert(gen3_set_frontier_symbol(&save, GEN3_FACILITY_DOME, 0));
    assert(gen3_frontier_symbol(&save, GEN3_FACILITY_DOME) == 0);
    assert(!gen3_set_frontier_symbol(&save, GEN3_FACILITY_DOME, 3));

    assert(!gen3_frontier_pass(&save));
    assert(gen3_set_frontier_pass(&save, true));
    assert(gen3_event_flag(&save, 0x860 + 0x72) && gen3_frontier_pass(&save));

    assert(gen3_set_battle_points(&save, 1234));
    assert(save.small[0xEB8] == 0xD2 && save.small[0xEB9] == 0x04);
    assert(gen3_battle_points(&save) == 1234);
    /* The games cap Battle Points at four digits. */
    assert(gen3_set_battle_points(&save, 65535) && gen3_battle_points(&save) == 9999);
}

/* The swarm, the museum paintings and the e-Reader berry slot. */
static void test_misc_blocks(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    assert(gen3_has_swarm(&save) == (e->swarm != 0));
    assert(gen3_has_paintings(&save) == (e->paintings != 0));
    assert(gen3_has_eberry(&save));

    /* A save with no berry loaded holds the Enigma Berry, which the games
     * cannot resolve; that is worth telling apart from a real berry. */
    assert(gen3_eberry_is_enigma(&save));
    gen3_encode_text("PECHA", save.large + e->eberry, 7);
    assert(!gen3_eberry_is_enigma(&save));
    char berry[16];
    gen3_eberry_name(&save, berry, sizeof(berry));
    assert(strcmp(berry, "PECHA") == 0);
    save.large[e->eberry] = GEN3_TEXT_TERMINATOR;
    assert(gen3_eberry_is_enigma(&save));

    if (!e->swarm) {
        Gen3Swarm sw;
        assert(!gen3_swarm(&save, &sw));
        Gen3Painting pt;
        assert(!gen3_painting(&save, 0, &pt));
        return;
    }

    Gen3Swarm sw = { .species = 183, .map_num = 17, .map_group = 0, .level = 25,
                     .moves = { 145, 55, 0, 0 }, .probability = 50, .days_left = 3 };
    assert(gen3_set_swarm(&save, &sw));
    const uint8_t *b = save.large + e->swarm;
    assert(gen3_species_national((uint16_t)(b[0] | (b[1] << 8))) == 183u);
    assert(b[2] == 17 && b[4] == 25 && b[0x11] == 50 && b[0x12] == 3);
    Gen3Swarm back;
    assert(gen3_swarm(&save, &back));
    assert(back.active && back.species == 183 && back.level == 25);
    assert(back.moves[0] == 145 && back.moves[1] == 55 && back.probability == 50);
    /* A probability over 100 is clamped rather than stored. */
    sw.probability = 200;
    assert(gen3_set_swarm(&save, &sw) && save.large[e->swarm + 0x11] == 100);
    assert(gen3_clear_swarm(&save));
    assert(gen3_swarm(&save, &back) && !back.active && back.species == 0);

    /* Paintings: five records of 0x20, read only. */
    uint8_t *p2 = save.large + e->paintings + 2u * 0x20u;
    p2[0] = 0x78; p2[1] = 0x56; p2[2] = 0x34; p2[3] = 0x12;
    p2[4] = 0x39; p2[5] = 0x30;
    const uint16_t internal = gen3_species_internal_from_national(129);
    p2[8] = (uint8_t)internal; p2[9] = (uint8_t)(internal >> 8);
    p2[0x0A] = 3;
    gen3_encode_text("SPLASHY", p2 + 0x0B, 10);
    gen3_encode_text("SAM", p2 + 0x16, 7);
    Gen3Painting pt;
    assert(gen3_painting(&save, 2, &pt));
    assert(pt.present && pt.species == 129 && pt.pid == 0x12345678u);
    assert(pt.tid == 12345 && pt.caption == 3);
    assert(strcmp(pt.nickname, "SPLASHY") == 0 && strcmp(pt.ot_name, "SAM") == 0);
    assert(gen3_painting(&save, 0, &pt) && !pt.present);
    assert(!gen3_painting(&save, GEN3_PAINTING_COUNT, &pt));
}

/* The same reflected CRC-16 both mystery blocks use, written out here from
 * PKHeX's table rather than from the port's generator. */
static uint16_t mystery_crc_ref(const uint8_t *data, size_t len)
{
    static const uint16_t seed_table_check[4] = { 0x0000, 0x1189, 0x2312, 0x329B };
    static uint16_t table[256];
    static int built = 0;
    if (!built) {
        for (unsigned i = 0; i < 256u; ++i) {
            uint16_t c = (uint16_t)i;
            for (unsigned b = 0; b < 8u; ++b)
                c = (uint16_t)((c & 1u) ? ((c >> 1) ^ 0x8408u) : (c >> 1));
            table[i] = c;
        }
        /* The first four entries are quoted from PKHeX, so a wrong polynomial
         * or bit direction fails here rather than silently agreeing. */
        for (unsigned i = 0; i < 4u; ++i) assert(table[i] == seed_table_check[i]);
        built = 1;
    }
    uint16_t chk = 0x1121u;
    for (size_t i = 0; i < len; ++i)
        chk = (uint16_t)(table[(uint8_t)(data[i] ^ (uint8_t)chk)] ^ (chk >> 8));
    return (uint16_t)~chk;
}

static void test_mystery_gift(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    assert(gen3_has_wonder_card(&save) == (e->wonder_news != 0));
    assert(gen3_has_mystery_event(&save));

    /* An untouched save has neither a card nor an event. */
    assert(!gen3_mystery_event_present(&save));
    Gen3WonderCard card;
    if (e->wonder_news) {
        assert(gen3_wonder_card(&save, &card) && !card.present);
    } else {
        assert(!gen3_wonder_card(&save, &card));
    }

    /* The event block is 1004 bytes and checksums everything past the word. */
    uint8_t *ev = save.large + e->mystery_event;
    for (unsigned i = 4; i < 1004u; ++i) ev[i] = (uint8_t)(i * 7u);
    assert(gen3_mystery_event_present(&save));
    assert(!gen3_mystery_event_checksum_ok(&save));
    const uint16_t ev_crc = mystery_crc_ref(ev + 4, 1000u);
    ev[0] = (uint8_t)ev_crc; ev[1] = (uint8_t)(ev_crc >> 8);
    assert(gen3_mystery_event_checksum_ok(&save));
    assert(gen3_clear_mystery_event(&save));
    assert(!gen3_mystery_event_present(&save));

    if (!e->wonder_news) return;

    /* Wonder News sits before the card, so the card's offset depends on it. */
    const size_t card_at = e->wonder_news + 4u + 444u;
    uint8_t *c = save.large + card_at;
    c[4] = 0x22; c[5] = 0x11;      /* card id 0x1122 */
    c[6] = 0x05;                   /* icon */
    c[8] = 0x03;                   /* count */
    c[0x0C] = (uint8_t)(2u | (5u << 2));
    gen3_encode_text("EON TICKET", c + 0x0E, 40);
    assert(gen3_wonder_card(&save, &card));
    assert(!card.checksum_ok && !card.present);
    const uint16_t crc = mystery_crc_ref(c + 4, 336u - 4u);
    c[0] = (uint8_t)crc; c[1] = (uint8_t)(crc >> 8);
    assert(gen3_wonder_card(&save, &card));
    assert(card.present && card.checksum_ok);
    assert(card.card_id == 0x1122 && card.icon == 5 && card.count == 3);
    assert(card.type == 2 && card.color == 5);
    assert(strcmp(card.title, "EON TICKET") == 0);
    assert(gen3_clear_wonder_card(&save));
    assert(gen3_wonder_card(&save, &card) && !card.present);

    /*
     * Installing a card from a file. The games will not resolve a card whose
     * CRC does not match, so a file that fails it must be refused rather than
     * written into a save that has to keep working.
     */
    uint8_t file[GEN3_WONDER_CARD_BYTES];
    memset(file, 0, sizeof(file));
    file[4] = 0x44; file[5] = 0x33;                   /* card id 0x3344 */
    file[8] = 0x07;
    gen3_encode_text("MYSTIC TICKET", file + 0x0E, 40);
    assert(!gen3_wonder_card_data_valid(file, sizeof(file), false));   /* no CRC yet */
    assert(!gen3_set_wonder_card(&save, file, sizeof(file)));

    const uint16_t file_crc = mystery_crc_ref(file + 4, sizeof(file) - 4u);
    file[0] = (uint8_t)file_crc; file[1] = (uint8_t)(file_crc >> 8);
    assert(gen3_wonder_card_data_valid(file, sizeof(file), false));
    assert(!gen3_wonder_card_data_valid(file, sizeof(file) - 1u, false));  /* wrong length */
    assert(!gen3_wonder_card_data_valid(file, sizeof(file), true));        /* wrong region */

    assert(gen3_set_wonder_card(&save, file, sizeof(file)));
    assert(gen3_wonder_card(&save, &card));
    assert(card.present && card.checksum_ok && card.card_id == 0x3344 && card.count == 7);
    assert(strcmp(card.title, "MYSTIC TICKET") == 0);

    /* One flipped byte and it is refused again, with the save untouched. */
    file[0x20] ^= 0x01u;
    assert(!gen3_set_wonder_card(&save, file, sizeof(file)));
    assert(gen3_wonder_card(&save, &card) && card.card_id == 0x3344);

    /* An all-zero block is an empty slot, not a card, whatever its CRC. */
    uint8_t empty[GEN3_WONDER_CARD_BYTES];
    memset(empty, 0, sizeof(empty));
    const uint16_t empty_crc = mystery_crc_ref(empty + 4, sizeof(empty) - 4u);
    empty[0] = (uint8_t)empty_crc; empty[1] = (uint8_t)(empty_crc >> 8);
    assert(!gen3_wonder_card_data_valid(empty, sizeof(empty), false));
}

/* The Pokemon Jump and Berry Picking high scores. */
static void test_joyful(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    assert(gen3_has_joyful(&save) == (e->joyful != 0));
    if (e->joyful) {
        static const unsigned offsets[GEN3_JOYFUL_STAT_COUNT] =
            { 0x00, 0x04, 0x06, 0x0C, 0x10, 0x14, 0x16 };
        for (unsigned i = 0; i < GEN3_JOYFUL_STAT_COUNT; ++i) {
            assert(gen3_set_joyful_stat(&save, (Gen3JoyfulStat)i, 1234u + i));
            assert(save.small[e->joyful + offsets[i]] == (uint8_t)(1234u + i));
            assert(gen3_joyful_stat(&save, (Gen3JoyfulStat)i) == 1234u + i);
        }
        /* The caps are 9999 for the counts and 99990 for the two scores. */
        assert(gen3_set_joyful_stat(&save, GEN3_JOYFUL_JUMP_IN_ROW, 50000));
        assert(gen3_joyful_stat(&save, GEN3_JOYFUL_JUMP_IN_ROW) == 9999u);
        assert(gen3_set_joyful_stat(&save, GEN3_JOYFUL_JUMP_SCORE, 500000));
        assert(gen3_joyful_stat(&save, GEN3_JOYFUL_JUMP_SCORE) == 99990u);
        assert(!gen3_set_joyful_stat(&save, GEN3_JOYFUL_STAT_COUNT, 1));
    } else {
        assert(!gen3_set_joyful_stat(&save, GEN3_JOYFUL_JUMP_IN_ROW, 1));
        assert(gen3_joyful_stat(&save, GEN3_JOYFUL_JUMP_IN_ROW) == 0);
    }

}


/*
 * Emerald's Trainer Hill times, trendy words and Walda wallpaper. All three
 * are Emerald-only, so the point of the test is as much that the other three
 * games refuse them as that Emerald round-trips them.
 */
static void test_emerald_extras(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;
    const bool emerald = e->game == GEN3_GAME_EMERALD;

    assert(gen3_has_emerald_extras(&save) == emerald);
    if (!emerald) {
        assert(!gen3_set_trainer_hill_record(&save, 0, 1234));
        assert(gen3_trainer_hill_record(&save, 0) == 0);
        assert(!gen3_set_trendy_word(&save, 0, true));
        assert(!gen3_trendy_word(&save, 0));
        Gen3Walda w;
        assert(!gen3_walda(&save, &w));
        return;
    }

    /* Four modes, each a 32-bit count of sixtieths of a second. */
    for (unsigned mode = 0; mode < GEN3_TRAINER_HILL_MODES; ++mode) {
        assert(gen3_set_trainer_hill_record(&save, mode, 60u * (61u + mode)));
        assert(gen3_trainer_hill_record(&save, mode) == 60u * (61u + mode));
        assert(save.large[0x3718 + mode * 4u] == (uint8_t)(60u * (61u + mode)));
    }
    assert(!gen3_set_trainer_hill_record(&save, GEN3_TRAINER_HILL_MODES, 1));

    /* One bit per word, low bit first, from 0x2E20. */
    for (unsigned word = 0; word < GEN3_TRENDY_WORD_COUNT; ++word) {
        assert(!gen3_trendy_word(&save, word));
        assert(gen3_set_trendy_word(&save, word, true));
        assert(gen3_trendy_word(&save, word));
        assert(save.large[0x2E20 + (word >> 3)] & (1u << (word & 7u)));
        /* Setting one must not disturb its neighbours. */
        for (unsigned other = 0; other < GEN3_TRENDY_WORD_COUNT; ++other)
            assert(gen3_trendy_word(&save, other) == (other <= word));
    }
    assert(gen3_set_trendy_word(&save, 5, false));
    assert(!gen3_trendy_word(&save, 5) && gen3_trendy_word(&save, 4) && gen3_trendy_word(&save, 6));
    assert(!gen3_set_trendy_word(&save, GEN3_TRENDY_WORD_COUNT, true));

    const Gen3Walda want = { 0x1234u, 0x5678u, 9u, 3u, true };
    assert(gen3_set_walda(&save, &want));
    Gen3Walda got;
    assert(gen3_walda(&save, &got));
    assert(got.background == want.background && got.foreground == want.foreground);
    assert(got.icon == want.icon && got.pattern == want.pattern && got.unlocked);
    assert(save.large[0x3D70] == 0x34u && save.large[0x3D71] == 0x12u);
    assert(save.large[0x3D86] == 1u);
}


/*
 * The block the GameCube games write into a cartridge save. Every game has
 * one, at a different offset, and PKHeX packs four bonus disc flags into the
 * low byte of the lifetime PokeCoupon total - so setting a flag must leave
 * the count above it alone.
 */
static void test_external_events(const Expected *e)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = e->game;

    Gen3ExternalEvents ev;
    assert(gen3_external_events(&save, &ev));
    assert(ev.coupons == 0 && ev.coupons_total == 0 && !ev.received_jirachi);

    /* A lifetime total with all four flags set beneath it. */
    const size_t at = e->external_events;
    save.large[at + 11] = 0x0Fu;                       /* the four flags */
    save.large[at + 12] = 0x40u; save.large[at + 13] = 0x9Cu; /* 40000 << 8 */
    save.large[at + 14] = 0x00u;
    save.large[at + 7] = 0x00u; save.large[at + 8] = 0x10u;   /* 4096 coupons */
    save.large[at + 0x14] = 0x07u;                     /* RSBOX used, eggs 3 */
    save.large[at + 0x16] = 0x01u;                     /* Jirachi received */

    assert(gen3_external_events(&save, &ev));
    assert(ev.coupons_total == 40000u && ev.coupons == 16u);
    assert(ev.title_gold && ev.title_silver && ev.title_bronze && ev.received_celebi);
    assert(ev.used_rsbox && ev.rsbox_eggs == 3u && ev.received_jirachi);

    /* Clearing a flag must not disturb the count sharing its word. */
    ev.title_gold = false;
    ev.received_jirachi = false;
    ev.rsbox_eggs = 1u;
    assert(gen3_set_external_event_flags(&save, &ev));
    assert(gen3_external_events(&save, &ev));
    assert(ev.coupons_total == 40000u && ev.coupons == 16u);
    assert(!ev.title_gold && ev.title_silver && ev.title_bronze && ev.received_celebi);
    assert(ev.used_rsbox && ev.rsbox_eggs == 1u && !ev.received_jirachi);
}

int main(void)
{
    for (unsigned i = 0; i < sizeof games / sizeof games[0]; ++i) {
        test_daycare(&games[i]);
        test_roamer(&games[i]);
        test_mail(&games[i]);
        test_gift_ribbons(&games[i]);
        test_records(&games[i]);
        test_rival_name(&games[i]);
        test_hoenn_blocks(&games[i]);
        test_clock(&games[i]);
        test_battle_frontier(&games[i]);
        test_misc_blocks(&games[i]);
        test_mystery_gift(&games[i]);
        test_joyful(&games[i]);
        test_emerald_extras(&games[i]);
        test_external_events(&games[i]);
        printf("  %s: every optional block at PKHeX's offsets\n", games[i].name);
    }
    test_met_locations();
    printf("  met location names index straight off the met byte\n");
    test_hall_of_fame();
    printf("  hall of fame reads across the two trailing sectors\n");
    printf("Gen III optional block tests: PASS\n");
    return 0;
}
