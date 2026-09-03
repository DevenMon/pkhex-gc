/*
 * How a Generation III Pokemon can come to know a move.
 *
 * The tables in gen3_learnsets.c are generated from PKHeX's binary resources
 * by tools/build_learnsets.py. Level-up lists differ per game; egg moves are
 * shared across all three; machines and tutors are compatibility bits on the
 * species.
 */
#ifndef PKHEXGC_GEN3_LEARNSETS_H
#define PKHEXGC_GEN3_LEARNSETS_H

#include <stdbool.h>

#include "gen3.h"

#define GEN3_TM_COUNT 50u
#define GEN3_HM_COUNT 8u

typedef struct Gen3LevelMove {
    unsigned char level;
    unsigned short move;  /* Generation III goes to 354, so this is not a byte */
} Gen3LevelMove;

typedef struct Gen3LearnIndex {
    unsigned short start;
    unsigned char count;
} Gen3LearnIndex;

/* Level-up moves for a species in one game, in teaching order. */
unsigned gen3_level_moves(Gen3Game game, unsigned national, const Gen3LevelMove **out);

/* Egg moves, the same in every Generation III game. */
unsigned gen3_egg_moves(unsigned national, const unsigned short **out);

/* Machine 0-49 are TM01-TM50; 50-57 are HM01-HM08. */
bool gen3_learns_machine(unsigned national, unsigned machine);
bool gen3_learns_tutor(unsigned national, unsigned tutor);

/* Which move each machine and tutor slot teaches, and how many there are. */
unsigned gen3_machine_count(void);
unsigned gen3_tutor_count(void);
unsigned gen3_machine_move(unsigned machine);
unsigned gen3_tutor_move(unsigned tutor);

/* Base PP for a move, before PP Ups. Zero if the games have no such move. */
unsigned gen3_move_pp(unsigned move);

#endif
