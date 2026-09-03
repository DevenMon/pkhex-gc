#define _POSIX_C_SOURCE 200809L

#include "recovery.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void temp_backup_path(char path[]) {
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    unlink(path);
}

static void test_file_meta_round_trip(void) {
    char backup[] = "/tmp/pkhexgc-file-meta-XXXXXX";
    temp_backup_path(backup);
    BackupMeta in = {0}, out = {0};
    in.source = BACKUP_SOURCE_FILE;
    in.kind = GEN3_KIND_GBA;
    in.size = 131072;
    snprintf(in.target, sizeof(in.target), "carda:/saves/BPGE01.sav");
    assert(backup_meta_write(backup, &in));
    assert(backup_meta_read(backup, &out));
    assert(out.source == in.source && out.kind == in.kind && out.size == in.size);
    assert(strcmp(out.target, in.target) == 0);
    char meta[BACKUP_TARGET_LEN + 8]; backup_meta_path(backup, meta, sizeof(meta)); unlink(meta);
}

static void test_v054_header_is_accepted(void) {
    char backup[] = "/tmp/pkhexgc-v054-meta-XXXXXX";
    temp_backup_path(backup);
    char meta[BACKUP_TARGET_LEN + 8]; backup_meta_path(backup, meta, sizeof(meta));
    FILE *f = fopen(meta, "wb"); assert(f);
    fputs("PKHEXGC_BACKUP_V1\nsource=file\nkind=1\nsize=131072\ntarget=carda:/BPGE01.sav\n", f);
    assert(fclose(f) == 0);
    BackupMeta out = {0};
    assert(backup_meta_read(backup, &out));
    assert(out.source == BACKUP_SOURCE_FILE && out.kind == GEN3_KIND_GBA);
    assert(strcmp(out.target, "carda:/BPGE01.sav") == 0);
    unlink(meta);
}


static void test_card_meta_round_trip(void) {
    char backup[] = "/tmp/pkhexgc-card-meta-XXXXXX";
    temp_backup_path(backup);
    BackupMeta in = {0}, out = {0};
    in.source = BACKUP_SOURCE_CARD;
    in.kind = GEN3_KIND_COLOSSEUM;
    in.size = GEN3_COLO_SIZE;
    in.card_slot = 0;
    snprintf(in.card_filename, sizeof(in.card_filename), "pokemon_colosseum");
    memcpy(in.gamecode, "GC6E", 4);
    memcpy(in.company, "01", 2);
    assert(backup_meta_write(backup, &in));
    assert(backup_meta_read(backup, &out));
    assert(out.source == in.source && out.kind == in.kind && out.size == in.size);
    assert(out.card_slot == 0 && strcmp(out.card_filename, in.card_filename) == 0);
    assert(memcmp(out.gamecode, in.gamecode, 4) == 0 && memcmp(out.company, in.company, 2) == 0);
    char meta[BACKUP_TARGET_LEN + 8]; backup_meta_path(backup, meta, sizeof(meta)); unlink(meta);
}

int main(void) {
    test_file_meta_round_trip();
    test_v054_header_is_accepted();
    test_card_meta_round_trip();
    puts("backup metadata tests: PASS");
    return 0;
}
