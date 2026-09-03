/*
 * Generation III legality analysis. See gen3_legality.h for what this is and
 * is not.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gen3_legality.h"
#include "gen3_learnsets.h"
#include "gen3_personal.h"

/* ------------------------------------------------------------- findings -- */

static void add(Gen3LegalityReport *r, Gen3Verdict verdict, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void add(Gen3LegalityReport *r, Gen3Verdict verdict, const char *fmt, ...)
{
    if (r->count >= GEN3_LEGALITY_MAX_FINDINGS) return;
    Gen3Finding *f = &r->findings[r->count++];
    f->verdict = verdict;

    va_list args;
    va_start(args, fmt);
    vsnprintf(f->text, sizeof(f->text), fmt, args);
    va_end(args);

    if (verdict > r->worst) r->worst = verdict;
}

const char *gen3_verdict_name(Gen3Verdict verdict)
{
    switch (verdict) {
        case GEN3_LEGAL:      return "looks legal";
        case GEN3_NOTE:       return "note";
        case GEN3_SUSPICIOUS: return "suspicious";
        default:              return "impossible";
    }
}

/* ------------------------------------------------------------------ RNG -- */

uint32_t gen3_rng_next(uint32_t seed)
{
    return seed * 0x41C64E6Du + 0x00006073u;
}

/*
 * A Generation III Pokemon's PID and IVs come from four consecutive draws of
 * the generator's top sixteen bits: the low half of the PID, the high half,
 * then two fifteen-bit IV words. The methods differ in whether a draw is
 * skipped between them, which is a real hardware artefact rather than a
 * classification anyone invented.
 *
 * The search does not need to find the original seed. Everything after the
 * first draw is determined by that draw, so enumerating the 65536 states whose
 * top half is the PID's low half - and keeping those whose successor gives the
 * PID's high half - covers every possibility. That is a few hundred thousand
 * operations, which is nothing on this console and means no lookup tables.
 */
#define IV_CHUNK(a, b, c) ((unsigned)((a) | ((b) << 5) | ((c) << 10)))

unsigned gen3_rng_method(uint32_t pid, const uint8_t ivs[6])
{
    if (!ivs) return 0;
    const unsigned iv1 = IV_CHUNK(ivs[0] & 31u, ivs[1] & 31u, ivs[2] & 31u); /* HP, Atk, Def */
    const unsigned iv2 = IV_CHUNK(ivs[3] & 31u, ivs[4] & 31u, ivs[5] & 31u); /* Spe, SpA, SpD */
    const uint16_t low = (uint16_t)(pid & 0xFFFFu);
    const uint16_t high = (uint16_t)(pid >> 16);

    for (uint32_t tail = 0; tail < 0x10000u; ++tail) {
        const uint32_t r1 = ((uint32_t)low << 16) | tail;
        const uint32_t r2 = gen3_rng_next(r1);
        if ((uint16_t)(r2 >> 16) != high) continue;

        const uint32_t r3 = gen3_rng_next(r2);
        const uint32_t r4 = gen3_rng_next(r3);
        const uint32_t r5 = gen3_rng_next(r4);
        const unsigned a = (r3 >> 16) & 0x7FFFu;
        const unsigned b = (r4 >> 16) & 0x7FFFu;
        const unsigned c = (r5 >> 16) & 0x7FFFu;

        if (a == iv1 && b == iv2) return 1;   /* consecutive */
        if (b == iv1 && c == iv2) return 2;   /* one draw skipped before the IVs */
        if (a == iv1 && c == iv2) return 4;   /* one draw skipped between them */
    }
    return 0;
}

/* ---------------------------------------------------------------- moves -- */

/*
 * A move is accepted if ANY Generation III game can teach it to the species,
 * not just the game whose save this is: a traded Pokemon legitimately knows
 * moves the receiving game has never heard of, and refusing those would flag
 * half of every well-travelled box.
 */
static bool learnable(unsigned national, uint16_t move, uint8_t level)
{
    static const Gen3Game games[3] = { GEN3_GAME_RS, GEN3_GAME_EMERALD, GEN3_GAME_FRLG };
    for (unsigned g = 0; g < 3u; ++g) {
        const Gen3LevelMove *list = NULL;
        const unsigned n = gen3_level_moves(games[g], national, &list);
        for (unsigned i = 0; i < n; ++i)
            if (list[i].move == move && (level == 0u || list[i].level <= level)) return true;
    }

    const unsigned short *eggs = NULL;
    const unsigned egg_count = gen3_egg_moves(national, &eggs);
    for (unsigned i = 0; i < egg_count; ++i)
        if (eggs[i] == move) return true;

    for (unsigned m = 0; m < gen3_machine_count(); ++m)
        if (gen3_learns_machine(national, m) && gen3_machine_move(m) == move) return true;
    for (unsigned t = 0; t < gen3_tutor_count(); ++t)
        if (gen3_learns_tutor(national, t) && gen3_tutor_move(t) == move) return true;

    return false;
}

/* --------------------------------------------------------------- checks -- */

void gen3_check_legality(const Gen3Pokemon *p, Gen3Game game, Gen3LegalityReport *r)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    if (!p || !p->present) {
        add(r, GEN3_NOTE, "Empty slot.");
        return;
    }

    if (!p->checksum_ok)
        add(r, GEN3_INVALID, "The record's checksum does not match its contents.");

    const unsigned national = gen3_species_national(p->species_internal);
    if (national < 1u || national > GEN3_DEX_SPECIES) {
        add(r, GEN3_INVALID, "Species %u is not a Generation III species.", p->species_internal);
        return;   /* nothing below can mean anything without a species */
    }
    const Gen3Personal *personal = gen3_personal(national);
    if (!personal) return;

    /* Effort values: 255 a stat is the byte's own limit, 510 in total is the
     * games' and is the one an editor can exceed. */
    unsigned ev_total = 0;
    for (unsigned i = 0; i < 6u; ++i) ev_total += p->evs[i];
    if (ev_total > 510u)
        add(r, GEN3_INVALID, "Effort values total %u; the games cap the total at 510.", ev_total);

    /* Experience against the species' growth curve. */
    const uint8_t level = p->level ? p->level : gen3_level_from_exp(p->experience, personal->growth);
    if (level < 1u || level > 100u) {
        add(r, GEN3_INVALID, "Level %u is outside 1-100.", level);
    } else {
        const uint32_t floor_exp = gen3_exp_for_level(level, personal->growth);
        const uint32_t next_exp = level < 100u ? gen3_exp_for_level((uint8_t)(level + 1u), personal->growth) : 0xFFFFFFFFu;
        if (p->experience < floor_exp || (level < 100u && p->experience >= next_exp))
            add(r, GEN3_INVALID, "Experience %lu does not belong to level %u.",
                (unsigned long)p->experience, level);
    }

    if (p->met_level > level && level)
        add(r, GEN3_INVALID, "Met at level %u but is level %u.", p->met_level, level);

    /*
     * The second ability slot only exists for species that have two. The
     * Generation III personal table writes the same ability into both slots
     * for a species with one, rather than leaving the second empty, so testing
     * for zero finds nothing - they have to be compared.
     */
    if (p->ability_bit && (personal->ability2 == 0u || personal->ability2 == personal->ability1))
        add(r, GEN3_INVALID, "Has the second ability set, but %s has only one.",
            gen3_species_name(p->species_internal));

    /* Enumerations the games never write outside their range. */
    if (p->origin_game != 1u && p->origin_game != 2u && p->origin_game != 3u &&
        p->origin_game != 4u && p->origin_game != 5u && p->origin_game != 15u)
        add(r, GEN3_INVALID, "Origin game %u is not a Generation III game.", p->origin_game);
    if (p->ball == 0u || p->ball > 12u)
        add(r, GEN3_INVALID, "Ball %u is not one the games use.", p->ball);
    if (p->language == 0u || p->language > 7u)
        add(r, GEN3_INVALID, "Language %u is not one the games use.", p->language);

    /* Pokerus: the strain decides how many days it can have left. */
    const unsigned strain = (unsigned)(p->pokerus >> 4);
    const unsigned days = (unsigned)(p->pokerus & 0x0Fu);
    if (days > (strain % 4u) + 1u)
        add(r, GEN3_INVALID, "Pokerus strain %u cannot have %u days left.", strain, days);

    /* Moves. A blank slot may not sit above a filled one, PP may not exceed
     * what the move plus its PP ups allows, and the species has to be able to
     * learn it somewhere. */
    bool seen_blank = false;
    for (unsigned i = 0; i < 4u; ++i) {
        const uint16_t move = p->moves[i];
        if (!move) { seen_blank = true; continue; }
        if (seen_blank)
            add(r, GEN3_INVALID, "Move slot %u is filled below an empty one.", i + 1u);
        if (move > 354u) {
            add(r, GEN3_INVALID, "Move %u does not exist in Generation III.", move);
            continue;
        }
        for (unsigned j = 0; j < i; ++j)
            if (p->moves[j] == move)
                add(r, GEN3_INVALID, "%s appears twice.", gen3_move_name(move));

        const unsigned ups = (p->pp_ups >> (i * 2u)) & 3u;
        const unsigned base = gen3_move_pp(move);
        if (base) {
            const unsigned max_pp = base + (base / 5u) * ups;
            if (p->pp[i] > max_pp)
                add(r, GEN3_INVALID, "%s has %u PP; %u is its maximum.",
                    gen3_move_name(move), p->pp[i], max_pp);
        }
        if (!learnable(national, move, level))
            add(r, GEN3_INVALID, "%s cannot be learned by %s in any Generation III game.",
                gen3_move_name(move), gen3_species_name(p->species_internal));
    }

    /* Eggs. */
    if (p->is_egg) {
        if (level != 5u)
            add(r, GEN3_INVALID, "An egg is level 5; this one is level %u.", level);
        if (ev_total)
            add(r, GEN3_INVALID, "An egg cannot have effort values.");
    }

    /*
     * The RNG. Colosseum and XD generate their Pokemon by another route
     * entirely, and eggs are bred rather than drawn, so neither is expected to
     * match and neither is flagged.
     */
    const bool gc_origin = p->origin_game == 15u;
    r->rng_method = gen3_rng_method(p->pid, p->ivs);
    if (r->rng_method)
        add(r, GEN3_NOTE, "PID and IVs match Generation III RNG method %u.", r->rng_method);
    else if (p->is_egg)
        add(r, GEN3_NOTE, "Bred, so no wild RNG method is expected to match.");
    else if (gc_origin)
        add(r, GEN3_NOTE, "From Colosseum or XD, which generate Pokemon another way.");
    else
        add(r, GEN3_SUSPICIOUS,
            "No Generation III RNG method produces this PID and IV pair.");

    (void)game;
    if (!r->count) add(r, GEN3_LEGAL, "Nothing to report.");
}
