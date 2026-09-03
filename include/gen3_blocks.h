#ifndef PKHEX_GC_GEN3_BLOCKS_H
#define PKHEX_GC_GEN3_BLOCKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gen3.h"

/*
 * The optional blocks of a Generation III cartridge save: the daycare, the
 * roaming Pokemon, the mailbox, the Hall of Fame and the gift ribbons.
 *
 * Each sits at a different offset in each game, so every accessor goes through
 * one layout table rather than open-coding offsets. Offsets follow PKHeX's
 * SaveBlock3Large{RS,E,FRLG}; see SOURCES.md.
 */

/* ------------------------------------------------------------- daycare --- */

#define GEN3_DAYCARE_SLOTS 2u

/* The record in a daycare slot is an ordinary stored PK3. */
bool gen3_daycare_pokemon(const Gen3Save *save, unsigned slot, Gen3Pokemon *out);
bool gen3_set_daycare_pokemon(Gen3Save *save, unsigned slot, const Gen3Pokemon *pokemon);
/* Experience the slot has banked since it was deposited. */
uint32_t gen3_daycare_exp(const Gen3Save *save, unsigned slot);
bool gen3_set_daycare_exp(Gen3Save *save, unsigned slot, uint32_t experience);
/* Ruby/Sapphire and FireRed/LeafGreen store 16 bits of seed; Emerald 32. */
uint32_t gen3_daycare_seed(const Gen3Save *save);
/* 16 everywhere but Emerald, which widened it to 32. */
unsigned gen3_daycare_seed_bits(const Gen3Save *save);
bool gen3_set_daycare_seed(Gen3Save *save, uint32_t seed);
/* The games signal a waiting egg with an event flag rather than a record. */
bool gen3_daycare_egg_waiting(const Gen3Save *save);
bool gen3_set_daycare_egg_waiting(Gen3Save *save, bool waiting);

/* -------------------------------------------------------------- roamer --- */

typedef struct Gen3Roamer {
    bool active;
    uint16_t species;        /* National Dex */
    uint32_t pid;
    uint32_t iv32;
    uint8_t ivs[6];          /* HP, Atk, Def, Spe, SpA, SpD */
    uint16_t hp_current;
    uint8_t level;
    uint8_t status;
    uint8_t contest[5];      /* cool, beauty, cute, smart, tough */
} Gen3Roamer;

bool gen3_roamer(const Gen3Save *save, Gen3Roamer *out);
bool gen3_set_roamer(Gen3Save *save, const Gen3Roamer *roamer);
/*
 * Only Emerald loads all four IV bytes when the roamer is encountered; the
 * other games load one, so the IVs the player actually meets differ from the
 * ones stored. PKHeX shows both.
 */
bool gen3_roamer_ivs_are_glitched(const Gen3Save *save);
void gen3_roamer_encounter_ivs(const Gen3Roamer *roamer, bool glitched, uint8_t out[6]);

/* ---------------------------------------------------------------- mail --- */

#define GEN3_MAIL_SLOTS 16u   /* six held by the party, ten in the PC */
#define GEN3_MAIL_PARTY_SLOTS 6u
#define GEN3_MAIL_WORDS 9u
#define GEN3_MAIL_SIZE 0x24u

typedef struct Gen3Mail {
    bool present;
    uint16_t words[GEN3_MAIL_WORDS];  /* easy-chat word ids, 0xFFFF = blank */
    char author[12];
    uint16_t author_tid, author_sid;
    uint16_t appear_species;          /* National Dex of the sprite on the paper */
    uint16_t mail_item;               /* item id 121-132; 0 means the slot is empty */
} Gen3Mail;

bool gen3_mail(const Gen3Save *save, unsigned slot, Gen3Mail *out);
bool gen3_clear_mail(Gen3Save *save, unsigned slot);
/* Items 121-132 are the twelve kinds of mail paper. */
bool gen3_item_is_mail(uint16_t item_id);

/* -------------------------------------------------------- hall of fame --- */

/*
 * Fifty entries of six members each, in the two sectors past the main save.
 * Those sectors are outside the rotating block layout, so they are read
 * straight from the raw image and are not rewritten on export.
 */
#define GEN3_HOF_ENTRIES 50u
#define GEN3_HOF_TEAM_SIZE 6u

typedef struct Gen3HofMember {
    bool present;
    uint16_t species;   /* National Dex */
    uint8_t level;
    uint32_t pid;
    uint16_t tid, sid;
    bool shiny;
    char nickname[24];
} Gen3HofMember;

bool gen3_hof_available(const Gen3Save *save);
bool gen3_hof_member(const Gen3Save *save, unsigned entry, unsigned index, Gen3HofMember *out);
/* Entries in use, counted from the first one with no members. */
unsigned gen3_hof_entry_count(const Gen3Save *save);

/* ------------------------------------------------------------- records --- */

/*
 * A block of 32-bit counters: steps taken, battles fought, eggs hatched. The
 * block moves per game and so does what each index counts, so the names come
 * from a generated table keyed on the game.
 */
uint32_t gen3_record(const Gen3Save *save, unsigned index);
bool gen3_set_record(Gen3Save *save, unsigned index, uint32_t value);

/* ---------------------------------------------------------- rival name --- */

/* FireRed and LeafGreen are the only Generation III games that store one. */
bool gen3_has_rival_name(const Gen3Save *save);
void gen3_rival_name(const Gen3Save *save, char *out, size_t out_size);
#define GEN3_RIVAL_NAME_LEN 8u
void gen3_rival_name_raw(const Gen3Save *save, uint8_t out[GEN3_RIVAL_NAME_LEN]);
bool gen3_set_rival_name(Gen3Save *save, const uint8_t *raw, size_t len);

/* ---------------------------------------------------------- pokeblocks --- */

/*
 * Ruby, Sapphire and Emerald only. Forty eight-byte blocks: a colour, the five
 * contest boosts and the sheen boost, then one alignment byte.
 */
#define GEN3_POKEBLOCK_COUNT 40u

typedef struct Gen3PokeBlock {
    uint8_t color;   /* 0 = empty slot, 14 = Gold */
    uint8_t spicy, dry, sweet, bitter, sour;  /* cool, beauty, cute, smart, tough */
    uint8_t feel;    /* sheen */
} Gen3PokeBlock;

bool gen3_has_pokeblocks(const Gen3Save *save);
bool gen3_pokeblock(const Gen3Save *save, unsigned index, Gen3PokeBlock *out);
bool gen3_set_pokeblock(Gen3Save *save, unsigned index, const Gen3PokeBlock *block);
const char *gen3_pokeblock_color_name(uint8_t color);
#define GEN3_POKEBLOCK_COLOR_COUNT 15u

/* ----------------------------------------------------------- game clock --- */

/*
 * Ruby, Sapphire and Emerald keep two clocks in the small block: the one the
 * cartridge started from, and how much has elapsed since. FireRed and
 * LeafGreen have no real-time clock.
 */
typedef struct Gen3Clock {
    uint16_t day;
    uint8_t hour, minute, second;
} Gen3Clock;

bool gen3_has_clock(const Gen3Save *save);
bool gen3_clock(const Gen3Save *save, bool elapsed, Gen3Clock *out);
bool gen3_set_clock(Gen3Save *save, bool elapsed, const Gen3Clock *clock);

/* --------------------------------------------------------- secret bases --- */

/*
 * Twenty bases of 160 bytes each, Ruby/Sapphire/Emerald only. The first is the
 * player's own; the rest are ones traded in by other players.
 */
#define GEN3_SECRET_BASE_COUNT 20u

typedef struct Gen3SecretBase {
    bool present;
    uint8_t location;
    uint8_t ot_gender;
    bool battled_today;
    uint8_t registry_status;
    char ot_name[16];
    uint8_t ot_class;       /* 0-4, names a trainer class pairing */
    uint8_t language;
    uint16_t received;      /* bases received from other players */
    uint8_t times_entered;
    uint16_t tid, sid;
} Gen3SecretBase;

/*
 * Each base also stores the six-Pokemon team that defends it. It is not a PK3:
 * just a PID, four moves, species, held item, level and one EV value applied
 * to every stat.
 */
#define GEN3_SECRET_BASE_TEAM 6u

typedef struct Gen3SecretBaseMon {
    bool present;
    uint16_t species;   /* National Dex */
    uint32_t pid;
    uint16_t moves[4];
    uint16_t held_item;
    uint8_t level;
    uint8_t ev_all;     /* one value, applied to every stat */
} Gen3SecretBaseMon;

bool gen3_secret_base_mon(const Gen3Save *save, unsigned base, unsigned slot,
                          Gen3SecretBaseMon *out);

bool gen3_has_secret_bases(const Gen3Save *save);
bool gen3_secret_base(const Gen3Save *save, unsigned index, Gen3SecretBase *out);
/* Clears one base's whole 160-byte record, which is how the games free a slot. */
bool gen3_clear_secret_base(Gen3Save *save, unsigned index);
const char *gen3_secret_base_class_name(uint8_t trainer_class);



/* ------------------------------------------- external event data --- */

/*
 * The block the GameCube games write into a cartridge save: PokeCoupons from
 * Mt. Battle, the Japanese Colosseum bonus disc's gift flags, and what
 * Pokemon Box unlocked. From PKHeX's SAV3 external event properties.
 */
typedef struct Gen3ExternalEvents {
    uint32_t coupons;          /* PokeCoupons held, capped by the games at 9,999,999 */
    uint32_t coupons_total;    /* lifetime total, which sets the bonus disc rank */
    bool title_gold;           /* Master Ball, at 30,000 total */
    bool title_silver;         /* Light Ball Pikachu, at 5,000 */
    bool title_bronze;         /* PP Max, at 2,500 */
    bool received_celebi;      /* Ageto Celebi */
    bool received_jirachi;     /* Wishmaker Jirachi */
    bool used_rsbox;           /* connected to Pokemon Box */
    uint8_t rsbox_eggs;        /* 0-3: Zigzagoon, Skitty, Pichu */
} Gen3ExternalEvents;

bool gen3_external_events(const Gen3Save *save, Gen3ExternalEvents *out);
/*
 * Only the flags can be written. The two coupon totals sit seven and eleven
 * bytes into the block, and in Emerald PKHeX places the eleven gift ribbon
 * bytes at that same address - so writing a total there would land inside the
 * ribbons. Until that overlap is resolved against hardware they are read-only.
 */
bool gen3_set_external_event_flags(Gen3Save *save, const Gen3ExternalEvents *in);

/* ------------------------------------------------- Emerald extras --- */

/*
 * Three blocks Emerald has and the other cartridges do not: the times set in
 * Trainer Hill, the Easy Chat "trendy saying" words unlocked so far, and
 * Walda's box wallpaper.
 */
#define GEN3_TRAINER_HILL_MODES 4u
#define GEN3_TRENDY_WORD_COUNT 33u

bool gen3_has_emerald_extras(const Gen3Save *save);

/* Each unit is one sixtieth of a second; zero means no time is set. */
uint32_t gen3_trainer_hill_record(const Gen3Save *save, unsigned mode);
bool gen3_set_trainer_hill_record(Gen3Save *save, unsigned mode, uint32_t frames);

bool gen3_trendy_word(const Gen3Save *save, unsigned word);
bool gen3_set_trendy_word(Gen3Save *save, unsigned word, bool unlocked);

typedef struct Gen3Walda {
    uint16_t background;
    uint16_t foreground;
    uint8_t icon;
    uint8_t pattern;
    bool unlocked;
} Gen3Walda;

bool gen3_walda(const Gen3Save *save, Gen3Walda *out);
bool gen3_set_walda(Gen3Save *save, const Gen3Walda *in);

/* --------------------------------------------------------- decorations --- */

/*
 * The decoration inventory is 150 bytes of one-byte decoration ids, split into
 * eight categories of fixed length.
 */
typedef enum Gen3DecorationKind {
    GEN3_DECO_DESK = 0, GEN3_DECO_CHAIR, GEN3_DECO_PLANT, GEN3_DECO_ORNAMENT,
    GEN3_DECO_MAT, GEN3_DECO_POSTER, GEN3_DECO_DOLL, GEN3_DECO_CUSHION,
    GEN3_DECO_KIND_COUNT
} Gen3DecorationKind;

bool gen3_has_decorations(const Gen3Save *save);
const char *gen3_decoration_kind_name(Gen3DecorationKind kind);
unsigned gen3_decoration_slot_count(Gen3DecorationKind kind);
uint8_t gen3_decoration(const Gen3Save *save, Gen3DecorationKind kind, unsigned slot);
bool gen3_set_decoration(Gen3Save *save, Gen3DecorationKind kind, unsigned slot, uint8_t value);

/* ------------------------------------------------------ battle frontier --- */

/*
 * Emerald only. The streak block sits in the small section; the symbols are
 * ordinary event flags, and the Battle Points are a halfword of their own.
 */
typedef enum Gen3Facility {
    GEN3_FACILITY_TOWER = 0, GEN3_FACILITY_DOME, GEN3_FACILITY_PALACE,
    GEN3_FACILITY_ARENA, GEN3_FACILITY_FACTORY, GEN3_FACILITY_PIKE,
    GEN3_FACILITY_PYRAMID, GEN3_FACILITY_COUNT
} Gen3Facility;

typedef enum Gen3FrontierStat {
    GEN3_FRONTIER_CURRENT = 0, GEN3_FRONTIER_RECORD,
    GEN3_FRONTIER_CURRENT_SWAPPED, GEN3_FRONTIER_RECORD_SWAPPED,
    GEN3_FRONTIER_CHAMPIONSHIPS, GEN3_FRONTIER_RECORD_CLEARED,
    GEN3_FRONTIER_STAT_COUNT
} Gen3FrontierStat;

bool gen3_has_battle_frontier(const Gen3Save *save);
const char *gen3_facility_name(Gen3Facility facility);
const char *gen3_frontier_stat_name(Gen3FrontierStat stat);
/* Not every facility keeps every statistic, or more than one battle mode. */
unsigned gen3_facility_mode_count(Gen3Facility facility);
bool gen3_facility_has_stat(Gen3Facility facility, Gen3FrontierStat stat);
/* mode 0-3, record 0 = Level 50 and 1 = Open Level. */
bool gen3_frontier_stat(const Gen3Save *save, Gen3Facility facility, unsigned mode,
                        unsigned record, Gen3FrontierStat stat, uint16_t *out);
bool gen3_set_frontier_stat(Gen3Save *save, Gen3Facility facility, unsigned mode,
                            unsigned record, Gen3FrontierStat stat, uint16_t value);
/* Symbols: 0 none, 1 silver, 2 gold. Stored as a pair of event flags. */
uint8_t gen3_frontier_symbol(const Gen3Save *save, Gen3Facility facility);
bool gen3_set_frontier_symbol(Gen3Save *save, Gen3Facility facility, uint8_t level);
bool gen3_frontier_pass(const Gen3Save *save);
bool gen3_set_frontier_pass(Gen3Save *save, bool active);
uint16_t gen3_battle_points(const Gen3Save *save);
bool gen3_set_battle_points(Gen3Save *save, uint16_t points);

/* ---------------------------------------------------------- swarm --- */

/*
 * The daily swarm: which Pokemon is currently swarming, where, at what level
 * and with which moves. Ruby, Sapphire and Emerald only.
 */
typedef struct Gen3Swarm {
    bool active;
    uint16_t species;    /* National Dex; 0 when nothing is swarming */
    uint8_t map_num, map_group;
    uint8_t level;
    uint16_t moves[4];
    uint8_t probability; /* percent chance of meeting it */
    uint16_t days_left;
} Gen3Swarm;

bool gen3_has_swarm(const Gen3Save *save);
bool gen3_swarm(const Gen3Save *save, Gen3Swarm *out);
bool gen3_set_swarm(Gen3Save *save, const Gen3Swarm *swarm);
bool gen3_clear_swarm(Gen3Save *save);

/* ------------------------------------------------------ e-reader berry --- */

/*
 * The e-Reader berry slot. A save whose berry data is blank holds the
 * "Enigma Berry", which the games cannot resolve and which breaks trading, so
 * whether the slot is blank is worth reporting on its own.
 */
bool gen3_has_eberry(const Gen3Save *save);
void gen3_eberry_name(const Gen3Save *save, char *out, size_t out_size);
bool gen3_eberry_is_enigma(const Gen3Save *save);

/* ------------------------------------------------------------ paintings --- */

/*
 * The five Lilycove museum paintings, Ruby/Sapphire/Emerald only. Read only:
 * the artwork is generated from the record, not stored.
 */
#define GEN3_PAINTING_COUNT 5u

typedef struct Gen3Painting {
    bool present;
    uint16_t species;   /* National Dex */
    uint32_t pid;
    uint16_t tid, sid;
    uint8_t caption;
    char nickname[16];
    char ot_name[16];
} Gen3Painting;

bool gen3_has_paintings(const Gen3Save *save);
bool gen3_painting(const Gen3Save *save, unsigned index, Gen3Painting *out);

/* ------------------------------------------------------- mystery gift --- */

/*
 * Emerald and FireRed/LeafGreen hold a Wonder Card; all three Hoenn games
 * also hold a script-driven Mystery Event. Both carry a CRC of their own,
 * which is the useful thing to report: a card whose checksum does not match
 * is one the game will refuse.
 */
typedef struct Gen3WonderCard {
    bool present;
    bool checksum_ok;
    uint16_t card_id;
    uint16_t icon;
    uint16_t count;
    uint8_t type;
    uint8_t color;
    char title[48];
} Gen3WonderCard;

bool gen3_has_wonder_card(const Gen3Save *save);
bool gen3_wonder_card(const Gen3Save *save, Gen3WonderCard *out);
bool gen3_clear_wonder_card(Gen3Save *save);

/*
 * Install a card read from a file. A Generation III card is 336 bytes, or 168
 * in Japanese, and carries its own CRC - which is checked here, because the
 * games will not resolve a card whose checksum does not match and this is
 * writing into a save that has to keep working.
 */
#define GEN3_WONDER_CARD_BYTES 336u
#define GEN3_WONDER_CARD_BYTES_JP 168u
bool gen3_wonder_card_data_valid(const uint8_t *data, size_t length, bool japanese);
bool gen3_set_wonder_card(Gen3Save *save, const uint8_t *data, size_t length);

bool gen3_has_mystery_event(const Gen3Save *save);
/* True when the stored event's own CRC matches its contents. */
bool gen3_mystery_event_present(const Gen3Save *save);
bool gen3_mystery_event_checksum_ok(const Gen3Save *save);
bool gen3_clear_mystery_event(Gen3Save *save);

/* -------------------------------------------------- named event flags --- */


/* -------------------------------------------------------- joyful game --- */

/*
 * Emerald and FireRed/LeafGreen keep the Pokemon Jump and Berry Picking
 * high scores in the small block, at different offsets.
 */
typedef enum Gen3JoyfulStat {
    GEN3_JOYFUL_JUMP_IN_ROW = 0, GEN3_JOYFUL_JUMP_5_IN_ROW,
    GEN3_JOYFUL_JUMP_MAX_PLAYERS, GEN3_JOYFUL_JUMP_SCORE,
    GEN3_JOYFUL_BERRIES_SCORE, GEN3_JOYFUL_BERRIES_IN_ROW,
    GEN3_JOYFUL_BERRIES_5_IN_ROW, GEN3_JOYFUL_STAT_COUNT
} Gen3JoyfulStat;

bool gen3_has_joyful(const Gen3Save *save);
const char *gen3_joyful_stat_name(Gen3JoyfulStat stat);
uint32_t gen3_joyful_stat(const Gen3Save *save, Gen3JoyfulStat stat);
bool gen3_set_joyful_stat(Gen3Save *save, Gen3JoyfulStat stat, uint32_t value);

/* -------------------------------------------------------- gift ribbons --- */

#define GEN3_GIFT_RIBBON_COUNT 11u
uint8_t gen3_gift_ribbon(const Gen3Save *save, unsigned index);
bool gen3_set_gift_ribbon(Gen3Save *save, unsigned index, uint8_t value);

#endif
