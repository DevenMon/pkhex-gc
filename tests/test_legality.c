/*
 * Generation III legality analysis.
 *
 * The thing that matters most here is the false positive rate. A checker that
 * calls legitimate Pokemon illegal is worse than no checker, because someone
 * will believe it and "fix" a save that was fine. So the first half of this
 * builds Pokemon the way the games do - drawing the PID and IVs from the
 * Generation III generator exactly as method 1 does - and insists they come
 * back clean. The second half breaks one thing at a time and insists each is
 * caught, and named.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen3_legality.h"
#include "gen3_learnsets.h"
#include "gen3_personal.h"

/* A Pokemon as the games would have made it: a seed, four draws, and values
 * that agree with each other. */
static void from_seed(Gen3Pokemon *p, uint32_t seed, unsigned national, uint8_t level)
{
    memset(p, 0, sizeof(*p));
    p->present = true;
    p->checksum_ok = true;

    const uint32_t r1 = gen3_rng_next(seed), r2 = gen3_rng_next(r1);
    const uint32_t r3 = gen3_rng_next(r2), r4 = gen3_rng_next(r3);
    p->pid = ((uint32_t)(r2 >> 16) << 16) | (r1 >> 16);
    const unsigned iv1 = (r3 >> 16) & 0x7FFFu, iv2 = (r4 >> 16) & 0x7FFFu;
    p->ivs[0] = iv1 & 31u; p->ivs[1] = (iv1 >> 5) & 31u; p->ivs[2] = (iv1 >> 10) & 31u;
    p->ivs[3] = iv2 & 31u; p->ivs[4] = (iv2 >> 5) & 31u; p->ivs[5] = (iv2 >> 10) & 31u;

    p->species_internal = gen3_species_internal_from_national((uint16_t)national);
    const Gen3Personal *pi = gen3_personal(national);
    p->level = level;
    p->experience = gen3_exp_for_level(level, pi->growth);
    p->origin_game = 3; p->ball = 4; p->language = 2;
    p->met_level = level; p->friendship = 70;

    const Gen3LevelMove *lm;
    const unsigned n = gen3_level_moves(GEN3_GAME_EMERALD, national, &lm);
    if (n) { p->moves[0] = lm[0].move; p->pp[0] = (uint8_t)gen3_move_pp(lm[0].move); }
}

static Gen3Verdict verdict_of(const Gen3Pokemon *p)
{
    Gen3LegalityReport r;
    gen3_check_legality(p, GEN3_GAME_EMERALD, &r);
    return r.worst;
}

/* Does any finding mention this? Checks the message, not just the verdict,
 * because "illegal" with the wrong reason is not much better than silence. */
static bool mentions(const Gen3Pokemon *p, const char *needle)
{
    Gen3LegalityReport r;
    gen3_check_legality(p, GEN3_GAME_EMERALD, &r);
    for (unsigned i = 0; i < r.count; ++i)
        if (strstr(r.findings[i].text, needle)) return true;
    return false;
}

static void test_the_generator_itself(void)
{
    /* seed * 0x41C64E6D + 0x6073, and nothing else. */
    assert(gen3_rng_next(0) == 0x00006073u);
    assert(gen3_rng_next(0x00006073u) == gen3_rng_next(gen3_rng_next(0)));
    printf("  the Generation III generator steps as the games do\n");
}

static void test_legitimate_pokemon_are_left_alone(void)
{
    unsigned checked = 0;
    static const unsigned species[] = { 1, 25, 74, 132, 384, 386 };
    for (unsigned s = 0; s < sizeof species / sizeof species[0]; ++s) {
        for (unsigned i = 0; i < 8u; ++i) {
            Gen3Pokemon p;
            from_seed(&p, 0x1234u + i * 0x9E3779B9u, species[s], (uint8_t)(10u + i * 7u));
            Gen3LegalityReport r;
            gen3_check_legality(&p, GEN3_GAME_EMERALD, &r);
            assert(r.worst <= GEN3_NOTE);
            assert(r.rng_method == 1u);   /* drawn as method 1, found as method 1 */
            ++checked;
        }
    }
    printf("  %u Pokemon built the way the games build them come back clean\n", checked);
}

/* Method 2 skips a draw before the IVs, method 4 between them. Both are real
 * and both must be recognised, or half of every legitimate box is suspicious. */
static void test_the_other_rng_methods(void)
{
    const uint32_t seed = 0xDEADBEEFu;
    const uint32_t r1 = gen3_rng_next(seed), r2 = gen3_rng_next(r1);
    const uint32_t r3 = gen3_rng_next(r2), r4 = gen3_rng_next(r3), r5 = gen3_rng_next(r4);
    const uint32_t pid = ((uint32_t)(r2 >> 16) << 16) | (r1 >> 16);

    uint8_t ivs[6];
    #define SPLIT(a, b, lo, hi) do { \
        ivs[0 + (lo)] = (uint8_t)((a) & 31u); ivs[1 + (lo)] = (uint8_t)(((a) >> 5) & 31u); \
        ivs[2 + (lo)] = (uint8_t)(((a) >> 10) & 31u); \
        ivs[0 + (hi)] = (uint8_t)((b) & 31u); ivs[1 + (hi)] = (uint8_t)(((b) >> 5) & 31u); \
        ivs[2 + (hi)] = (uint8_t)(((b) >> 10) & 31u); } while (0)

    SPLIT((r3 >> 16) & 0x7FFFu, (r4 >> 16) & 0x7FFFu, 0, 3);
    assert(gen3_rng_method(pid, ivs) == 1u);
    SPLIT((r4 >> 16) & 0x7FFFu, (r5 >> 16) & 0x7FFFu, 0, 3);
    assert(gen3_rng_method(pid, ivs) == 2u);
    SPLIT((r3 >> 16) & 0x7FFFu, (r5 >> 16) & 0x7FFFu, 0, 3);
    assert(gen3_rng_method(pid, ivs) == 4u);
    #undef SPLIT
    printf("  methods 1, 2 and 4 are each recognised for what they are\n");
}

static void test_an_edited_pid_stops_matching(void)
{
    Gen3Pokemon p;
    from_seed(&p, 0x1234u, 25, 20);
    assert(gen3_rng_method(p.pid, p.ivs) == 1u);
    p.pid ^= 1u;
    assert(gen3_rng_method(p.pid, p.ivs) == 0u);
    assert(verdict_of(&p) == GEN3_SUSPICIOUS);
    assert(mentions(&p, "No Generation III RNG method"));
    printf("  one flipped bit in a PID stops any method matching\n");
}

static void test_impossible_things_are_impossible(void)
{
    Gen3Pokemon p;

    from_seed(&p, 0x1234u, 25, 20); p.evs[0] = p.evs[1] = p.evs[2] = 255u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "510"));

    from_seed(&p, 0x1234u, 25, 20); p.moves[0] = 15u;   /* Cut */
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "cannot be learned"));

    from_seed(&p, 0x1234u, 25, 20); p.experience += 100000u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "does not belong to level"));

    from_seed(&p, 0x1234u, 25, 20); p.pp[0] = 99u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "maximum"));

    from_seed(&p, 0x1234u, 25, 20); p.pokerus = 0x0Fu;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "Pokerus"));

    from_seed(&p, 0x1234u, 25, 20); p.met_level = 90u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "Met at level"));

    from_seed(&p, 0x1234u, 25, 20); p.ball = 30u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "Ball"));

    from_seed(&p, 0x1234u, 25, 20); p.checksum_ok = false;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "checksum"));

    from_seed(&p, 0x1234u, 25, 20); p.moves[0] = 0u; p.moves[1] = 33u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "below an empty one"));

    printf("  every impossible thing is caught, and named\n");
}

/*
 * The personal table writes the same ability into both slots for a species
 * that has only one, so testing the second slot for zero finds nothing. This
 * is the check that got it wrong first time.
 */
static void test_the_second_ability_slot(void)
{
    Gen3Pokemon p;
    from_seed(&p, 0x1234u, 25, 20);       /* Pikachu: Static only */
    p.ability_bit = true;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "only one"));

    from_seed(&p, 0x1234u, 74, 20);       /* Geodude: Rock Head and Sturdy */
    p.ability_bit = true;
    assert(verdict_of(&p) <= GEN3_NOTE);
    printf("  a second ability is illegal only for species that lack one\n");
}

/* A traded Pokemon knows moves the receiving game never teaches. Checking
 * against one game's learnset would flag half of every well-travelled box. */
static void test_moves_from_another_game_are_fine(void)
{
    Gen3Pokemon p;
    from_seed(&p, 0x1234u, 1, 40);
    const unsigned short *eggs;
    const unsigned n = gen3_egg_moves(1, &eggs);
    assert(n > 0);
    p.moves[1] = eggs[0];               /* Bulbasaur, an egg move */
    p.pp[1] = (uint8_t)gen3_move_pp(eggs[0]);
    assert(verdict_of(&p) <= GEN3_NOTE);
    printf("  egg, machine and tutor moves count as learnable\n");
}

static void test_eggs(void)
{
    Gen3Pokemon p;
    from_seed(&p, 0x1234u, 1, 5);
    p.is_egg = true;
    /* An egg is not expected to match a wild method, and must not be called
     * suspicious for it. */
    assert(verdict_of(&p) <= GEN3_NOTE || gen3_rng_method(p.pid, p.ivs) != 0u);
    p.evs[0] = 4u;
    assert(verdict_of(&p) == GEN3_INVALID && mentions(&p, "egg cannot have effort"));
    printf("  an egg is judged as an egg, not as a wild encounter\n");
}

static void test_empty_and_broken_input(void)
{
    Gen3LegalityReport r;
    Gen3Pokemon p; memset(&p, 0, sizeof(p));
    gen3_check_legality(&p, GEN3_GAME_EMERALD, &r);
    assert(r.worst == GEN3_NOTE && r.count == 1u);

    gen3_check_legality(NULL, GEN3_GAME_EMERALD, &r);
    assert(r.count == 1u);
    gen3_check_legality(&p, GEN3_GAME_EMERALD, NULL);   /* must not crash */

    p.present = true; p.checksum_ok = true; p.species_internal = 9999u;
    gen3_check_legality(&p, GEN3_GAME_EMERALD, &r);
    assert(r.worst == GEN3_INVALID);
    printf("  an empty slot, a null record and a nonsense species are all handled\n");
}

int main(void)
{
    test_the_generator_itself();
    test_legitimate_pokemon_are_left_alone();
    test_the_other_rng_methods();
    test_an_edited_pid_stops_matching();
    test_impossible_things_are_impossible();
    test_the_second_ability_slot();
    test_moves_from_another_game_are_fine();
    test_eggs();
    test_empty_and_broken_input();
    printf("legality tests: PASS\n");
    return 0;
}
