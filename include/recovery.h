#ifndef PKHEX_GC_RECOVERY_H
#define PKHEX_GC_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gen3_all.h"

#define BACKUP_TARGET_LEN 768
#define BACKUP_CARD_FILENAME_LEN 32

typedef enum BackupSourceType {
    BACKUP_SOURCE_NONE = 0,
    BACKUP_SOURCE_FILE,
    BACKUP_SOURCE_CARD,
    BACKUP_SOURCE_GBA_LINK,
} BackupSourceType;

typedef struct BackupMeta {
    BackupSourceType source;
    Gen3SaveKind kind;
    size_t size;
    char target[BACKUP_TARGET_LEN];
    int card_slot;
    char card_filename[BACKUP_CARD_FILENAME_LEN + 1];
    uint8_t gamecode[4];
    uint8_t company[2];
} BackupMeta;

void backup_meta_path(const char *backup, char *out, size_t out_size);
bool backup_meta_write(const char *backup, const BackupMeta *meta);
bool backup_meta_read(const char *backup, BackupMeta *meta);

#endif
