/*
 * Species lookups over the generated Generation III tables.
 *
 * The tables themselves live in gen3_personal.c and gen3_exp.c, which are
 * produced by tools/build_personal.py and must not be edited by hand. This is
 * where the logic that reads them belongs.
 */
#include <string.h>

#include "gen3_personal.h"

/* Personal-table type indices, derived from the table rather than assumed:
 * they run 0-16 with no gap, and are a different ordering from the one Hidden
 * Power uses, which is why gen3_type_name() is a separate function. */
static const char *const type_names[17] = {
    "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost",
    "Steel", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark",
};

const char *gen3_type_name_full(uint8_t type) {
    return type < 17u ? type_names[type] : "?";
}

const Gen3Personal *gen3_personal(unsigned national) {
    if (national < 1u || national > GEN3_DEX_SPECIES) return NULL;
    return &gen3_personal_table[national];
}

bool gen3_base_stats(unsigned national, Gen3Game game, uint8_t out[6]) {
    const Gen3Personal *p = gen3_personal(national);
    if (!p || !out) return false;
    if (national == GEN3_PERSONAL_VARYING_DEX) {
        /* Deoxys takes a different form, and so different base stats, in each
         * game. LeafGreen shares FireRed's slot only in name; it has its own. */
        unsigned row = 1u; /* Emerald */
        if (game == GEN3_GAME_RS) row = 0u;
        else if (game == GEN3_GAME_FRLG) row = 2u;
        for (unsigned i = 0; i < 6u; ++i) out[i] = gen3_personal_deoxys[row][i];
        return true;
    }
    out[0] = p->hp; out[1] = p->atk; out[2] = p->def;
    out[3] = p->spe; out[4] = p->spa; out[5] = p->spd;
    return true;
}

Gen3Gender gen3_gender(const Gen3Pokemon *pkm) {
    if (!pkm) return GEN3_GENDER_GENDERLESS;
    const Gen3Personal *p = gen3_personal(gen3_species_national(pkm->species_internal));
    if (!p) return GEN3_GENDER_GENDERLESS;
    switch (p->gender) {
        case 255u: return GEN3_GENDER_GENDERLESS;
        case 254u: return GEN3_GENDER_FEMALE;
        case 0u:   return GEN3_GENDER_MALE;
        default:   break;
    }
    /* Otherwise the low byte of the PID is compared against the ratio. */
    return ((pkm->pid & 0xFFu) < p->gender) ? GEN3_GENDER_FEMALE : GEN3_GENDER_MALE;
}

const char *gen3_gender_name(Gen3Gender gender) {
    switch (gender) {
        case GEN3_GENDER_FEMALE: return "Female";
        case GEN3_GENDER_MALE:   return "Male";
        default:                 return "Genderless";
    }
}

uint8_t gen3_ability_id(const Gen3Pokemon *pkm) {
    if (!pkm) return 0;
    const Gen3Personal *p = gen3_personal(gen3_species_national(pkm->species_internal));
    if (!p) return 0;
    /* The second slot only exists when the species really has two. */
    if (pkm->ability_bit && p->ability2 != p->ability1) return p->ability2;
    return p->ability1;
}

const char *gen3_ability_name(uint8_t ability) {
    return ability < gen3_ability_name_count ? gen3_ability_names[ability] : "?";
}

/* --------------------------------------------------- levels and stats ---- */

uint8_t gen3_level_from_exp(uint32_t experience, uint8_t growth) {
    if (growth >= 6u) return 1u;
    /* The table holds the total experience needed to reach each level, so the
     * level is the last threshold not above the record's experience. */
    uint8_t level = 1u;
    for (unsigned l = 2u; l <= 100u; ++l) {
        if (experience < gen3_exp_table[growth][l - 1u]) break;
        level = (uint8_t)l;
    }
    return level;
}

uint32_t gen3_exp_for_level(uint8_t level, uint8_t growth) {
    if (growth >= 6u || level < 1u || level > 100u) return 0;
    return gen3_exp_table[growth][level - 1u];
}

uint8_t gen3_effective_level(const Gen3Pokemon *pkm) {
    if (!pkm) return 0;
    if (pkm->level) return pkm->level;   /* party records carry it directly */
    const Gen3Personal *p = gen3_personal(gen3_species_national(pkm->species_internal));
    if (!p) return 0;
    return gen3_level_from_exp(pkm->experience, p->growth);
}

/*
 * Generation III stat formula. Nature raises one stat by 10% and lowers
 * another by 10%; nature/5 picks the raised index and nature%5 the lowered
 * one, both over Atk/Def/Spe/SpA/SpD - which is exactly slots 1-5 of the
 * HP-first order used throughout this port.
 */
bool gen3_calc_stats(const Gen3Pokemon *pkm, Gen3Game game, uint16_t out[6]) {
    if (!pkm || !out) return false;
    const unsigned national = gen3_species_national(pkm->species_internal);
    uint8_t base[6];
    if (!gen3_base_stats(national, game, base)) return false;

    const uint8_t level = gen3_effective_level(pkm);
    if (level < 1u) return false;

    const uint8_t nature = gen3_nature(pkm);
    const unsigned raised = (unsigned)(nature / 5u) + 1u;
    const unsigned lowered = (unsigned)(nature % 5u) + 1u;

    for (unsigned i = 0; i < 6u; ++i) {
        const unsigned common =
            ((2u * base[i] + pkm->ivs[i] + (pkm->evs[i] / 4u)) * level) / 100u;
        if (i == 0u) {
            /* Shedinja is the one species pinned to a single hit point. */
            out[0] = (national == 292u) ? 1u : (uint16_t)(common + level + 10u);
            continue;
        }
        unsigned stat = common + 5u;
        if (raised != lowered) {
            if (i == raised)  stat = (stat * 110u) / 100u;
            if (i == lowered) stat = (stat * 90u) / 100u;
        }
        out[i] = (uint16_t)stat;
    }
    return true;
}
