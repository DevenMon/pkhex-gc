#ifndef PKHEX_GC_GEN3_ALL_H
#define PKHEX_GC_GEN3_ALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gen3.h"

#define GEN3_COLO_SIZE   0x60000u
#define GEN3_XD_SIZE     0x56000u
#define GEN3_RSBOX_SIZE  0x76000u
#define GEN3_GCI_HEADER  0x40u
#define GEN3_MAX_BOXES   50u
#define GEN3_MAX_PARTY   6u

/* Largest standalone Gen III save we intentionally open, plus a GCI header. */
#define GEN3_MAX_SAVE_FILE (GEN3_RSBOX_SIZE + GEN3_GCI_HEADER)

typedef enum Gen3SaveKind {
    GEN3_KIND_UNKNOWN = 0,
    GEN3_KIND_GBA,
    GEN3_KIND_COLOSSEUM,
    GEN3_KIND_XD,
    GEN3_KIND_RSBOX,
} Gen3SaveKind;

/*
 * Colosseum and XD share the cartridge item ids and add their own key items
 * above 500. gen3_item_name knows only the cartridge list, so anything a
 * GameCube bag holds has to be named through this.
 */
const char *gen3_item_name_for(Gen3SaveKind kind, uint16_t item_id);

typedef struct Gen3AnySave {
    Gen3SaveKind kind;
    const uint8_t *input;
    size_t input_size;
    size_t data_offset;      /* 0 for raw save data, 0x40 for .gci */

    Gen3Save gba;

    /* Owned decrypted/unpacked buffer for GameCube formats. */
    uint8_t *work;
    size_t work_size;

    int active_slot;
    uint32_t save_counter;
    bool japanese;
    bool has_trainer;
    bool integrity_ok;
    unsigned integrity_failures;

    char trainer_name[32];
    uint8_t trainer_gender;
    uint16_t tid;
    uint16_t sid;
    uint32_t money;
    uint64_t played_seconds;

    unsigned party_count;
    unsigned box_count;
    unsigned current_box;

    /* XD substructure metadata. */
    uint16_t xd_sub_lengths[16];
    uint32_t xd_sub_offsets[16];
    size_t party_offset;
    size_t box_offset;
} Gen3AnySave;

bool gen3_any_open(Gen3AnySave *save, const uint8_t *data, size_t size);
void gen3_any_close(Gen3AnySave *save);

const char *gen3_any_kind_name(Gen3SaveKind kind);
const char *gen3_any_game_name(const Gen3AnySave *save);

unsigned gen3_any_party_count(const Gen3AnySave *save);
bool gen3_any_party_pokemon(const Gen3AnySave *save, unsigned slot, Gen3Pokemon *out);
unsigned gen3_any_box_count(const Gen3AnySave *save);
unsigned gen3_any_current_box(const Gen3AnySave *save);
void gen3_any_box_name(const Gen3AnySave *save, unsigned box, char *out, size_t out_size);
/* Wallpaper index for a box, or 0xFF when the format has no wallpapers
 * (Colosseum, XD and Box store no per-box background). */
uint8_t gen3_any_box_wallpaper(const Gen3AnySave *save, unsigned box);
bool gen3_any_set_box_wallpaper(Gen3AnySave *save, unsigned box, uint8_t wallpaper);

/* Pokedex. Only the GBA formats carry one; the GameCube titles return false
 * and ignore writes. */
bool gen3_any_has_pokedex(const Gen3AnySave *save);
bool gen3_any_dex_seen(const Gen3AnySave *save, unsigned national);
bool gen3_any_dex_caught(const Gen3AnySave *save, unsigned national);
void gen3_any_set_dex_seen(Gen3AnySave *save, unsigned national, bool seen);
void gen3_any_set_dex_caught(Gen3AnySave *save, unsigned national, bool caught);
unsigned gen3_any_dex_seen_count(const Gen3AnySave *save);
unsigned gen3_any_dex_caught_count(const Gen3AnySave *save);
bool gen3_any_national_dex(const Gen3AnySave *save);
void gen3_any_set_national_dex(Gen3AnySave *save, bool unlocked);

/* Event flags and constants. GameCube titles have none. */
unsigned gen3_any_event_flag_count(const Gen3AnySave *save);
unsigned gen3_any_event_work_count(const Gen3AnySave *save);
bool gen3_any_event_flag(const Gen3AnySave *save, unsigned flag);
void gen3_any_set_event_flag(Gen3AnySave *save, unsigned flag, bool value);
uint16_t gen3_any_event_work(const Gen3AnySave *save, unsigned index);
void gen3_any_set_event_work(Gen3AnySave *save, unsigned index, uint16_t value);
bool gen3_any_badge(const Gen3AnySave *save, unsigned badge);
void gen3_any_set_badge(Gen3AnySave *save, unsigned badge, bool earned);
unsigned gen3_any_badge_count(const Gen3AnySave *save);
bool gen3_any_box_pokemon(const Gen3AnySave *save, unsigned box, unsigned slot, Gen3Pokemon *out);

/* Editing/export surface shared by the GameCube frontend.
 * GBA, Colosseum and XD are writable; Pokemon Box remains read-only. */
bool gen3_any_can_edit(const Gen3AnySave *save);
/* True for the formats that carry their own checksums, so a reopened export
 * can be checked structurally as well as byte for byte. */
bool gen3_any_has_integrity_check(Gen3SaveKind kind);

/* ------------------------------------------------- strategy memo --- */

/*
 * Colosseum and XD keep a "Strategy Memo": one twelve-byte entry per species
 * the player has met, holding the species, the original trainer's IDs and the
 * PID of the individual. Colosseum reads a filled species as seen and uses a
 * flag for owned; XD inverts the same flag to mean seen and has no owned
 * state at all. The layout is PKHeX's StrategyMemo/StrategyMemoEntry.
 */
#define GEN3_MEMO_ENTRY_SIZE 12u
#define GEN3_MEMO_MAX_ENTRIES 500u

typedef struct Gen3MemoEntry {
    uint16_t species;        /* National Dex, converted from the stored index */
    uint16_t species_internal;
    uint16_t tid, sid;
    uint32_t pid;
    bool seen;
    bool owned;              /* always false in XD, which does not track it */
} Gen3MemoEntry;

bool gen3_any_has_memo(const Gen3AnySave *save);
unsigned gen3_any_memo_count(const Gen3AnySave *save);
bool gen3_any_memo_entry(const Gen3AnySave *save, unsigned index, Gen3MemoEntry *out);
bool gen3_any_set_memo_seen(Gen3AnySave *save, unsigned index, bool seen);

/* True where a record carries Colosseum/XD shadow state. */
bool gen3_any_has_shadow(Gen3SaveKind kind);
bool gen3_any_set_tid(Gen3AnySave *save, uint16_t tid);
bool gen3_any_set_sid(Gen3AnySave *save, uint16_t sid);
bool gen3_any_set_trainer_gender(Gen3AnySave *save, uint8_t gender);
bool gen3_any_set_money(Gen3AnySave *save, uint32_t money);
bool gen3_any_set_played_seconds(Gen3AnySave *save, uint64_t seconds);

/*
 * Names, from plain ASCII. The cartridge saves encode into the Generation III
 * character set; the GameCube saves store UTF-16 big-endian, so anything the
 * keyboard can type maps straight to a code unit.
 */
bool gen3_any_name_is_utf16(const Gen3AnySave *save);
/* Characters the field holds, not bytes. */
unsigned gen3_any_trainer_name_length(const Gen3AnySave *save);
bool gen3_any_set_trainer_name_ascii(Gen3AnySave *save, const char *name);
unsigned gen3_any_box_name_length(const Gen3AnySave *save);
bool gen3_any_set_box_name_ascii(Gen3AnySave *save, unsigned box, const char *name);
bool gen3_any_set_party_pokemon(Gen3AnySave *save, unsigned slot, const Gen3Pokemon *pokemon);
bool gen3_any_set_box_pokemon(Gen3AnySave *save, unsigned box, unsigned slot, const Gen3Pokemon *pokemon);
/* Empties a box slot. The games mark a slot empty by zeroing the record, so
 * this zeroes exactly the record and leaves any per-slot trailer alone. */
bool gen3_any_clear_box_slot(Gen3AnySave *save, unsigned box, unsigned slot);

/*
 * The stored record for a box slot, exactly as it sits in the save. The size
 * and meaning differ per format - PK3 on a cartridge and in Pokemon Box, CK3
 * in Colosseum, XK3 in XD - so the caller asks how big one is first. This is
 * what a .pk3 / .ck3 / .xk3 file on the card holds.
 */
size_t gen3_any_record_size(const Gen3AnySave *save);
const char *gen3_any_record_extension(const Gen3AnySave *save);
bool gen3_any_box_record_raw(const Gen3AnySave *save, unsigned box, unsigned slot,
                             uint8_t *out, size_t out_size);
/* Refuses a record that does not parse as this format's, so a file of the
 * right length but the wrong kind cannot be written into a save. */
bool gen3_any_set_box_record_raw(Gen3AnySave *save, unsigned box, unsigned slot,
                                 const uint8_t *in, size_t in_size);

/*
 * XD's shadow table. A record carries only its Shadow ID; whether that
 * Pokemon is still a Shadow, how far its heart gauge has come and what IVs it
 * was generated with all live here, indexed by that ID. Colosseum keeps the
 * same information in the record itself, so it has no table.
 */
#define GEN3_SHADOW_MAX 128u

typedef struct Gen3ShadowEntry {
    bool present;
    bool snagged;
    bool purified;
    uint16_t species;     /* National Dex */
    uint32_t pid;
    int32_t purification; /* heart gauge; counts up to 0 */
    uint32_t experience;
    uint8_t ivs[6];       /* HP, Atk, Def, Spe, SpA, SpD */
    uint8_t index;
} Gen3ShadowEntry;

bool gen3_any_has_shadow_table(const Gen3AnySave *save);
unsigned gen3_any_shadow_count(const Gen3AnySave *save);
bool gen3_any_shadow_entry(const Gen3AnySave *save, unsigned id, Gen3ShadowEntry *out);
bool gen3_any_set_shadow_purified(Gen3AnySave *save, unsigned id, bool purified);
bool gen3_any_set_shadow_purification(Gen3AnySave *save, unsigned id, int32_t value);
/*
 * Item pouches. The cartridge saves keep four-byte little-endian slots with
 * the quantity masked by the security key; Colosseum and XD keep the same
 * shape big-endian and unmasked, with a different set of pouches. This picks
 * the right one so the inventory editor does not have to.
 */
unsigned gen3_any_pocket_capacity(const Gen3AnySave *save, Gen3Pocket pocket);
bool gen3_any_get_item_slot(const Gen3AnySave *save, Gen3Pocket pocket, unsigned slot, Gen3ItemSlot *out);
bool gen3_any_set_item_slot(Gen3AnySave *save, Gen3Pocket pocket, unsigned slot,
                            uint16_t item_id, uint16_t quantity);
/* The largest quantity a pouch accepts; key items and discs hold one each. */
uint16_t gen3_any_pocket_max_quantity(const Gen3AnySave *save, Gen3Pocket pocket);

bool gen3_any_export(const Gen3AnySave *save, uint8_t *out, size_t out_size);

#endif
