/*
 * Names for Generation III scripted event flags and event constants.
 *
 * The tables in gen3_event_names.c are generated from PKHeX's label files by
 * tools/build_event_names.py. Not every index is named - the games have far
 * more flags than anyone has identified - so a lookup returning NULL means
 * "no name known", not "no such flag".
 */
#ifndef PKHEXGC_GEN3_EVENT_NAMES_H
#define PKHEXGC_GEN3_EVENT_NAMES_H

#include "gen3.h"

/* PKHeX's NamedEventType, which it uses to group and colour the list. */
typedef enum Gen3EventCategory {
    GEN3_EVENT_NONE = 0,
    GEN3_EVENT_HIDDEN_ITEM,
    GEN3_EVENT_TRAINER,
    GEN3_EVENT_STORY,
    GEN3_EVENT_FLY,
    GEN3_EVENT_MISC,
    GEN3_EVENT_STATISTIC,
    GEN3_EVENT_ACHIEVEMENT,
    GEN3_EVENT_USEFUL,
    GEN3_EVENT_ENCOUNTER,
    GEN3_EVENT_GIFT,
    GEN3_EVENT_REBATTLE
} Gen3EventCategory;

/* A named value an event constant is known to take, such as which starter
 * was chosen. */
typedef struct Gen3EventValue {
    unsigned short value;
    const char *name;
} Gen3EventValue;

typedef struct Gen3EventName {
    unsigned short index;
    Gen3EventCategory category;
    const char *name;
    const Gen3EventValue *values;   /* constants only; NULL for flags */
    unsigned value_count;
} Gen3EventName;

const Gen3EventName *gen3_event_flag_entry(Gen3Game game, unsigned index);
const Gen3EventName *gen3_event_work_entry(Gen3Game game, unsigned index);

/* NULL when the index has no known name. */
const char *gen3_event_flag_name(Gen3Game game, unsigned index);
const char *gen3_event_work_name(Gen3Game game, unsigned index);
const char *gen3_event_work_value_name(Gen3Game game, unsigned index, unsigned value);

const char *gen3_event_category_name(Gen3EventCategory category);

#endif
