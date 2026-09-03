/*
 * Event flag and constant labels.
 *
 * The tables are generated, so what is worth testing is that the lookup finds
 * what is in them: they are sorted for binary search, the three games do not
 * share a numbering, and an index nobody has identified has to come back as
 * unnamed rather than as some neighbour's label.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen3_event_names.h"

static const Gen3Game GAMES[3] = { GEN3_GAME_RS, GEN3_GAME_EMERALD, GEN3_GAME_FRLG };

/* Walk every index a game could hold and confirm the table is ordered and
 * every entry is reachable. A binary search over an unsorted table finds some
 * of its entries and silently misses the rest, so counting matters. */
static void test_tables_are_sorted_and_reachable(void)
{
    unsigned total = 0;
    for (unsigned g = 0; g < 3; ++g) {
        unsigned found = 0, previous = 0;
        bool first = true;
        for (unsigned i = 0; i < 4096u; ++i) {
            const Gen3EventName *e = gen3_event_flag_entry(GAMES[g], i);
            if (!e) continue;
            assert(e->index == i);
            assert(e->name && e->name[0]);
            assert(e->values == NULL && e->value_count == 0u);
            assert(first || previous < e->index);
            previous = e->index;
            first = false;
            ++found;
        }
        assert(found > 300u);
        total += found;
    }
    assert(total > 3000u);
    printf("  %u flag labels, each reachable and in order\n", total);
}

static void test_labels_are_ascii(void)
{
    unsigned checked = 0;
    for (unsigned g = 0; g < 3; ++g) {
        for (unsigned i = 0; i < 4096u; ++i) {
            const Gen3EventName *e = gen3_event_flag_entry(GAMES[g], i);
            if (!e) continue;
            for (const char *p = e->name; *p; ++p)
                assert((unsigned char)*p >= 0x20u && (unsigned char)*p <= 0x7Eu);
            ++checked;
        }
    }
    assert(checked > 3000u);
    printf("  every label is printable ASCII, which is all the font has\n");
}

/* The three games number their flags independently: the same index means
 * different things, or nothing at all, depending on which game is open. */
static void test_games_do_not_share_numbering(void)
{
    assert(gen3_event_flag_name(GEN3_GAME_FRLG, 40) != NULL);
    assert(gen3_event_flag_name(GEN3_GAME_RS, 40) == NULL);
    assert(strstr(gen3_event_flag_name(GEN3_GAME_FRLG, 40), "Bulbasaur") != NULL);
    assert(gen3_event_flag_name(GEN3_GAME_UNKNOWN, 40) == NULL);
    printf("  a flag index means different things in different games\n");
}

static void test_unnamed_indices_stay_unnamed(void)
{
    /* Between named entries, and past the end of every table. */
    assert(gen3_event_flag_name(GEN3_GAME_FRLG, 0) == NULL);
    assert(gen3_event_flag_name(GEN3_GAME_FRLG, 60000) == NULL);
    assert(gen3_event_work_name(GEN3_GAME_FRLG, 60000) == NULL);
    printf("  an index with no label reports none, rather than a neighbour's\n");
}

/* Constants carry named values, which is the difference between showing a
 * player "3" and showing them what the story is waiting on. */
static void test_constants_carry_named_values(void)
{
    const Gen3EventName *e = gen3_event_work_entry(GEN3_GAME_FRLG, 0x55);
    assert(e && strcmp(e->name, "Oak's Lab Status") == 0);
    assert(e->value_count > 4u && e->values != NULL);
    assert(strcmp(gen3_event_work_value_name(GEN3_GAME_FRLG, 0x55, 2), "Starter Available") == 0);
    assert(gen3_event_work_value_name(GEN3_GAME_FRLG, 0x55, 4242) == NULL);

    /* PKHeX writes constant ids as 0x4023 and parses them by dropping the
     * "0x4", so this one is index 0x023, not 0x35. */
    const Gen3EventName *starter = gen3_event_work_entry(GEN3_GAME_RS, 0x23);
    assert(starter && strcmp(starter->name, "Starter Pokemon") == 0);
    assert(strcmp(gen3_event_work_value_name(GEN3_GAME_RS, 0x23, 0), "Treecko") == 0);
    printf("  event constants name the values they are known to take\n");
}

static void test_categories(void)
{
    /* A trainer flag and a story constant, against PKHeX's category letters. */
    const Gen3EventName *trainer = gen3_event_flag_entry(GEN3_GAME_FRLG, 1514);
    assert(trainer && trainer->category == GEN3_EVENT_TRAINER);
    assert(strcmp(gen3_event_category_name(GEN3_EVENT_TRAINER), "Trainer") == 0);
    assert(gen3_event_category_name(GEN3_EVENT_NONE)[0] == '\0');
    printf("  categories follow PKHeX's letters\n");
}

int main(void)
{
    test_tables_are_sorted_and_reachable();
    test_labels_are_ascii();
    test_games_do_not_share_numbering();
    test_unnamed_indices_stay_unnamed();
    test_constants_carry_named_values();
    test_categories();
    printf("event label tests: PASS\n");
    return 0;
}
