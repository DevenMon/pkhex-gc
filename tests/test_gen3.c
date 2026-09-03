#include "gen3.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG 0x08012025u

static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static uint16_t checksum(const uint8_t *p, unsigned id) {
    uint32_t s = 0;
    size_t n = id==0?0xF2Cu:(id==4?0xF08u:(id==13?0x7D0u:GEN3_SECTOR_USED));
    for (size_t i=0; i<n; i+=4) s += get32(p+i);
    return (uint16_t)((s & 0xffffu) + (s >> 16));
}

static uint8_t enc(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0xBB + c - 'A');
    if (c >= 'a' && c <= 'z') return (uint8_t)(0xD5 + c - 'a');
    if (c >= '0' && c <= '9') return (uint8_t)(0xA1 + c - '0');
    if (c == ' ') return 0x00;
    return 0xAC;
}

static void text(uint8_t *dst, size_t n, const char *s) {
    memset(dst, 0xFF, n);
    for (size_t i=0; i<n && s[i]; ++i)
        dst[i] = enc(s[i]);
}

static void make_pk3(uint8_t *dst, size_t n, uint16_t species, uint8_t level) {
    assert(n >= 80);
    memset(dst, 0, n);
    const uint32_t pid = 0; /* permutation 0 = GAEM */
    const uint32_t otid = 0x12345678u;
    put32(dst+0x00, pid);
    put32(dst+0x04, otid);
    text(dst+0x08, 10, "PIKACHU");
    dst[0x12] = 2; /* English */
    dst[0x13] = 2; /* Has Species: what marks the slot occupied */
    text(dst+0x14, 7, "TESTER");

    uint8_t plain[48] = {0};
    put16(plain+0x00, species);
    put16(plain+0x02, 0);
    put32(plain+0x04, 15625u);
    plain[0x09] = 70;
    put16(plain+0x0C, 85);  /* Thunderbolt */
    put16(plain+0x0E, 98);  /* Quick Attack */
    plain[0x14] = 15; plain[0x15] = 30; /* PP */
    plain[0x18] = 12; plain[0x19] = 34; plain[0x1A] = 56;
    /* IV word at canonical misc+4 = offset 0x28. */
    uint32_t ivw = 31u | (30u<<5) | (29u<<10) | (28u<<15) | (27u<<20) | (26u<<25);
    put32(plain+0x28, ivw);

    uint32_t sum = 0;
    for (size_t i=0; i<sizeof(plain); i+=2)
        sum += get16(plain+i);
    put16(dst+0x1C, (uint16_t)sum);

    const uint32_t key = pid ^ otid;
    for (size_t i=0; i<sizeof(plain); i+=4)
        put32(dst+0x20+i, get32(plain+i) ^ key);

    if (n >= 100)
        dst[0x54] = level;
}

static const uint8_t *logical_chunk(const uint8_t *small, const uint8_t *large, const uint8_t *storage, unsigned id) {
    if (id == 0) return small;
    if (id <= 4) return large + (size_t)(id-1)*GEN3_SECTOR_USED;
    return storage + (size_t)(id-5)*GEN3_SECTOR_USED;
}

static void write_slot(uint8_t *raw, int slot, uint32_t index, const uint8_t *small, const uint8_t *large, const uint8_t *storage, unsigned rotate) {
    const size_t base = (size_t)slot * GEN3_HALF_SIZE;
    for (unsigned physical=0; physical<GEN3_MAIN_SECTORS; ++physical) {
        unsigned id = (physical + rotate) % GEN3_MAIN_SECTORS;
        uint8_t *sector = raw + base + (size_t)physical*GEN3_SECTOR_SIZE;
        memset(sector, 0, GEN3_SECTOR_SIZE);
        memcpy(sector, logical_chunk(small, large, storage, id), GEN3_SECTOR_USED);
        put16(sector+0xFF4, (uint16_t)id);
        put16(sector+0xFF6, checksum(sector,id));
        put32(sector+0xFF8, SIG);
        put32(sector+0xFFC, index);
    }
}

int main(void) {
    uint8_t *raw = calloc(1, GEN3_RAW_SIZE);
    uint8_t *small = calloc(1, GEN3_SECTOR_USED);
    uint8_t *large = calloc(1, 4*GEN3_SECTOR_USED);
    uint8_t *storage = calloc(1, 9*GEN3_SECTOR_USED);
    assert(raw && small && large && storage);

    text(small, 7, "TESTER");
    small[8] = 0;
    put16(small+0x0A, 12345);
    put16(small+0x0C, 54321);
    put16(small+0x0E, 321);
    small[0x10] = 45;
    small[0x11] = 12;
    put32(small+0xAC, 0xDEADBEEFu); /* non-RS/non-FRLG -> inspect E tail */
    small[0x900] = 1;               /* Emerald extends beyond RS 0x890 */
    small[0xF50] = 0xA5;            /* outside section-0 checksum range */
    large[3*GEN3_SECTOR_USED + 0xF40] = 0x5A; /* outside section-4 checksum range */

    large[0x234] = 1;
    make_pk3(large+0x238, GEN3_PK3_PARTY_SIZE, 25, 25);
    /* Emerald protects money/coins/bag quantities with the 0xAC security key. */
    const uint32_t sec_key = 0xDEADBEEFu;
    put32(large+0x490, 123456u ^ sec_key);
    put16(large+0x494, (uint16_t)(777u ^ (uint16_t)sec_key));
    put16(large+0x498, 13); put16(large+0x49A, 20); /* PC item: quantity is plain */
    put16(large+0x560, 1);  put16(large+0x562, (uint16_t)(42u ^ (uint16_t)sec_key));

    storage[0] = 3;
    make_pk3(storage+4, GEN3_PK3_STORED_SIZE, 277, 0); /* Treecko internal ID */
    const size_t names = 4 + GEN3_BOX_COUNT*GEN3_BOX_SLOTS*GEN3_PK3_STORED_SIZE;
    text(storage+names, 9, "BOX1");

    /* Older slot A + newer slot B, both with rotated physical sector order. */
    write_slot(raw, 0, 100, small, large, storage, 5);
    write_slot(raw, 1, 101, small, large, storage, 9);

    Gen3Save save;
    assert(gen3_open(&save, raw, GEN3_RAW_SIZE));
    assert(save.active_slot == 1);
    assert(save.game == GEN3_GAME_EMERALD);
    assert(!save.japanese);
    assert(save.slots[0].checksums_ok && save.slots[1].checksums_ok);

    char name[32];
    gen3_trainer_name(&save, name, sizeof(name));
    assert(strcmp(name, "TESTER") == 0);
    assert(gen3_tid(&save) == 12345);
    assert(gen3_sid(&save) == 54321);
    assert(gen3_played_hours(&save) == 321);
    assert(gen3_played_minutes(&save) == 45);
    assert(gen3_party_count(&save) == 1);
    assert(gen3_money(&save) == 123456u);
    assert(gen3_coins(&save) == 777u);
    assert(gen3_pocket_capacity(&save, GEN3_POCKET_PC) == 50);
    assert(gen3_pocket_capacity(&save, GEN3_POCKET_ITEMS) == 30);
    Gen3ItemSlot item;
    assert(gen3_get_item_slot(&save, GEN3_POCKET_PC, 0, &item) && item.item_id == 13 && item.quantity == 20);
    assert(gen3_get_item_slot(&save, GEN3_POCKET_ITEMS, 0, &item) && item.item_id == 1 && item.quantity == 42);

    Gen3Pokemon p;
    assert(gen3_party_pokemon(&save, 0, &p));
    assert(p.present && p.checksum_ok);
    assert(p.species_internal == 25);
    assert(p.level == 25);
    assert(strcmp(p.nickname, "PIKACHU") == 0);
    assert(strcmp(gen3_species_name(p.species_internal), "Pikachu") == 0);
    assert(p.tid == 0x5678 && p.sid == 0x1234);
    assert(p.moves[0] == 85 && p.moves[1] == 98 && p.pp[0] == 15);
    assert(p.evs[0] == 12 && p.evs[1] == 34 && p.evs[2] == 56);
    assert(p.ivs[0] == 31 && p.ivs[1] == 30 && p.ivs[5] == 26);

    assert(gen3_current_box(&save) == 3);
    gen3_box_name(&save, 0, name, sizeof(name));
    assert(strcmp(name, "BOX1") == 0);
    assert(gen3_box_pokemon(&save, 0, 0, &p));
    assert(p.present && p.checksum_ok && p.species_internal == 277);
    assert(strcmp(gen3_species_name(p.species_internal), "Treecko") == 0);


    /* Edit trainer + a boxed Pokemon, export a separate raw save, and reopen it. */
    assert(gen3_set_trainer_name_ascii(&save, "SAM"));
    gen3_set_trainer_gender(&save, 1);
    gen3_set_tid(&save, 22222);
    gen3_set_sid(&save, 33333);
    gen3_set_playtime(&save, 444, 58, 59);
    gen3_set_money(&save, 999999);
    gen3_set_coins(&save, 9999);
    assert(gen3_set_item_slot(&save, GEN3_POCKET_PC, 0, 50, 99));
    assert(gen3_set_item_slot(&save, GEN3_POCKET_ITEMS, 0, 25, 88));
    assert(gen3_set_item_slot(&save, GEN3_POCKET_BERRIES, 45, 133, 12));
    assert(gen3_box_pokemon(&save, 0, 0, &p));
    p.species_internal = gen3_species_internal_from_national(386);
    p.held_item = 42;
    p.experience = 999999;
    p.friendship = 255;
    p.pid = 0xCAFEBABEu; /* forces a different substructure permutation/key */
    p.tid = 22222; p.sid = 33333;
    p.moves[0] = 354; p.moves[1] = 85; p.moves[2] = 98; p.moves[3] = 0;
    p.pp[0] = 5; p.pp[1] = 15; p.pp[2] = 30; p.pp[3] = 0;
    p.evs[0]=252; p.evs[1]=1; p.evs[2]=2; p.evs[3]=3; p.evs[4]=252; p.evs[5]=0;
    for (unsigned i=0;i<6;++i) p.ivs[i]=(uint8_t)(31-i);
    p.is_egg = true;
    assert(gen3_set_box_pokemon(&save, 0, 0, &p));

    uint8_t *edited = calloc(1, GEN3_RAW_SIZE);
    uint8_t *source_copy = malloc(GEN3_RAW_SIZE);
    assert(edited && source_copy);
    memcpy(source_copy, raw, GEN3_RAW_SIZE);
    assert(gen3_export_raw(&save, edited, GEN3_RAW_SIZE));
    /* Source buffer stays untouched. */
    assert(memcmp(raw, source_copy, GEN3_RAW_SIZE) == 0);
    free(source_copy);
    Gen3Save reopened;
    assert(gen3_open(&reopened, edited, GEN3_RAW_SIZE));
    gen3_trainer_name(&reopened, name, sizeof(name));
    assert(strcmp(name, "SAM") == 0);
    assert(gen3_trainer_gender(&reopened) == 1);
    assert(gen3_tid(&reopened) == 22222 && gen3_sid(&reopened) == 33333);
    assert(gen3_played_hours(&reopened) == 444 && gen3_played_minutes(&reopened) == 58 && gen3_played_seconds(&reopened) == 59);
    assert(gen3_money(&reopened) == 999999u && gen3_coins(&reopened) == 9999u);
    assert(gen3_get_item_slot(&reopened, GEN3_POCKET_PC, 0, &item) && item.item_id == 50 && item.quantity == 99);
    assert(gen3_get_item_slot(&reopened, GEN3_POCKET_ITEMS, 0, &item) && item.item_id == 25 && item.quantity == 88);
    assert(gen3_get_item_slot(&reopened, GEN3_POCKET_BERRIES, 45, &item) && item.item_id == 133 && item.quantity == 12);
    assert(gen3_box_pokemon(&reopened, 0, 0, &p));
    assert(p.present && p.checksum_ok);
    assert(gen3_species_national(p.species_internal) == 386);
    assert(p.held_item == 42 && p.experience == 999999 && p.friendship == 255);
    assert(p.pid == 0xCAFEBABEu && p.tid == 22222 && p.sid == 33333);
    assert(p.moves[0] == 354 && p.moves[1] == 85 && p.pp[0] == 5);
    assert(p.evs[0] == 252 && p.evs[4] == 252 && p.ivs[0] == 31 && p.ivs[5] == 26 && p.is_egg);
    free(edited);

    /* Layout sanity checks for the other GBA families: capacities, offsets,
     * RS plain quantities, and FRLG security-key XOR. */
    Gen3Save rs = {0}; rs.game = GEN3_GAME_RS;
    assert(gen3_pocket_capacity(&rs,GEN3_POCKET_PC)==50);
    assert(gen3_pocket_capacity(&rs,GEN3_POCKET_ITEMS)==20);
    assert(gen3_pocket_capacity(&rs,GEN3_POCKET_KEY_ITEMS)==20);
    assert(gen3_pocket_capacity(&rs,GEN3_POCKET_BALLS)==16);
    assert(gen3_pocket_capacity(&rs,GEN3_POCKET_TMS)==64);
    assert(gen3_pocket_capacity(&rs,GEN3_POCKET_BERRIES)==46);
    gen3_set_money(&rs,321000); gen3_set_coins(&rs,1234);
    assert(get32(rs.large+0x490)==321000u && get16(rs.large+0x494)==1234u);
    assert(gen3_set_item_slot(&rs,GEN3_POCKET_ITEMS,19,13,55));
    assert(get16(rs.large+0x560+19*4)==13 && get16(rs.large+0x562+19*4)==55);

    Gen3Save fr = {0}; fr.game = GEN3_GAME_FRLG; put32(fr.small+0xAF8,0x11223344u);
    assert(gen3_pocket_capacity(&fr,GEN3_POCKET_PC)==30);
    assert(gen3_pocket_capacity(&fr,GEN3_POCKET_ITEMS)==42);
    assert(gen3_pocket_capacity(&fr,GEN3_POCKET_KEY_ITEMS)==30);
    assert(gen3_pocket_capacity(&fr,GEN3_POCKET_BALLS)==13);
    assert(gen3_pocket_capacity(&fr,GEN3_POCKET_TMS)==58);
    assert(gen3_pocket_capacity(&fr,GEN3_POCKET_BERRIES)==43);
    gen3_set_money(&fr,654321); gen3_set_coins(&fr,4321);
    assert(get32(fr.large+0x290)==(654321u^0x11223344u));
    assert(get16(fr.large+0x294)==(uint16_t)(4321u^0x3344u));
    assert(gen3_set_item_slot(&fr,GEN3_POCKET_BERRIES,42,133,77));
    assert(get16(fr.large+0x54C + 42*4)==133);
    assert(get16(fr.large+0x54E + 42*4)==(uint16_t)(77u^0x3344u));

    /* Names set from the on-screen keyboard go in as raw Gen III bytes and
     * have to come back out through the ordinary readers. */
    {
        uint8_t typed[9];
        gen3_encode_text("ZIGZAGOON", typed, sizeof(typed));
        assert(gen3_set_box_name(&save, 3, typed, 9));
        char box_name[24];
        gen3_box_name(&save, 3, box_name, sizeof(box_name));
        assert(strcmp(box_name, "ZIGZAGOON") == 0);

        uint8_t read_back[GEN3_BOX_NAME_LEN];
        gen3_box_name_raw(&save, 3, read_back);
        assert(memcmp(read_back, typed, sizeof(typed)) == 0);

        /* A shorter name must clear what the longer one left behind. */
        gen3_encode_text("PC", typed, sizeof(typed));
        assert(gen3_set_box_name(&save, 3, typed, 2));
        gen3_box_name(&save, 3, box_name, sizeof(box_name));
        assert(strcmp(box_name, "PC") == 0);
        gen3_box_name_raw(&save, 3, read_back);
        for (unsigned i = 2; i < GEN3_BOX_NAME_LEN; ++i) assert(read_back[i] == GEN3_TEXT_TERMINATOR);

        uint8_t trainer[7];
        assert(gen3_trainer_name_length(&save) == 7);
        gen3_encode_text("BRENDAN", trainer, sizeof(trainer));
        assert(gen3_set_trainer_name(&save, trainer, 7));
        gen3_trainer_name(&save, name, sizeof(name));
        assert(strcmp(name, "BRENDAN") == 0);
        /* One byte too many for the field is refused rather than truncated. */
        assert(!gen3_set_trainer_name(&save, trainer, 8));
    }

    /* UI item-name table must expose actual Gen III names, not raw IDs. */
    assert(strcmp(gen3_item_name(14), "Antidote") == 0);
    assert(strcmp(gen3_item_name(19), "Full Restore") == 0);
    assert(strcmp(gen3_item_name(85), "Escape Rope") == 0);
    assert(strcmp(gen3_item_name(103), "TinyMushroom") == 0);
    assert(strcmp(gen3_item_name(289), "TM01") == 0);
    assert(strcmp(gen3_item_name(346), "HM08") == 0);
    assert(strcmp(gen3_move_name(15), "Cut") == 0);
    assert(strcmp(gen3_move_name(57), "Surf") == 0);
    assert(strcmp(gen3_move_name(354), "Psycho Boost") == 0);

    /* Corrupt one payload byte in newer slot; reader should prefer valid older slot. */
    raw[GEN3_HALF_SIZE + 0x20] ^= 1;
    assert(gen3_open(&save, raw, GEN3_RAW_SIZE));
    assert(save.active_slot == 0);
    assert(save.slots[1].checksum_failures == 1);

    free(storage); free(large); free(small); free(raw);
    puts("gen3 parser tests: PASS");
    return 0;
}
