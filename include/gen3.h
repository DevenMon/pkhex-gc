#ifndef PKHEX_GC_GEN3_H
#define PKHEX_GC_GEN3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GEN3_RAW_SIZE        0x20000u
#define GEN3_HALF_SIZE       0x0E000u
#define GEN3_SECTOR_SIZE     0x1000u
#define GEN3_SECTOR_USED     0x0F80u
#define GEN3_MAIN_SECTORS    14
#define GEN3_BOX_COUNT       14
#define GEN3_BOX_SLOTS       30
/*
 * The plaintext flag byte at 0x13. A box or party slot counts as occupied only
 * when Has Species is set and Bad Egg is clear; the games clear this byte when
 * a Pokemon is withdrawn, leaving the rest of the record behind.
 */
#define GEN3_PK3_FLAG_IS_BAD_EGG 1u
#define GEN3_PK3_FLAG_HAS_SPECIES 2u
#define GEN3_PK3_FLAG_IS_EGG 4u

#define GEN3_PK3_STORED_SIZE 80
#define GEN3_PK3_PARTY_SIZE  100

typedef enum Gen3Game {
    GEN3_GAME_UNKNOWN = 0,
    GEN3_GAME_RS,
    GEN3_GAME_EMERALD,
    GEN3_GAME_FRLG,
} Gen3Game;

typedef struct Gen3SlotInfo {
    bool all_sections;
    bool signatures_ok;
    bool checksums_ok;
    unsigned checksum_failures;
    uint32_t save_index;
    size_t section0_offset;
} Gen3SlotInfo;


typedef enum Gen3Pocket {
    GEN3_POCKET_PC = 0,
    GEN3_POCKET_ITEMS,
    GEN3_POCKET_KEY_ITEMS,
    GEN3_POCKET_BALLS,
    GEN3_POCKET_TMS,
    GEN3_POCKET_BERRIES,
    /* Colosseum and XD only: colognes, and XD's battle discs. */
    GEN3_POCKET_COLOGNE,
    GEN3_POCKET_DISCS,
    GEN3_POCKET_COUNT
} Gen3Pocket;

typedef struct Gen3ItemSlot {
    uint16_t item_id;
    uint16_t quantity;
} Gen3ItemSlot;

typedef struct Gen3Pokemon {
    bool present;
    bool checksum_ok;
    bool is_egg;
    bool is_shadow;
    uint16_t shadow_id;
    int32_t purification; /* Colosseum CK3 heart-gauge value; read-only in UI */
    uint32_t pid;
    uint32_t otid;
    uint16_t tid;
    uint16_t sid;
    uint16_t species_internal;
    uint16_t held_item;
    uint32_t experience;
    uint8_t friendship;
    uint16_t moves[4];
    uint8_t pp[4];
    uint8_t evs[6]; /* HP, Atk, Def, Spe, SpA, SpD */
    uint8_t ivs[6]; /* HP, Atk, Def, Spe, SpA, SpD */
    uint8_t level; /* populated for party records; 0 for box-only records */
    char nickname[24];
    char ot_name[20];
    uint8_t nickname_raw[10];  /* 0x08, as stored */
    uint8_t ot_raw[7];         /* 0x14, as stored */

    /* Remaining PK3 fields, in PKHeX's terms. Offsets are relative to the
     * decrypted, unshuffled record; see SOURCES.md. */
    uint8_t language;      /* 0x12 */
    uint8_t markings;      /* 0x1B, already un-swapped: circle/square/triangle/heart */
    uint8_t pp_ups;        /* Growth 0x08, two bits per move */
    uint8_t contest[6];    /* EV block 0x06: cool, beauty, cute, smart, tough, sheen */
    uint8_t pokerus;       /* Misc 0x00, low nibble days / high nibble strain */
    uint8_t met_location;  /* Misc 0x01 */
    uint8_t met_level;     /* Origins bits 0-6 */
    uint8_t origin_game;   /* Origins bits 7-10 */
    uint8_t ball;          /* Origins bits 11-14 */
    uint8_t ot_gender;     /* Origins bit 15 */
    bool ability_bit;      /* IV32 bit 31 - selects the second ability */
    bool fateful;          /* Ribbon word bit 31 */
    uint32_t ribbons;      /* Ribbon word, contest counts in bits 0-14 */
} Gen3Pokemon;

typedef enum Gen3Nature {
    GEN3_NATURE_HARDY = 0, GEN3_NATURE_COUNT = 25
} Gen3Nature;

/* Derived Generation III values. Nature, gender and shininess are all
 * functions of the PID, so they are computed rather than stored. */
uint8_t gen3_nature(const Gen3Pokemon *p);
bool gen3_is_shiny(const Gen3Pokemon *p);
uint8_t gen3_hidden_power_type(const Gen3Pokemon *p);
uint8_t gen3_hidden_power_power(const Gen3Pokemon *p);
uint8_t gen3_pokerus_days(const Gen3Pokemon *p);

/*
 * Ribbons. The word at Misc 0x08 packs the five contest ribbons as three-bit
 * levels in bits 0-14, twelve single-bit ribbons in bits 15-26, and Fateful
 * Encounter in bit 31; bits 27-30 are unused. Same layout as PKHeX's PK3.
 */
#define GEN3_CONTEST_RIBBON_COUNT 5u
#define GEN3_RIBBON_FLAG_COUNT 12u
#define GEN3_CONTEST_RIBBON_MAX 4u
uint8_t gen3_contest_ribbon(const Gen3Pokemon *p, unsigned contest);
bool gen3_set_contest_ribbon(Gen3Pokemon *p, unsigned contest, uint8_t level);
const char *gen3_contest_ribbon_name(unsigned contest);
/* "None", then Normal / Super / Hyper / Master. */
const char *gen3_contest_ribbon_level_name(uint8_t level);
bool gen3_ribbon_flag(const Gen3Pokemon *p, unsigned index);
bool gen3_set_ribbon_flag(Gen3Pokemon *p, unsigned index, bool on);
const char *gen3_ribbon_flag_name(unsigned index);
/* Every ribbon the record carries, contest levels counted individually. */
unsigned gen3_ribbon_count(const Gen3Pokemon *p);
uint8_t gen3_pokerus_strain(const Gen3Pokemon *p);
unsigned gen3_pp_up_count(const Gen3Pokemon *p, unsigned move_slot);
void gen3_set_pp_up_count(Gen3Pokemon *p, unsigned move_slot, unsigned ups);

/* Find a PID that gives the requested nature while keeping the current
 * shininess and ability bit. Returns false if nature is out of range. */
bool gen3_set_nature(Gen3Pokemon *p, uint8_t nature);

const char *gen3_nature_name(uint8_t nature);
const char *gen3_type_name(uint8_t type);
const char *gen3_language_name(uint8_t language);
const char *gen3_ball_name(uint8_t ball);
const char *gen3_origin_game_name(uint8_t version);

typedef struct Gen3Save {
    const uint8_t *raw;
    size_t raw_size;
    int active_slot;
    Gen3SlotInfo slots[2];
    Gen3Game game;
    bool japanese;

    uint8_t small[GEN3_SECTOR_USED];
    uint8_t large[4 * GEN3_SECTOR_USED];
    uint8_t storage[9 * GEN3_SECTOR_USED];
} Gen3Save;

bool gen3_open(Gen3Save *save, const uint8_t *data, size_t size);
const char *gen3_game_name(Gen3Game game);

void gen3_trainer_name(const Gen3Save *save, char *out, size_t out_size);
size_t gen3_trainer_name_length(const Gen3Save *save);
bool gen3_set_trainer_name(Gen3Save *save, const uint8_t *raw, size_t len);
uint8_t gen3_trainer_gender(const Gen3Save *save);
uint16_t gen3_tid(const Gen3Save *save);
uint16_t gen3_sid(const Gen3Save *save);
/*
 * Met location names, indexed by the met location byte. The cartridge games
 * and the GameCube games reuse the same indices for different places, so the
 * caller says which table it wants. Generated by tools/build_locations.py.
 */
const char *gen3_met_location_name(uint8_t location, bool gamecube);
/* Game record names, generated by tools/build_records.py. */
unsigned gen3_record_count(Gen3Game game);
const char *gen3_record_name(Gen3Game game, unsigned index);
uint16_t gen3_played_hours(const Gen3Save *save);
uint8_t gen3_played_minutes(const Gen3Save *save);
uint8_t gen3_played_seconds(const Gen3Save *save);
uint32_t gen3_money(const Gen3Save *save);
uint16_t gen3_coins(const Gen3Save *save);
uint32_t gen3_security_key(const Gen3Save *save);
/*
 * Eight bytes of 0xFF where the security key belongs. A save in that state has
 * had its Pokedex mangled by a bad third-party tool, and every encrypted
 * quantity in it reads as nonsense.
 */
bool gen3_is_pokedex_corrupt(const Gen3Save *save);
unsigned gen3_pocket_capacity(const Gen3Save *save, Gen3Pocket pocket);
const char *gen3_pocket_name(Gen3Pocket pocket);
bool gen3_get_item_slot(const Gen3Save *save, Gen3Pocket pocket, unsigned slot, Gen3ItemSlot *out);

unsigned gen3_party_count(const Gen3Save *save);
bool gen3_party_pokemon(const Gen3Save *save, unsigned slot, Gen3Pokemon *out);
unsigned gen3_current_box(const Gen3Save *save);
void gen3_box_name(const Gen3Save *save, unsigned box, char *out, size_t out_size);
#define GEN3_BOX_NAME_LEN 9u
void gen3_box_name_raw(const Gen3Save *save, unsigned box, uint8_t out[GEN3_BOX_NAME_LEN]);
bool gen3_set_box_name(Gen3Save *save, unsigned box, const uint8_t *raw, size_t len);

/* Box wallpaper index, 0-15. The games store one byte per box after the box
 * names; the artwork itself lives in the ROM, so only the index is here. */
#define GEN3_WALLPAPER_COUNT 16u
uint8_t gen3_box_wallpaper(const Gen3Save *save, unsigned box);
bool gen3_set_box_wallpaper(Gen3Save *save, unsigned box, uint8_t wallpaper);
const char *gen3_wallpaper_name(uint8_t wallpaper);
bool gen3_box_pokemon(const Gen3Save *save, unsigned box, unsigned slot, Gen3Pokemon *out);

/* Pokedex. Flags are indexed by National Dex number, 1-386. The games keep
 * three copies of the seen flags and cross-check them, so writing one without
 * the others corrupts the dex; gen3_set_seen writes all three. */
#define GEN3_DEX_SPECIES 386u
bool gen3_dex_seen(const Gen3Save *save, unsigned national);
bool gen3_dex_caught(const Gen3Save *save, unsigned national);
void gen3_set_dex_seen(Gen3Save *save, unsigned national, bool seen);
void gen3_set_dex_caught(Gen3Save *save, unsigned national, bool caught);
unsigned gen3_dex_seen_count(const Gen3Save *save);
unsigned gen3_dex_caught_count(const Gen3Save *save);
/* True when all three seen copies agree for every species. */
bool gen3_dex_seen_consistent(const Gen3Save *save);
void gen3_mirror_dex_seen(Gen3Save *save);
bool gen3_national_dex(const Gen3Save *save);
/* Unlocking the National Dex needs a magic byte, an event flag and a work
 * value together; setting only the byte leaves the games unconvinced. */
void gen3_set_national_dex(Gen3Save *save, bool unlocked);

/* Event flags and event constants ("work" values), the game's story state.
 * Counts and offsets differ per game; both live in the large block. */
unsigned gen3_event_flag_count(const Gen3Save *save);
unsigned gen3_event_work_count(const Gen3Save *save);
bool gen3_event_flag(const Gen3Save *save, unsigned flag);
void gen3_set_event_flag(Gen3Save *save, unsigned flag, bool value);
uint16_t gen3_event_work(const Gen3Save *save, unsigned index);
void gen3_set_event_work(Gen3Save *save, unsigned index, uint16_t value);
unsigned gen3_event_flags_set(const Gen3Save *save);

/* Gym badges are eight consecutive event flags, based per game. */
#define GEN3_BADGE_COUNT 8u
bool gen3_badge(const Gen3Save *save, unsigned badge);
void gen3_set_badge(Gen3Save *save, unsigned badge, bool earned);
unsigned gen3_badge_count(const Gen3Save *save);

unsigned gen3_species_national(uint16_t internal_species);
const char *gen3_species_name(uint16_t internal_species);
const char *gen3_item_name(uint16_t item_id);
/* Secret base decorations, from PKHeX's Decoration3 enum: 0 is an empty
 * slot and the last id is Registeel Doll. */
#define GEN3_DECORATION_MAX 120
const char *gen3_decoration_name(uint8_t decoration_id);
/* Emerald's Easy Chat trendy words and Trainer Hill modes, from PKHeX's
 * TrendyWord3E and TrainerHillMode3E enums. */
const char *gen3_trendy_word_name(unsigned word);
const char *gen3_trainer_hill_mode_name(unsigned mode);
const char *gen3_move_name(uint16_t move_id);
void gen3_decode_text(const uint8_t *src, size_t src_len, bool japanese, char *dst, size_t dst_len);
/* Gen III strings end with, and are padded by, 0xFF. */
#define GEN3_TEXT_TERMINATOR 0xFFu
char gen3_decode_char(uint8_t c);
/* Returns GEN3_TEXT_TERMINATOR for characters the encoding has no slot for. */
uint8_t gen3_encode_char(char c);
void gen3_encode_text(const char *src, uint8_t *dst, size_t dst_len);
bool gen3_parse_pk3_record(const uint8_t *src, size_t record_size, bool japanese, Gen3Pokemon *out);
bool gen3_write_pk3_record(uint8_t *record, size_t record_size, const Gen3Pokemon *pokemon);

/* Editing helpers. These mutate only the reconstructed active save in RAM.
 * gen3_export_raw writes a separate validated 128 KiB save image; it never
 * touches the original source pointer. */
bool gen3_set_trainer_name_ascii(Gen3Save *save, const char *name);
void gen3_set_trainer_gender(Gen3Save *save, uint8_t gender);
void gen3_set_tid(Gen3Save *save, uint16_t tid);
void gen3_set_sid(Gen3Save *save, uint16_t sid);
void gen3_set_playtime(Gen3Save *save, uint16_t hours, uint8_t minutes, uint8_t seconds);
void gen3_set_money(Gen3Save *save, uint32_t money);
void gen3_set_coins(Gen3Save *save, uint16_t coins);
bool gen3_set_item_slot(Gen3Save *save, Gen3Pocket pocket, unsigned slot, uint16_t item_id, uint16_t quantity);
bool gen3_set_party_pokemon(Gen3Save *save, unsigned slot, const Gen3Pokemon *pokemon);
bool gen3_set_box_pokemon(Gen3Save *save, unsigned box, unsigned slot, const Gen3Pokemon *pokemon);
bool gen3_export_raw(const Gen3Save *save, uint8_t *out, size_t out_size);
uint16_t gen3_species_internal_from_national(unsigned national_species);

#endif
