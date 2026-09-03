/*
 * Move, item and species names.
 *
 * These were hand-transcribed once and five entries came out wrong, so the
 * tables are generated from PKHeX's lists now. What is worth pinning is the
 * handful of names that differ from the spellings a Generation III cartridge
 * itself shows - those are the ones a hand-editor would "correct" back - plus
 * the two structural things the old lookup got wrong: the GameCube games'
 * own key items, and returning a pointer into one shared buffer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen3.h"
#include "gen3_all.h"

static void test_counts(void)
{
    assert(strcmp(gen3_move_name(354), "Psycho Boost") == 0);   /* the last one */
    assert(strcmp(gen3_move_name(355), "Unknown move") == 0);
    assert(strcmp(gen3_item_name(376), "Old Sea Map") == 0);
    assert(strcmp(gen3_item_name(377), "???") == 0);
    assert(strcmp(gen3_species_name(gen3_species_internal_from_national(386)), "Deoxys") == 0);
    printf("  the tables end where Generation III ends\n");
}

/* PKHeX uses the modern spellings, and this is a port of PKHeX. Each of these
 * was previously transcribed as the cartridge spells it. */
static void test_pkhex_spellings(void)
{
    assert(strcmp(gen3_move_name(11), "Vise Grip") == 0);        /* not Vice Grip */
    assert(strcmp(gen3_move_name(136), "High Jump Kick") == 0);  /* not Hi Jump Kick */
    assert(strcmp(gen3_move_name(185), "Feint Attack") == 0);    /* not Faint Attack */
    assert(strcmp(gen3_item_name(96), "Thunder Stone") == 0);    /* not Thunderstone */
    assert(strcmp(gen3_item_name(362), "Vs. Seeker") == 0);      /* not VS Seeker */
    printf("  the five names that used to disagree with PKHeX agree now\n");
}

static void test_machines_are_named(void)
{
    assert(strcmp(gen3_item_name(289), "TM01") == 0);
    assert(strcmp(gen3_item_name(338), "TM50") == 0);
    assert(strcmp(gen3_item_name(339), "HM01") == 0);
    assert(strcmp(gen3_item_name(346), "HM08") == 0);
    printf("  every TM and HM has a name of its own\n");
}

/*
 * The old lookup built TM names and unknown ids into one shared static buffer
 * and returned a pointer to it, so naming two items in a single expression
 * showed the same name twice.
 */
static void test_two_names_at_once(void)
{
    const char *a = gen3_item_name(289);
    const char *b = gen3_item_name(338);
    assert(a != b);
    assert(strcmp(a, "TM01") == 0 && strcmp(b, "TM50") == 0);

    const char *unknown_a = gen3_item_name(400);
    const char *unknown_b = gen3_item_name(500);
    assert(strcmp(unknown_a, "???") == 0 && strcmp(unknown_b, "???") == 0);
    printf("  two item names in one expression stay two names\n");
}

/* Colosseum and XD share the cartridge ids and add key items above 500. */
static void test_gamecube_key_items(void)
{
    assert(strcmp(gen3_item_name_for(GEN3_KIND_COLOSSEUM, 4), "Poke Ball") == 0);
    assert(strcmp(gen3_item_name_for(GEN3_KIND_XD, 4), "Poke Ball") == 0);

    assert(strcmp(gen3_item_name_for(GEN3_KIND_COLOSSEUM, 501), "Elevator Key") == 0);
    assert(strcmp(gen3_item_name_for(GEN3_KIND_XD, 501), "Elevator Key") == 0);
    /* The two games disagree about their own id 500. */
    assert(strcmp(gen3_item_name_for(GEN3_KIND_COLOSSEUM, 500), "Jail Key") == 0);
    assert(strcmp(gen3_item_name_for(GEN3_KIND_XD, 500), "Safe Key") == 0);

    /* A cartridge save has nothing up there, and neither game has everything. */
    assert(strcmp(gen3_item_name_for(GEN3_KIND_GBA, 500), "???") == 0);
    assert(strcmp(gen3_item_name_for(GEN3_KIND_XD, 9000), "???") == 0);
    printf("  Colosseum and XD name their own key items, and only their own\n");
}

static void test_species_use_the_internal_index(void)
{
    /* Internal 406 is Rayquaza; a linear offset would make it Latios, which is
     * exactly what a hardware run once showed. */
    assert(strcmp(gen3_species_name(406), "Rayquaza") == 0);
    assert(strcmp(gen3_species_name(1), "Bulbasaur") == 0);
    assert(strcmp(gen3_species_name(0), "Unknown") == 0);
    assert(strcmp(gen3_species_name(9999), "Unknown") == 0);
    /* The gender signs have no glyph in the UI font. */
    assert(strcmp(gen3_species_name(gen3_species_internal_from_national(29)), "Nidoran(F)") == 0);
    assert(strcmp(gen3_species_name(gen3_species_internal_from_national(32)), "Nidoran(M)") == 0);
    printf("  species names index off the internal number, not the National Dex\n");
}

/* The decoration inventory had a working read/write API and no names at all,
 * so nothing could be shown. PKHeX only lists them in its Decoration3 enum. */
static void test_decorations(void)
{
    assert(strcmp(gen3_decoration_name(0), "-") == 0);
    assert(strcmp(gen3_decoration_name(1), "Small Desk") == 0);
    assert(strcmp(gen3_decoration_name(GEN3_DECORATION_MAX), "Registeel Doll") == 0);
    assert(strcmp(gen3_decoration_name(GEN3_DECORATION_MAX + 1), "???") == 0);
    /* Words the enum spells in a way plain title case would mangle. */
    assert(strcmp(gen3_decoration_name(2), "Pokemon Desk") == 0);
    printf("  secret base decorations are named, all %u of them\n",
           (unsigned)GEN3_DECORATION_MAX + 1u);
}

static void test_every_name_is_printable_ascii(void)
{
    unsigned checked = 0;
    for (unsigned i = 0; i < 355u; ++i, ++checked)
        for (const char *p = gen3_move_name((uint16_t)i); *p; ++p)
            assert((unsigned char)*p >= 0x20u && (unsigned char)*p <= 0x7Eu);
    for (unsigned i = 0; i < 377u; ++i, ++checked)
        for (const char *p = gen3_item_name((uint16_t)i); *p; ++p)
            assert((unsigned char)*p >= 0x20u && (unsigned char)*p <= 0x7Eu);
    for (unsigned n = 1; n <= 386u; ++n, ++checked)
        for (const char *p = gen3_species_name(gen3_species_internal_from_national(n)); *p; ++p)
            assert((unsigned char)*p >= 0x20u && (unsigned char)*p <= 0x7Eu);
    assert(checked == 355u + 377u + 386u);
    printf("  all %u names are printable ASCII, which is all the font has\n", checked);
}

int main(void)
{
    test_counts();
    test_pkhex_spellings();
    test_machines_are_named();
    test_two_names_at_once();
    test_gamecube_key_items();
    test_species_use_the_internal_index();
    test_decorations();
    test_every_name_is_printable_ascii();
    printf("name table tests: PASS\n");
    return 0;
}
