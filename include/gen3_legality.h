/*
 * Generation III legality analysis.
 *
 * This is not PKHeX's legality engine. That engine is most of PKHeX.Core and
 * rests on an encounter database this port does not carry. What is here are
 * the checks that can be made from the record, the personal table and the
 * learnsets - which is enough to catch the great majority of edited Pokemon,
 * and to say precisely why rather than just "illegal".
 *
 * Findings are graded, because a checker that cannot tell "this is impossible"
 * from "this is unusual" is worse than no checker at all:
 *
 *   INVALID     the games cannot produce this. A move the species cannot
 *               learn, more effort values than the cap allows, a checksum
 *               that does not match.
 *   SUSPICIOUS  possible, but not by any route this port can confirm. The
 *               main one is a PID and IV pair that no Generation III RNG
 *               method produces - which is how most edited Pokemon are
 *               caught, and which also flags eggs and some event Pokemon
 *               that were generated another way.
 *   NOTE        worth knowing, not a problem.
 */
#ifndef PKHEXGC_GEN3_LEGALITY_H
#define PKHEXGC_GEN3_LEGALITY_H

#include "gen3.h"

typedef enum Gen3Verdict {
    GEN3_LEGAL = 0,
    GEN3_NOTE,
    GEN3_SUSPICIOUS,
    GEN3_INVALID
} Gen3Verdict;

#define GEN3_LEGALITY_MAX_FINDINGS 16u

typedef struct Gen3Finding {
    Gen3Verdict verdict;
    char text[72];
} Gen3Finding;

typedef struct Gen3LegalityReport {
    Gen3Verdict worst;
    unsigned count;
    Gen3Finding findings[GEN3_LEGALITY_MAX_FINDINGS];
    /* Which Generation III RNG method produces this PID and IV pair, if any.
     * Zero means none matched. */
    unsigned rng_method;
} Gen3LegalityReport;

/*
 * `game` is the save the record is sitting in, which decides the level-up
 * lists to check against. A record traded in from another game may legitimately
 * know moves this game's list does not have, so a move is accepted if any
 * Generation III game can teach it.
 */
void gen3_check_legality(const Gen3Pokemon *pokemon, Gen3Game game,
                         Gen3LegalityReport *out);

const char *gen3_verdict_name(Gen3Verdict verdict);

/*
 * The Generation III linear congruential generator, and the method search.
 * Exposed because it is the single most useful check here and worth testing
 * on its own: seed = seed * 0x41C64E6D + 0x6073, and a Pokemon's PID and IVs
 * come from four consecutive draws of its top sixteen bits.
 */
uint32_t gen3_rng_next(uint32_t seed);
unsigned gen3_rng_method(uint32_t pid, const uint8_t ivs[6]);

#endif
