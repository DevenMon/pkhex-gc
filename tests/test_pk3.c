/*
 * PK3 field round trips.
 *
 * Every field has to survive the shuffle-and-encrypt that a Generation III
 * record goes through on the way into a save and back out. Offsets and bit
 * layouts follow PKHeX's PK3 (see SOURCES.md).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen3.h"
#include "gen3_personal.h"

static void fill_reference(Gen3Pokemon *p)
{
    memset(p, 0, sizeof(*p));
    p->present = true;
    p->pid = 0x9B8E7F0Bu;
    p->tid = 4386;
    p->sid = 13124;
    p->species_internal = 410;   /* Deoxys' internal index */
    p->held_item = 179;
    p->experience = 125000;
    p->friendship = 70;
    p->level = 52;

    for (unsigned i = 0; i < 4; ++i) { p->moves[i] = (uint16_t)(1u + i * 37u); p->pp[i] = (uint8_t)(35u - i); }
    for (unsigned i = 0; i < 6; ++i) { p->evs[i] = (uint8_t)(i * 40u); p->ivs[i] = (uint8_t)(31u - i * 3u); }
    for (unsigned i = 0; i < 6; ++i) p->contest[i] = (uint8_t)(10u + i * 7u);

    p->pp_ups = 0xE4;            /* 0,1,2,3 ups across the four slots */
    p->language = 2;             /* English */
    p->markings = 0x0B;          /* exercises the bit-1/bit-2 swap */
    p->pokerus = 0x34;           /* strain 3, 4 days remaining */
    p->met_location = 88;
    p->met_level = 47;
    p->origin_game = 5;          /* LeafGreen */
    p->ball = 4;                 /* Poke Ball */
    p->ot_gender = 1;
    p->ability_bit = true;
    p->fateful = true;
    p->ribbons = 0x0000ABCDu;
    p->is_egg = false;
    p->present = true;
    /* Names live in the record as Gen III bytes; the decoded copies are what
     * the UI shows. Both are set so a round trip has something to check. */
    gen3_encode_text("DEOXYS", p->nickname_raw, sizeof(p->nickname_raw));
    gen3_encode_text("TESTER", p->ot_raw, sizeof(p->ot_raw));
    snprintf(p->nickname, sizeof(p->nickname), "DEOXYS");
    snprintf(p->ot_name, sizeof(p->ot_name), "TESTER");
}

static void expect_equal(const Gen3Pokemon *a, const Gen3Pokemon *b)
{
    assert(b->present);
    assert(b->checksum_ok);
    assert(a->pid == b->pid);
    assert(a->tid == b->tid && a->sid == b->sid);
    assert(a->species_internal == b->species_internal);
    assert(a->held_item == b->held_item);
    assert(a->experience == b->experience);
    assert(a->friendship == b->friendship);
    assert(memcmp(a->moves, b->moves, sizeof(a->moves)) == 0);
    assert(memcmp(a->pp, b->pp, sizeof(a->pp)) == 0);
    assert(memcmp(a->evs, b->evs, sizeof(a->evs)) == 0);
    assert(memcmp(a->ivs, b->ivs, sizeof(a->ivs)) == 0);
    assert(memcmp(a->contest, b->contest, sizeof(a->contest)) == 0);
    assert(a->pp_ups == b->pp_ups);
    assert(a->language == b->language);
    assert(a->markings == b->markings);
    assert(a->pokerus == b->pokerus);
    assert(a->met_location == b->met_location);
    assert(a->met_level == b->met_level);
    assert(a->origin_game == b->origin_game);
    assert(a->ball == b->ball);
    assert(a->ot_gender == b->ot_gender);
    assert(a->ability_bit == b->ability_bit);
    assert(a->fateful == b->fateful);
    assert((a->ribbons & 0x7FFFFFFFu) == (b->ribbons & 0x7FFFFFFFu));
    assert(a->is_egg == b->is_egg);
    assert(a->level == b->level);
    assert(memcmp(a->nickname_raw, b->nickname_raw, sizeof(a->nickname_raw)) == 0);
    assert(memcmp(a->ot_raw, b->ot_raw, sizeof(a->ot_raw)) == 0);
    assert(strcmp(a->nickname, b->nickname) == 0);
    assert(strcmp(a->ot_name, b->ot_name) == 0);
}

static void test_party_round_trip(void)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_RS;

    Gen3Pokemon in; fill_reference(&in);
    assert(gen3_set_party_pokemon(&save, 0, &in));

    Gen3Pokemon out;
    assert(gen3_party_pokemon(&save, 0, &out));
    expect_equal(&in, &out);
    printf("  party record keeps every PK3 field\n");
}

static void test_box_round_trip(void)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_EMERALD;

    Gen3Pokemon in; fill_reference(&in);
    in.level = 0; /* box records carry no battle stats */
    assert(gen3_set_box_pokemon(&save, 3, 11, &in));

    Gen3Pokemon out;
    assert(gen3_box_pokemon(&save, 3, 11, &out));
    expect_equal(&in, &out);
    printf("  box record keeps every PK3 field\n");
}

/*
 * A record's four substructures are stored in one of 24 orders chosen by
 * PID % 24. Exercising a single PID only ever covers one of them, so walk all
 * 24 and confirm every field survives each shuffle.
 */
static void test_every_shuffle_order_round_trips(void)
{
    for (unsigned order = 0; order < 24u; ++order) {
        Gen3Save save; memset(&save, 0, sizeof(save));
        save.game = GEN3_GAME_RS;

        Gen3Pokemon in; fill_reference(&in);
        /* Pick a PID landing on this shuffle order. The high bits stay busy so
         * the XOR key differs from the order index. */
        const uint32_t base = 0x12340000u + order * 0x00010001u;
        in.pid = base + ((order + 24u) - (base % 24u)) % 24u;
        assert(in.pid % 24u == order);
        in.species_internal = (uint16_t)(277u + order);
        in.level = 0;

        assert(gen3_set_box_pokemon(&save, 0, order, &in));
        Gen3Pokemon out;
        assert(gen3_box_pokemon(&save, 0, order, &out));
        expect_equal(&in, &out);
        assert(out.species_internal == in.species_internal);
    }
    printf("  all 24 substructure shuffle orders round-trip\n");
}

static void test_egg_flag_is_mirrored(void)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_FRLG;

    Gen3Pokemon in; fill_reference(&in);
    in.is_egg = true;
    assert(gen3_set_party_pokemon(&save, 1, &in));

    Gen3Pokemon out;
    assert(gen3_party_pokemon(&save, 1, &out));
    assert(out.is_egg);
    /* The plaintext flag byte carries bit 2 alongside the encrypted IV bit. */
    const uint8_t *record = save.large + 0x38 + GEN3_PK3_PARTY_SIZE;
    assert((record[0x13] & 4u) != 0);
    printf("  egg flag is written in both places\n");
}

static void test_derived_values(void)
{
    Gen3Pokemon p; fill_reference(&p);

    /* Nature is the PID modulo 25. */
    assert(gen3_nature(&p) == (uint8_t)(p.pid % 25u));
    assert(strcmp(gen3_nature_name(0), "Hardy") == 0);
    assert(strcmp(gen3_nature_name(24), "Quirky") == 0);

    /* Shiny when TID^SID^PIDhi^PIDlo < 8. */
    p.pid = 0x00000000u; p.tid = 0; p.sid = 0;
    assert(gen3_is_shiny(&p));
    p.tid = 8;
    assert(!gen3_is_shiny(&p));

    /* Hidden Power over all-31 IVs is Dark 70. */
    for (unsigned i = 0; i < 6; ++i) p.ivs[i] = 31;
    assert(gen3_hidden_power_type(&p) == 15);
    assert(strcmp(gen3_type_name(gen3_hidden_power_type(&p)), "Dark") == 0);
    assert(gen3_hidden_power_power(&p) == 70);

    /* ... and Fighting 30 over all-zero IVs. */
    for (unsigned i = 0; i < 6; ++i) p.ivs[i] = 0;
    assert(gen3_hidden_power_type(&p) == 0);
    assert(strcmp(gen3_type_name(gen3_hidden_power_type(&p)), "Fighting") == 0);
    assert(gen3_hidden_power_power(&p) == 30);

    /* IV 30 in every slot keeps the type bits clear but sets every power bit. */
    for (unsigned i = 0; i < 6; ++i) p.ivs[i] = 30;
    assert(gen3_hidden_power_type(&p) == 0);
    assert(gen3_hidden_power_power(&p) == 70);

    p.pokerus = 0x34;
    assert(gen3_pokerus_strain(&p) == 3 && gen3_pokerus_days(&p) == 4);

    p.pp_ups = 0;
    gen3_set_pp_up_count(&p, 2, 3);
    assert(gen3_pp_up_count(&p, 2) == 3);
    assert(gen3_pp_up_count(&p, 0) == 0 && gen3_pp_up_count(&p, 3) == 0);
    gen3_set_pp_up_count(&p, 2, 9); /* clamped */
    assert(gen3_pp_up_count(&p, 2) == 3);

    printf("  derived values match the Generation III formulas\n");
}

static void test_nature_change_preserves_shininess(void)
{
    Gen3Pokemon p; fill_reference(&p);
    for (uint8_t nature = 0; nature < 25u; ++nature) {
        Gen3Pokemon c = p;
        const bool was_shiny = gen3_is_shiny(&c);
        assert(gen3_set_nature(&c, nature));
        assert(gen3_nature(&c) == nature);
        assert(gen3_is_shiny(&c) == was_shiny);
    }
    /* A shiny stays shiny. */
    Gen3Pokemon shiny; fill_reference(&shiny);
    shiny.pid = 0u; shiny.tid = 0; shiny.sid = 0;
    assert(gen3_is_shiny(&shiny));
    assert(gen3_set_nature(&shiny, 7));
    assert(gen3_nature(&shiny) == 7 && gen3_is_shiny(&shiny));

    assert(!gen3_set_nature(&p, 25));
    printf("  nature changes find a PID without altering shininess\n");
}

/*
 * Generation III stores Hoenn species in the order the games added them, not
 * National Dex order. A linear offset happens to work for the first 24 and
 * then diverges, which showed up on hardware as Rayquaza displaying as Latios.
 */
static void test_species_index_conversion(void)
{
    /* Kanto and Johto are 1:1. */
    for (unsigned n = 1; n <= 251; ++n) {
        assert(gen3_species_internal_from_national(n) == n);
        assert(gen3_species_national((uint16_t)n) == n);
    }

    /* Every Hoenn species round-trips through the table. */
    for (unsigned n = 252; n <= 386; ++n) {
        const uint16_t internal = gen3_species_internal_from_national(n);
        assert(internal >= 277 && internal <= 411);
        assert(gen3_species_national(internal) == n);
    }

    /* Known anchors, checked against PKHeX's SpeciesConverter. */
    assert(gen3_species_national(277) == 252); /* Treecko  */
    assert(gen3_species_national(406) == 384); /* Rayquaza */
    assert(gen3_species_national(407) == 380); /* Latias   */
    assert(gen3_species_national(409) == 385); /* Jirachi  */
    assert(gen3_species_national(410) == 386); /* Deoxys   */
    assert(strcmp(gen3_species_name(406), "Rayquaza") == 0);
    assert(strcmp(gen3_species_name(410), "Deoxys") == 0);
    assert(strcmp(gen3_species_name(25), "Pikachu") == 0);

    /* The old linear guess produced Latios here; make sure it cannot return. */
    assert(gen3_species_national(406) != 381);

    /* The 252-276 gap has no species and must not index the name table. */
    for (unsigned raw = 252; raw <= 276; ++raw)
        assert(gen3_species_national((uint16_t)raw) == 0);
    assert(gen3_species_national(412) == 0);
    assert(strcmp(gen3_species_name(260), "Unknown") == 0);

    printf("  species indices convert against PKHeX's table\n");
}

static void test_box_wallpapers(void)
{
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_FRLG;

    for (unsigned box = 0; box < GEN3_BOX_COUNT; ++box) {
        const uint8_t want = (uint8_t)(box % GEN3_WALLPAPER_COUNT);
        assert(gen3_set_box_wallpaper(&save, box, want));
        assert(gen3_box_wallpaper(&save, box) == want);
    }

    /* Out-of-range writes are refused rather than corrupting the save. */
    assert(!gen3_set_box_wallpaper(&save, 0, GEN3_WALLPAPER_COUNT));
    assert(!gen3_set_box_wallpaper(&save, GEN3_BOX_COUNT, 0));
    assert(gen3_box_wallpaper(&save, 0) == 0);

    /* Wallpapers sit immediately after the 14 nine-byte box names, and must
     * not overlap the last box's name. */
    const size_t names = 4u + (size_t)GEN3_BOX_COUNT * GEN3_BOX_SLOTS * GEN3_PK3_STORED_SIZE;
    assert(save.storage[names + GEN3_BOX_COUNT * 9u + 3u] == 3u);

    /* A corrupt byte must never index past the name table. */
    save.storage[names + GEN3_BOX_COUNT * 9u] = 0xEE;
    assert(gen3_box_wallpaper(&save, 0) < GEN3_WALLPAPER_COUNT);
    assert(strcmp(gen3_wallpaper_name(0), "Forest") == 0);
    assert(strcmp(gen3_wallpaper_name(15), "Simple") == 0);
    assert(strcmp(gen3_wallpaper_name(16), "?") == 0);

    printf("  box wallpapers read, write and stay in range\n");
}

static void test_pokedex_flags(void)
{
    /* The seen bits exist in three places and the games cross-check them, so
     * the writer must keep all three in step. */
    for (int g = 0; g < 3; ++g) {
        Gen3Save save; memset(&save, 0, sizeof(save));
        save.game = (g == 0) ? GEN3_GAME_RS : (g == 1) ? GEN3_GAME_EMERALD : GEN3_GAME_FRLG;

        assert(gen3_dex_seen_count(&save) == 0);
        assert(gen3_dex_caught_count(&save) == 0);
        assert(gen3_dex_seen_consistent(&save));

        for (unsigned n = 1; n <= GEN3_DEX_SPECIES; n += 3) {
            gen3_set_dex_seen(&save, n, true);
            gen3_set_dex_caught(&save, n, true);
        }
        assert(gen3_dex_seen_consistent(&save));
        for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n) {
            const bool want = (n - 1u) % 3u == 0u;
            assert(gen3_dex_seen(&save, n) == want);
            assert(gen3_dex_caught(&save, n) == want);
        }

        /* Corrupting one mirror must be detected, and repairable. */
        const size_t mirror = (save.game == GEN3_GAME_EMERALD) ? 0x988u
                            : (save.game == GEN3_GAME_FRLG) ? 0x5F8u : 0x938u;
        save.large[mirror] ^= 0xFFu;
        assert(!gen3_dex_seen_consistent(&save));
        gen3_mirror_dex_seen(&save);
        assert(gen3_dex_seen_consistent(&save));

        /* Out-of-range species must not touch memory. */
        gen3_set_dex_seen(&save, 0, true);
        gen3_set_dex_seen(&save, GEN3_DEX_SPECIES + 1u, true);
        assert(!gen3_dex_seen(&save, 0));
        assert(!gen3_dex_seen(&save, GEN3_DEX_SPECIES + 1u));

        /* Clearing works too. */
        for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n) {
            gen3_set_dex_seen(&save, n, false);
            gen3_set_dex_caught(&save, n, false);
        }
        assert(gen3_dex_seen_count(&save) == 0 && gen3_dex_caught_count(&save) == 0);
    }

    /* National Dex magic byte and value differ per family. */
    Gen3Save rs; memset(&rs, 0, sizeof(rs)); rs.game = GEN3_GAME_RS;
    assert(!gen3_national_dex(&rs));
    rs.small[0x1A] = 0xDA;
    assert(gen3_national_dex(&rs));

    Gen3Save fr; memset(&fr, 0, sizeof(fr)); fr.game = GEN3_GAME_FRLG;
    fr.small[0x1A] = 0xDA;              /* the RSE byte must not count here */
    assert(!gen3_national_dex(&fr));
    fr.small[0x1B] = 0xB9;
    assert(gen3_national_dex(&fr));

    /* Unlocking the National Dex must write the magic byte, the event flag and
     * the work value together. */
    Gen3Save em; memset(&em, 0, sizeof(em)); em.game = GEN3_GAME_EMERALD;
    gen3_set_national_dex(&em, true);
    assert(gen3_national_dex(&em));
    assert(em.small[0x19] == 1u && em.small[0x1A] == 0xDAu);
    assert(gen3_event_flag(&em, 0x836u));
    assert(gen3_event_work(&em, 0x46u) == 0x0302u);
    gen3_set_national_dex(&em, false);
    assert(!gen3_national_dex(&em) && !gen3_event_flag(&em, 0x836u));

    Gen3Save lg; memset(&lg, 0, sizeof(lg)); lg.game = GEN3_GAME_FRLG;
    gen3_set_national_dex(&lg, true);
    assert(gen3_national_dex(&lg) && lg.small[0x1B] == 0xB9u);
    assert(gen3_event_flag(&lg, 0x840u));
    assert(gen3_event_work(&lg, 0x4Eu) == 0x6258u);

    printf("  pokedex flags stay consistent across all three copies\n");
}

static void test_event_flags_and_work(void)
{
    const struct { Gen3Game game; unsigned flags; size_t flag_off, work_off; } cases[] = {
        { GEN3_GAME_RS,      8u * 288u, 0x1220u, 0x1340u },
        { GEN3_GAME_EMERALD, 8u * 300u, 0x1270u, 0x139Cu },
        { GEN3_GAME_FRLG,    8u * 288u, 0x0EE0u, 0x1000u },
    };

    for (unsigned c = 0; c < 3u; ++c) {
        Gen3Save save; memset(&save, 0, sizeof(save));
        save.game = cases[c].game;

        assert(gen3_event_flag_count(&save) == cases[c].flags);
        assert(gen3_event_work_count(&save) == 0x100u);
        assert(gen3_event_flags_set(&save) == 0);

        /* Flag 0 must be the low bit of the first flag byte, so the offsets
         * are pinned rather than merely self-consistent. */
        gen3_set_event_flag(&save, 0, true);
        assert(save.large[cases[c].flag_off] == 1u);
        gen3_set_event_flag(&save, 9, true);
        assert(save.large[cases[c].flag_off + 1u] == 2u);
        assert(gen3_event_flag(&save, 0) && gen3_event_flag(&save, 9));
        assert(!gen3_event_flag(&save, 1));
        assert(gen3_event_flags_set(&save) == 2);

        gen3_set_event_flag(&save, 0, false);
        assert(!gen3_event_flag(&save, 0));

        /* Work values are little-endian 16-bit. */
        gen3_set_event_work(&save, 3, 0xBEEF);
        assert(save.large[cases[c].work_off + 6u] == 0xEF);
        assert(save.large[cases[c].work_off + 7u] == 0xBE);
        assert(gen3_event_work(&save, 3) == 0xBEEF);

        /* Out of range touches nothing and reads as zero. */
        gen3_set_event_flag(&save, cases[c].flags, true);
        gen3_set_event_work(&save, 0x100u, 0xFFFF);
        assert(!gen3_event_flag(&save, cases[c].flags));
        assert(gen3_event_work(&save, 0x100u) == 0);
        assert(gen3_event_flags_set(&save) == 1);
    }
    /* Badges are eight consecutive event flags from a per-game base. */
    const struct { Gen3Game game; unsigned base; } badge[] = {
        { GEN3_GAME_RS, 0x807u }, { GEN3_GAME_EMERALD, 0x867u }, { GEN3_GAME_FRLG, 0x820u },
    };
    for (unsigned c = 0; c < 3u; ++c) {
        Gen3Save save; memset(&save, 0, sizeof(save));
        save.game = badge[c].game;
        assert(gen3_badge_count(&save) == 0);
        gen3_set_badge(&save, 0, true);
        gen3_set_badge(&save, 7, true);
        assert(gen3_badge(&save, 0) && gen3_badge(&save, 7) && !gen3_badge(&save, 3));
        assert(gen3_badge_count(&save) == 2);
        assert(gen3_event_flag(&save, badge[c].base));
        assert(gen3_event_flag(&save, badge[c].base + 7u));
        gen3_set_badge(&save, 8, true);  /* out of range */
        assert(gen3_badge_count(&save) == 2);
    }

    printf("  event flags and work values match the per-game layouts\n");
}

static void test_personal_table(void)
{
    /* Spot-check against well-known species so a mis-generated table shows up. */
    const Gen3Personal *bulba = gen3_personal(1);
    assert(bulba && bulba->hp == 45 && bulba->atk == 49 && bulba->def == 49);
    assert(bulba->spe == 45 && bulba->spa == 65 && bulba->spd == 65);
    assert(strcmp(gen3_type_name_full(bulba->type1), "Grass") == 0);
    assert(strcmp(gen3_type_name_full(bulba->type2), "Poison") == 0);
    assert(strcmp(gen3_ability_name(bulba->ability1), "Overgrow") == 0);

    assert(gen3_personal(0) == NULL);
    assert(gen3_personal(GEN3_DEX_SPECIES + 1u) == NULL);

    /* Deoxys changes form, and so base stats, per game. */
    uint8_t rs[6], e[6], fr[6];
    assert(gen3_base_stats(386, GEN3_GAME_RS, rs));
    assert(gen3_base_stats(386, GEN3_GAME_EMERALD, e));
    assert(gen3_base_stats(386, GEN3_GAME_FRLG, fr));
    assert(memcmp(rs, e, 6) != 0 && memcmp(e, fr, 6) != 0);
    assert(rs[0] == 50 && e[0] == 50 && fr[0] == 50); /* HP is the same form to form */

    /* Everything else is game-independent. */
    uint8_t a[6], b[6];
    assert(gen3_base_stats(25, GEN3_GAME_RS, a) && gen3_base_stats(25, GEN3_GAME_FRLG, b));
    assert(memcmp(a, b, 6) == 0);

    /* Gender comes from the PID compared against the species ratio. */
    Gen3Pokemon p; memset(&p, 0, sizeof(p));
    p.species_internal = gen3_species_internal_from_national(1); /* 12.5% female */
    p.pid = 0x00000000u;  assert(gen3_gender(&p) == GEN3_GENDER_FEMALE);
    p.pid = 0x000000FFu;  assert(gen3_gender(&p) == GEN3_GENDER_MALE);

    p.species_internal = gen3_species_internal_from_national(132); /* Ditto, genderless */
    assert(gen3_gender(&p) == GEN3_GENDER_GENDERLESS);
    p.species_internal = gen3_species_internal_from_national(113); /* Chansey, all female */
    assert(gen3_gender(&p) == GEN3_GENDER_FEMALE);

    /* The ability bit only selects a second ability when one exists. */
    p.species_internal = gen3_species_internal_from_national(1);
    p.ability_bit = false;
    const uint8_t first = gen3_ability_id(&p);
    p.ability_bit = true;
    assert(gen3_ability_id(&p) == first); /* Bulbasaur has only Overgrow */

    p.species_internal = gen3_species_internal_from_national(25); /* Pikachu: Static only in Gen 3 */
    assert(gen3_ability_id(&p) != 0);

    /* Experience curves: level 1 costs nothing, level 100 is the known total. */
    const uint32_t max_exp[6] = { 1000000u, 600000u, 1640000u, 1059860u, 800000u, 1250000u };
    for (uint8_t g = 0; g < 6u; ++g) {
        assert(gen3_exp_for_level(1, g) == 0);
        assert(gen3_exp_for_level(100, g) == max_exp[g]);
        assert(gen3_level_from_exp(0, g) == 1);
        assert(gen3_level_from_exp(max_exp[g], g) == 100);
        assert(gen3_level_from_exp(max_exp[g] + 1000u, g) == 100);
        /* Round-trip every level through its own threshold. */
        for (uint8_t lv = 1; lv <= 100u; ++lv)
            assert(gen3_level_from_exp(gen3_exp_for_level(lv, g), g) == lv);
    }

    /* A level-100 Bulbasaur with perfect IVs, no EVs and a neutral nature. */
    Gen3Pokemon s; memset(&s, 0, sizeof(s));
    s.species_internal = gen3_species_internal_from_national(1);
    for (unsigned i = 0; i < 6u; ++i) s.ivs[i] = 31;
    s.level = 100;
    s.pid = 0;                       /* nature 0 = Hardy, neutral */
    assert(gen3_nature(&s) == 0);
    uint16_t st[6];
    assert(gen3_calc_stats(&s, GEN3_GAME_EMERALD, st));
    assert(st[0] == 2u * 45u + 31u + 110u);   /* HP: 2*base + IV + level + 10 */
    assert(st[1] == 2u * 49u + 31u + 5u);     /* Atk */

    /* Nature must move exactly two stats, by 10% each. */
    Gen3Pokemon n = s;
    assert(gen3_set_nature(&n, 3));  /* Adamant: +Atk, -SpA */
    uint16_t nst[6];
    assert(gen3_calc_stats(&n, GEN3_GAME_EMERALD, nst));
    assert(nst[1] == (uint16_t)((st[1] * 110u) / 100u));
    assert(nst[4] == (uint16_t)((st[4] * 90u) / 100u));
    assert(nst[0] == st[0] && nst[2] == st[2] && nst[3] == st[3] && nst[5] == st[5]);

    /* Shedinja always has one hit point. */
    Gen3Pokemon sh = s;
    sh.species_internal = gen3_species_internal_from_national(292);
    assert(gen3_calc_stats(&sh, GEN3_GAME_EMERALD, st) && st[0] == 1u);

    /* A box record has no level byte, so it comes from experience. */
    Gen3Pokemon box; memset(&box, 0, sizeof(box));
    box.species_internal = gen3_species_internal_from_national(1);
    const Gen3Personal *bp = gen3_personal(1);
    box.experience = gen3_exp_for_level(37, bp->growth);
    assert(box.level == 0 && gen3_effective_level(&box) == 37);

    printf("  personal table matches PKHeX for stats, types, gender and abilities\n");
}

/*
 * The keyboard writes Gen III bytes, so the encoder has to be the exact
 * inverse of the decoder for every character it claims to support.
 */
static void test_text_codec(void)
{
    const char *printable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        " .,'-!?:/&+()%$";
    for (const char *c = printable; *c; ++c) {
        const uint8_t enc = gen3_encode_char(*c);
        assert(enc != GEN3_TEXT_TERMINATOR);
        assert(gen3_decode_char(enc) == *c);
    }
    /* The gender symbols are shown as M and F, but those letters must encode
     * back to letters, never to the symbols. */
    assert(gen3_decode_char(0xB5) == 'M' && gen3_encode_char('M') == 0xC7);
    assert(gen3_decode_char(0xB6) == 'F' && gen3_encode_char('F') == 0xC0);
    /* Characters Generation III has no slot for are reported, not guessed. */
    assert(gen3_encode_char('~') == GEN3_TEXT_TERMINATOR);
    assert(gen3_encode_char('#') == GEN3_TEXT_TERMINATOR);

    /* A short name is padded to the full field width with terminators. */
    uint8_t field[10];
    gen3_encode_text("MEW", field, sizeof(field));
    assert(field[0] == 0xC7 && field[1] == 0xBF && field[2] == 0xD1);
    for (unsigned i = 3; i < sizeof(field); ++i) assert(field[i] == GEN3_TEXT_TERMINATOR);

    char back[24];
    gen3_decode_text(field, sizeof(field), false, back, sizeof(back));
    assert(strcmp(back, "MEW") == 0);

    /* A space inside a name is the byte 0x00, which must survive decoding;
     * dropping it turned "SAM B" into "SAMB". Trailing padding still goes. */
    gen3_encode_text("SAM B", field, sizeof(field));
    assert(field[3] == 0x00);
    gen3_decode_text(field, sizeof(field), false, back, sizeof(back));
    assert(strcmp(back, "SAM B") == 0);
    uint8_t padded[10];
    memset(padded, 0, sizeof(padded));
    padded[0] = 0xC7; padded[1] = 0xBF; padded[2] = 0xD1;   /* MEW then zeroes */
    gen3_decode_text(padded, sizeof(padded), false, back, sizeof(back));
    assert(strcmp(back, "MEW") == 0);

    /* Overlong input is truncated to the field, not written past it. */
    uint8_t narrow[4] = { 0, 0, 0, 0 };
    gen3_encode_text("ABCDEFGH", narrow, sizeof(narrow));
    assert(narrow[0] == 0xBB && narrow[3] == 0xBE);

    printf("  text encoder inverts the decoder for every supported character\n");
}

/*
 * Ribbon bit layout, against PKHeX's PK3: five three-bit contest levels in
 * bits 0-14, twelve single-bit ribbons in bits 15-26, fateful in bit 31.
 */
static void test_ribbons(void)
{
    Gen3Pokemon p;
    memset(&p, 0, sizeof(p));
    p.present = true;

    for (unsigned c = 0; c < GEN3_CONTEST_RIBBON_COUNT; ++c) {
        assert(gen3_set_contest_ribbon(&p, c, GEN3_CONTEST_RIBBON_MAX));
        assert(gen3_contest_ribbon(&p, c) == GEN3_CONTEST_RIBBON_MAX);
        assert(p.ribbons == (4u << (c * 3u)));
        assert(gen3_set_contest_ribbon(&p, c, 0));
        assert(p.ribbons == 0);
    }
    /* Five is past Master, so it is refused rather than clipped. */
    assert(!gen3_set_contest_ribbon(&p, 0, 5));
    assert(!gen3_set_contest_ribbon(&p, GEN3_CONTEST_RIBBON_COUNT, 1));

    for (unsigned i = 0; i < GEN3_RIBBON_FLAG_COUNT; ++i) {
        assert(gen3_set_ribbon_flag(&p, i, true));
        assert(p.ribbons == (1u << (15u + i)));
        assert(gen3_ribbon_flag(&p, i));
        assert(gen3_set_ribbon_flag(&p, i, false));
        assert(p.ribbons == 0);
    }
    /* The unused bits and the fateful bit are never touched by a ribbon. */
    assert(!gen3_set_ribbon_flag(&p, GEN3_RIBBON_FLAG_COUNT, true));

    /* Contest levels count as one ribbon per level; flags count as one each. */
    assert(gen3_set_contest_ribbon(&p, 0, 4) && gen3_set_contest_ribbon(&p, 1, 2));
    assert(gen3_set_ribbon_flag(&p, 0, true) && gen3_set_ribbon_flag(&p, 11, true));
    assert(gen3_ribbon_count(&p) == 4u + 2u + 2u);

    /* Ribbons and fateful share a word, and must not disturb each other. */
    p.fateful = true;
    p.species_internal = 1;
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_EMERALD;
    Gen3Pokemon back;
    assert(gen3_set_box_pokemon(&save, 0, 0, &p));
    assert(gen3_box_pokemon(&save, 0, 0, &back));
    assert(back.fateful);
    assert(gen3_ribbon_count(&back) == gen3_ribbon_count(&p));
    assert(gen3_contest_ribbon(&back, 0) == 4 && gen3_contest_ribbon(&back, 1) == 2);
    assert(gen3_ribbon_flag(&back, 0) && gen3_ribbon_flag(&back, 11));

    printf("  ribbon levels and flags match the PK3 bit layout\n");
}

/*
 * Two records built to the shape a real save shows, with no trainer identity
 * in them. The first is a slot a Pokemon was withdrawn from: the game clears
 * the flag byte and the species and leaves everything else where it was, so
 * the body still decrypts and its checksum still matches - nothing but that
 * one byte says the slot is free. The second is a slot holding a Rayquaza.
 *
 * The save that exposed this held two Pokemon and a hundred and fifty-four
 * slots of residue, and reading residue as occupied filled the boxes with
 * nameless entries. tools/build_pk3_fixture.py regenerates these.
 */
/* flag byte clear: the slot is free */
static const uint8_t residue_slot[80] = {
    0x05, 0x1D, 0x6A, 0x2C, 0x22, 0x22, 0x11, 0x11, 0xCC, 0xC2, 0xD3, 0xC2,
    0xC9, 0xCC, 0xC8, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0xCE, 0xBF, 0xCD, 0xCE,
    0xBF, 0xCC, 0xFF, 0x00, 0xAC, 0xD2, 0x00, 0x00, 0x27, 0x3F, 0x7B, 0x3D,
    0x27, 0x3F, 0x7B, 0x3D, 0x27, 0x3F, 0x7B, 0x3D, 0x27, 0x3F, 0x5E, 0x3D,
    0xF5, 0x6A, 0x7B, 0x3D, 0x27, 0x79, 0x7B, 0x3D, 0x27, 0x3F, 0x7B, 0x3D,
    0x93, 0xE5, 0xF0, 0x0E, 0x27, 0x3F, 0x7B, 0x3D, 0x06, 0x3F, 0x56, 0x3D,
    0x27, 0x3F, 0x7B, 0x3D, 0x33, 0x2B, 0x6F, 0x29,
};

/* flag byte 0x02: Has Species */
static const uint8_t real_slot[80] = {
    0x14, 0x3E, 0xA0, 0x51, 0x44, 0x44, 0x33, 0x33, 0xCC, 0xBB, 0xD3, 0xCB,
    0xCF, 0xBB, 0xD4, 0xBB, 0xFF, 0xFF, 0x02, 0x02, 0xCE, 0xBF, 0xCD, 0xCE,
    0xBF, 0xCC, 0xFF, 0x00, 0x25, 0xAA, 0x00, 0x00, 0x50, 0x7A, 0x93, 0x62,
    0x8F, 0x0D, 0x2D, 0x57, 0x50, 0x7A, 0x93, 0x62, 0x98, 0x7A, 0xAC, 0x62,
    0xBF, 0x7A, 0x93, 0x62, 0x44, 0x6E, 0x87, 0x76, 0xC6, 0x7B, 0x93, 0x62,
    0x9E, 0xF0, 0x95, 0x62, 0x50, 0x3C, 0x93, 0x62, 0x50, 0x7A, 0x93, 0x62,
    0x50, 0x7A, 0x93, 0x62, 0x50, 0x7A, 0x93, 0x62,
};

static void test_withdrawn_slots_are_empty(void)
{
    Gen3Pokemon p;

    /* The residue decrypts cleanly - its checksum even matches - so nothing
     * about the body says the slot is free. Only the flag byte does. */
    assert(gen3_parse_pk3_record(residue_slot, sizeof(residue_slot), false, &p));
    assert(!p.present);
    assert(residue_slot[0x13] == 0);
    assert((residue_slot[0x13] & (uint8_t)~GEN3_PK3_FLAG_IS_EGG) != GEN3_PK3_FLAG_HAS_SPECIES);

    assert(gen3_parse_pk3_record(real_slot, sizeof(real_slot), false, &p));
    assert(p.present && p.checksum_ok);
    assert(gen3_species_national(p.species_internal) == 384);   /* Rayquaza */
    assert(strcmp(p.nickname, "RAYQUAZA") == 0);
    assert(!p.is_egg);
    assert(real_slot[0x13] == GEN3_PK3_FLAG_HAS_SPECIES);

    /* Writing a record has to set the flag, or it lands in a slot that still
     * reads as empty. */
    Gen3Save save; memset(&save, 0, sizeof(save));
    save.game = GEN3_GAME_FRLG;
    Gen3Pokemon in; fill_reference(&in);
    in.level = 0;
    assert(gen3_set_box_pokemon(&save, 0, 0, &in));
    const size_t at = 4u + 0u * GEN3_PK3_STORED_SIZE;
    assert((save.storage[at + 0x13] & GEN3_PK3_FLAG_HAS_SPECIES) != 0);
    assert((save.storage[at + 0x13] & GEN3_PK3_FLAG_IS_BAD_EGG) == 0);
    Gen3Pokemon out;
    assert(gen3_box_pokemon(&save, 0, 0, &out) && out.present);

    /* An egg keeps both bits, and still reads as occupied. */
    in.is_egg = true;
    assert(gen3_set_box_pokemon(&save, 0, 1, &in));
    assert(save.storage[4u + GEN3_PK3_STORED_SIZE + 0x13] ==
           (GEN3_PK3_FLAG_HAS_SPECIES | GEN3_PK3_FLAG_IS_EGG));
    assert(gen3_box_pokemon(&save, 0, 1, &out) && out.present && out.is_egg);

    printf("  a withdrawn slot reads as empty, however much data it still holds\n");
}

int main(void)
{
    test_species_index_conversion();
    test_personal_table();
    test_pokedex_flags();
    test_event_flags_and_work();
    test_box_wallpapers();
    test_party_round_trip();
    test_box_round_trip();
    test_every_shuffle_order_round_trips();
    test_egg_flag_is_mirrored();
    test_derived_values();
    test_nature_change_preserves_shininess();
    test_text_codec();
    test_ribbons();
    test_withdrawn_slots_are_empty();
    printf("PK3 field tests: PASS\n");
    return 0;
}
