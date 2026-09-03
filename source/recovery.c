#include "recovery.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BACKUP_META_MAGIC "PKHEXGC_BACKUP_V1"

void backup_meta_path(const char *backup, char *out, size_t out_size) {
    if (!out || !out_size) return;
    snprintf(out, out_size, "%s.meta", backup ? backup : "");
}

bool backup_meta_write(const char *backup, const BackupMeta *m) {
    if (!backup || !m) return false;
    char mp[BACKUP_TARGET_LEN + 8];
    backup_meta_path(backup, mp, sizeof(mp));
    FILE *f = fopen(mp, "wb");
    if (!f) return false;
    const char *src = m->source == BACKUP_SOURCE_FILE ? "file" :
                      m->source == BACKUP_SOURCE_CARD ? "card" :
                      m->source == BACKUP_SOURCE_GBA_LINK ? "gba-link" : "none";
    bool ok = fprintf(f, BACKUP_META_MAGIC "\nsource=%s\nkind=%d\nsize=%lu\n",
                      src, (int)m->kind, (unsigned long)m->size) > 0;
    if (m->source == BACKUP_SOURCE_FILE)
        ok = ok && fprintf(f, "target=%s\n", m->target) > 0;
    else if (m->source == BACKUP_SOURCE_CARD) {
        ok = ok && fprintf(f, "slot=%d\nfilename=%s\ngamecode=%02X%02X%02X%02X\ncompany=%02X%02X\n",
                           m->card_slot, m->card_filename,
                           m->gamecode[0], m->gamecode[1], m->gamecode[2], m->gamecode[3],
                           m->company[0], m->company[1]) > 0;
    } else if (m->source == BACKUP_SOURCE_GBA_LINK) {
        /* The game code identifies the cartridge a restore is allowed to
         * touch, so it is recorded alongside the human-readable target. */
        ok = ok && fprintf(f, "target=%s\ngamecode=%02X%02X%02X%02X\ncompany=%02X%02X\n",
                           m->target,
                           m->gamecode[0], m->gamecode[1], m->gamecode[2], m->gamecode[3],
                           m->company[0], m->company[1]) > 0;
    }
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) unlink(mp);
    return ok;
}

static bool parse_hex_byte(const char *p, uint8_t *out) {
    if (!p || !out || !isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return false;
    unsigned hi=(unsigned)(isdigit((unsigned char)p[0])?p[0]-'0':10+tolower((unsigned char)p[0])-'a');
    unsigned lo=(unsigned)(isdigit((unsigned char)p[1])?p[1]-'0':10+tolower((unsigned char)p[1])-'a');
    *out = (uint8_t)((hi<<4)|lo);
    return true;
}

bool backup_meta_read(const char *backup, BackupMeta *m) {
    if (!backup || !m) return false;
    memset(m, 0, sizeof(*m));
    m->card_slot = -1;
    char mp[BACKUP_TARGET_LEN + 8];
    backup_meta_path(backup, mp, sizeof(mp));
    FILE *f = fopen(mp, "rb");
    if (!f) return false;

    char line[BACKUP_TARGET_LEN + 64];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return false;
    }
    char *nl = strpbrk(line, "\r\n");
    if (nl) *nl = '\0';
    if (strcmp(line, BACKUP_META_MAGIC) != 0) {
        fclose(f);
        return false;
    }

    while (fgets(line, sizeof(line), f)) {
        nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = '\0';
        if (!strcmp(line, "source")) {
            if (!strcmp(eq, "file")) m->source = BACKUP_SOURCE_FILE;
            else if (!strcmp(eq, "card")) m->source = BACKUP_SOURCE_CARD;
            else if (!strcmp(eq, "gba-link")) m->source = BACKUP_SOURCE_GBA_LINK;
        } else if (!strcmp(line, "kind")) {
            m->kind = (Gen3SaveKind)strtol(eq, NULL, 10);
        } else if (!strcmp(line, "size")) {
            m->size = (size_t)strtoul(eq, NULL, 10);
        } else if (!strcmp(line, "target")) {
            snprintf(m->target, sizeof(m->target), "%s", eq);
        } else if (!strcmp(line, "slot")) {
            m->card_slot = (int)strtol(eq, NULL, 10);
        } else if (!strcmp(line, "filename")) {
            snprintf(m->card_filename, sizeof(m->card_filename), "%.32s", eq);
        } else if (!strcmp(line, "gamecode") && strlen(eq) == 8) {
            for (int i = 0; i < 4; ++i)
                if (!parse_hex_byte(eq + i * 2, &m->gamecode[i])) { fclose(f); return false; }
        } else if (!strcmp(line, "company") && strlen(eq) == 4) {
            for (int i = 0; i < 2; ++i)
                if (!parse_hex_byte(eq + i * 2, &m->company[i])) { fclose(f); return false; }
        }
    }
    fclose(f);

    if (m->source == BACKUP_SOURCE_FILE)
        return m->target[0] && m->size > 0 && m->kind != GEN3_KIND_UNKNOWN;
    if (m->source == BACKUP_SOURCE_CARD)
        return m->card_slot >= 0 && m->card_filename[0] && m->size > 0 &&
               m->kind != GEN3_KIND_UNKNOWN;
    if (m->source == BACKUP_SOURCE_GBA_LINK)
        return m->target[0] && m->size > 0 && m->kind == GEN3_KIND_GBA;
    return false;
}
