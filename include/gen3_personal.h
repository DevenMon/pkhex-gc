#ifndef PKHEX_GC_GEN3_PERSONAL_H
#define PKHEX_GC_GEN3_PERSONAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gen3.h"

/*
 * Per-species data the games keep in ROM: base stats, types, gender ratio and
 * the two possible abilities. Generated from PKHeX's Generation III personal
 * tables by tools/build_personal.py.
 */
typedef struct Gen3Personal {
    uint8_t hp, atk, def, spe, spa, spd;
    uint8_t type1, type2;
    uint8_t gender;   /* 0 = always male, 254 = always female, 255 = genderless */
    uint8_t growth;   /* experience curve, 0-5 */
    uint8_t ability1, ability2;
} Gen3Personal;

extern const Gen3Personal gen3_personal_table[387];
extern const uint8_t gen3_personal_deoxys[4][6];
/* Dex number of the one species whose base stats differ per game (Deoxys). */
#define GEN3_PERSONAL_VARYING_DEX 386u
extern const char *const gen3_ability_names[];
extern const unsigned gen3_ability_name_count;

/* Species data by National Dex number; NULL outside 1-386. */
const Gen3Personal *gen3_personal(unsigned national);
/* Base stats, with the per-game Deoxys form applied. */
bool gen3_base_stats(unsigned national, Gen3Game game, uint8_t out[6]);

typedef enum Gen3Gender { GEN3_GENDER_MALE = 0, GEN3_GENDER_FEMALE, GEN3_GENDER_GENDERLESS } Gen3Gender;
/* Gender is a function of the PID and the species' ratio. */
Gen3Gender gen3_gender(const Gen3Pokemon *p);
const char *gen3_gender_name(Gen3Gender gender);
/* The ability the record actually uses, from its stored ability bit. */
uint8_t gen3_ability_id(const Gen3Pokemon *p);
const char *gen3_ability_name(uint8_t ability);
const char *gen3_type_name_full(uint8_t type);

/* Experience curves. Levels are 1-100. */
extern const uint32_t gen3_exp_table[6][100];
uint8_t gen3_level_from_exp(uint32_t experience, uint8_t growth);
uint32_t gen3_exp_for_level(uint8_t level, uint8_t growth);
/* The level a record is actually at: stored for party records, derived from
 * experience for box records, which carry no level byte. */
uint8_t gen3_effective_level(const Gen3Pokemon *p);
/* Final stats from base, IVs, EVs, level and nature, in HP/Atk/Def/Spe/SpA/SpD
 * order. */
bool gen3_calc_stats(const Gen3Pokemon *p, Gen3Game game, uint16_t out[6]);

#endif
