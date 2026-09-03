#include <gccore.h>
#include <ogc/card.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>

#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "gen3_all.h"
#include "gen3_personal.h"
#include "gen3_blocks.h"
#include "gen3_event_names.h"
#include "gen3_legality.h"
#include "gui.h"
#include "gbalink.h"
#include "recovery.h"
#include "uinput.h"

#define APP_NAME "PKHeX-GC Gen III"
#define APP_VERSION "1.0"
#define MAX_ENTRIES 256
#define MAX_CARD_ENTRIES 64
#define NAME_LEN 192
#define PATH_LEN 768
#define VISIBLE_ROWS 12

typedef struct BrowserEntry {
    char name[NAME_LEN];
    bool is_dir;
    off_t size;
} BrowserEntry;

typedef struct CardBrowserEntry {
    int slot;
    card_dir dir;
} CardBrowserEntry;

typedef struct BackupEntry {
    char name[NAME_LEN];
    char path[PATH_LEN];
    off_t size;
    bool has_meta;
} BackupEntry;

typedef enum UiMode {
    UI_BROWSER,
    UI_CARD_BROWSER,
    UI_BACKUP_BROWSER,
    UI_SUMMARY,
    UI_BOXES,
    UI_TRAINER_EDIT,
    UI_INVENTORY_EDIT,
    UI_PKM_EDIT,
    UI_POKEDEX,
    UI_EVENTS,
    UI_TOOLS,
    UI_DAYCARE,
    UI_ROAMER,
    UI_MAIL,
    UI_HALL_OF_FAME,
    UI_RECORDS,
    UI_BOX_LAYOUT,
    UI_POKEBLOCKS,
    UI_SECRET_BASES,
    UI_DECORATIONS,
    UI_EMERALD_EXTRAS,
    UI_MEMO,
    UI_GAMECUBE_LINK,
    UI_LEGALITY,
    UI_FRONTIER,
    UI_CLOCK,
    UI_SHADOWS,
    UI_SAVE_CHECK,
    UI_MISC,
    UI_RECORD_FILES,
    UI_CONFIRM_SAVE,
    UI_KEYBOARD,
    UI_ERROR,
} UiMode;

static GuiContext gui;
static BrowserEntry entries[MAX_ENTRIES];
static CardBrowserEntry card_entries[MAX_CARD_ENTRIES];
static BackupEntry backup_entries[MAX_ENTRIES];
static int entry_count, card_entry_count, backup_entry_count;
static int selected, card_selected, backup_selected;
static int backup_root_index;
/*
 * The selected backup's metadata, and which entry it belongs to. Reading it is
 * a file open and read on the SD card: doing that once per frame to draw the
 * side panel was enough to stall the UI while the selection was being moved
 * quickly, which looked like the application had hung.
 */
static BackupMeta backup_meta_cache;
static bool backup_meta_cache_valid;
static int backup_meta_cache_index = -1;
static char current_path[PATH_LEN] = "sd:/";
static const char *roots[4];
static int root_count, current_root;
static uint8_t *save_bytes;
static size_t save_bytes_size;
static Gen3AnySave parsed_save;
static char loaded_path[PATH_LEN];
static UiMode mode = UI_BROWSER;
static char error_message[512];
static UiMode dex_return;
static UiMode event_return;
/* A Pokemon picked up with X, waiting to be put down in another slot. */
static Gen3Pokemon box_held;
static bool box_holding;
static unsigned box_held_box, box_held_slot;
static unsigned box_index;
static unsigned box_selected;
static unsigned party_selected;
static bool fat_available;
static bool loaded_from_card;
static bool loaded_from_cart;
static bool loaded_from_backup;
static bool live_edit_allowed(void);
static void inventory_step_pocket(int direction);
static void scan_record_files(void);
static unsigned check_count, check_scroll;
static bool check_ran;
static void set_status(const char *msg);
static void render_current(void);
static CardBrowserEntry loaded_card_source;
static bool save_dirty;
static char status_message[160];
static unsigned status_frames;
static char card_io_detail[128];

typedef enum PkmEditSource { PKM_EDIT_NONE=0, PKM_EDIT_PARTY, PKM_EDIT_BOX, PKM_EDIT_DAYCARE } PkmEditSource;
static Gen3Pokemon edit_pkm;
static PkmEditSource edit_source;
static unsigned edit_source_slot;
static unsigned edit_source_box;
/*
 * CARD_Mount blocks, and on real hardware it can block for a long time in the
 * first moments after boot - long enough that entering Hardware early looks
 * like a freeze. So the scan is deferred rather than skipped: entering the
 * screen schedules one, and it runs as soon as the console has settled.
 */
#define CARD_SETTLE_MS 1500u
static uint64_t app_start_time;
static bool card_scan_pending;

static unsigned extras_row;
static unsigned memo_index;
static unsigned gclink_row;
static unsigned legality_row;
static bool legality_scanned;
static unsigned deco_kind;
static unsigned deco_slot;
static unsigned dex_selected;
static unsigned event_selected;
static bool event_show_work;
static unsigned pkm_edit_field;
static unsigned pkm_edit_page;
static unsigned trainer_edit_field;
static Gen3Pocket inventory_pocket = GEN3_POCKET_ITEMS;
static unsigned inventory_slot;
static unsigned inventory_field; /* 0=item id, 1=quantity */

static uint8_t card_work_a[CARD_WORKAREA_SIZE] ATTRIBUTE_ALIGN(32);
static uint8_t card_work_b[CARD_WORKAREA_SIZE] ATTRIBUTE_ALIGN(32);

static bool path_exists_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return false;
    closedir(d);
    return true;
}

static void detect_roots(void) {
    root_count = 0;
    if (path_exists_dir("sd:/")) roots[root_count++] = "sd:/";       /* SD2SP2 */
    if (path_exists_dir("carda:/")) roots[root_count++] = "carda:/"; /* SD Gecko A */
    if (path_exists_dir("cardb:/")) roots[root_count++] = "cardb:/"; /* SD Gecko B */
    if (root_count == 0 && path_exists_dir("/")) roots[root_count++] = "/";
    current_root = 0;
    if (root_count) snprintf(current_path, sizeof(current_path), "%s", roots[0]);
}

static const char *extension(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

/*
 * Flash carts and emulators number their save slots in the extension rather
 * than the name: an R4 writes .sv4, No$GBA writes .sv, VisualBoyAdvance writes
 * .sa1 and .sa2. The bytes inside are an ordinary cartridge save, so a save
 * pulled off a cartridge with a flash cart was being hidden by the browser for
 * the sake of a digit.
 */
static bool is_slot_numbered_extension(const char *ext) {
    if (tolower((unsigned char)ext[0]) != 's') return false;
    const char second = (char)tolower((unsigned char)ext[1]);
    if (second != 'v' && second != 'a') return false;
    if (ext[2] == '\0') return second == 'v';   /* .sv, but not a bare .sa */
    return ext[3] == '\0' && ext[2] >= '0' && ext[2] <= '9';
}

static bool is_save_candidate(const char *name) {
    const char *ext = extension(name);
    return strcasecmp(ext, "sav") == 0 || strcasecmp(ext, "srm") == 0 ||
           strcasecmp(ext, "bin") == 0 || strcasecmp(ext, "dat") == 0 ||
           strcasecmp(ext, "gci") == 0 || strcasecmp(ext, "fla") == 0 ||
           strcasecmp(ext, "bak") == 0 || is_slot_numbered_extension(ext);
}

static bool is_gc_gen3_size(size_t size) {
    return size == GEN3_COLO_SIZE || size == GEN3_XD_SIZE || size == GEN3_RSBOX_SIZE;
}

static int entry_cmp(const void *a, const void *b) {
    const BrowserEntry *ea = (const BrowserEntry *)a;
    const BrowserEntry *eb = (const BrowserEntry *)b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

static int backup_entry_cmp(const void *a, const void *b) {
    const BackupEntry *ea=(const BackupEntry*)a, *eb=(const BackupEntry*)b;
    /* Timestamp is in the filename, so reverse lexical order puts newest first. */
    return -strcasecmp(ea->name, eb->name);
}

static void make_child_path(char *out, size_t out_size, const char *base, const char *name) {
    size_t n = strlen(base);
    if (n && base[n - 1] == '/') snprintf(out, out_size, "%s%s", base, name);
    else snprintf(out, out_size, "%s/%s", base, name);
}

static void scan_directory(void) {
    entry_count = 0;
    selected = 0;
    if (!fat_available || root_count == 0) return;
    DIR *d = opendir(current_path);
    if (!d) {
        snprintf(error_message, sizeof(error_message), "Cannot open %s\nerrno=%d", current_path, errno);
        mode = UI_ERROR;
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && entry_count < MAX_ENTRIES) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[PATH_LEN];
        make_child_path(full, sizeof(full), current_path, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        bool dir = S_ISDIR(st.st_mode);
        if (!dir && !is_save_candidate(de->d_name)) continue;
        BrowserEntry *e = &entries[entry_count++];
        snprintf(e->name, sizeof(e->name), "%s", de->d_name);
        e->is_dir = dir;
        e->size = st.st_size;
    }
    closedir(d);
    qsort(entries, (size_t)entry_count, sizeof(entries[0]), entry_cmp);
}

static void path_up(void) {
    size_t n = strlen(current_path);
    if (!n) return;
    for (int i = 0; i < root_count; ++i) if (!strcmp(current_path, roots[i])) return;
    while (n > 0 && current_path[n - 1] == '/') current_path[--n] = '\0';
    char *slash = strrchr(current_path, '/');
    if (slash) slash[1] = '\0';
}

static void switch_root(void) {
    if (root_count <= 1) return;
    current_root = (current_root + 1) % root_count;
    snprintf(current_path, sizeof(current_path), "%s", roots[current_root]);
    scan_directory();
}

static void *card_workarea(int slot) { return slot == CARD_SLOTA ? card_work_a : card_work_b; }

static void scan_memory_cards(void) {
    card_entry_count = 0;
    card_selected = 0;
    for (int slot = CARD_SLOTA; slot <= CARD_SLOTB; ++slot) {
        s32 m = CARD_Mount(slot, card_workarea(slot), NULL);
        if (m < CARD_ERROR_READY) continue;
        card_dir d;
        s32 r = CARD_FindFirst(slot, &d, true);
        while (r >= CARD_ERROR_READY && card_entry_count < MAX_CARD_ENTRIES) {
            if (is_gc_gen3_size(d.filelen)) {
                card_entries[card_entry_count].slot = slot;
                card_entries[card_entry_count].dir = d;
                ++card_entry_count;
            }
            r = CARD_FindNext(&d);
        }
        CARD_Unmount(slot);
    }
}


static void scan_backups(void) {
    /* The list is about to change under the cache, so drop it. */
    backup_meta_cache_index = -1;
    backup_meta_cache_valid = false;
    backup_entry_count=0; backup_selected=0;
    if(!fat_available||root_count<=0) return;
    if(backup_root_index<0||backup_root_index>=root_count) backup_root_index=current_root;
    char dir[PATH_LEN]; snprintf(dir,sizeof(dir),"%spkhex-gc-backups",roots[backup_root_index]);
    if(mkdir(dir,0777)!=0 && errno!=EEXIST) return;
    DIR*d=opendir(dir); if(!d) return; struct dirent*de;
    while((de=readdir(d))&&backup_entry_count<MAX_ENTRIES){
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;
        if(strcasecmp(extension(de->d_name),"bak")!=0)continue;
        BackupEntry*e=&backup_entries[backup_entry_count];
        snprintf(e->name,sizeof(e->name),"%s",de->d_name);
        make_child_path(e->path,sizeof(e->path),dir,de->d_name);
        struct stat st;if(stat(e->path,&st)!=0||!S_ISREG(st.st_mode))continue;
        e->size=st.st_size; BackupMeta meta; e->has_meta=backup_meta_read(e->path,&meta);
        ++backup_entry_count;
    }
    closedir(d);qsort(backup_entries,(size_t)backup_entry_count,sizeof(backup_entries[0]),backup_entry_cmp);
}

static void reset_loaded_save(void) {
    gen3_any_close(&parsed_save);
    loaded_from_card = false;
    loaded_from_cart = false;
    loaded_from_backup = false;
    free(save_bytes);
    save_bytes = NULL;
    save_bytes_size = 0;
}

static bool parse_loaded_bytes(uint8_t *buf, size_t size, const char *label) {
    reset_loaded_save();
    save_bytes = buf;
    save_bytes_size = size;
    if (!gen3_any_open(&parsed_save, save_bytes, save_bytes_size)) {
        snprintf(error_message, sizeof(error_message),
                 "Not recognized as a supported Gen III save:\n%.430s", label);
        return false;
    }
    snprintf(loaded_path, sizeof(loaded_path), "%s", label);
    save_dirty = false;
    status_message[0] = '\0'; status_frames = 0;
    party_selected = 0;
    edit_source = PKM_EDIT_NONE;
    box_index = gen3_any_current_box(&parsed_save);
    if (box_index >= gen3_any_box_count(&parsed_save)) box_index = 0;
    box_selected = 0;
    return true;
}

static bool load_save_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error_message, sizeof(error_message), "Could not open:\n%s\nerrno=%d", path, errno);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz <= 0 || (size_t)sz > GEN3_MAX_SAVE_FILE) {
        fclose(f);
        snprintf(error_message, sizeof(error_message),
                 "Unsupported file size: %ld bytes.\nExpected GBA save, Colosseum, XD, Box, or .gci.", sz);
        return false;
    }
    rewind(f);
    uint8_t *buf = (uint8_t *)memalign(32, ((size_t)sz + 31u) & ~31u);
    if (!buf) { fclose(f); snprintf(error_message, sizeof(error_message), "Out of memory."); return false; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); snprintf(error_message, sizeof(error_message), "Short read from %.400s", path); return false; }
    if (!parse_loaded_bytes(buf, got, path)) { free(save_bytes); save_bytes = NULL; save_bytes_size = 0; return false; }
    loaded_from_card = false;
    loaded_from_cart = false;
    return true;
}

static bool load_card_entry(const CardBrowserEntry *e) {
    s32 m = CARD_Mount(e->slot, card_workarea(e->slot), NULL);
    if (m < CARD_ERROR_READY) {
        snprintf(error_message, sizeof(error_message), "Could not mount physical memory card Slot %c (error %ld).",
                 e->slot == CARD_SLOTA ? 'A' : 'B', (long)m);
        return false;
    }
    card_dir d = e->dir;
    card_file f;
    s32 r = CARD_OpenEntry(e->slot, &d, &f);
    if (r < CARD_ERROR_READY) {
        CARD_Unmount(e->slot);
        snprintf(error_message, sizeof(error_message), "Could not open card save (error %ld).", (long)r);
        return false;
    }
    size_t size = d.filelen;
    uint8_t *buf = (uint8_t *)memalign(32, (size + 31u) & ~31u);
    if (!buf) {
        CARD_Close(&f); CARD_Unmount(e->slot);
        snprintf(error_message, sizeof(error_message), "Out of memory reading memory card save.");
        return false;
    }
    r = CARD_Read(&f, buf, (u32)size, 0);
    CARD_Close(&f);
    CARD_Unmount(e->slot);
    if (r < CARD_ERROR_READY) {
        free(buf);
        snprintf(error_message, sizeof(error_message), "Memory card read failed (error %ld).", (long)r);
        return false;
    }
    char label[PATH_LEN];
    snprintf(label, sizeof(label), "Memory Card Slot %c: %.32s",
             e->slot == CARD_SLOTA ? 'A' : 'B', d.filename);
    if (!parse_loaded_bytes(buf, size, label)) { free(save_bytes); save_bytes = NULL; save_bytes_size = 0; return false; }
    loaded_from_card = true;
    loaded_from_cart = false;
    loaded_card_source = *e;
    return true;
}


/* ------------------------------------------------------ GBA link cable ---- */

static GbaLinkCart linked_cart;
static bool linked_cart_valid;

static void gbalink_status(const char *stage, size_t done, size_t total, void *user) {
    (void)user;
    if (total) snprintf(status_message, sizeof(status_message), "%s %lu/%lu KiB",
                        stage ? stage : "GBA link", (unsigned long)(done / 1024u),
                        (unsigned long)(total / 1024u));
    else if (done) snprintf(status_message, sizeof(status_message), "%s... %lus",
                            stage ? stage : "GBA link", (unsigned long)done);
    else snprintf(status_message, sizeof(status_message), "%s...", stage ? stage : "GBA link");
    status_frames = 300;
    render_current();
}

/* Find the GBA, upload the save agent, and describe the cartridge. */
static bool connect_gba_cart(char *detail, size_t detail_size) {
    linked_cart_valid = false;

    int chan = 0;
    if (!gbalink_find_gba(&chan, detail, detail_size)) return false;

    snprintf(status_message, sizeof(status_message),
             "GBA found on port %d; uploading save agent...", chan + 1);
    status_frames = 300;
    render_current();

    if (!gbalink_boot_agent(chan, detail, detail_size, gbalink_status, NULL)) return false;
    if (!gbalink_identify(chan, &linked_cart, detail, detail_size)) return false;

    linked_cart_valid = true;
    return true;
}


static void set_status(const char *msg) {
    snprintf(status_message, sizeof(status_message), "%s", msg ? msg : "");
    status_frames = 180;
}

static void refresh_gba_summary(void) {
    if (parsed_save.kind != GEN3_KIND_GBA) return;
    gen3_trainer_name(&parsed_save.gba, parsed_save.trainer_name, sizeof(parsed_save.trainer_name));
    parsed_save.trainer_gender = gen3_trainer_gender(&parsed_save.gba);
    parsed_save.tid = gen3_tid(&parsed_save.gba);
    parsed_save.sid = gen3_sid(&parsed_save.gba);
    parsed_save.money = gen3_money(&parsed_save.gba);
    parsed_save.played_seconds = (uint64_t)gen3_played_hours(&parsed_save.gba) * 3600u +
                                 (uint64_t)gen3_played_minutes(&parsed_save.gba) * 60u +
                                 gen3_played_seconds(&parsed_save.gba);
    parsed_save.party_count = gen3_party_count(&parsed_save.gba);
}

static void backup_timestamp(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv)
        snprintf(out, out_size, "%04d-%02d-%02d_%02d-%02d-%02d",
                 tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                 tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    else
        snprintf(out, out_size, "RTC-UNKNOWN");
}

static int root_index_for_path(const char *path) {
    if (!path) return current_root;
    for (int i = 0; i < root_count; ++i) {
        size_t n = strlen(roots[i]);
        if (n && strncmp(path, roots[i], n) == 0) return i;
    }
    return current_root;
}

static bool backup_dir_for_root(int preferred, char *out, size_t out_size) {
    if (!fat_available || root_count <= 0 || !out || !out_size) return false;
    if (preferred < 0 || preferred >= root_count) preferred = current_root;
    for (int pass = 0; pass < root_count; ++pass) {
        int ri = (preferred + pass) % root_count;
        snprintf(out, out_size, "%spkhex-gc-backups", roots[ri]);
        if ((mkdir(out, 0777) == 0 || errno == EEXIST) && path_exists_dir(out)) return true;
    }
    return false;
}

static void sanitize_backup_stem(char *out, size_t out_size, const char *source) {
    const char *name = source ? strrchr(source, '/') : NULL;
    name = name ? name + 1 : (source ? source : "save");
    snprintf(out, out_size, "%.80s", name);
    for (char *q = out; *q; ++q) {
        unsigned char c = (unsigned char)*q;
        if (c < 0x20 || *q == '/' || *q == '\\' || *q == ':' || *q == '*' ||
            *q == '?' || *q == '"' || *q == '<' || *q == '>' || *q == '|') *q = '_';
    }
}

static bool make_backup_path(const char *stem_source, int preferred_root, char *out, size_t out_size) {
    char dir[PATH_LEN], stem[96], stamp[32];
    if (!backup_dir_for_root(preferred_root, dir, sizeof(dir))) return false;
    sanitize_backup_stem(stem, sizeof(stem), stem_source);
    backup_timestamp(stamp, sizeof(stamp));
    for (unsigned suffix = 0; suffix < 100u; ++suffix) {
        if (!suffix) snprintf(out, out_size, "%s/%s.%s.bak", dir, stem, stamp);
        else snprintf(out, out_size, "%s/%s.%s-%02u.bak", dir, stem, stamp, suffix);
        if (access(out, F_OK) != 0) return true;
    }
    return false;
}

static bool read_whole_file_exact(const char *path, uint8_t **out, size_t *size_out) {
    if (!path || !out || !size_out) return false;
    *out = NULL; *size_out = 0;
    FILE *f = fopen(path, "rb"); if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f); if (sz <= 0 || (size_t)sz > GEN3_MAX_SAVE_FILE) { fclose(f); return false; }
    rewind(f);
    uint8_t *b = (uint8_t*)memalign(32, ((size_t)sz + 31u) & ~31u);
    if (!b) { fclose(f); return false; }
    bool ok = fread(b,1,(size_t)sz,f)==(size_t)sz && fgetc(f)==EOF;
    if (fclose(f)!=0) ok=false;
    if (!ok) { free(b); return false; }
    *out=b; *size_out=(size_t)sz; return true;
}

static bool validate_disk_file(const char *path, size_t expected_size, Gen3SaveKind expected_kind) {
    if (!path || !*path || expected_size == 0 || expected_size > GEN3_MAX_SAVE_FILE) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t *buf = (uint8_t *)memalign(32, (expected_size + 31u) & ~31u);
    if (!buf) { fclose(f); return false; }
    bool ok = fread(buf, 1, expected_size, f) == expected_size;
    int extra = fgetc(f);
    if (fclose(f) != 0) ok = false;
    if (extra != EOF) ok = false;
    if (ok) {
        Gen3AnySave verify; memset(&verify, 0, sizeof(verify));
        ok = gen3_any_open(&verify, buf, expected_size) && verify.kind == expected_kind;
        if (verify.kind != GEN3_KIND_UNKNOWN) gen3_any_close(&verify);
    }
    free(buf);
    return ok;
}

static bool write_whole_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f=fopen(path,"wb"); if(!f) return false;
    bool ok=fwrite(data,1,size,f)==size && fflush(f)==0; if(fclose(f)!=0) ok=false; return ok;
}

static bool create_verified_backup(const char *stem_source, int preferred_root,
                                   const uint8_t *data, size_t size, Gen3SaveKind kind,
                                   const BackupMeta *meta, char *out, size_t out_size) {
    if (!data || !size || !meta || !out || !out_size) return false;
    if (!make_backup_path(stem_source, preferred_root, out, out_size)) return false;
    if (!write_whole_file(out, data, size) || !validate_disk_file(out, size, kind)) {
        unlink(out); return false;
    }
    if (!backup_meta_write(out, meta)) {
        unlink(out); return false;
    }
    return true;
}

/* ------------------------------------------- GBA cartridge save transfers -- */

static void backup_meta_for_cart(BackupMeta *m, const GbaLinkCart *cart, size_t size) {
    memset(m, 0, sizeof(*m));
    m->source = BACKUP_SOURCE_GBA_LINK;
    m->kind = GEN3_KIND_GBA;
    m->size = size;
    m->card_slot = -1;
    snprintf(m->target, sizeof(m->target), "GBA cartridge %.12s [%.4s%.2s]",
             cart->title, cart->gamecode, cart->maker);
    memcpy(m->gamecode, cart->header + 0xAC, 4);
    memcpy(m->company, cart->header + 0xB0, 2);
}

static void cart_label(const GbaLinkCart *cart, char *out, size_t out_size) {
    snprintf(out, out_size, "GBA cartridge %.12s [%.4s%.2s]",
             cart->title, cart->gamecode, cart->maker);
}

/* A cartridge image is only worth keeping if it parses as a Gen III save. */
static bool cart_bytes_are_gen3(const uint8_t *data, size_t size) {
    Gen3AnySave v; memset(&v, 0, sizeof(v));
    bool ok = gen3_any_open(&v, data, size) && v.kind == GEN3_KIND_GBA;
    if (v.kind != GEN3_KIND_UNKNOWN) gen3_any_close(&v);
    return ok;
}

static uint8_t *cart_read_bytes(const GbaLinkCart *cart, char *detail, size_t detail_size) {
    const size_t n = cart->save_size;
    uint8_t *buf = (uint8_t *)memalign(32, (n + 31u) & ~31u);
    if (!buf) {
        snprintf(detail, detail_size, "Out of memory reading the cartridge save.");
        return NULL;
    }
    if (!gbalink_read_save(cart, buf, n, detail, detail_size, gbalink_status, NULL)) {
        free(buf);
        return NULL;
    }
    return buf;
}

/*
 * Extract: connect, read the cartridge save, write a verified SD backup, and
 * only then open it for editing.  The backup is mandatory - it is the thing
 * that makes writing back to the cartridge later a recoverable operation.
 */
static bool read_gba_cart_save(void) {
    char detail[384] = {0};

    if (!fat_available || root_count <= 0) {
        snprintf(error_message, sizeof(error_message),
                 "Reading a GBA cartridge needs writable SD storage,\n"
                 "because PKHeX-GC always backs the save up before opening it.");
        return false;
    }
    if (!connect_gba_cart(detail, sizeof(detail))) {
        snprintf(error_message, sizeof(error_message), "%.480s", detail);
        return false;
    }

    uint8_t *buf = cart_read_bytes(&linked_cart, detail, sizeof(detail));
    if (!buf) {
        snprintf(error_message, sizeof(error_message), "%.480s", detail);
        return false;
    }
    const size_t n = linked_cart.save_size;

    if (!cart_bytes_are_gen3(buf, n)) {
        free(buf);
        snprintf(error_message, sizeof(error_message),
                 "%.12s [%.4s%.2s] was read successfully (%lu KiB, %s),\n"
                 "but its save is not a supported Generation III save.",
                 linked_cart.title, linked_cart.gamecode, linked_cart.maker,
                 (unsigned long)(n / 1024u), gbalink_save_type_name(linked_cart.save_type));
        return false;
    }

    BackupMeta meta; backup_meta_for_cart(&meta, &linked_cart, n);
    char stem[96]; snprintf(stem, sizeof(stem), "%.12s-%.4s%.2s",
                            linked_cart.title, linked_cart.gamecode, linked_cart.maker);
    char backup[PATH_LEN];
    if (!create_verified_backup(stem, current_root, buf, n, GEN3_KIND_GBA,
                                &meta, backup, sizeof(backup))) {
        free(buf);
        snprintf(error_message, sizeof(error_message),
                 "The cartridge was read, but no verified backup could be written to\n"
                 "pkhex-gc-backups. Refusing to open it for editing without one.");
        return false;
    }

    char label[PATH_LEN]; cart_label(&linked_cart, label, sizeof(label));
    if (!parse_loaded_bytes(buf, n, label)) {
        free(save_bytes); save_bytes = NULL; save_bytes_size = 0;
        return false;
    }
    loaded_from_card = false;
    loaded_from_cart = true;

    char msg[160]; const char *fn = strrchr(backup, '/'); fn = fn ? fn + 1 : backup;
    snprintf(msg, sizeof(msg), "Cartridge read; backup: %.108s", fn);
    set_status(msg);
    return true;
}

/* Replace: serialize, back the cartridge's current save up, program it, and
 * require a byte-for-byte read-back before reporting success. */
static bool save_gba_cart(void) {
    if (!linked_cart_valid) {
        set_status("Reconnect the cartridge from Hardware before writing.");
        return false;
    }
    if (!fat_available || root_count <= 0) {
        set_status("Cartridge writes require SD for a verified backup first.");
        return false;
    }
    const size_t n = save_bytes_size;
    if (n != linked_cart.save_size) {
        set_status("Cartridge save size changed since it was read; re-read it first.");
        return false;
    }

    uint8_t *out = (uint8_t *)memalign(32, (n + 31u) & ~31u);
    if (!out) { set_status("Save failed: out of memory."); return false; }
    if (!gen3_any_export(&parsed_save, out, n) || !cart_bytes_are_gen3(out, n)) {
        free(out);
        set_status("Serialization validation failed; cartridge untouched.");
        return false;
    }

    BackupMeta meta; backup_meta_for_cart(&meta, &linked_cart, n);
    char stem[96]; snprintf(stem, sizeof(stem), "%.12s-%.4s%.2s",
                            linked_cart.title, linked_cart.gamecode, linked_cart.maker);
    char backup[PATH_LEN];
    if (!create_verified_backup(stem, current_root, save_bytes, n, GEN3_KIND_GBA,
                                &meta, backup, sizeof(backup))) {
        free(out);
        set_status("Could not create a verified SD backup; cartridge untouched.");
        return false;
    }

    char detail[384] = {0};
    if (!gbalink_write_save(&linked_cart, out, n, detail, sizeof(detail),
                            gbalink_status, NULL)) {
        free(out);
        snprintf(error_message, sizeof(error_message), "%.480s", detail);
        mode = UI_ERROR;
        return false;
    }

    uint8_t *check = cart_read_bytes(&linked_cart, detail, sizeof(detail));
    bool verified = check && memcmp(check, out, n) == 0;
    if (!verified) {
        /* Put the cartridge back the way we found it before reporting. */
        char rollback[384] = {0};
        const bool restored =
            gbalink_write_save(&linked_cart, save_bytes, n, rollback, sizeof(rollback),
                               gbalink_status, NULL);
        free(check); free(out);
        snprintf(error_message, sizeof(error_message),
                 restored
                     ? "The cartridge did not read back byte-for-byte, so the original\n"
                       "save was rewritten. The verified backup is still in Backups."
                     : "WRITE FAILED AND ROLLBACK FAILED.\n"
                       "Do not play the cartridge. Restore the backup from Backups.\n%.300s",
                 rollback);
        mode = UI_ERROR;
        return false;
    }
    free(out);

    /* Show what is actually on the cartridge now, not what we intended. */
    char label[PATH_LEN]; cart_label(&linked_cart, label, sizeof(label));
    if (!parse_loaded_bytes(check, n, label)) {
        free(save_bytes); save_bytes = NULL; save_bytes_size = 0;
        set_status("Cartridge written and verified, but the UI reload failed.");
        return false;
    }
    loaded_from_card = false;
    loaded_from_cart = true;
    save_dirty = false;

    char msg[160]; const char *fn = strrchr(backup, '/'); fn = fn ? fn + 1 : backup;
    snprintf(msg, sizeof(msg), "Cartridge written + read back; backup: %.88s", fn);
    set_status(msg);
    return true;
}

static bool card_dir_matches(const card_dir *d, const CardBrowserEntry *src) {
    return d && src && d->filelen == src->dir.filelen &&
           strncmp(d->filename, src->dir.filename, CARD_FILENAMELEN) == 0 &&
           memcmp(d->gamecode, src->dir.gamecode, 4) == 0 &&
           memcmp(d->company, src->dir.company, 2) == 0;
}

static bool card_find_fresh(int slot, const CardBrowserEntry *src, card_dir *out) {
    if (!src || !out) return false;
    card_dir d; s32 r = CARD_FindFirst(slot, &d, true);
    while (r >= CARD_ERROR_READY) {
        if (card_dir_matches(&d, src)) { *out = d; return true; }
        r = CARD_FindNext(&d);
    }
    return false;
}

static void set_card_io_detail(const char *stage, s32 error, size_t offset) {
    if (error < CARD_ERROR_READY)
        snprintf(card_io_detail, sizeof(card_io_detail), "%s at 0x%lX (CARD error %ld)",
                 stage, (unsigned long)offset, (long)error);
    else
        snprintf(card_io_detail, sizeof(card_io_detail), "%s at 0x%lX",
                 stage, (unsigned long)offset);
}

static bool card_read_payload(const CardBrowserEntry *src, uint8_t *data, size_t size) {
    if(!src||!data||size!=src->dir.filelen) { set_card_io_detail("invalid read request",CARD_ERROR_READY,0); return false; }
    DCInvalidateRange(data, (u32)size);
    s32 m=CARD_Mount(src->slot,card_workarea(src->slot),NULL);
    if(m<CARD_ERROR_READY) { set_card_io_detail("mount for read",m,0); return false; }
    card_dir d; bool ok=false;
    if (card_find_fresh(src->slot, src, &d)) {
        card_file f; s32 r=CARD_OpenEntry(src->slot,&d,&f);
        if(r>=CARD_ERROR_READY){
            u32 sector=0; r=CARD_GetSectorSize(src->slot,&sector);
            if(r>=CARD_ERROR_READY && sector && size%sector==0){
                ok=true;
                for(size_t off=0;off<size;off+=sector){
                    r=CARD_Read(&f,data+off,sector,(u32)off);
                    if(r<CARD_ERROR_READY){set_card_io_detail("read sector",r,off);ok=false;break;}
                }
            } else set_card_io_detail("read sector-size check",r,0);
            CARD_Close(&f);
        } else set_card_io_detail("open for read",r,0);
    } else set_card_io_detail("find entry for read",CARD_ERROR_READY,0);
    CARD_Unmount(src->slot); return ok;
}

static bool card_write_payload(const CardBrowserEntry *src, const uint8_t *data, size_t size) {
    if(!src||!data||size!=src->dir.filelen) { set_card_io_detail("invalid write request",CARD_ERROR_READY,0); return false; }
    /* CARD uses hardware I/O from the supplied aligned buffer; make the intended
       bytes visible even if the caller just mutated them in CPU cache. */
    DCFlushRange((void *)data, (u32)size);
    s32 m=CARD_Mount(src->slot,card_workarea(src->slot),NULL);
    if(m<CARD_ERROR_READY) { set_card_io_detail("mount for write",m,0); return false; }
    card_dir d; bool ok=false;
    if (card_find_fresh(src->slot, src, &d)) {
        card_file f; s32 r=CARD_OpenEntry(src->slot,&d,&f);
        if(r>=CARD_ERROR_READY){
            u32 sector=0; r=CARD_GetSectorSize(src->slot,&sector);
            if(r>=CARD_ERROR_READY && sector && size%sector==0){
                /* libogc completes one physical card sector per CARD_Write call. */
                ok=true;
                for(size_t off=0;off<size;off+=sector){
                    r=CARD_Write(&f,(void*)(data+off),sector,(u32)off);
                    if(r<CARD_ERROR_READY){set_card_io_detail("write sector",r,off);ok=false;break;}
                }
            } else set_card_io_detail("write sector-size check",r,0);
            CARD_Close(&f);
        } else set_card_io_detail("open for write",r,0);
    } else set_card_io_detail("find entry for write",CARD_ERROR_READY,0);
    CARD_Unmount(src->slot); return ok;
}

static bool card_readback_matches(const CardBrowserEntry *src, const uint8_t *expected,
                                  Gen3SaveKind kind, size_t size) {
    uint8_t *buf=(uint8_t*)memalign(32,(size+31u)&~31u); if(!buf){set_card_io_detail("readback allocation",CARD_ERROR_READY,0);return false;}
    bool ok=card_read_payload(src,buf,size);
    if(ok && memcmp(buf,expected,size)!=0){
        size_t off=0;while(off<size&&buf[off]==expected[off])++off;
        set_card_io_detail("readback mismatch",CARD_ERROR_READY,off);ok=false;
    }
    if(ok){ Gen3AnySave v; memset(&v,0,sizeof(v)); ok=gen3_any_open(&v,buf,size)&&v.kind==kind;
        if(!ok)set_card_io_detail("readback parse failure",CARD_ERROR_READY,0);
        if(ok && gen3_any_has_integrity_check(kind) && !v.integrity_ok){set_card_io_detail("readback integrity failure",CARD_ERROR_READY,0);ok=false;}
        if(v.kind!=GEN3_KIND_UNKNOWN) gen3_any_close(&v);
    }
    free(buf); return ok;
}

static void backup_meta_for_card(BackupMeta *m, const CardBrowserEntry *src,
                                 Gen3SaveKind kind, size_t size) {
    memset(m,0,sizeof(*m)); m->source=BACKUP_SOURCE_CARD; m->kind=kind; m->size=size;
    m->card_slot=src->slot; snprintf(m->card_filename,sizeof(m->card_filename),"%.32s",src->dir.filename);
    memcpy(m->gamecode,src->dir.gamecode,4); memcpy(m->company,src->dir.company,2);
}

static bool save_physical_card(void) {
    if(!loaded_from_card || !gen3_any_can_edit(&parsed_save)) return false;
    if(!fat_available || root_count<=0){ set_status("Memory-card save requires SD for a verified backup first."); return false; }
    size_t n=save_bytes_size;
    uint8_t *out=(uint8_t*)memalign(32,(n+31u)&~31u); if(!out){set_status("Save failed: out of memory.");return false;}
    if(!gen3_any_export(&parsed_save,out,n)){free(out);set_status("Serialization/integrity validation failed; card untouched.");return false;}

    /* Export must represent a self-consistent save before we ever touch CARD. */
    Gen3AnySave verify; memset(&verify,0,sizeof(verify)); bool serial_ok=gen3_any_open(&verify,out,n)&&verify.kind==parsed_save.kind;
    if(serial_ok && gen3_any_has_integrity_check(parsed_save.kind)) serial_ok=verify.integrity_ok;
    if(verify.kind!=GEN3_KIND_UNKNOWN) gen3_any_close(&verify);
    if(!serial_ok){free(out);set_status("Serialized card image failed independent verification; card untouched.");return false;}

    CardBrowserEntry src=loaded_card_source; Gen3SaveKind kind=parsed_save.kind;
    BackupMeta meta; backup_meta_for_card(&meta,&src,kind,n);
    char backup[PATH_LEN];
    if(!create_verified_backup(src.dir.filename,current_root,save_bytes,n,kind,&meta,backup,sizeof(backup))){
        free(out);set_status("Could not create verified restorable SD backup; card untouched.");return false;
    }

    /* Write, then require BYTE-FOR-BYTE hardware readback plus parser integrity. */
    card_io_detail[0]='\0';
    if(!card_write_payload(&src,out,n) || !card_readback_matches(&src,out,kind,n)){
        char failure[sizeof(card_io_detail)];snprintf(failure,sizeof(failure),"%s",card_io_detail[0]?card_io_detail:"unknown CARD failure");
        bool restored=card_write_payload(&src,save_bytes,n) && card_readback_matches(&src,save_bytes,kind,n);
        free(out);
        char msg[160];snprintf(msg,sizeof(msg),restored?"%s; original restored. Backup kept.":"%s; rollback failed. Use verified backup.",failure);
        set_status(msg); return false;
    }
    free(out);

    /* Re-scan to obtain a fresh card_dir/fileno and load bytes actually on card. */
    scan_memory_cards();
    CardBrowserEntry fresh=src; bool found=false;
    for(int i=0;i<card_entry_count;i++) if(card_dir_matches(&card_entries[i].dir,&src)){fresh=card_entries[i];found=true;break;}
    if(!found || !load_card_entry(&fresh)){
        set_status("Card write verified byte-for-byte, but UI reload failed; restart before more edits."); return false;
    }
    save_dirty=false;
    char msg[160]; const char *fn=strrchr(backup,'/'); fn=fn?fn+1:backup;
    snprintf(msg,sizeof(msg),"Card saved + read back; backup: %.104s",fn); set_status(msg); return true;
}

static bool save_in_place(void) {
    if (!gen3_any_can_edit(&parsed_save)) { set_status("Saving is not enabled for this format yet."); return false; }
    if (loaded_from_backup) { set_status("Backup is open for inspection. Restore it before editing the live save."); return false; }
    if (loaded_from_cart) return save_gba_cart();
    if (loaded_from_card) return save_physical_card();
    if (!loaded_path[0]) { set_status("No writable source path."); return false; }

    size_t out_size=save_bytes_size;
    uint8_t *outbuf=(uint8_t*)memalign(32,(out_size+31u)&~31u); if(!outbuf){set_status("Save failed: out of memory.");return false;}
    if(!gen3_any_export(&parsed_save,outbuf,out_size)){free(outbuf);set_status("Serialization validation failed; original untouched.");return false;}
    Gen3AnySave independent; memset(&independent,0,sizeof(independent)); bool reopened=gen3_any_open(&independent,outbuf,out_size)&&independent.kind==parsed_save.kind;
    if(reopened && gen3_any_has_integrity_check(parsed_save.kind)) reopened=independent.integrity_ok;
    if(independent.kind!=GEN3_KIND_UNKNOWN) gen3_any_close(&independent);
    if(!reopened){free(outbuf);set_status("Independent reopen failed; original untouched.");return false;}

    char original[PATH_LEN]; snprintf(original,sizeof(original),"%s",loaded_path);
    BackupMeta meta; memset(&meta,0,sizeof(meta)); meta.source=BACKUP_SOURCE_FILE; meta.kind=parsed_save.kind; meta.size=out_size;
    snprintf(meta.target,sizeof(meta.target),"%s",original);
    char backup[PATH_LEN];
    if(!create_verified_backup(original,root_index_for_path(original),save_bytes,out_size,parsed_save.kind,&meta,backup,sizeof(backup))){
        free(outbuf);set_status("Could not create verified backup in pkhex-gc-backups; original untouched.");return false;
    }

    bool ok=write_whole_file(original,outbuf,out_size) && validate_disk_file(original,out_size,parsed_save.kind);
    if(ok){ uint8_t *check=NULL; size_t check_n=0; ok=read_whole_file_exact(original,&check,&check_n)&&check_n==out_size&&memcmp(check,outbuf,out_size)==0; free(check); }
    if(!ok){
        bool restored=write_whole_file(original,save_bytes,out_size)&&validate_disk_file(original,out_size,parsed_save.kind);
        free(outbuf);set_status(restored?"Save failed verification; original restored. Backup kept.":"SAVE FAILED: backup is verified; use Backups -> Restore.");return false;
    }
    free(outbuf);
    if(!load_save_file(original)){set_status("Saved and verified, but UI reload failed; restart before more edits.");return false;}
    save_dirty=false;
    char msg[160]; const char *fn=strrchr(backup,'/'); fn=fn?fn+1:backup;
    snprintf(msg,sizeof(msg),"Saved in place; backup: %.112s",fn); set_status(msg); return true;
}

static bool backup_bytes_valid(const uint8_t *data, size_t size, Gen3SaveKind kind) {
    Gen3AnySave v; memset(&v,0,sizeof(v));
    bool ok=gen3_any_open(&v,data,size)&&v.kind==kind;
    if(ok&&gen3_any_has_integrity_check(kind)) ok=v.integrity_ok;
    if(v.kind!=GEN3_KIND_UNKNOWN) gen3_any_close(&v);
    return ok;
}

static bool restore_file_backup(const char *backup, const BackupMeta *meta,
                                const uint8_t *bak, size_t bak_size) {
    (void)backup;
    if(!meta->target[0]){set_status("Backup metadata has no original file path.");return false;}
    uint8_t *current=NULL; size_t current_n=0; bool had_current=read_whole_file_exact(meta->target,&current,&current_n);
    if(had_current){
        if(current_n!=meta->size || !backup_bytes_valid(current,current_n,meta->kind)){
            free(current);set_status("Restore refused: current target is a different/invalid save.");return false;
        }
        BackupMeta safety=*meta; char safety_path[PATH_LEN];
        if(!create_verified_backup(meta->target,root_index_for_path(meta->target),current,current_n,meta->kind,&safety,safety_path,sizeof(safety_path))){
            free(current);set_status("Restore refused: could not back up current target first.");return false;
        }
    }
    bool ok=write_whole_file(meta->target,bak,bak_size)&&validate_disk_file(meta->target,bak_size,meta->kind);
    if(ok){uint8_t*check=NULL;size_t cn=0;ok=read_whole_file_exact(meta->target,&check,&cn)&&cn==bak_size&&!memcmp(check,bak,bak_size);free(check);}
    if(!ok){
        bool rolled=!had_current || (write_whole_file(meta->target,current,current_n)&&validate_disk_file(meta->target,current_n,meta->kind));
        free(current);set_status(rolled?"Restore failed verification; previous target restored.":"RESTORE FAILED: safety backup exists in Backups.");return false;
    }
    free(current);
    if(load_save_file(meta->target)){loaded_from_backup=false;mode=UI_SUMMARY;set_status("Backup restored to original file and verified.");}
    else set_status("Backup restored and verified; UI reload failed.");
    return true;
}

static bool card_entry_from_meta(const BackupMeta *meta, CardBrowserEntry *src) {
    if(!meta||!src||meta->source!=BACKUP_SOURCE_CARD||meta->card_slot<CARD_SLOTA||meta->card_slot>CARD_SLOTB)return false;
    memset(src,0,sizeof(*src));src->slot=meta->card_slot;src->dir.filelen=(u32)meta->size;
    memcpy(src->dir.filename,meta->card_filename,CARD_FILENAMELEN);
    memcpy(src->dir.gamecode,meta->gamecode,4);memcpy(src->dir.company,meta->company,2);return true;
}

static bool restore_card_backup(const char *backup, const BackupMeta *meta,
                                const uint8_t *bak, size_t bak_size) {
    (void)backup;
    CardBrowserEntry src;if(!card_entry_from_meta(meta,&src)){set_status("Backup has invalid memory-card metadata.");return false;}
    uint8_t*current=(uint8_t*)memalign(32,(bak_size+31u)&~31u);if(!current){set_status("Out of memory restoring card.");return false;}
    card_io_detail[0]='\0';
    if(!card_read_payload(&src,current,bak_size)||!backup_bytes_valid(current,bak_size,meta->kind)){
        char msg[160];snprintf(msg,sizeof(msg),"Restore refused: %.132s",card_io_detail[0]?card_io_detail:"target card save is invalid");
        free(current);set_status(msg);return false;
    }
    BackupMeta safety=*meta;char safety_path[PATH_LEN];
    if(!create_verified_backup(meta->card_filename,current_root,current,bak_size,meta->kind,&safety,safety_path,sizeof(safety_path))){
        free(current);set_status("Restore refused: could not back up current card save first.");return false;
    }
    card_io_detail[0]='\0';
    if(!card_write_payload(&src,bak,bak_size)||!card_readback_matches(&src,bak,meta->kind,bak_size)){
        char failure[sizeof(card_io_detail)];snprintf(failure,sizeof(failure),"%s",card_io_detail[0]?card_io_detail:"unknown CARD failure");
        bool rolled=card_write_payload(&src,current,bak_size)&&card_readback_matches(&src,current,meta->kind,bak_size);
        char msg[160];snprintf(msg,sizeof(msg),rolled?"%s; previous card save restored.":"%s; rollback failed. Safety backup is on SD.",failure);
        free(current);set_status(msg);return false;
    }
    free(current);scan_memory_cards();
    for(int i=0;i<card_entry_count;i++)if(card_dir_matches(&card_entries[i].dir,&src)){
        if(load_card_entry(&card_entries[i])){mode=UI_SUMMARY;set_status("Memory-card backup restored and read back byte-for-byte.");return true;}
    }
    set_status("Card backup restored + verified; UI reload failed.");return true;
}

/*
 * Restore a cartridge backup.  The recorded game code has to match the
 * cartridge that is actually connected, and whatever is currently on it is
 * itself backed up first whenever that is a valid save.
 */
static bool restore_cart_backup(const BackupMeta *meta, const uint8_t *bak, size_t bak_size) {
    char detail[384] = {0};
    if (!connect_gba_cart(detail, sizeof(detail))) {
        snprintf(error_message, sizeof(error_message), "%.480s", detail);
        mode = UI_ERROR;
        return false;
    }
    if (memcmp(linked_cart.header + 0xAC, meta->gamecode, 4) != 0 ||
        memcmp(linked_cart.header + 0xB0, meta->company, 2) != 0) {
        set_status("Restore refused: a different cartridge is connected.");
        return false;
    }
    if (linked_cart.save_size != bak_size || !linked_cart.writable) {
        set_status("Restore refused: cartridge save size does not match the backup.");
        return false;
    }

    uint8_t *current = cart_read_bytes(&linked_cart, detail, sizeof(detail));
    if (!current) {
        snprintf(error_message, sizeof(error_message), "%.480s", detail);
        mode = UI_ERROR;
        return false;
    }
    bool safety_skipped = false;
    if (cart_bytes_are_gen3(current, bak_size)) {
        BackupMeta safety = *meta;
        char stem[96]; snprintf(stem, sizeof(stem), "%.12s-%.4s%.2s",
                                linked_cart.title, linked_cart.gamecode, linked_cart.maker);
        char safety_path[PATH_LEN];
        if (!create_verified_backup(stem, current_root, current, bak_size, GEN3_KIND_GBA,
                                    &safety, safety_path, sizeof(safety_path))) {
            free(current);
            set_status("Restore refused: could not back up the cartridge's current save.");
            return false;
        }
    } else {
        /* Nothing recoverable is provably there, so proceed and say so. */
        safety_skipped = true;
    }

    if (!gbalink_write_save(&linked_cart, bak, bak_size, detail, sizeof(detail),
                            gbalink_status, NULL)) {
        free(current);
        snprintf(error_message, sizeof(error_message), "%.480s", detail);
        mode = UI_ERROR;
        return false;
    }

    uint8_t *check = cart_read_bytes(&linked_cart, detail, sizeof(detail));
    const bool verified = check && memcmp(check, bak, bak_size) == 0;
    if (!verified) {
        char rollback[384] = {0};
        const bool rolled = gbalink_write_save(&linked_cart, current, bak_size, rollback,
                                               sizeof(rollback), gbalink_status, NULL);
        free(check); free(current);
        snprintf(error_message, sizeof(error_message),
                 rolled ? "Restore failed verification; the previous save was rewritten."
                        : "RESTORE FAILED AND ROLLBACK FAILED.\n"
                          "Do not play the cartridge; the backup files are intact.");
        mode = UI_ERROR;
        return false;
    }
    free(current);

    char label[PATH_LEN]; cart_label(&linked_cart, label, sizeof(label));
    if (parse_loaded_bytes(check, bak_size, label)) {
        loaded_from_card = false;
        loaded_from_cart = true;
        loaded_from_backup = false;
        mode = UI_SUMMARY;
        set_status(safety_skipped
                       ? "Backup restored and verified (cartridge held no valid save first)."
                       : "Backup restored to the cartridge and verified.");
    } else {
        free(save_bytes); save_bytes = NULL; save_bytes_size = 0;
        set_status("Backup restored and verified; UI reload failed.");
    }
    return true;
}

static bool restore_backup_path(const char *backup) {
    BackupMeta meta;if(!backup_meta_read(backup,&meta)){set_status("Legacy backup: open works, but restore needs recovery metadata.");return false;}
    uint8_t*bak=NULL;size_t n=0;if(!read_whole_file_exact(backup,&bak,&n)||n!=meta.size||!backup_bytes_valid(bak,n,meta.kind)){
        free(bak);set_status("Backup validation failed; nothing restored.");return false;
    }
    bool ok=false;
    if(meta.source==BACKUP_SOURCE_FILE)ok=restore_file_backup(backup,&meta,bak,n);
    else if(meta.source==BACKUP_SOURCE_CARD)ok=restore_card_backup(backup,&meta,bak,n);
    else if(meta.source==BACKUP_SOURCE_GBA_LINK)ok=restore_cart_backup(&meta,bak,n);
    else set_status("This backup source type is not restorable in this build.");
    free(bak);return ok;
}

static bool open_backup_path(const char *path) {
    if(!load_save_file(path))return false;
    loaded_from_backup=true;mode=UI_SUMMARY;set_status("Backup opened read-only. B returns to Backups; restore from backup list with X.");return true;
}

static bool take_screenshot(void) {
    if (!fat_available || root_count <= 0) { set_status("Screenshot needs a mounted FAT SD device."); return false; }
    for (int pass=0; pass<root_count; ++pass) {
        int ri = (current_root + pass) % root_count;
        char dir[PATH_LEN]; snprintf(dir, sizeof(dir), "%spkhex-gc-screenshots", roots[ri]);
        if (mkdir(dir, 0777) != 0 && errno != EEXIST) continue;
        for (unsigned i=1; i<=9999; ++i) {
            char path[PATH_LEN]; snprintf(path, sizeof(path), "%s/PKHeX-GC-%04u.png", dir, i);
            if (access(path, F_OK) == 0) continue;
            if (gui_screenshot_png(&gui, path)) {
                char msg[160]; snprintf(msg, sizeof(msg), "Screenshot saved: PKHeX-GC-%04u.png", i); set_status(msg);
                return true;
            }
            break;
        }
    }
    set_status("Screenshot failed on all mounted FAT devices.");
    return false;
}

static bool begin_box_edit(void) {
    if (!live_edit_allowed()) { set_status(loaded_from_backup ? "Backup opened read-only; restore it before editing." : "Pokemon editing is not enabled for this format yet."); return false; }
    Gen3Pokemon pkm={0};
    if (!gen3_any_box_pokemon(&parsed_save, box_index, box_selected, &pkm) || !pkm.present) return false;
    edit_pkm = pkm; edit_source = PKM_EDIT_BOX; edit_source_box = box_index; edit_source_slot = box_selected;
    pkm_edit_field = 0; pkm_edit_page = 0; mode = UI_PKM_EDIT; return true;
}

static bool begin_party_edit(void) {
    if (!live_edit_allowed()) { set_status(loaded_from_backup ? "Backup opened read-only; restore it before editing." : "Pokemon editing is not enabled for this format yet."); return false; }
    Gen3Pokemon pkm={0};
    if (!gen3_any_party_pokemon(&parsed_save, party_selected, &pkm) || !pkm.present) return false;
    edit_pkm = pkm; edit_source = PKM_EDIT_PARTY; edit_source_slot = party_selected; edit_source_box = 0;
    pkm_edit_field = 0; pkm_edit_page = 0; mode = UI_PKM_EDIT; return true;
}

/* Daycare records are ordinary stored PK3s, so the same editor works on them;
 * only where the result is written back differs. */
static bool begin_daycare_edit(unsigned slot, const Gen3Pokemon *pkm) {
    if (!live_edit_allowed()) { set_status(loaded_from_backup ? "Backup opened read-only; restore it before editing." : "Pokemon editing is not enabled for this format yet."); return false; }
    if (!pkm || !pkm->present) return false;
    edit_pkm = *pkm; edit_source = PKM_EDIT_DAYCARE; edit_source_slot = slot; edit_source_box = 0;
    pkm_edit_field = 0; pkm_edit_page = 0; mode = UI_PKM_EDIT; return true;
}

/*
 * A light blue ground, and every other colour re-picked around it rather than
 * inverted. Text, the "off" state and the three status colours all need
 * different values on a light background: a green that reads on near-black is
 * washed out on near-white, and a fill colour used as text disappears
 * entirely.
 *
 * The one colour that keeps its old job under a new name is the badge text.
 * It was C_BADGE_TEXT because the badges were bright on a dark screen; the badge
 * fills are darker now, so their text is near-white.
 */
static const GXColor C_BG         = {216, 232, 246, 255};  /* the light blue */
static const GXColor C_HEADER     = {182, 209, 234, 255};
static const GXColor C_PANEL      = {243, 249, 254, 255};
static const GXColor C_PANEL2     = {206, 223, 238, 255};  /* an inset fill, never text */
static const GXColor C_BORDER     = {138, 172, 205, 255};
static const GXColor C_TEXT       = { 20,  34,  52, 255};
static const GXColor C_MUTED      = { 84, 108, 134, 255};
static const GXColor C_FAINT      = {140, 162, 184, 255};  /* an off or empty state */
static const GXColor C_ACCENT     = { 13,  92, 178, 255};
static const GXColor C_SELECT     = {158, 199, 236, 255};
static const GXColor C_GREEN      = { 19, 115,  68, 255};
static const GXColor C_YELLOW     = {150,  98,   8, 255};
static const GXColor C_RED        = {176,  36,  46, 255};
static const GXColor C_BADGE_TEXT = {248, 252, 255, 255};
static const GXColor C_BADGE_OFF  = {104, 126, 148, 255};  /* an unset badge, still dark enough for its text */

static void fit_text(char *dst, size_t dst_size, const char *src, size_t chars) {
    if (!dst_size) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n <= chars) snprintf(dst, dst_size, "%s", src);
    else if (chars >= 3) snprintf(dst, dst_size, "%.*s...", (int)(chars - 3), src);
    else snprintf(dst, dst_size, "%.*s", (int)chars, src);
}

/* Truncate to a pixel width rather than a character count, so a proportional
 * string lands inside the column it was given instead of near it. */
static void fit_text_px(char *dst, size_t dst_size, const char *src, float max_px, float scale) {
    if (!dst_size) return;
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
    if (gui_text_width(dst, scale) <= max_px) return;
    for (size_t n = strlen(dst); n > 0; --n) {
        dst[n - 1] = '\0';
        if (n >= 4) { dst[n - 4] = '.'; dst[n - 3] = '.'; dst[n - 2] = '.'; }
        if (gui_text_width(dst, scale) <= max_px) return;
        if (n >= 4) dst[n - 4] = '\0';
    }
}

/* Lay `text` into up to `max_lines` lines no wider than `max_px`, breaking on
 * spaces. Returns how many lines were used. */
static unsigned wrap_text(const char *text, float max_px, float scale,
                          char lines[][96], unsigned max_lines) {
    unsigned used = 0;
    const char *p = text ? text : "";
    while (*p && used < max_lines) {
        size_t take = 0, last_space = 0;
        char probe[96];
        for (size_t n = 1; n < sizeof(probe) && p[n - 1]; ++n) {
            memcpy(probe, p, n); probe[n] = '\0';
            if (gui_text_width(probe, scale) > max_px) break;
            take = n;
            if (p[n - 1] == ' ') last_space = n;
        }
        if (!take) break;
        if (p[take] && last_space && used + 1u < max_lines) take = last_space;
        if (take >= sizeof(lines[0])) take = sizeof(lines[0]) - 1u;
        memcpy(lines[used], p, take);
        lines[used][take] = '\0';
        /* Trim the break space so the next line does not start with one. */
        while (take && lines[used][take - 1] == ' ') lines[used][--take] = '\0';
        ++used;
        p += take;
        while (*p == ' ') ++p;
    }
    return used;
}

/*
 * Writing the save is the one thing here that changes a file on the card, so
 * it is never one press. Every path that used to call save_in_place() asks
 * first; the screen it was asked from is where B goes back to.
 */
static UiMode confirm_return;

static void request_save(void) {
    if (!live_edit_allowed()) { set_status("This save is not open for editing."); return; }
    confirm_return = mode;
    mode = UI_CONFIRM_SAVE;
}

static bool live_edit_allowed(void) {
    return !loaded_from_backup && gen3_any_can_edit(&parsed_save);
}

static void draw_header(const char *section) {
    gui_rect(0, 0, GUI_W, GUI_H, C_BG);
    gui_rect(0, 0, GUI_W, 58, C_HEADER);
    gui_text(20, 8, 1.75f, C_TEXT, APP_NAME);
    bool edit_screen = mode == UI_SUMMARY || mode == UI_BOXES || mode == UI_TRAINER_EDIT ||
                       mode == UI_INVENTORY_EDIT || mode == UI_PKM_EDIT;
    if (loaded_from_backup && edit_screen)
        gui_badge(462, 18, "BACKUP", C_YELLOW, C_BADGE_TEXT);
    else if (loaded_from_cart && edit_screen)
        gui_badge(462, 18, save_dirty ? "UNSAVED" : "GBA CART",
                  save_dirty ? C_YELLOW : C_ACCENT, C_BADGE_TEXT);
    else if (live_edit_allowed() && edit_screen)
        gui_badge(462, 18, save_dirty ? "UNSAVED" : "EDITABLE", save_dirty ? C_YELLOW : C_GREEN, C_BADGE_TEXT);
    else
        gui_badge(462, 18, "READ ONLY", C_GREEN, C_BADGE_TEXT);
    gui_textf(575, 18, 0.8f, C_MUTED, "v%s", APP_VERSION);
    if (section) gui_text(22, 66, 1.0f, C_MUTED, section);
}

static void draw_footer(const char *controls) {
    gui_panel(18, 438, 604, 28, C_PANEL, C_BORDER);
    if (status_frames && status_message[0]) {
        char msg[84]; fit_text(msg, sizeof(msg), status_message, 76);
        gui_text(30, 441, 0.78f, C_YELLOW, msg);
    } else {
        float scale=0.78f;
        float w=gui_controls_width(controls,scale);
        if(w>580.0f) scale*=580.0f/w;
        gui_controls_text(30,441,scale,C_TEXT,controls);
    }
}

static void show_browser(void) {
    draw_header("FILES / SD STORAGE");
    gui_panel(18, 88, 398, 334, C_PANEL, C_BORDER);
    gui_panel(428, 88, 194, 334, C_PANEL, C_BORDER);

    char path_short[56]; fit_text(path_short, sizeof(path_short), current_path, 50);
    gui_text(30, 100, 0.9f, C_MUTED, path_short);
    gui_rect(28, 119, 378, 1, C_BORDER);

    if (!fat_available || root_count == 0) {
        gui_text(34, 148, 1.1f, C_TEXT, "No FAT SD device mounted.");
        gui_controls_text(34, 174, 0.72f, C_MUTED, "Use [Y] to scan physical hardware.");
    } else if (entry_count == 0) {
        gui_text(34, 148, 1.0f, C_MUTED, "No folders or supported saves here.");
    } else {
        int first = selected - VISIBLE_ROWS / 2;
        if (first < 0) first = 0;
        if (first + VISIBLE_ROWS > entry_count) first = entry_count - VISIBLE_ROWS;
        if (first < 0) first = 0;
        int last = first + VISIBLE_ROWS; if (last > entry_count) last = entry_count;
        const float row_h = 24.0f;
        float y = 132.0f;
        for (int i = first; i < last; ++i, y += row_h) {
            const BrowserEntry *e = &entries[i];
            if (i == selected) gui_rect(28, y - 1, 378, row_h - 1, C_SELECT);
            char name[39]; fit_text(name, sizeof(name), e->name, 35);
            gui_text(36, y, 0.68f, i == selected ? C_TEXT : C_MUTED, name);
            if (e->is_dir) gui_text(350, y, 0.66f, C_ACCENT, "DIR");
        }
    }

    gui_text(442, 104, 1.05f, C_TEXT, "OPEN SAVE");
    gui_rect(440, 126, 168, 1, C_BORDER);
    if (entry_count > 0 && selected >= 0 && selected < entry_count) {
        const BrowserEntry *e = &entries[selected];
        char name[24]; fit_text(name, sizeof(name), e->name, 20);
        gui_text(442, 143, 1.0f, C_TEXT, name);
        if (e->is_dir) {
            gui_badge(442, 171, "FOLDER", C_ACCENT, C_BADGE_TEXT);
            gui_controls_text(442, 205, 0.68f, C_MUTED, "[A] opens this directory.");
        } else {
            const char *ext = extension(e->name);
            gui_textf(442, 171, 0.9f, C_MUTED, "%ld bytes", (long)e->size);
            if (strcasecmp(ext, "fla") == 0) {
                gui_badge(442, 196, "GBI .FLA", C_YELLOW, C_BADGE_TEXT);
                gui_text(442, 228, 0.78f, C_MUTED, "GBI Flash save dump");
                gui_text(442, 244, 0.78f, C_MUTED, "read directly as GBA save.");
            } else if (strcasecmp(ext, "gci") == 0) {
                gui_badge(442, 196, "GAMECUBE GCI", C_ACCENT, C_BADGE_TEXT);
            } else {
                gui_badge(442, 196, "GEN III SAVE", C_GREEN, C_BADGE_TEXT);
            }
        }
    }
    gui_text(442, 330, 0.75f, C_MUTED, "Supported:");
    gui_text(442, 350, 0.75f, C_TEXT, "R/S/E/FR/LG");
    gui_text(442, 366, 0.75f, C_TEXT, "Colosseum / XD / Box");
    gui_text(442, 382, 0.75f, C_TEXT, ".sav .fla .gci .bin");
    draw_footer("[STICK] File  [DPAD] Page  [B] Up  [A] Open  [Y] Hardware  [X] Device  [Z] Backups  [START] Exit");
}

/*
 * The hardware screen lists memory-card saves and then the one thing that is
 * not a file: the cartridge in a linked Game Boy Advance. That used to sit on
 * a shoulder trigger. Shoulder triggers are analog, they stick, and their
 * meaning changed from screen to screen, so it is a row in the same list now
 * and A opens whichever one is selected.
 */
#define CARD_ACTION_ROWS 1u

static int card_row_count(void) {
    return card_entry_count + (int)CARD_ACTION_ROWS;
}

static const char *card_action_name(int row) {
    (void)row;
    return "Game Boy Advance cartridge (link cable)";
}

static void show_card_browser(void) {
    draw_header("PHYSICAL HARDWARE");
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);
    gui_text(30, 99, 0.86f, C_ACCENT, "SOURCES");
    gui_rect(28, 121, 584, 1, C_BORDER);

    const int visible = 6;
    const int rows = card_row_count();
    if (card_scan_pending) {
        gui_text(36, 142, 0.78f, C_MUTED, "Reading memory cards...");
    } else {
        int first = card_selected - visible / 2; if (first < 0) first = 0;
        if (first + visible > rows) first = rows - visible;
        if (first < 0) first = 0;
        int last = first + visible; if (last > rows) last = rows;
        const float row_h = 24.0f; float y = 136.0f;
        for (int i = first; i < last; ++i, y += row_h) {
            const bool sel = i == card_selected;
            if (sel) gui_rect(28, y - 1, 584, row_h - 2, C_SELECT);
            if (i < card_entry_count) {
                CardBrowserEntry *e = &card_entries[i];
                char fn[34]; fit_text(fn, sizeof(fn), e->dir.filename, 29);
                gui_textf(38, y, 0.64f, C_TEXT, "Slot %c", e->slot == CARD_SLOTA ? 'A' : 'B');
                gui_text(112, y, 0.64f, sel ? C_TEXT : C_MUTED, fn);
                gui_textf(493, y, 0.58f, C_MUTED, "%lu B", (unsigned long)e->dir.filelen);
            } else {
                gui_text(38, y, 0.64f, C_ACCENT, "GBA");
                gui_text(112, y, 0.64f, sel ? C_TEXT : C_MUTED, card_action_name(i));
            }
        }
        if (card_entry_count == 0)
            gui_text(38, 136.0f + visible * 24.0f, 0.58f, C_MUTED,
                     "No supported GameCube Gen III saves on either card.");
    }

    gui_rect(28, 292, 584, 1, C_BORDER);
    if (linked_cart_valid) {
        gui_textf(36, 304, 0.62f, C_GREEN, "%.12s [%.4s%.2s]  %s",
                  linked_cart.title, linked_cart.gamecode, linked_cart.maker,
                  gbalink_save_type_name(linked_cart.save_type));
        gui_textf(36, 326, 0.60f, C_MUTED, "Port %d   ROM %lu MiB   Save %lu KiB",
                  linked_cart.chan + 1,
                  (unsigned long)(linked_cart.rom_size / (1024u * 1024u)),
                  (unsigned long)(linked_cart.save_size / 1024u));
    } else {
        gui_text(36, 304, 0.62f, C_MUTED, "DOL-011 link cable to any controller port.");
        gui_text(36, 326, 0.60f, C_MUTED, "Switch the GBA on with NO cartridge, then insert it.");
    }
    gui_text(36, 374, 0.58f, C_GREEN, "Every read is backed up to SD before it can be edited.");
    draw_footer("[STICK] Source  [DPAD] Jump  [B] Files  [A] Read  [Y] Rescan  [Z] Backups");
}

static void show_backup_browser(void) {
    draw_header("BACKUPS / RECOVERY");
    gui_panel(18,88,430,334,C_PANEL,C_BORDER); gui_panel(460,88,162,334,C_PANEL,C_BORDER);
    char rootlabel[54]; snprintf(rootlabel,sizeof(rootlabel),"%spkhex-gc-backups/",root_count?roots[backup_root_index]:"");
    gui_text(30,101,0.78f,C_MUTED,rootlabel); gui_rect(28,122,410,1,C_BORDER);
    if(!backup_entry_count) gui_text(34,151,0.90f,C_MUTED,"No PKHeX-GC backups on this device.");
    else {
        int first=backup_selected-5;if(first<0)first=0;if(first+11>backup_entry_count)first=backup_entry_count-11;if(first<0)first=0;
        int last=first+11;if(last>backup_entry_count)last=backup_entry_count;
        const float row_h=25.0f;float y=137.0f;
        for(int i=first;i<last;i++,y+=row_h){BackupEntry*e=&backup_entries[i];
            if(i==backup_selected)gui_rect(28,y-1,410,row_h-2,C_SELECT);
            char nm[48];fit_text(nm,sizeof(nm),e->name,44);gui_text(36,y,0.64f,i==backup_selected?C_TEXT:C_MUTED,nm);
            if(e->has_meta)gui_text(408,y,0.60f,C_GREEN,"R");
        }
    }
    gui_text(472,103,0.92f,C_ACCENT,"SELECTED");gui_rect(470,125,140,1,C_BORDER);
    if(backup_entry_count){BackupEntry*e=&backup_entries[backup_selected];
        if(backup_meta_cache_index!=backup_selected){
            backup_meta_cache_valid=backup_meta_read(e->path,&backup_meta_cache);
            backup_meta_cache_index=backup_selected;
        }
        const BackupMeta m=backup_meta_cache; const bool hm=backup_meta_cache_valid;
        char nm[22];fit_text(nm,sizeof(nm),e->name,19);gui_text(472,143,0.72f,C_TEXT,nm);gui_textf(472,168,0.68f,C_MUTED,"%ld bytes",(long)e->size);
        if(hm){const char*st=m.source==BACKUP_SOURCE_FILE?"FILE":m.source==BACKUP_SOURCE_CARD?"MEM CARD":m.source==BACKUP_SOURCE_GBA_LINK?"GBA LINK":"?";gui_badge(472,194,st,C_GREEN,C_BADGE_TEXT);
            if(m.source==BACKUP_SOURCE_FILE){char t[22];fit_text(t,sizeof(t),m.target,19);gui_text(472,226,0.62f,C_MUTED,"Restores to:");gui_text(472,245,0.62f,C_TEXT,t);}
            else if(m.source==BACKUP_SOURCE_GBA_LINK){char t[22];fit_text(t,sizeof(t),m.target,19);gui_text(472,226,0.62f,C_MUTED,"Restores to:");gui_text(472,245,0.62f,C_TEXT,t);}
            else if(m.source==BACKUP_SOURCE_CARD){gui_textf(472,226,0.64f,C_MUTED,"Slot %c",m.card_slot==CARD_SLOTA?'A':'B');char t[22];fit_text(t,sizeof(t),m.card_filename,19);gui_text(472,245,0.62f,C_TEXT,t);}
            gui_controls_text(472,286,0.52f,C_GREEN,"[X] Restore");
        } else {gui_badge(472,194,"LEGACY",C_YELLOW,C_BADGE_TEXT);gui_text(472,226,0.60f,C_MUTED,"Can open; no restore");gui_text(472,244,0.60f,C_MUTED,"metadata available.");}
    }
    draw_footer("[STICK] Backup  [DPAD] Device  [B] Files  [A] Open  [Y] Rescan  [X] Restore  [START] Exit");
}

static void format_playtime(char *out, size_t out_size, uint64_t sec) {
    unsigned long hours = (unsigned long)(sec / 3600u);
    unsigned min = (unsigned)((sec / 60u) % 60u);
    unsigned s = (unsigned)(sec % 60u);
    snprintf(out, out_size, "%lu:%02u:%02u", hours, min, s);
}

static void show_summary(void) {
    draw_header(gen3_any_game_name(&parsed_save));
    gui_panel(18, 88, 242, 134, C_PANEL, C_BORDER);
    gui_panel(272, 88, 350, 134, C_PANEL, C_BORDER);
    gui_panel(18, 234, 604, 188, C_PANEL, C_BORDER);

    gui_text(30, 100, 1.0f, C_ACCENT, "TRAINER");
    if (parsed_save.has_trainer) {
        char play[32]; format_playtime(play, sizeof(play), parsed_save.played_seconds);
        gui_text(30, 127, 1.25f, C_TEXT, parsed_save.trainer_name[0] ? parsed_save.trainer_name : "<unnamed>");
        gui_textf(30, 154, 0.9f, C_MUTED, "TID %05u   SID %05u", parsed_save.tid, parsed_save.sid);
        gui_textf(30, 175, 0.9f, C_MUTED, "%s    Play %s", parsed_save.trainer_gender == 1 ? "Female" : "Male", play);
    } else {
        gui_text(30, 137, 0.9f, C_MUTED, "Pokemon Box storage save");
        gui_text(30, 158, 0.9f, C_MUTED, "No trainer block in this title.");
    }

    gui_text(286, 100, 1.0f, C_ACCENT, "SAVE STATUS");
    char src[45]; fit_text(src, sizeof(src), loaded_path, 40);
    gui_text(286, 127, 0.8f, C_MUTED, src);
    if (parsed_save.kind == GEN3_KIND_GBA) {
        gui_textf(286, 151, 0.9f, C_TEXT, "Active copy: %c", parsed_save.gba.active_slot == 0 ? 'A' : 'B');
        gui_badge(286, 176, parsed_save.gba.slots[parsed_save.gba.active_slot].checksums_ok ? "CHECKSUMS OK" : "CHECKSUM WARNING",
                  parsed_save.gba.slots[parsed_save.gba.active_slot].checksums_ok ? C_GREEN : C_YELLOW, C_BADGE_TEXT);
    } else {
        gui_textf(286, 151, 0.9f, C_TEXT, "Active slot %d   Counter %lu", parsed_save.active_slot + 1, (unsigned long)parsed_save.save_counter);
        gui_badge(286, 176, parsed_save.integrity_ok ? "INTEGRITY OK" : "INTEGRITY WARNING",
                  parsed_save.integrity_ok ? C_GREEN : C_YELLOW, C_BADGE_TEXT);
    }

    gui_textf(30, 247, 1.0f, C_ACCENT, "PARTY  %u / 6", gen3_any_party_count(&parsed_save));
    unsigned count = gen3_any_party_count(&parsed_save);
    if (!count) gui_text(36, 285, 0.95f, C_MUTED, "No party storage exposed by this save type.");
    for (unsigned i = 0; i < count && i < 6; ++i) {
        Gen3Pokemon p = {0};
        if (!gen3_any_party_pokemon(&parsed_save, i, &p) || !p.present) continue;
        float x = 30.0f + (i % 3) * 194.0f;
        float y = 278.0f + (i / 3) * 63.0f;
        bool sel = i == party_selected;
        GXColor border = !p.checksum_ok ? C_RED : (p.is_shadow ? C_YELLOW : (sel ? C_ACCENT : C_BORDER));
        gui_panel(x, y, 182, 52, sel ? C_SELECT : C_PANEL2, border);
        gui_textf(x + 7, y + 6, 0.72f, C_MUTED, "%u", i + 1);
        gui_pokemon_sprite(x + 23, y + 9, 34, 34, gen3_species_national(p.species_internal));
        char party_sp[16]; fit_text(party_sp, sizeof(party_sp), gen3_species_name(p.species_internal), 13);
        char party_nick[14]; fit_text(party_nick, sizeof(party_nick), p.nickname, 11);
        gui_text(x + 62, y + 7, 0.78f, C_TEXT, party_sp);
        gui_text(x + 62, y + 27, 0.70f, C_MUTED, party_nick);
        if (p.level) gui_textf(x + 142, y + 27, 0.68f, C_MUTED, "L%u", p.level);
        if (p.is_shadow) gui_text(x + 137, y + 7, 0.55f, C_YELLOW, "SHD");
    }
    if (loaded_from_cart)
        draw_footer("[STICK] Party  [DPAD] Jump  [B] Back  [A] Edit  [Y] Boxes  [X] Tools  [Z] Backups");
    else if (loaded_from_backup)
        draw_footer("[B] Backups  [Y] Boxes  [X] Tools  [Z] Backup List  [CSTICK] Screenshot");
    else if (live_edit_allowed())
        draw_footer("[STICK] Party  [DPAD] Jump  [B] Back  [A] Edit  [Y] Boxes  [X] Tools  [Z] Backups");
    else
        draw_footer("[B] Back  [Y] Boxes  [X] Tools  [Z] Backups  [CSTICK] Screenshot");
}

/*
 * The games draw each box's wallpaper from artwork stored in the cartridge
 * ROM, which PKHeX-GC has no access to. Rather than leave every box looking
 * identical, each wallpaper gets its own palette and a simple motif drawn from
 * rectangles, so boxes are told apart at a glance and match the name the game
 * gives them.
 */
typedef struct BoxWallpaperTheme {
    GXColor back;   /* panel fill                    */
    GXColor accent; /* motif colour                  */
    GXColor border; /* panel edge                    */
    unsigned motif; /* 0 bands, 1 dots, 2 chequer, 3 plain */
} BoxWallpaperTheme;

static BoxWallpaperTheme box_wallpaper_theme(uint8_t wallpaper) {
    static const struct { uint8_t r, g, b, ar, ag, ab; unsigned motif; } t[GEN3_WALLPAPER_COUNT] = {
        { 26, 54, 34,  44, 92, 54, 1 },  /* Forest     */
        { 38, 42, 58,  72, 80, 108, 2 }, /* City       */
        { 66, 56, 32, 104, 88, 48, 0 },  /* Desert     */
        { 54, 56, 30,  88, 92, 46, 0 },  /* Savanna    */
        { 52, 44, 40,  86, 74, 66, 2 },  /* Crag       */
        { 66, 32, 28, 116, 52, 40, 1 },  /* Volcano    */
        { 48, 60, 72,  86, 104, 122, 1 },/* Snow       */
        { 34, 30, 40,  60, 54, 70, 2 },  /* Cave       */
        { 62, 58, 36, 100, 94, 58, 0 },  /* Beach      */
        { 22, 46, 60,  38, 78, 100, 0 }, /* Seafloor   */
        { 26, 46, 62,  44, 78, 104, 0 }, /* River      */
        { 34, 52, 74,  58, 86, 120, 1 }, /* Sky        */
        { 58, 36, 56,  98, 60, 94, 1 },  /* Polka-dot  */
        { 60, 34, 40, 100, 56, 66, 2 },  /* Pokecenter */
        { 40, 44, 48,  70, 76, 82, 2 },  /* Machine    */
        { 40, 44, 52,  64, 70, 82, 3 },  /* Simple     */
    };
    BoxWallpaperTheme out;
    if (wallpaper >= GEN3_WALLPAPER_COUNT) {
        out.back = C_PANEL; out.accent = C_PANEL2; out.border = C_BORDER; out.motif = 3;
        return out;
    }
    out.back   = (GXColor){ t[wallpaper].r,  t[wallpaper].g,  t[wallpaper].b,  255 };
    out.accent = (GXColor){ t[wallpaper].ar, t[wallpaper].ag, t[wallpaper].ab, 255 };
    out.border = (GXColor){ (u8)(t[wallpaper].ar + 30), (u8)(t[wallpaper].ag + 30),
                            (u8)(t[wallpaper].ab + 30), 255 };
    out.motif = t[wallpaper].motif;
    return out;
}

static void draw_box_wallpaper(float x, float y, float w, float h, uint8_t wallpaper) {
    const BoxWallpaperTheme th = box_wallpaper_theme(wallpaper);
    gui_panel(x, y, w, h, th.back, th.border);
    switch (th.motif) {
        case 0: /* horizontal bands */
            for (float by = y + 14.0f; by < y + h - 6.0f; by += 34.0f)
                gui_rect(x + 6.0f, by, w - 12.0f, 3.0f, th.accent);
            break;
        case 1: /* scattered dots */
            for (unsigned row = 0; row < 9; ++row)
                for (unsigned col = 0; col < 12; ++col) {
                    const float dx = x + 12.0f + col * 36.0f + (row & 1u ? 18.0f : 0.0f);
                    const float dy = y + 16.0f + row * 36.0f;
                    if (dx < x + w - 8.0f && dy < y + h - 8.0f)
                        gui_rect(dx, dy, 5.0f, 5.0f, th.accent);
                }
            break;
        case 2: /* chequer */
            for (unsigned row = 0; row < 9; ++row)
                for (unsigned col = 0; col < 12; ++col) {
                    if (((row + col) & 1u) == 0) continue;
                    const float dx = x + 6.0f + col * 36.0f;
                    const float dy = y + 8.0f + row * 36.0f;
                    if (dx < x + w - 10.0f && dy < y + h - 10.0f)
                        gui_rect(dx, dy, 30.0f, 30.0f, th.accent);
                }
            break;
        default:
            break;
    }
}

static void show_boxes(void) {
    unsigned boxes = gen3_any_box_count(&parsed_save);
    char box_name[32]; gen3_any_box_name(&parsed_save, box_index, box_name, sizeof(box_name));
    char title[96]; snprintf(title, sizeof(title), "STORAGE - %s", gen3_any_game_name(&parsed_save));
    draw_header(title);

    const uint8_t wallpaper = gen3_any_box_wallpaper(&parsed_save, box_index);
    draw_box_wallpaper(18, 88, 438, 334, wallpaper);
    gui_panel(468, 88, 154, 334, C_PANEL, C_BORDER);
    gui_textf(30, 101, 1.0f, C_TEXT, "Box %u / %u", box_index + 1, boxes);
    char bn[28]; fit_text(bn, sizeof(bn), box_name, 23);
    gui_text(150, 101, 1.0f, C_ACCENT, bn);
    if (wallpaper < GEN3_WALLPAPER_COUNT)
        gui_text(30, 122, 0.58f, C_MUTED, gen3_wallpaper_name(wallpaper));
    if (box_index == gen3_any_current_box(&parsed_save)) gui_badge(345, 96, "CURRENT", C_GREEN, C_BADGE_TEXT);
    if (box_holding) {
        char held[20];
        fit_text(held, sizeof(held), gen3_species_name(box_held.species_internal), 12);
        gui_badge(240, 96, held, C_YELLOW, C_BADGE_TEXT);
    }

    const float gx0 = 30.0f, gy0 = 136.0f, cw = 68.0f, ch = 50.0f;
    for (unsigned slot = 0; slot < 30; ++slot) {
        unsigned col = slot % 6, row = slot / 6;
        float x = gx0 + col * cw, y = gy0 + row * ch;
        Gen3Pokemon p = {0}; gen3_any_box_pokemon(&parsed_save, box_index, slot, &p);
        bool sel = slot == box_selected;
        GXColor fill = sel ? C_SELECT : (GXColor){ 0, 0, 0, 90 };
        GXColor border = p.present && !p.checksum_ok ? C_RED : (p.present && p.is_shadow ? C_YELLOW : (sel ? C_ACCENT : C_BORDER));
        gui_panel(x, y, cw - 5, ch - 5, fill, border);
        gui_textf(x + 3, y + 3, 0.55f, C_MUTED, "%02u", slot + 1);
        if (p.present) {
            gui_pokemon_sprite(x + 16, y + 8, 32, 32, gen3_species_national(p.species_internal));
            if (p.is_shadow) gui_text(x + 50, y + 3, 0.50f, C_YELLOW, "S");
            else if (!p.checksum_ok) gui_text(x + 50, y + 3, 0.50f, C_RED, "!");
        } else gui_text(x + 27, y + 19, 0.65f, C_MUTED, "-");
    }

    Gen3Pokemon p = {0}; gen3_any_box_pokemon(&parsed_save, box_index, box_selected, &p);
    gui_text(480, 101, 1.0f, C_ACCENT, "SELECTED");
    gui_textf(480, 128, 0.8f, C_MUTED, "Slot %u", box_selected + 1);
    if (!p.present) {
        gui_text(480, 162, 1.0f, C_MUTED, "Empty");
    } else {
        char sp[18]; fit_text(sp, sizeof(sp), gen3_species_name(p.species_internal), 14);
        char nick[18]; fit_text(nick, sizeof(nick), p.nickname, 14);
        char ot[18]; fit_text(ot, sizeof(ot), p.ot_name, 14);
        gui_pokemon_sprite(505, 143, 80, 80, gen3_species_national(p.species_internal));
        gui_text(480, 231, 0.88f, C_TEXT, sp);
        gui_text(480, 251, 0.72f, C_MUTED, nick);
        if (p.level) gui_textf(480, 271, 0.72f, C_TEXT, "Level %u", p.level);
        gui_textf(480, 291, 0.67f, C_MUTED, "OT %s", ot);
        gui_textf(480, 311, 0.58f, C_MUTED, "PID %08lX", (unsigned long)p.pid);
        gui_textf(480, 329, 0.60f, C_MUTED, "%s", gen3_item_name_for(parsed_save.kind, p.held_item));
        float badge_y = 350.0f;
        if (p.is_egg) { gui_badge(480, badge_y, "EGG", C_ACCENT, C_BADGE_TEXT); badge_y += 22.0f; }
        if (p.is_shadow) { gui_badge(480, badge_y, "SHADOW", C_YELLOW, C_BADGE_TEXT); badge_y += 22.0f; }
        if (!p.checksum_ok) gui_badge(480, badge_y, "BAD", C_RED, C_TEXT);
    }
    if (loaded_from_backup)
        draw_footer("[STICK] Slot  [DPAD] Box  [B] Summary  [Z] Backups  [CSTICK] Shot");
    else if (live_edit_allowed())
        draw_footer(box_holding ? "[STICK] Slot  [DPAD] Box  [B] Put back  [Y] Save  [X] Place"
                                : "[STICK] Slot  [DPAD] Box  [B] Summary  [A] Edit  [Y] Save  [X] Pick up");
    else
        draw_footer("[STICK] Slot  [DPAD] Box  [B] Trainer  [Z] Backups  [CSTICK] Shot");
}


/*
 * Move a wrapping selection. The analog stick steps one row and the D-pad ten,
 * which is the whole point of telling the two apart: a 386-entry Pokedex and a
 * 3344-entry flag list are not lists you cross one row at a time, and L and R
 * are already spoken for on most of these screens.
 *
 * Wrapping is kept, so a coarse step off either end lands back in range rather
 * than sticking. A count of zero returns zero rather than dividing by it.
 */
static unsigned nav_index(u32 down, unsigned index, unsigned count) {
    if (!count) return 0u;
    /* Ten rows through a list of ten is a press that does nothing, and through
     * a list of five it is two laps. A list shorter than the jump gets a
     * single step instead, so the D-pad never feels dead. */
    const bool coarse = (down & UI_COARSE) && count > (unsigned)UI_COARSE_STEP;
    const long long step = coarse ? UI_COARSE_STEP : 1;
    long long i = (long long)index;
    if (down & PAD_BUTTON_UP)   i -= step;
    if (down & PAD_BUTTON_DOWN) i += step;
    const long long n = (long long)count;
    i %= n;
    if (i < 0) i += n;
    return (unsigned)i;
}

/* How much one left/right press should change a value by. */
static long long nav_step(u32 down, long long fine, long long coarse) {
    return (down & UI_COARSE) ? coarse : fine;
}

/*
 * The D-pad's sideways step: the previous or next page, box, pocket, category
 * or facility - whatever the screen is a list of.
 *
 * That job used to sit on L and R, and on Y and X in the Pokemon editor. It
 * does not need to sit on either: the D-pad is a direction control the stick no
 * longer shadows, and left and right on it are exactly the shape of the job. So
 * the triggers now carry nothing at all - the end of a long argument with two
 * analog buttons that stick, hover at the digital threshold and used to mean
 * four different things - and Y and X are free for actions.
 */
static bool nav_page_prev(u32 down) {
    return (down & UI_COARSE) && (down & PAD_BUTTON_LEFT);
}

static bool nav_page_next(u32 down) {
    return (down & UI_COARSE) && (down & PAD_BUTTON_RIGHT);
}

/*
 * Left or right on the stick alone: -1, 0 or +1. A screen that both edits a
 * value and holds a container reads this for the value, so that stepping to the
 * next page does not also nudge the field under the cursor.
 */
static int nav_fine(u32 down) {
    if (down & UI_COARSE) return 0;
    if (down & PAD_BUTTON_LEFT) return -1;
    if (down & PAD_BUTTON_RIGHT) return 1;
    return 0;
}

static long long clamp_ll(long long v, long long lo, long long hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/*
 * The trainer editor's rows differ by save kind: the trainer name, the coin
 * case and the box names only exist on the cartridge saves, so the rows are
 * listed per kind rather than indexed by position.
 */
typedef enum TrainerField {
    TF_NAME = 0, TF_TID, TF_SID, TF_GENDER, TF_HOURS, TF_MIN, TF_SEC,
    TF_MONEY, TF_COINS, TF_RIVAL, TF_BADGES,
} TrainerField;

static const TrainerField trainer_fields_gba[] = {
    TF_NAME, TF_TID, TF_SID, TF_GENDER, TF_HOURS, TF_MIN, TF_SEC,
    TF_MONEY, TF_COINS, TF_BADGES,
};
/* FireRed and LeafGreen are the only games that store a rival name. */
static const TrainerField trainer_fields_frlg[] = {
    TF_NAME, TF_RIVAL, TF_TID, TF_SID, TF_GENDER, TF_HOURS, TF_MIN, TF_SEC,
    TF_MONEY, TF_COINS, TF_BADGES,
};
static const TrainerField trainer_fields_gc[] = {
    TF_NAME, TF_TID, TF_SID, TF_GENDER, TF_HOURS, TF_MIN, TF_SEC, TF_MONEY,
};

static bool trainer_has_rival(void) {
    return parsed_save.kind == GEN3_KIND_GBA && gen3_has_rival_name(&parsed_save.gba);
}

static unsigned trainer_field_count(void) {
    if (parsed_save.kind != GEN3_KIND_GBA)
        return (unsigned)(sizeof trainer_fields_gc / sizeof trainer_fields_gc[0]);
    return trainer_has_rival()
               ? (unsigned)(sizeof trainer_fields_frlg / sizeof trainer_fields_frlg[0])
               : (unsigned)(sizeof trainer_fields_gba / sizeof trainer_fields_gba[0]);
}

static TrainerField trainer_field_at(unsigned row) {
    const unsigned count = trainer_field_count();
    if (row >= count) row = 0;
    if (parsed_save.kind != GEN3_KIND_GBA) return trainer_fields_gc[row];
    return trainer_has_rival() ? trainer_fields_frlg[row] : trainer_fields_gba[row];
}

static const char *trainer_field_label(TrainerField f) {
    switch (f) {
        case TF_BADGES:  return "Gym badges";
        case TF_NAME:    return "Trainer name";
        case TF_TID:     return "Trainer ID";
        case TF_SID:     return "Secret ID";
        case TF_GENDER:  return "Gender";
        case TF_HOURS:   return "Hours";
        case TF_MIN:     return "Minutes";
        case TF_SEC:     return "Seconds";
        case TF_MONEY:   return "Money";
        case TF_COINS:   return "Coins";
        default:         return "Rival name";
    }
}

/* ------------------------------------------------------------ keyboard ---
 *
 * Generation III names are not ASCII, and several of the characters the games
 * allow have no ASCII equivalent at all. So the keyboard edits the stored
 * bytes directly: every key carries the Gen III byte it produces, and the
 * label is only what this build happens to draw for it. A name loaded for
 * editing keeps its original bytes until a key actually replaces them.
 */
typedef enum KeyboardTarget {
    KB_NICKNAME = 0,
    KB_OT_NAME,
    KB_TRAINER_NAME,
    KB_BOX_NAME,
    KB_RIVAL_NAME,
    /* GameCube saves store names as UTF-16, so the keyboard works on plain
     * characters for these rather than on Generation III bytes. */
    KB_GC_TRAINER_NAME,
    KB_GC_BOX_NAME,
} KeyboardTarget;

#define KB_ROWS 6u
#define KB_COLS 10u
#define KB_MAX_LEN 10u

/* Upper and lower layers differ only in the letter block; the punctuation and
 * digits are shared, so the second layer is generated rather than duplicated. */
static const char *const kb_rows[KB_ROWS] = {
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ",
    "0123456789",
    " .,'-!?:/&",
    "+()%$",
};

/* Which box a KB_BOX_NAME edit belongs to, set before the keyboard opens. */
static unsigned kb_box_index;

static KeyboardTarget kb_target;
static UiMode kb_return_to;
static uint8_t kb_buf[KB_MAX_LEN];
static unsigned kb_len, kb_max_len;
static unsigned kb_row, kb_col;
static bool kb_lower;
static char kb_title[48];
/* True while editing a GameCube name: kb_buf then holds ASCII characters
 * rather than Generation III bytes. */
static bool kb_ascii;

static unsigned kb_row_len(unsigned row) {
    return row < KB_ROWS ? (unsigned)strlen(kb_rows[row]) : 0u;
}

static char kb_key_char(unsigned row, unsigned col) {
    if (row >= KB_ROWS || col >= kb_row_len(row)) return '\0';
    const char c = kb_rows[row][col];
    if (kb_lower && c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

/* Build the editable buffer from stored bytes, dropping the terminator run. */
static void kb_load(const uint8_t *raw, size_t raw_len, unsigned max_len,
                    KeyboardTarget target, UiMode return_to, const char *title)
{
    kb_max_len = max_len > KB_MAX_LEN ? KB_MAX_LEN : max_len;
    kb_len = 0;
    kb_ascii = false;
    for (size_t i = 0; i < raw_len && kb_len < kb_max_len; ++i) {
        if (raw[i] == GEN3_TEXT_TERMINATOR) break;
        kb_buf[kb_len++] = raw[i];
    }
    kb_target = target;
    kb_return_to = return_to;
    kb_row = kb_col = 0;
    kb_lower = false;
    snprintf(kb_title, sizeof(kb_title), "%s", title);
    mode = UI_KEYBOARD;
}

/* Loads an already-decoded name for a format that stores plain characters. */
static void kb_load_ascii(const char *text, unsigned max_len, KeyboardTarget target,
                          UiMode return_to, const char *title)
{
    kb_max_len = max_len > KB_MAX_LEN ? KB_MAX_LEN : max_len;
    kb_len = 0;
    for (const char *c = text; c && *c && kb_len < kb_max_len; ++c) {
        /* Only what this keyboard can retype; anything else would be lost on
         * the first keypress anyway, so it is dropped up front rather than
         * appearing to be editable. */
        if (gen3_encode_char(*c) == GEN3_TEXT_TERMINATOR) continue;
        kb_buf[kb_len++] = (uint8_t)*c;
    }
    kb_ascii = true;
    kb_target = target;
    kb_return_to = return_to;
    kb_row = kb_col = 0;
    kb_lower = false;
    snprintf(kb_title, sizeof(kb_title), "%s", title);
    mode = UI_KEYBOARD;
}

/* Render the buffer through the same decoder the rest of the UI uses, so what
 * the keyboard shows and what the summary shows can never disagree. */
static void kb_preview(char *out, size_t out_size) {
    if (kb_ascii) {
        const size_t n = kb_len < out_size - 1u ? kb_len : out_size - 1u;
        memcpy(out, kb_buf, n);
        out[n] = '\0';
        return;
    }
    uint8_t framed[KB_MAX_LEN + 1u];
    memcpy(framed, kb_buf, kb_len);
    framed[kb_len] = GEN3_TEXT_TERMINATOR;
    gen3_decode_text(framed, kb_len + 1u, false, out, out_size);
    if (kb_len == 0 && out_size > 1) { out[0] = '\0'; }
}

static void kb_commit(void) {
    bool ok = false;
    switch (kb_target) {
        case KB_NICKNAME:
            memset(edit_pkm.nickname_raw, GEN3_TEXT_TERMINATOR, sizeof(edit_pkm.nickname_raw));
            memcpy(edit_pkm.nickname_raw, kb_buf, kb_len);
            gen3_decode_text(edit_pkm.nickname_raw, sizeof(edit_pkm.nickname_raw), false,
                             edit_pkm.nickname, sizeof(edit_pkm.nickname));
            ok = true;
            break;
        case KB_OT_NAME:
            memset(edit_pkm.ot_raw, GEN3_TEXT_TERMINATOR, sizeof(edit_pkm.ot_raw));
            memcpy(edit_pkm.ot_raw, kb_buf, kb_len);
            gen3_decode_text(edit_pkm.ot_raw, sizeof(edit_pkm.ot_raw), false,
                             edit_pkm.ot_name, sizeof(edit_pkm.ot_name));
            ok = true;
            break;
        case KB_TRAINER_NAME:
            ok = gen3_set_trainer_name(&parsed_save.gba, kb_buf, kb_len);
            if (ok) { save_dirty = true; refresh_gba_summary(); }
            break;
        case KB_BOX_NAME:
            ok = gen3_set_box_name(&parsed_save.gba, kb_box_index, kb_buf, kb_len);
            if (ok) save_dirty = true;
            break;
        case KB_RIVAL_NAME:
            ok = gen3_set_rival_name(&parsed_save.gba, kb_buf, kb_len);
            if (ok) save_dirty = true;
            break;
        case KB_GC_TRAINER_NAME: {
            char text[KB_MAX_LEN + 1u];
            memcpy(text, kb_buf, kb_len);
            text[kb_len] = '\0';
            ok = gen3_any_set_trainer_name_ascii(&parsed_save, text);
            if (ok) save_dirty = true;
            break;
        }
        case KB_GC_BOX_NAME: {
            char text[KB_MAX_LEN + 1u];
            memcpy(text, kb_buf, kb_len);
            text[kb_len] = '\0';
            ok = gen3_any_set_box_name_ascii(&parsed_save, kb_box_index, text);
            if (ok) save_dirty = true;
            break;
        }
    }
    /* Nickname and OT go into the working record; they reach the save when the
     * Pokemon editor is applied, like every other field on those pages. */
    set_status(ok ? "Name updated." : "Could not set that name.");
}

static void show_keyboard(void) {
    draw_header(kb_title);
    gui_panel(60, 84, 520, 74, C_PANEL, C_BORDER);
    char preview[32];
    kb_preview(preview, sizeof(preview));
    gui_textf(80, 96, 0.62f, C_MUTED, "%u of %u characters", kb_len, kb_max_len);
    gui_text(80, 116, 1.25f, C_TEXT, preview);
    /* A caret so an empty name still shows where typing lands. */
    gui_rect(80.0f + gui_text_width(preview, 1.25f) + 2.0f, 118, 3, 26, C_ACCENT);

    gui_panel(60, 170, 520, 216, C_PANEL, C_BORDER);
    for (unsigned r = 0; r < KB_ROWS; ++r) {
        const unsigned len = kb_row_len(r);
        for (unsigned c = 0; c < len; ++c) {
            const float x = 76.0f + c * 49.0f;
            const float y = 184.0f + r * 34.0f;
            const bool sel = (r == kb_row && c == kb_col);
            gui_rect(x, y, 42, 28, sel ? C_SELECT : C_PANEL2);
            if (sel) gui_outline(x, y, 42, 28, 2, C_ACCENT);
            const char label[2] = { kb_key_char(r, c), '\0' };
            /* Space has no glyph; name the key instead. */
            if (label[0] == ' ') gui_text(x + 8, y + 5, 0.5f, C_MUTED, "SPC");
            else gui_text(x + 21.0f - gui_text_width(label, 0.9f) * 0.5f, y + 2, 0.9f, C_TEXT, label);
        }
    }
    gui_badge(494, 176, kb_lower ? "abc" : "ABC", C_ACCENT, C_BADGE_TEXT);
    draw_footer("[STICK] Key  [DPAD] Jump  [B] Cancel  [A] Type  [Y] Case  [X] Delete  [Z] Accept");
}

static void handle_keyboard(u32 down) {
    kb_row = nav_index(down, kb_row, KB_ROWS);
    /* Rows are ragged, so keep the column inside whichever row we landed on. */
    unsigned len = kb_row_len(kb_row);
    if (len == 0) len = 1u;
    if (down & PAD_BUTTON_LEFT)  kb_col = kb_col ? kb_col - 1u : len - 1u;
    if (down & PAD_BUTTON_RIGHT) kb_col = (kb_col + 1u) % len;
    if (kb_col >= len) kb_col = len - 1u;

    if (down & PAD_BUTTON_Y) kb_lower = !kb_lower;
    if ((down & PAD_BUTTON_A) && kb_len < kb_max_len) {
        const char ch = kb_key_char(kb_row, kb_col);
        if (kb_ascii) kb_buf[kb_len++] = (uint8_t)ch;
        else {
            const uint8_t enc = gen3_encode_char(ch);
            if (enc != GEN3_TEXT_TERMINATOR) kb_buf[kb_len++] = enc;
        }
    }
    /* Space is the first key of the punctuation row, so it needs no button of
     * its own; X backspaces instead. Accept is Z rather than a shoulder
     * trigger, which is analog and can hold; B backs out like everywhere. */
    if ((down & PAD_BUTTON_X) && kb_len) --kb_len;
    if (down & PAD_TRIGGER_Z) { kb_commit(); mode = kb_return_to; }
    if (down & PAD_BUTTON_B) mode = kb_return_to;
}

/* ------------------------------------------------------------- tools ---
 *
 * The summary screen ran out of buttons long before the editors ran out, so
 * everything that is not the party, the boxes or the Pokedex is listed here.
 */
/*
 * The same browser imports two kinds of file: a boxed Pokemon record, and a
 * Wonder Card. Both are fixed-size blobs picked out of the same folder by
 * their length, so the only differences are what length is wanted and where
 * the bytes end up.
 */
typedef enum RecordFileKind { RECORD_KIND_PKM = 0, RECORD_KIND_WONDER_CARD } RecordFileKind;
static RecordFileKind record_kind;

typedef struct ToolEntry {
    const char *name;
    UiMode target;
    bool gba_only;
    bool needs_edit;
    /* Two entries open the file browser, for two kinds of file. */
    int file_kind;
} ToolEntry;

static const ToolEntry tools[] = {
    { "Trainer",       UI_TRAINER_EDIT,   false, true  , 0 },
    { "Inventory",     UI_INVENTORY_EDIT, false, true  , 0 },
    { "Pokedex",       UI_POKEDEX,        false, false , 0 },
    { "Event flags",   UI_EVENTS,         true,  false , 0 },
    { "Daycare",       UI_DAYCARE,        true,  false , 0 },
    { "Roaming Pokemon", UI_ROAMER,       true,  false , 0 },
    { "Mail box",      UI_MAIL,           true,  false , 0 },
    { "Hall of Fame",  UI_HALL_OF_FAME,   true,  false , 0 },
    { "Game records",  UI_RECORDS,        true,  false , 0 },
    { "Box layout",    UI_BOX_LAYOUT,     false, true  , 0 },
    { "PokeBlocks",    UI_POKEBLOCKS,     true,  false , 0 },
    { "Secret bases",  UI_SECRET_BASES,   true,  false , 0 },
    { "Decorations",   UI_DECORATIONS,    true,  false , 0 },
    { "Emerald extras", UI_EMERALD_EXTRAS, true,  false , 0 },
    { "Strategy Memo", UI_MEMO,           false, false , 0 },
    { "GameCube link", UI_GAMECUBE_LINK,  true,  false , 0 },
    { "Legality check", UI_LEGALITY,      false, false , 0 },
    { "Wonder Card file", UI_RECORD_FILES, true, true  , RECORD_KIND_WONDER_CARD },
    { "Battle Frontier", UI_FRONTIER,     true,  false , 0 },
    { "Game clock",    UI_CLOCK,          true,  false , 0 },
    { "Shadow table",  UI_SHADOWS,        false, false , 0 },
    { "Save check",    UI_SAVE_CHECK,     false, false , 0 },
    { "Misc",          UI_MISC,           true,  false , 0 },
    { "Pokemon files", UI_RECORD_FILES,   false, true  , 0 },
    { "Save changes to file", UI_CONFIRM_SAVE, false, true  , 0 },
};
#define TOOL_COUNT (sizeof tools / sizeof tools[0])

static unsigned tool_selected;
static unsigned daycare_slot, roamer_field, mail_slot, hof_entry, record_selected, layout_box;
static unsigned pokeblock_index, secret_base_index, frontier_facility, frontier_row, clock_field;
static unsigned shadow_index;

static bool tool_available(const ToolEntry *t) {
    if (t->gba_only && parsed_save.kind != GEN3_KIND_GBA) return false;
    if (t->needs_edit && !live_edit_allowed()) return false;
    if (t->target == UI_POKEDEX && !gen3_any_has_pokedex(&parsed_save)) return false;
    if (t->target == UI_HALL_OF_FAME && !gen3_hof_available(&parsed_save.gba)) return false;
    /* Several blocks exist in only some of the cartridge games. */
    if (t->target == UI_POKEBLOCKS && !gen3_has_pokeblocks(&parsed_save.gba)) return false;
    if (t->target == UI_SECRET_BASES && !gen3_has_secret_bases(&parsed_save.gba)) return false;
    if (t->target == UI_DECORATIONS && !gen3_has_decorations(&parsed_save.gba)) return false;
    if (t->target == UI_EMERALD_EXTRAS && !gen3_has_emerald_extras(&parsed_save.gba)) return false;
    if (t->target == UI_MEMO && !gen3_any_has_memo(&parsed_save)) return false;
    /* Ruby and Sapphire have no Wonder Card slot to install one into. */
    if (t->file_kind == RECORD_KIND_WONDER_CARD && !gen3_has_wonder_card(&parsed_save.gba)) return false;
    if (t->target == UI_FRONTIER && !gen3_has_battle_frontier(&parsed_save.gba)) return false;
    if (t->target == UI_CLOCK && !gen3_has_clock(&parsed_save.gba)) return false;
    if (t->target == UI_SHADOWS && !gen3_any_has_shadow_table(&parsed_save)) return false;
    return true;
}

/* One line of context per row, so the list says what is in each editor rather
 * than only what it is called. */
static void tool_detail(const ToolEntry *t, char *out, size_t n) {
    out[0] = '\0';
    if (!tool_available(t)) { snprintf(out, n, "not in this save"); return; }
    if (t->target == UI_DAYCARE) {
        unsigned occupied = 0;
        for (unsigned i = 0; i < GEN3_DAYCARE_SLOTS; ++i) {
            Gen3Pokemon p;
            if (gen3_daycare_pokemon(&parsed_save.gba, i, &p) && p.present) ++occupied;
        }
        snprintf(out, n, "%u of %u   %s", occupied, GEN3_DAYCARE_SLOTS,
                 gen3_daycare_egg_waiting(&parsed_save.gba) ? "egg waiting" : "no egg");
    } else if (t->target == UI_ROAMER) {
        Gen3Roamer r;
        if (gen3_roamer(&parsed_save.gba, &r) && r.active && r.species)
            snprintf(out, n, "%s  Lv %u", gen3_species_name(gen3_species_internal_from_national(r.species)), r.level);
        else snprintf(out, n, "none roaming");
    } else if (t->target == UI_MAIL) {
        unsigned held = 0;
        for (unsigned i = 0; i < GEN3_MAIL_SLOTS; ++i) {
            Gen3Mail m;
            if (gen3_mail(&parsed_save.gba, i, &m) && m.present) ++held;
        }
        snprintf(out, n, "%u of %u sheets", held, GEN3_MAIL_SLOTS);
    } else if (t->target == UI_HALL_OF_FAME) {
        snprintf(out, n, "%u entries", gen3_hof_entry_count(&parsed_save.gba));
    } else if (t->target == UI_BOX_LAYOUT) {
        snprintf(out, n, "%u boxes", gen3_any_box_count(&parsed_save));
    } else if (t->target == UI_POKEBLOCKS) {
        unsigned made = 0;
        for (unsigned i = 0; i < GEN3_POKEBLOCK_COUNT; ++i) {
            Gen3PokeBlock b;
            if (gen3_pokeblock(&parsed_save.gba, i, &b) && b.color) ++made;
        }
        snprintf(out, n, "%u of %u", made, GEN3_POKEBLOCK_COUNT);
    } else if (t->target == UI_SECRET_BASES) {
        unsigned used = 0;
        for (unsigned i = 0; i < GEN3_SECRET_BASE_COUNT; ++i) {
            Gen3SecretBase b;
            if (gen3_secret_base(&parsed_save.gba, i, &b) && b.present) ++used;
        }
        snprintf(out, n, "%u of %u", used, GEN3_SECRET_BASE_COUNT);
    } else if (t->target == UI_FRONTIER) {
        unsigned symbols = 0;
        for (unsigned f = 0; f < GEN3_FACILITY_COUNT; ++f)
            if (gen3_frontier_symbol(&parsed_save.gba, (Gen3Facility)f)) ++symbols;
        snprintf(out, n, "%u symbols   %u BP", symbols, gen3_battle_points(&parsed_save.gba));
    } else if (t->target == UI_RECORD_FILES) {
        snprintf(out, n, "import a .%s into a box", gen3_any_record_extension(&parsed_save));
    } else if (t->target == UI_MISC) {
        Gen3Swarm sw;
        if (gen3_has_swarm(&parsed_save.gba) && gen3_swarm(&parsed_save.gba, &sw) && sw.active)
            snprintf(out, n, "swarm: %s", gen3_species_name(gen3_species_internal_from_national(sw.species)));
        else if (gen3_eberry_is_enigma(&parsed_save.gba))
            snprintf(out, n, "Enigma Berry present");
        else
            snprintf(out, n, "swarm, berry, paintings");
    } else if (t->target == UI_SAVE_CHECK) {
        snprintf(out, n, "checksums and record sanity");
    } else if (t->target == UI_SHADOWS) {
        unsigned caught = 0, pure = 0;
        for (unsigned i = 0; i < gen3_any_shadow_count(&parsed_save); ++i) {
            Gen3ShadowEntry e;
            if (!gen3_any_shadow_entry(&parsed_save, i, &e) || !e.present) continue;
            ++caught;
            if (e.purified) ++pure;
        }
        snprintf(out, n, "%u snagged, %u purified", caught, pure);
    } else if (t->target == UI_CLOCK) {
        Gen3Clock c;
        if (gen3_clock(&parsed_save.gba, true, &c)) snprintf(out, n, "%u days elapsed", c.day);
    } else if (t->target == UI_RECORDS) {
        snprintf(out, n, "%u counters", gen3_record_count(parsed_save.gba.game));
    } else if (t->target == UI_POKEDEX) {
        snprintf(out, n, "%u caught", gen3_any_dex_caught_count(&parsed_save));
    }
}

static void show_tools(void) {
    char title[80];
    snprintf(title, sizeof(title), "TOOLS - %s", gen3_any_game_name(&parsed_save));
    draw_header(title);
    gui_panel(90, 84, 460, 344, C_PANEL, C_BORDER);
    /* The list is longer than the panel once every editor is in it, so it
     * scrolls with the cursor kept near the middle. */
    const unsigned visible = 12u;
    unsigned top = tool_selected > visible / 2u ? tool_selected - visible / 2u : 0u;
    if (TOOL_COUNT > visible && top > TOOL_COUNT - visible) top = (unsigned)TOOL_COUNT - visible;
    for (unsigned row = 0; row < visible && top + row < TOOL_COUNT; ++row) {
        const unsigned i = top + row;
        const float y = 96.0f + row * 25.0f;
        const bool sel = i == tool_selected;
        const bool ok = tool_available(&tools[i]);
        if (sel) gui_rect(102, y - 3, 436, 24, C_SELECT);
        gui_text(120, y, 0.74f, ok ? (sel ? C_TEXT : C_MUTED) : C_FAINT, tools[i].name);
        char detail[48];
        tool_detail(&tools[i], detail, sizeof(detail));
        gui_text(330, y + 2, 0.58f, ok ? C_MUTED : C_FAINT, detail);
    }
    if (TOOL_COUNT > visible)
        gui_textf(120, 410, 0.55f, C_MUTED, "%u of %u", tool_selected + 1u, (unsigned)TOOL_COUNT);
    draw_footer("[STICK] Tool  [DPAD] Jump  [B] Back  [A] Open");
}

static void handle_tools(u32 down) {
    tool_selected = nav_index(down, tool_selected, TOOL_COUNT);
    if (down & PAD_BUTTON_A) {
        const ToolEntry *t = &tools[tool_selected];
        if (!tool_available(t)) { set_status("That editor is not part of this save."); return; }
        /* Each editor keeps its own cursor; reset the ones that index into
         * data that may have changed since the last visit. */
        if (t->target == UI_TRAINER_EDIT) trainer_edit_field = 0;
        if (t->target == UI_INVENTORY_EDIT) { inventory_pocket = GEN3_POCKET_ITEMS; inventory_slot = 0; inventory_field = 0; if (!gen3_any_pocket_capacity(&parsed_save, inventory_pocket)) inventory_step_pocket(1); }
        if (t->target == UI_POKEDEX) { dex_selected = 0; dex_return = UI_TOOLS; }
        if (t->target == UI_EVENTS) { event_selected = 0; event_show_work = false; event_return = UI_TOOLS; }
        if (t->target == UI_DAYCARE) daycare_slot = 0;
        if (t->target == UI_ROAMER) roamer_field = 0;
        if (t->target == UI_MAIL) mail_slot = 0;
        if (t->target == UI_HALL_OF_FAME) hof_entry = 0;
        if (t->target == UI_RECORDS) record_selected = 0;
        if (t->target == UI_BOX_LAYOUT) layout_box = 0;
        if (t->target == UI_POKEBLOCKS) pokeblock_index = 0;
        if (t->target == UI_SECRET_BASES) secret_base_index = 0;
        if (t->target == UI_DECORATIONS) { deco_kind = 0; deco_slot = 0; }
        if (t->target == UI_EMERALD_EXTRAS) extras_row = 0;
        if (t->target == UI_MEMO) memo_index = 0;
        if (t->target == UI_GAMECUBE_LINK) gclink_row = 0;
        if (t->target == UI_LEGALITY) { legality_row = 0; legality_scanned = false; }
        if (t->target == UI_FRONTIER) { frontier_facility = 0; frontier_row = 0; }
        if (t->target == UI_CLOCK) clock_field = 0;
        if (t->target == UI_SHADOWS) shadow_index = 0;
        if (t->target == UI_SAVE_CHECK) { check_ran = false; check_count = 0; check_scroll = 0; }
        if (t->target == UI_RECORD_FILES) {
            record_kind = (RecordFileKind)t->file_kind;
            scan_record_files();
        }
        if (t->target == UI_CONFIRM_SAVE) { request_save(); return; }
        mode = t->target;
    }
    if (down & PAD_BUTTON_B) mode = UI_SUMMARY;
}

/* ------------------------------------------------------------ daycare --- */

static void show_daycare(void) {
    draw_header("DAYCARE");
    gui_panel(18, 84, 604, 250, C_PANEL, C_BORDER);
    for (unsigned slot = 0; slot < GEN3_DAYCARE_SLOTS; ++slot) {
        const float x = 34.0f + slot * 296.0f;
        const bool sel = slot == daycare_slot;
        gui_panel(x, 100, 272, 216, sel ? C_SELECT : C_PANEL2, sel ? C_ACCENT : C_BORDER);
        Gen3Pokemon p;
        const bool have = gen3_daycare_pokemon(&parsed_save.gba, slot, &p) && p.present;
        gui_textf(x + 14, 108, 0.62f, C_MUTED, "SLOT %u", slot + 1u);
        if (!have) {
            gui_text(x + 14, 190, 0.86f, C_MUTED, "Empty");
        } else {
            gui_pokemon_sprite(x + 14, 130, 72, 72, gen3_species_national(p.species_internal));
            gui_text(x + 100, 134, 0.9f, C_TEXT, gen3_species_name(p.species_internal));
            gui_textf(x + 100, 160, 0.7f, C_MUTED, "%s", p.nickname);
            gui_textf(x + 100, 182, 0.7f, C_MUTED, "Lv %u", gen3_effective_level(&p));
            gui_textf(x + 14, 218, 0.66f, C_MUTED, "OT %s", p.ot_name);
            gui_textf(x + 14, 240, 0.66f, C_MUTED, "%s", gen3_nature_name(gen3_nature(&p)));
        }
        /* Banked experience is the daycare's own counter, not the record's. */
        gui_textf(x + 14, 268, 0.62f, C_MUTED, "Banked EXP %lu",
                  (unsigned long)gen3_daycare_exp(&parsed_save.gba, slot));
    }
    gui_panel(18, 344, 604, 62, C_PANEL, C_BORDER);
    const bool egg = gen3_daycare_egg_waiting(&parsed_save.gba);
    gui_text(36, 354, 0.68f, C_MUTED, "Egg waiting");
    gui_badge(160, 356, egg ? "YES" : "NO", egg ? C_GREEN : C_BADGE_OFF, C_BADGE_TEXT);
    gui_textf(300, 354, 0.68f, C_MUTED, "Seed %08lX", (unsigned long)gen3_daycare_seed(&parsed_save.gba));
    gui_textf(300, 378, 0.58f, C_MUTED, "%u-bit seed", gen3_daycare_seed_bits(&parsed_save.gba));
    draw_footer("[STICK] Slot  [DPAD] Jump  [B] Back  [A] Edit Pokemon  [X] Toggle egg");
}

static void handle_daycare(u32 down) {
    if (down & (PAD_BUTTON_LEFT | PAD_BUTTON_UP)) daycare_slot = daycare_slot ? daycare_slot - 1u : GEN3_DAYCARE_SLOTS - 1u;
    if (down & (PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN)) daycare_slot = (daycare_slot + 1u) % GEN3_DAYCARE_SLOTS;
    if (down & PAD_BUTTON_A) {
        Gen3Pokemon p;
        if (gen3_daycare_pokemon(&parsed_save.gba, daycare_slot, &p) && p.present)
            begin_daycare_edit(daycare_slot, &p);
        else
            set_status("That daycare slot is empty.");
    }
    if ((down & PAD_BUTTON_X) && live_edit_allowed()) {
        const bool egg = !gen3_daycare_egg_waiting(&parsed_save.gba);
        if (gen3_set_daycare_egg_waiting(&parsed_save.gba, egg)) {
            save_dirty = true;
            set_status(egg ? "Egg flag set." : "Egg flag cleared.");
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------------------- roamer --- */

static const char *const roamer_labels[] = {
    "Species", "Level", "Current HP", "PID", "Status", "Roaming",
};
#define ROAMER_FIELDS (sizeof roamer_labels / sizeof roamer_labels[0])

static void show_roamer(void) {
    draw_header("ROAMING POKEMON");
    Gen3Roamer r;
    if (!gen3_roamer(&parsed_save.gba, &r)) { mode = UI_TOOLS; return; }

    gui_panel(18, 88, 200, 334, C_PANEL, C_BORDER);
    gui_pokemon_sprite(58, 120, 96, 96, r.species);
    gui_text(38, 228, 0.95f, C_TEXT,
             r.species ? gen3_species_name(gen3_species_internal_from_national(r.species)) : "None");
    gui_badge(38, 258, r.active ? "ROAMING" : "NOT ROAMING", r.active ? C_GREEN : C_BADGE_OFF, C_BADGE_TEXT);
    /* Only Emerald loads all four IV bytes when the roamer is met, so the
     * IVs the player can actually catch differ from the ones stored. */
    const bool glitched = gen3_roamer_ivs_are_glitched(&parsed_save.gba);
    uint8_t met[6];
    gen3_roamer_encounter_ivs(&r, glitched, met);
    static const char *const stat_names[6] = { "HP", "Atk", "Def", "Spe", "SpA", "SpD" };
    gui_text(38, 292, 0.6f, C_MUTED, glitched ? "IVs  stored / met" : "IVs");
    for (unsigned i = 0; i < 6u; ++i) {
        const float y = 312.0f + i * 18.0f;
        gui_text(38, y, 0.58f, C_MUTED, stat_names[i]);
        gui_textf(96, y, 0.58f, C_TEXT, "%u", r.ivs[i]);
        if (glitched) gui_textf(140, y, 0.58f, met[i] == r.ivs[i] ? C_MUTED : C_YELLOW, "%u", met[i]);
    }

    gui_panel(230, 88, 392, 334, C_PANEL, C_BORDER);
    for (unsigned i = 0; i < ROAMER_FIELDS; ++i) {
        const float y = 112.0f + i * 40.0f;
        const bool sel = i == roamer_field;
        if (sel) gui_rect(242, y - 4, 368, 32, C_SELECT);
        gui_text(256, y, 0.78f, sel ? C_TEXT : C_MUTED, roamer_labels[i]);
        switch (i) {
            case 0: gui_text(430, y, 0.78f, C_TEXT,
                             r.species ? gen3_species_name(gen3_species_internal_from_national(r.species)) : "None"); break;
            case 1: gui_textf(430, y, 0.78f, C_TEXT, "%u", r.level); break;
            case 2: gui_textf(430, y, 0.78f, C_TEXT, "%u", r.hp_current); break;
            case 3: gui_textf(430, y, 0.78f, C_TEXT, "%08lX", (unsigned long)r.pid); break;
            case 4: gui_textf(430, y, 0.78f, C_TEXT, "%u", r.status); break;
            default: gui_text(430, y, 0.78f, C_TEXT, r.active ? "Yes" : "No"); break;
        }
    }
    draw_footer("[STICK] Field/+/-  [DPAD] Jump  [B] Back");
}

static void adjust_roamer(int direction, bool coarse) {
    if (!live_edit_allowed()) return;
    Gen3Roamer r;
    if (!gen3_roamer(&parsed_save.gba, &r)) return;
    const long long d = (long long)direction * (coarse ? 10 : 1);
    switch (roamer_field) {
        case 0: r.species = (uint16_t)clamp_ll((long long)r.species + d, 0, GEN3_DEX_SPECIES); break;
        case 1: r.level = (uint8_t)clamp_ll((long long)r.level + d, 1, 100); break;
        case 2: r.hp_current = (uint16_t)clamp_ll((long long)r.hp_current + d * (coarse ? 10 : 1), 0, 999); break;
        case 3: r.pid = (uint32_t)((long long)r.pid + (coarse ? (long long)direction * 256 : direction)); break;
        case 4: r.status = (uint8_t)clamp_ll((long long)r.status + direction, 0, 255); break;
        default: r.active = !r.active; break;
    }
    if (gen3_set_roamer(&parsed_save.gba, &r)) save_dirty = true;
}

static void handle_roamer(u32 down) {
    roamer_field = nav_index(down, roamer_field, ROAMER_FIELDS);
    const bool coarse = (down & UI_COARSE) != 0;
    if (down & PAD_BUTTON_LEFT) adjust_roamer(-1, coarse);
    if (down & PAD_BUTTON_RIGHT) adjust_roamer(1, coarse);
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* --------------------------------------------------------------- mail --- */

static void show_mail(void) {
    draw_header("MAIL BOX");
    gui_panel(18, 84, 250, 344, C_PANEL, C_BORDER);
    /* Six sheets are held by the party; the other ten sit in the PC. */
    for (unsigned i = 0; i < GEN3_MAIL_SLOTS; ++i) {
        const float y = 96.0f + i * 20.0f;
        const bool sel = i == mail_slot;
        if (sel) gui_rect(26, y - 3, 234, 20, C_SELECT);
        Gen3Mail m;
        const bool have = gen3_mail(&parsed_save.gba, i, &m) && m.present;
        gui_textf(36, y, 0.58f, sel ? C_TEXT : C_MUTED, "%s %u",
                  i < GEN3_MAIL_PARTY_SLOTS ? "Party" : "PC   ",
                  i < GEN3_MAIL_PARTY_SLOTS ? i + 1u : i - GEN3_MAIL_PARTY_SLOTS + 1u);
        gui_text(130, y, 0.58f, have ? C_TEXT : C_FAINT,
                 have ? gen3_item_name_for(parsed_save.kind, m.mail_item) : "empty");
    }

    gui_panel(280, 84, 342, 344, C_PANEL, C_BORDER);
    Gen3Mail m;
    if (gen3_mail(&parsed_save.gba, mail_slot, &m) && m.present) {
        gui_text(298, 96, 0.86f, C_ACCENT, gen3_item_name_for(parsed_save.kind, m.mail_item));
        gui_pokemon_sprite(298, 122, 64, 64, m.appear_species);
        gui_textf(374, 128, 0.7f, C_TEXT, "From %s", m.author);
        gui_textf(374, 152, 0.62f, C_MUTED, "ID %05u / %05u", m.author_tid, m.author_sid);
        gui_text(298, 200, 0.6f, C_MUTED, "Message words");
        /* The message is nine easy-chat word ids; this build has no word
         * table, so it shows the ids rather than inventing text. */
        for (unsigned row = 0; row < 3u; ++row) {
            char line[48]; size_t at = 0; line[0] = '\0';
            for (unsigned col = 0; col < 3u; ++col) {
                const uint16_t w = m.words[row * 3u + col];
                at += (size_t)snprintf(line + at, sizeof(line) - at, "%s%s",
                                       col ? "  " : "", w == 0xFFFFu ? "----" : "");
                if (w != 0xFFFFu)
                    at += (size_t)snprintf(line + at, sizeof(line) - at, "%04X", w);
                if (at >= sizeof(line)) break;
            }
            gui_text(298, 222.0f + row * 22.0f, 0.66f, C_TEXT, line);
        }
    } else {
        gui_text(298, 96, 0.86f, C_MUTED, "No mail in this slot");
    }
    draw_footer("[STICK] Slot  [DPAD] Jump  [B] Back  [X] Clear sheet");
}

static void handle_mail(u32 down) {
    mail_slot = nav_index(down, mail_slot, GEN3_MAIL_SLOTS);
    if ((down & PAD_BUTTON_X) && live_edit_allowed()) {
        if (gen3_clear_mail(&parsed_save.gba, mail_slot)) {
            save_dirty = true;
            set_status("Mail sheet cleared. The Pokemon holding it keeps the item.");
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* --------------------------------------------------------- pokeblocks --- */

static void show_pokeblocks(void) {
    draw_header("POKEBLOCK CASE");
    gui_panel(18, 84, 300, 340, C_PANEL, C_BORDER);
    /* Ten to a page keeps the whole case reachable without a scrollbar. */
    const unsigned page = pokeblock_index / 10u;
    for (unsigned row = 0; row < 10u; ++row) {
        const unsigned i = page * 10u + row;
        if (i >= GEN3_POKEBLOCK_COUNT) break;
        const float y = 98.0f + row * 32.0f;
        const bool sel = i == pokeblock_index;
        if (sel) gui_rect(28, y - 3, 280, 29, C_SELECT);
        Gen3PokeBlock b = {0};
        gen3_pokeblock(&parsed_save.gba, i, &b);
        gui_textf(40, y, 0.6f, C_MUTED, "%2u", i + 1u);
        gui_text(78, y, 0.72f, b.color ? (sel ? C_TEXT : C_MUTED) : C_FAINT,
                 gen3_pokeblock_color_name(b.color));
    }

    gui_panel(332, 84, 290, 340, C_PANEL, C_BORDER);
    Gen3PokeBlock b = {0};
    gen3_pokeblock(&parsed_save.gba, pokeblock_index, &b);
    gui_textf(350, 96, 0.86f, C_ACCENT, "Block %u", pokeblock_index + 1u);
    gui_text(350, 124, 0.72f, C_TEXT, gen3_pokeblock_color_name(b.color));
    static const char *const boost[6] = { "Spicy  (Cool)", "Dry    (Beauty)", "Sweet  (Cute)",
                                          "Bitter (Smart)", "Sour   (Tough)", "Feel   (Sheen)" };
    const uint8_t values[6] = { b.spicy, b.dry, b.sweet, b.bitter, b.sour, b.feel };
    for (unsigned i = 0; i < 6u; ++i) {
        const float y = 164.0f + i * 30.0f;
        gui_text(350, y, 0.64f, C_MUTED, boost[i]);
        gui_textf(560, y, 0.7f, C_TEXT, "%u", values[i]);
    }
    gui_text(350, 356, 0.55f, C_MUTED, "A colour of None is an empty slot.");
    draw_footer("[STICK] Block  [DPAD] Colour  [B] Back  [Y] Delete  [X] Maximise");
}

static void handle_pokeblocks(u32 down) {
    pokeblock_index = nav_index(down, pokeblock_index, GEN3_POKEBLOCK_COUNT);
    if (!live_edit_allowed()) { if (down & PAD_BUTTON_B) mode = UI_TOOLS; return; }

    Gen3PokeBlock b = {0};
    if (!gen3_pokeblock(&parsed_save.gba, pokeblock_index, &b)) { mode = UI_TOOLS; return; }
    bool changed = false;
    if (down & PAD_BUTTON_LEFT) {
        b.color = (uint8_t)((b.color + GEN3_POKEBLOCK_COLOR_COUNT - 1u) % GEN3_POKEBLOCK_COLOR_COUNT);
        changed = true;
    }
    if (down & PAD_BUTTON_RIGHT) {
        b.color = (uint8_t)((b.color + 1u) % GEN3_POKEBLOCK_COLOR_COUNT);
        changed = true;
    }
    if (down & PAD_BUTTON_X) {
        /* A Gold block with every boost at maximum, which is what PKHeX's
         * "maximise" does. */
        b.color = 14u;
        b.spicy = b.dry = b.sweet = b.bitter = b.sour = b.feel = 255u;
        changed = true;
    }
    if (down & PAD_BUTTON_Y) { memset(&b, 0, sizeof(b)); changed = true; }
    if (changed && gen3_set_pokeblock(&parsed_save.gba, pokeblock_index, &b)) save_dirty = true;
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------------- secret bases --- */

static void show_secret_bases(void) {
    draw_header("SECRET BASES");
    gui_panel(18, 84, 280, 340, C_PANEL, C_BORDER);
    for (unsigned i = 0; i < GEN3_SECRET_BASE_COUNT; ++i) {
        const float y = 96.0f + i * 16.0f;
        const bool sel = i == secret_base_index;
        if (sel) gui_rect(26, y - 2, 262, 16, C_SELECT);
        Gen3SecretBase b = {0};
        gen3_secret_base(&parsed_save.gba, i, &b);
        gui_textf(36, y, 0.5f, C_MUTED, "%2u", i + 1u);
        gui_text(70, y, 0.5f, b.present ? (sel ? C_TEXT : C_MUTED) : C_FAINT,
                 b.present ? b.ot_name : "free");
        if (i == 0) gui_text(200, y, 0.45f, C_ACCENT, "yours");
    }

    gui_panel(312, 84, 310, 340, C_PANEL, C_BORDER);
    Gen3SecretBase b = {0};
    gen3_secret_base(&parsed_save.gba, secret_base_index, &b);
    gui_textf(330, 96, 0.86f, C_ACCENT, "Base %u", secret_base_index + 1u);
    if (!b.present) {
        gui_text(330, 140, 0.8f, C_MUTED, "This slot is free.");
    } else {
        gui_textf(330, 130, 0.78f, C_TEXT, "%s", b.ot_name);
        gui_text(330, 156, 0.62f, C_MUTED, gen3_secret_base_class_name(b.ot_class));
        gui_textf(330, 182, 0.62f, C_MUTED, "%s", b.ot_gender ? "Female" : "Male");
        gui_textf(330, 208, 0.62f, C_MUTED, "ID %05u / %05u", b.tid, b.sid);
        gui_textf(330, 234, 0.62f, C_MUTED, "Location %u", b.location);
        gui_textf(330, 260, 0.62f, C_MUTED, "Entered %u times", b.times_entered);
        gui_textf(330, 286, 0.62f, C_MUTED, "Received %u bases", b.received);
        gui_textf(330, 312, 0.62f, C_MUTED, "Registry status %u", b.registry_status);
        if (b.battled_today) gui_badge(330, 338, "BATTLED TODAY", C_ACCENT, C_BADGE_TEXT);
    }
    if (b.present) {
        /* The team that defends the base: species and level are the useful
         * part on a screen this size. */
        gui_text(330, 356, 0.55f, C_MUTED, "Defending team");
        float tx = 330.0f;
        unsigned shown = 0;
        for (unsigned i = 0; i < GEN3_SECRET_BASE_TEAM; ++i) {
            Gen3SecretBaseMon m = {0};
            if (!gen3_secret_base_mon(&parsed_save.gba, secret_base_index, i, &m) || !m.present)
                continue;
            gui_pokemon_sprite(tx, 372, 34, 34, m.species);
            gui_textf(tx + 4, 406, 0.45f, C_MUTED, "L%u", m.level);
            tx += 46.0f;
            ++shown;
        }
        if (!shown) gui_text(330, 378, 0.55f, C_MUTED, "No team stored.");
    } else {
        /* The trainer class and the ID read the same bytes; say so rather than
         * letting the two look independent. */
        gui_text(330, 380, 0.5f, C_MUTED, "Class and trainer ID share bytes 9-12,\nas they do in PKHeX.");
    }
    draw_footer("[STICK] Base  [DPAD] Jump  [B] Back  [X] Clear");
}

static void handle_secret_bases(u32 down) {
    secret_base_index = nav_index(down, secret_base_index, GEN3_SECRET_BASE_COUNT);
    if ((down & PAD_BUTTON_X) && live_edit_allowed()) {
        if (secret_base_index == 0) {
            set_status("Base 1 is your own; clearing it is not offered.");
        } else if (gen3_clear_secret_base(&parsed_save.gba, secret_base_index)) {
            save_dirty = true;
            set_status("Secret base cleared.");
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ---------------------------------------------------- battle frontier --- */

/* The rows a facility actually has: its statistics crossed with its modes and
 * the two record types, plus the symbol row on top. */
typedef struct FrontierRow { Gen3FrontierStat stat; unsigned mode, record; bool symbol; } FrontierRow;

static unsigned frontier_rows(Gen3Facility facility, FrontierRow *rows, unsigned max) {
    unsigned n = 0;
    if (n < max) rows[n++] = (FrontierRow){ 0, 0, 0, true };
    const unsigned modes = gen3_facility_mode_count(facility);
    for (unsigned stat = 0; stat < GEN3_FRONTIER_STAT_COUNT; ++stat) {
        if (!gen3_facility_has_stat(facility, (Gen3FrontierStat)stat)) continue;
        for (unsigned mode = 0; mode < modes; ++mode)
            for (unsigned record = 0; record < 2u; ++record)
                if (n < max) rows[n++] = (FrontierRow){ (Gen3FrontierStat)stat, mode, record, false };
    }
    return n;
}

#define FRONTIER_MAX_ROWS 40u

static const char *frontier_mode_name(Gen3Facility facility, unsigned mode) {
    static const char *const names[4] = { "Singles", "Doubles", "Multi", "Link" };
    return gen3_facility_mode_count(facility) == 1u ? "" : names[mode < 4u ? mode : 0u];
}

/* -------------------------------------------------------- legality --- */

/*
 * Every Pokemon in the save, checked against what the games can produce. This
 * is not PKHeX's legality engine - see gen3_legality.h for what it is and is
 * not - and the screen says so, because a checker whose limits are not stated
 * gets believed about things it never looked at.
 *
 * The scan runs once on entry and is cached. Checking one Pokemon walks 65536
 * generator states, which is nothing on its own and is not something to redo
 * sixty times a second.
 */
#define LEGALITY_MAX 64u
#define LEGALITY_ROWS 9u

typedef struct LegalityHit {
    unsigned box, slot;      /* box 0xFFFF means a party slot */
    Gen3Verdict verdict;
    char species[16];
    char text[72];
} LegalityHit;

static LegalityHit legality_hits[LEGALITY_MAX];
static unsigned legality_count, legality_checked, legality_clean;
static bool legality_truncated;

static void legality_record(const Gen3Pokemon *p, unsigned box, unsigned slot) {
    ++legality_checked;
    Gen3LegalityReport r;
    gen3_check_legality(p, parsed_save.gba.game, &r);
    if (r.worst <= GEN3_NOTE) { ++legality_clean; return; }

    for (unsigned i = 0; i < r.count; ++i) {
        if (r.findings[i].verdict < GEN3_SUSPICIOUS) continue;
        if (legality_count >= LEGALITY_MAX) { legality_truncated = true; return; }
        LegalityHit *h = &legality_hits[legality_count++];
        h->box = box; h->slot = slot; h->verdict = r.findings[i].verdict;
        fit_text(h->species, sizeof(h->species), gen3_species_name(p->species_internal), 13);
        snprintf(h->text, sizeof(h->text), "%s", r.findings[i].text);
    }
}

static void legality_scan(void) {
    legality_count = legality_checked = legality_clean = 0;
    legality_truncated = false;
    legality_row = 0;

    Gen3Pokemon p;
    const unsigned party = gen3_any_party_count(&parsed_save);
    for (unsigned i = 0; i < party; ++i)
        if (gen3_any_party_pokemon(&parsed_save, i, &p) && p.present)
            legality_record(&p, 0xFFFFu, i);

    const unsigned boxes = gen3_any_box_count(&parsed_save);
    const unsigned slots = GEN3_BOX_SLOTS;
    for (unsigned b = 0; b < boxes; ++b)
        for (unsigned s = 0; s < slots; ++s)
            if (gen3_any_box_pokemon(&parsed_save, b, s, &p) && p.present)
                legality_record(&p, b, s);
    legality_scanned = true;
}

static void show_legality(void) {
    draw_header("LEGALITY CHECK");
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);

    if (!legality_scanned) {
        gui_text(38, 150, 0.8f, C_MUTED, "Checking every Pokemon in this save...");
        draw_footer("[B] Back");
        return;
    }

    gui_textf(30, 99, 0.78f, C_ACCENT, "%u checked, %u with nothing to report",
              legality_checked, legality_clean);
    if (legality_count)
        gui_textf(430, 103, 0.62f, C_RED, "%u finding%s", legality_count,
                  legality_count == 1u ? "" : "s");
    gui_rect(28, 121, 584, 1, C_BORDER);

    if (!legality_count) {
        gui_text(38, 150, 0.8f, C_GREEN, "Nothing here looks impossible.");
        gui_text(38, 182, 0.6f, C_MUTED,
                 "This checks the record against itself, the species table and the\n"
                 "learnsets. It does not check where a Pokemon was met or whether the\n"
                 "encounter existed, so it is not a full legality check.");
        draw_footer("[B] Back");
        return;
    }
    if (legality_row >= legality_count) legality_row = legality_count - 1u;

    unsigned first = legality_row >= LEGALITY_ROWS / 2u ? legality_row - LEGALITY_ROWS / 2u : 0u;
    if (first + LEGALITY_ROWS > legality_count)
        first = legality_count > LEGALITY_ROWS ? legality_count - LEGALITY_ROWS : 0u;

    for (unsigned r = 0; r < LEGALITY_ROWS && first + r < legality_count; ++r) {
        const LegalityHit *h = &legality_hits[first + r];
        const float y = 134.0f + r * 25.0f;
        const bool sel = first + r == legality_row;
        if (sel) gui_rect(28, y - 3, 584, 24, C_SELECT);

        if (h->box == 0xFFFFu) gui_textf(40, y, 0.58f, C_MUTED, "Party %u", h->slot + 1u);
        else gui_textf(40, y, 0.58f, C_MUTED, "Box %u/%u", h->box + 1u, h->slot + 1u);
        gui_text(130, y, 0.62f, sel ? C_TEXT : C_MUTED, h->species);
        gui_text(250, y, 0.58f, h->verdict == GEN3_INVALID ? C_RED : C_YELLOW,
                 h->verdict == GEN3_INVALID ? "impossible" : "suspicious");

        char shown[80];
        fit_text_px(shown, sizeof(shown), h->text, 246.0f, 0.56f);
        gui_text(360, y + 1, 0.56f, sel ? C_TEXT : C_MUTED, shown);
    }

    gui_rect(28, 366, 584, 1, C_BORDER);
    {
        const LegalityHit *h = &legality_hits[legality_row];
        char lines[2][96];
        const unsigned used = wrap_text(h->text, 576.0f, 0.6f, lines, 2u);
        for (unsigned i = 0; i < used; ++i)
            gui_text(30, 373.0f + i * 18.0f, 0.6f, C_TEXT, lines[i]);
    }
    gui_text(30, 410, 0.52f, C_MUTED,
             "Checks the record, the species table and the learnsets - not where it was met.");

    draw_footer("[STICK] Finding  [DPAD] Page  [B] Back  [A] Open the Pokemon");
}

/* -------------------------------------------------- GameCube link --- */

/*
 * The block Colosseum, XD and Pokemon Box write back into a cartridge save:
 * PokeCoupons earned at Mt. Battle, the gifts the Japanese Colosseum bonus
 * disc handed out for reaching a coupon rank, and what connecting to Pokemon
 * Box unlocked. Read from PKHeX's SAV3 external event properties.
 *
 * The two coupon totals are shown and not edited. They sit seven and eleven
 * bytes into the block, and in Emerald PKHeX puts the eleven gift ribbon bytes
 * at that same address, so a write there could land in the ribbons. The flags
 * are well clear of it.
 */
#define GCLINK_ROWS 9u

static void show_gamecube_link(void) {
    draw_header("GAMECUBE LINK");
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);
    gui_text(30, 99, 0.8f, C_ACCENT, "What Colosseum, XD and Pokemon Box left here");
    gui_rect(28, 121, 584, 1, C_BORDER);

    Gen3ExternalEvents ev;
    if (!gen3_external_events(&parsed_save.gba, &ev)) {
        gui_text(38, 150, 0.78f, C_MUTED, "This save has no external event block.");
        draw_footer("[B] Back");
        return;
    }

    static const char *const labels[GCLINK_ROWS] = {
        "PokeCoupons held", "PokeCoupons earned in total",
        "Master Ball title (30,000)", "Light Ball title (5,000)",
        "PP Max title (2,500)", "Ageto Celebi received",
        "Wishmaker Jirachi received", "Connected to Pokemon Box",
        "Pokemon Box egg gifts unlocked",
    };
    static const char *const eggs[4] = {
        "none", "ExtremeSpeed Zigzagoon", "Pay Day Skitty", "Surf Pichu",
    };

    for (unsigned r = 0; r < GCLINK_ROWS; ++r) {
        const float y = 140.0f + r * 28.0f;
        const bool sel = r == gclink_row;
        if (sel) gui_rect(28, y - 4, 584, 26, C_SELECT);
        gui_text(44, y, 0.68f, sel ? C_TEXT : C_MUTED, labels[r]);

        char value[40];
        bool on = false, readonly = false;
        switch (r) {
            case 0: snprintf(value, sizeof(value), "%lu", (unsigned long)ev.coupons); readonly = true; break;
            case 1: snprintf(value, sizeof(value), "%lu", (unsigned long)ev.coupons_total); readonly = true; break;
            case 2: on = ev.title_gold; break;
            case 3: on = ev.title_silver; break;
            case 4: on = ev.title_bronze; break;
            case 5: on = ev.received_celebi; break;
            case 6: on = ev.received_jirachi; break;
            case 7: on = ev.used_rsbox; break;
            default: snprintf(value, sizeof(value), "%s", eggs[ev.rsbox_eggs & 3u]); break;
        }
        if (r >= 2u && r <= 7u) snprintf(value, sizeof(value), "%s", on ? "yes" : "no");
        gui_text(400, y, 0.66f, readonly ? C_MUTED : (on || r == 8u ? C_GREEN : C_FAINT), value);
        if (readonly) gui_text(560, y + 2, 0.52f, C_FAINT, "read only");
    }

    gui_rect(28, 396, 584, 1, C_BORDER);
    gui_text(30, 403, 0.56f, C_MUTED,
             "Coupon totals are shown only: in Emerald they share an address with the gift ribbons.");

    if (live_edit_allowed())
        draw_footer("[STICK] Row/+/-  [DPAD] Jump  [B] Back  [A] Toggle");
    else
        draw_footer("[STICK] Row  [DPAD] Jump  [B] Back");
}

/* ------------------------------------------------- strategy memo --- */

/*
 * Colosseum and XD keep one entry per species met, with the original
 * trainer's IDs and the individual's PID. Only the seen state can be changed:
 * XD has a flag for it, and Colosseum expresses "not seen" by clearing the
 * entry, so there is no way to put one back. The screen says so rather than
 * offering a control that would not work.
 */
#define MEMO_ROWS 10u

static void show_memo(void) {
    draw_header("STRATEGY MEMO");
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);

    const unsigned total = gen3_any_memo_count(&parsed_save);
    const bool xd = parsed_save.kind == GEN3_KIND_XD;
    gui_textf(30, 99, 0.8f, C_ACCENT, "%u %s", total, total == 1u ? "entry" : "entries");
    gui_text(300, 103, 0.6f, C_MUTED,
             xd ? "XD records seen only" : "Colosseum records seen and owned");
    gui_rect(28, 121, 584, 1, C_BORDER);

    if (!total) {
        gui_text(38, 150, 0.78f, C_MUTED, "Nothing has been recorded yet.");
        draw_footer("[B] Back");
        return;
    }
    if (memo_index >= total) memo_index = total - 1u;

    unsigned first = memo_index >= MEMO_ROWS / 2u ? memo_index - MEMO_ROWS / 2u : 0u;
    if (first + MEMO_ROWS > total) first = total > MEMO_ROWS ? total - MEMO_ROWS : 0u;
    for (unsigned r = 0; r < MEMO_ROWS && first + r < total; ++r) {
        const unsigned i = first + r;
        const float y = 136.0f + r * 25.0f;
        const bool sel = i == memo_index;
        if (sel) gui_rect(28, y - 3, 584, 24, C_SELECT);
        Gen3MemoEntry e;
        if (!gen3_any_memo_entry(&parsed_save, i, &e)) continue;
        gui_textf(40, y, 0.6f, C_MUTED, "%3u", i + 1u);
        gui_text(96, y, 0.64f, e.species_internal ? (sel ? C_TEXT : C_MUTED) : C_FAINT,
                 e.species_internal ? gen3_species_name(e.species_internal) : "-");
        gui_text(272, y, 0.6f, e.seen ? C_GREEN : C_FAINT, e.seen ? "seen" : "not seen");
        if (!xd) gui_text(352, y, 0.6f, e.owned ? C_GREEN : C_FAINT, e.owned ? "owned" : "-");
        gui_textf(430, y, 0.58f, C_MUTED, "%05u/%05u", e.tid, e.sid);
        gui_textf(540, y, 0.58f, C_MUTED, "%08lX", (unsigned long)e.pid);
    }

    gui_rect(28, 392, 584, 1, C_BORDER);
    gui_text(30, 399, 0.56f, C_MUTED,
             xd ? "A recorded entry can be hidden and shown again."
                : "Clearing an entry here is permanent: Colosseum has no seen flag.");

    if (live_edit_allowed())
        draw_footer(xd ? "[STICK] Entry  [DPAD] Page  [B] Back  [A] Toggle seen"
                       : "[STICK] Entry  [DPAD] Page  [B] Back  [A] Clear entry");
    else
        draw_footer("[STICK] Entry  [DPAD] Page  [B] Back");
}

/* ------------------------------------------------ Emerald extras --- */

/*
 * Three Emerald-only blocks that are each too small for a screen: the four
 * Trainer Hill times, Walda's box wallpaper, and the Easy Chat trendy words.
 * They share one list. The word labels keep PKHeX's spelling, which is the
 * enum's - guessing where the spaces go in KTHXBYE would be inventing text.
 */
#define EXTRAS_HILL_ROWS  GEN3_TRAINER_HILL_MODES
#define EXTRAS_WALDA_ROWS 5u
#define EXTRAS_ROWS_SHOWN 11u
#define EXTRAS_TOTAL (EXTRAS_HILL_ROWS + EXTRAS_WALDA_ROWS + GEN3_TRENDY_WORD_COUNT)

static void extras_row_text(unsigned row, char *label, size_t label_size,
                            char *value, size_t value_size, bool *toggled) {
    *toggled = false;
    if (row < EXTRAS_HILL_ROWS) {
        snprintf(label, label_size, "Trainer Hill: %s", gen3_trainer_hill_mode_name(row));
        const uint32_t frames = gen3_trainer_hill_record(&parsed_save.gba, row);
        if (!frames) snprintf(value, value_size, "no time set");
        else snprintf(value, value_size, "%lu:%02lu.%02lu",
                      (unsigned long)(frames / 3600u), (unsigned long)((frames / 60u) % 60u),
                      (unsigned long)(frames % 60u));
        return;
    }
    if (row < EXTRAS_HILL_ROWS + EXTRAS_WALDA_ROWS) {
        Gen3Walda w; memset(&w, 0, sizeof(w));
        gen3_walda(&parsed_save.gba, &w);
        switch (row - EXTRAS_HILL_ROWS) {
            case 0: snprintf(label, label_size, "Walda: background colour");
                    snprintf(value, value_size, "0x%04X", w.background); break;
            case 1: snprintf(label, label_size, "Walda: foreground colour");
                    snprintf(value, value_size, "0x%04X", w.foreground); break;
            case 2: snprintf(label, label_size, "Walda: icon");
                    snprintf(value, value_size, "%u", w.icon); break;
            case 3: snprintf(label, label_size, "Walda: pattern");
                    snprintf(value, value_size, "%u", w.pattern); break;
            default: snprintf(label, label_size, "Walda: wallpaper unlocked");
                     snprintf(value, value_size, "%s", w.unlocked ? "yes" : "no");
                     *toggled = true; break;
        }
        return;
    }
    const unsigned word = row - EXTRAS_HILL_ROWS - EXTRAS_WALDA_ROWS;
    snprintf(label, label_size, "Trendy word: %s", gen3_trendy_word_name(word));
    snprintf(value, value_size, "%s", gen3_trendy_word(&parsed_save.gba, word) ? "unlocked" : "locked");
    *toggled = true;
}

static void show_emerald_extras(void) {
    draw_header("EMERALD EXTRAS");
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);
    gui_text(30, 99, 0.8f, C_ACCENT, "Trainer Hill, Walda and the trendy words");
    gui_textf(470, 103, 0.62f, C_MUTED, "%u of %u", extras_row + 1u, (unsigned)EXTRAS_TOTAL);
    gui_rect(28, 121, 584, 1, C_BORDER);

    unsigned first = extras_row >= EXTRAS_ROWS_SHOWN / 2u ? extras_row - EXTRAS_ROWS_SHOWN / 2u : 0u;
    if (first + EXTRAS_ROWS_SHOWN > EXTRAS_TOTAL) first = EXTRAS_TOTAL - EXTRAS_ROWS_SHOWN;
    for (unsigned r = 0; r < EXTRAS_ROWS_SHOWN; ++r) {
        const unsigned row = first + r;
        const float y = 134.0f + r * 25.0f;
        const bool sel = row == extras_row;
        if (sel) gui_rect(28, y - 3, 584, 24, C_SELECT);
        char label[48], value[32]; bool toggled;
        extras_row_text(row, label, sizeof(label), value, sizeof(value), &toggled);
        gui_text(40, y, 0.64f, sel ? C_TEXT : C_MUTED, label);
        const bool off = strcmp(value, "locked") == 0 || strcmp(value, "no") == 0 ||
                         strcmp(value, "no time set") == 0;
        gui_text(400, y, 0.64f, off ? C_FAINT : (toggled ? C_GREEN : C_TEXT), value);
    }

    if (live_edit_allowed())
        draw_footer("[STICK] Row/+/-  [DPAD] Page  [B] Back  [A] Toggle");
    else
        draw_footer("[STICK] Row  [DPAD] Page  [B] Back");
}

/* ----------------------------------------------------- decorations --- */

/*
 * The decoration inventory is what the player owns, not what is placed in a
 * base: 150 one-byte ids in eight fixed-length categories. The names come
 * from PKHeX's Decoration3 enum, which is the only place it lists them.
 */
static void show_decorations(void) {
    draw_header("DECORATIONS");
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);

    const Gen3DecorationKind kind = (Gen3DecorationKind)deco_kind;
    const unsigned slots = gen3_decoration_slot_count(kind);
    gui_text(30, 99, 0.86f, C_ACCENT, gen3_decoration_kind_name(kind));
    gui_textf(430, 103, 0.62f, C_MUTED, "%u of %u categories", deco_kind + 1u,
              (unsigned)GEN3_DECO_KIND_COUNT);
    gui_rect(28, 121, 584, 1, C_BORDER);

    unsigned owned = 0;
    for (unsigned i = 0; i < slots; ++i)
        if (gen3_decoration(&parsed_save.gba, kind, i)) ++owned;

    const unsigned rows = 10u;
    unsigned first = deco_slot >= rows / 2u ? deco_slot - rows / 2u : 0u;
    if (first + rows > slots) first = slots > rows ? slots - rows : 0u;
    for (unsigned r = 0; r < rows && first + r < slots; ++r) {
        const unsigned slot = first + r;
        const float y = 136.0f + r * 25.0f;
        const bool sel = slot == deco_slot;
        if (sel) gui_rect(28, y - 3, 584, 24, C_SELECT);
        const uint8_t id = gen3_decoration(&parsed_save.gba, kind, slot);
        gui_textf(40, y, 0.62f, C_MUTED, "%02u", slot + 1u);
        gui_textf(96, y, 0.62f, id ? C_MUTED : C_FAINT, "%3u", id);
        gui_text(156, y, 0.64f, id ? (sel ? C_TEXT : C_MUTED) : C_FAINT,
                 id ? gen3_decoration_name(id) : "empty");
    }

    gui_rect(28, 392, 584, 1, C_BORDER);
    gui_textf(30, 399, 0.58f, C_MUTED, "%u of %u slots in this category hold something.",
              owned, slots);

    if (live_edit_allowed())
        draw_footer("[STICK] Slot/Change  [DPAD] Category  [B] Back  [X] Clear");
    else
        draw_footer("[STICK] Slot  [DPAD] Category  [B] Back");
}

static void show_frontier(void) {
    draw_header("BATTLE FRONTIER");
    const Gen3Facility facility = (Gen3Facility)frontier_facility;
    gui_panel(18, 84, 604, 54, C_PANEL, C_BORDER);
    gui_text(36, 96, 0.9f, C_ACCENT, gen3_facility_name(facility));
    gui_textf(400, 98, 0.66f, C_MUTED, "%u BP", gen3_battle_points(&parsed_save.gba));
    gui_badge(500, 96, gen3_frontier_pass(&parsed_save.gba) ? "PASS" : "NO PASS",
              gen3_frontier_pass(&parsed_save.gba) ? C_GREEN : C_PANEL2, C_BADGE_TEXT);

    FrontierRow rows[FRONTIER_MAX_ROWS];
    const unsigned count = frontier_rows(facility, rows, FRONTIER_MAX_ROWS);
    if (frontier_row >= count) frontier_row = 0;

    gui_panel(18, 146, 604, 278, C_PANEL, C_BORDER);
    const unsigned visible = 9u;
    unsigned top = frontier_row > visible / 2u ? frontier_row - visible / 2u : 0u;
    if (count > visible && top > count - visible) top = count - visible;
    for (unsigned r = 0; r < visible && top + r < count; ++r) {
        const FrontierRow *row = &rows[top + r];
        const float y = 158.0f + r * 29.0f;
        const bool sel = top + r == frontier_row;
        if (sel) gui_rect(28, y - 2, 584, 27, C_SELECT);
        if (row->symbol) {
            static const char *const symbol[3] = { "none", "silver", "gold" };
            gui_text(44, y, 0.7f, sel ? C_TEXT : C_MUTED, "Symbol");
            gui_text(400, y, 0.7f, C_TEXT, symbol[gen3_frontier_symbol(&parsed_save.gba, facility)]);
            continue;
        }
        gui_text(44, y, 0.66f, sel ? C_TEXT : C_MUTED, gen3_frontier_stat_name(row->stat));
        gui_text(250, y, 0.6f, C_MUTED, frontier_mode_name(facility, row->mode));
        gui_text(330, y, 0.6f, C_MUTED, row->record ? "Open" : "Lv 50");
        uint16_t value = 0;
        gen3_frontier_stat(&parsed_save.gba, facility, row->mode, row->record, row->stat, &value);
        gui_textf(430, y, 0.7f, C_TEXT, "%u", value);
    }
    draw_footer("[STICK] Row/+/-  [DPAD] Facility  [B] Back  [Y] BP +100  [X] Pass");
}

static void handle_legality(u32 down) {
    if (!legality_scanned) { legality_scan(); return; }
    if (legality_count) {
        legality_row = nav_index(down, legality_row, legality_count);
        if (nav_page_prev(down)) legality_row = legality_row >= LEGALITY_ROWS ? legality_row - LEGALITY_ROWS : 0u;
        if (nav_page_next(down)) {
            legality_row += LEGALITY_ROWS;
            if (legality_row >= legality_count) legality_row = legality_count - 1u;
        }
        /* Jump to whichever Pokemon the finding is about. */
        if (down & PAD_BUTTON_A) {
            const LegalityHit *h = &legality_hits[legality_row];
            if (h->box == 0xFFFFu) {
                party_selected = h->slot;
                mode = UI_SUMMARY;
            } else {
                box_index = h->box; box_selected = h->slot;
                mode = UI_BOXES;
            }
            return;
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

static void handle_gamecube_link(u32 down) {
    gclink_row = nav_index(down, gclink_row, GCLINK_ROWS);

    Gen3ExternalEvents ev;
    if (live_edit_allowed() && gen3_external_events(&parsed_save.gba, &ev)) {
        bool changed = false;
        if (down & PAD_BUTTON_A) {
            switch (gclink_row) {
                case 2: ev.title_gold = !ev.title_gold; changed = true; break;
                case 3: ev.title_silver = !ev.title_silver; changed = true; break;
                case 4: ev.title_bronze = !ev.title_bronze; changed = true; break;
                case 5: ev.received_celebi = !ev.received_celebi; changed = true; break;
                case 6: ev.received_jirachi = !ev.received_jirachi; changed = true; break;
                case 7: ev.used_rsbox = !ev.used_rsbox; changed = true; break;
                case 8: ev.rsbox_eggs = (uint8_t)((ev.rsbox_eggs + 1u) & 3u); changed = true; break;
                default: set_status("The coupon totals are read only here."); break;
            }
        }
        if (gclink_row == 8u) {
            if (down & PAD_BUTTON_LEFT) { ev.rsbox_eggs = (uint8_t)((ev.rsbox_eggs + 3u) & 3u); changed = true; }
            if (down & PAD_BUTTON_RIGHT) { ev.rsbox_eggs = (uint8_t)((ev.rsbox_eggs + 1u) & 3u); changed = true; }
        }
        if (changed && gen3_set_external_event_flags(&parsed_save.gba, &ev)) save_dirty = true;
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

static void handle_memo(u32 down) {
    const unsigned total = gen3_any_memo_count(&parsed_save);
    if (!total) { if (down & PAD_BUTTON_B) mode = UI_TOOLS; return; }
    if (memo_index >= total) memo_index = total - 1u;

    memo_index = nav_index(down, memo_index, total);
    if (nav_page_prev(down)) memo_index = memo_index >= MEMO_ROWS ? memo_index - MEMO_ROWS : 0u;
    if (nav_page_next(down)) {
        memo_index += MEMO_ROWS;
        if (memo_index >= total) memo_index = total - 1u;
    }
    if ((down & PAD_BUTTON_A) && live_edit_allowed()) {
        Gen3MemoEntry e;
        if (gen3_any_memo_entry(&parsed_save, memo_index, &e)) {
            if (gen3_any_set_memo_seen(&parsed_save, memo_index, !e.seen)) save_dirty = true;
            else set_status("Colosseum cannot record an entry it has cleared.");
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

static void handle_emerald_extras(u32 down) {
    extras_row = nav_index(down, extras_row, EXTRAS_TOTAL);
    if (nav_page_prev(down)) extras_row = extras_row >= EXTRAS_ROWS_SHOWN ? extras_row - EXTRAS_ROWS_SHOWN : 0u;
    if (nav_page_next(down)) {
        extras_row += EXTRAS_ROWS_SHOWN;
        if (extras_row >= EXTRAS_TOTAL) extras_row = EXTRAS_TOTAL - 1u;
    }
    if (!live_edit_allowed()) { if (down & PAD_BUTTON_B) mode = UI_TOOLS; return; }

    int step = 0;
    if (down & PAD_BUTTON_LEFT) step = -1;
    if (down & PAD_BUTTON_RIGHT) step = 1;
    const bool toggle = (down & PAD_BUTTON_A) != 0u;

    if (extras_row < EXTRAS_HILL_ROWS) {
        /* A whole second at a time; the stored unit is a sixtieth. */
        if (step) {
            long long frames = (long long)gen3_trainer_hill_record(&parsed_save.gba, extras_row) + step * 60;
            if (frames < 0) frames = 0;
            if (frames > 0xFFFFFFFFLL) frames = 0xFFFFFFFFLL;
            if (gen3_set_trainer_hill_record(&parsed_save.gba, extras_row, (uint32_t)frames)) save_dirty = true;
        }
    } else if (extras_row < EXTRAS_HILL_ROWS + EXTRAS_WALDA_ROWS) {
        Gen3Walda w; memset(&w, 0, sizeof(w));
        if (gen3_walda(&parsed_save.gba, &w) && (step || toggle)) {
            switch (extras_row - EXTRAS_HILL_ROWS) {
                case 0: w.background = (uint16_t)(w.background + step); break;
                case 1: w.foreground = (uint16_t)(w.foreground + step); break;
                case 2: w.icon = (uint8_t)(w.icon + step); break;
                case 3: w.pattern = (uint8_t)(w.pattern + step); break;
                default: if (toggle) w.unlocked = !w.unlocked; break;
            }
            if (gen3_set_walda(&parsed_save.gba, &w)) save_dirty = true;
        }
    } else if (toggle) {
        const unsigned word = extras_row - EXTRAS_HILL_ROWS - EXTRAS_WALDA_ROWS;
        if (gen3_set_trendy_word(&parsed_save.gba, word, !gen3_trendy_word(&parsed_save.gba, word)))
            save_dirty = true;
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

static void handle_decorations(u32 down) {
    const unsigned kinds = (unsigned)GEN3_DECO_KIND_COUNT;
    if (nav_page_prev(down)) { deco_kind = deco_kind ? deco_kind - 1u : kinds - 1u; deco_slot = 0; }
    if (nav_page_next(down)) { deco_kind = (deco_kind + 1u) % kinds; deco_slot = 0; }

    const Gen3DecorationKind kind = (Gen3DecorationKind)deco_kind;
    const unsigned slots = gen3_decoration_slot_count(kind);
    if (!slots) { if (down & PAD_BUTTON_B) mode = UI_TOOLS; return; }
    if (deco_slot >= slots) deco_slot = slots - 1u;

    deco_slot = nav_index(down, deco_slot, slots);

    if (live_edit_allowed()) {
        const int step = nav_fine(down);
        if (step) {
            int id = (int)gen3_decoration(&parsed_save.gba, kind, deco_slot) + step;
            if (id < 0) id = GEN3_DECORATION_MAX;
            if (id > GEN3_DECORATION_MAX) id = 0;
            if (gen3_set_decoration(&parsed_save.gba, kind, deco_slot, (uint8_t)id)) save_dirty = true;
        }
        if (down & PAD_BUTTON_X) {
            if (gen3_set_decoration(&parsed_save.gba, kind, deco_slot, 0)) save_dirty = true;
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

static void handle_frontier(u32 down) {
    FrontierRow rows[FRONTIER_MAX_ROWS];
    const Gen3Facility facility = (Gen3Facility)frontier_facility;
    const unsigned count = frontier_rows(facility, rows, FRONTIER_MAX_ROWS);
    if (frontier_row >= count) frontier_row = 0;

    frontier_row = nav_index(down, frontier_row, count);
    if (nav_page_prev(down)) { frontier_facility = frontier_facility ? frontier_facility - 1u : GEN3_FACILITY_COUNT - 1u; frontier_row = 0; }
    if (nav_page_next(down)) { frontier_facility = (frontier_facility + 1u) % GEN3_FACILITY_COUNT; frontier_row = 0; }

    const int frontier_direction = nav_fine(down);
    if (live_edit_allowed() && frontier_direction) {
        const int direction = frontier_direction;
        const FrontierRow *row = &rows[frontier_row];
        if (row->symbol) {
            const int next = (int)gen3_frontier_symbol(&parsed_save.gba, facility) + direction;
            if (next >= 0 && next <= 2 && gen3_set_frontier_symbol(&parsed_save.gba, facility, (uint8_t)next))
                save_dirty = true;
        } else {
            uint16_t value = 0;
            gen3_frontier_stat(&parsed_save.gba, facility, row->mode, row->record, row->stat, &value);
            const long long next = clamp_ll((long long)value + direction, 0, 9999);
            if (gen3_set_frontier_stat(&parsed_save.gba, facility, row->mode, row->record, row->stat, (uint16_t)next))
                save_dirty = true;
        }
    }
    if ((down & PAD_BUTTON_X) && live_edit_allowed()) {
        if (gen3_set_frontier_pass(&parsed_save.gba, !gen3_frontier_pass(&parsed_save.gba))) save_dirty = true;
    }
    if ((down & PAD_BUTTON_Y) && live_edit_allowed()) {
        const long long next = clamp_ll((long long)gen3_battle_points(&parsed_save.gba) + 100, 0, 9999);
        if (gen3_set_battle_points(&parsed_save.gba, (uint16_t)next)) save_dirty = true;
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------------------- misc --- */

/*
 * The small blocks that do not each deserve a screen: the daily swarm, the
 * e-Reader berry slot and the Lilycove museum paintings.
 */
static void show_misc(void) {
    draw_header("MISC");
    gui_panel(18, 84, 604, 150, C_PANEL, C_BORDER);
    gui_text(38, 94, 0.72f, C_ACCENT, "Swarm");
    Gen3Swarm sw = {0};
    if (!gen3_has_swarm(&parsed_save.gba) || !gen3_swarm(&parsed_save.gba, &sw)) {
        gui_text(38, 124, 0.68f, C_MUTED, "This game has no swarms.");
    } else if (!sw.active) {
        gui_text(38, 124, 0.68f, C_MUTED, "Nothing is swarming.");
    } else {
        gui_pokemon_sprite(38, 118, 56, 56, sw.species);
        gui_text(108, 124, 0.78f, C_TEXT,
                 gen3_species_name(gen3_species_internal_from_national(sw.species)));
        gui_textf(108, 150, 0.62f, C_MUTED, "Level %u   %u%% chance   %u days left",
                  sw.level, sw.probability, sw.days_left);
        gui_textf(108, 174, 0.58f, C_MUTED, "Map %u/%u", sw.map_group, sw.map_num);
        for (unsigned i = 0; i < 4u; ++i)
            if (sw.moves[i]) gui_text(340.0f + (i % 2u) * 140.0f, 124.0f + (i / 2u) * 22.0f,
                                      0.58f, C_MUTED, gen3_move_name(sw.moves[i]));
    }

    gui_panel(18, 244, 604, 76, C_PANEL, C_BORDER);
    gui_text(38, 254, 0.72f, C_ACCENT, "e-Reader berry");
    if (!gen3_has_eberry(&parsed_save.gba)) {
        gui_text(38, 284, 0.68f, C_MUTED, "Not present in this save.");
    } else if (gen3_eberry_is_enigma(&parsed_save.gba)) {
        gui_text(38, 284, 0.68f, C_YELLOW, "Enigma Berry: no berry data loaded.");
        gui_text(330, 286, 0.55f, C_MUTED, "The games cannot resolve this,\nand it blocks trading.");
    } else {
        char name[16];
        gen3_eberry_name(&parsed_save.gba, name, sizeof(name));
        gui_textf(38, 284, 0.72f, C_TEXT, "%s Berry", name);
    }
    /* Shares the berry panel; the paintings row below has no space left. */
    if (gen3_has_joyful(&parsed_save.gba)) {
        gui_textf(330, 262, 0.55f, C_MUTED, "%s %lu",
                  gen3_joyful_stat_name(GEN3_JOYFUL_JUMP_SCORE),
                  (unsigned long)gen3_joyful_stat(&parsed_save.gba, GEN3_JOYFUL_JUMP_SCORE));
        gui_textf(330, 280, 0.55f, C_MUTED, "%s %lu",
                  gen3_joyful_stat_name(GEN3_JOYFUL_BERRIES_SCORE),
                  (unsigned long)gen3_joyful_stat(&parsed_save.gba, GEN3_JOYFUL_BERRIES_SCORE));
    }

    gui_panel(18, 330, 300, 94, C_PANEL, C_BORDER);
    gui_text(38, 338, 0.72f, C_ACCENT, "Museum paintings");
    if (!gen3_has_paintings(&parsed_save.gba)) {
        gui_text(38, 368, 0.66f, C_MUTED, "This game has no museum.");
    } else {
        float x = 38.0f;
        unsigned shown = 0;
        for (unsigned i = 0; i < GEN3_PAINTING_COUNT; ++i) {
            Gen3Painting pt = {0};
            if (!gen3_painting(&parsed_save.gba, i, &pt) || !pt.present) continue;
            gui_pokemon_sprite(x, 360, 40, 40, pt.species);
            gui_text(x, 404, 0.5f, C_MUTED, pt.nickname);
            x += 118.0f;
            ++shown;
        }
        if (!shown) gui_text(38, 368, 0.66f, C_MUTED, "None on display.");
    }

    gui_panel(328, 330, 294, 94, C_PANEL, C_BORDER);
    gui_text(346, 338, 0.72f, C_ACCENT, "Mystery Gift");
    float my = 364.0f;
    if (gen3_has_wonder_card(&parsed_save.gba)) {
        Gen3WonderCard card = {0};
        gen3_wonder_card(&parsed_save.gba, &card);
        if (!card.present) {
            gui_text(346, my, 0.62f, C_MUTED,
                     card.checksum_ok ? "No Wonder Card." : "Wonder Card checksum bad.");
        } else {
            char t[26]; fit_text(t, sizeof(t), card.title, 22);
            gui_text(346, my, 0.62f, C_TEXT, t);
            gui_textf(346, my + 20.0f, 0.55f, C_MUTED, "Card %u   %u left", card.card_id, card.count);
        }
        my += 40.0f;
    }
    if (gen3_has_mystery_event(&parsed_save.gba)) {
        const bool present = gen3_mystery_event_present(&parsed_save.gba);
        gui_text(346, my, 0.55f, present ? C_TEXT : C_MUTED,
                 present ? (gen3_mystery_event_checksum_ok(&parsed_save.gba)
                                ? "Mystery Event stored." : "Mystery Event, checksum bad.")
                         : "No Mystery Event.");
    }
    draw_footer("[B] Back  [Y] Clear gift  [X] Clear swarm");
}

static void handle_misc(u32 down) {
    if ((down & PAD_BUTTON_X) && live_edit_allowed() && gen3_has_swarm(&parsed_save.gba)) {
        if (gen3_clear_swarm(&parsed_save.gba)) {
            save_dirty = true;
            set_status("Swarm cleared.");
        }
    }
    if ((down & PAD_BUTTON_Y) && live_edit_allowed()) {
        /* Both blocks together: a card and an event are two halves of the same
         * feature, and clearing one alone leaves a confusing state. */
        bool cleared = false;
        if (gen3_clear_wonder_card(&parsed_save.gba)) cleared = true;
        if (gen3_clear_mystery_event(&parsed_save.gba)) cleared = true;
        if (cleared) { save_dirty = true; set_status("Mystery Gift data cleared."); }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------- record files on SD --- */

/*
 * A box slot's stored record, written to the card as a .pk3 / .ck3 / .xk3
 * exactly as it sits in the save. Importing goes back through the record
 * parser, so a file of the right length but the wrong kind is refused rather
 * than written in.
 */
#define RECORD_DIR "pkhex-gc-pokemon"

static bool export_box_record(unsigned box, unsigned slot, char *out, size_t out_size) {
    if (out && out_size) out[0] = '\0';
    if (!fat_available || root_count <= 0) return false;
    const size_t len = gen3_any_record_size(&parsed_save);
    if (!len) return false;
    uint8_t record[GEN3_PK3_PARTY_SIZE > 312 ? GEN3_PK3_PARTY_SIZE : 312];
    if (len > sizeof record) return false;
    if (!gen3_any_box_record_raw(&parsed_save, box, slot, record, sizeof record)) return false;

    Gen3Pokemon p;
    if (!gen3_any_box_pokemon(&parsed_save, box, slot, &p) || !p.present) return false;

    for (int pass = 0; pass < root_count; ++pass) {
        const int ri = (current_root + pass) % root_count;
        char dir[PATH_LEN];
        snprintf(dir, sizeof(dir), "%s" RECORD_DIR, roots[ri]);
        if (mkdir(dir, 0777) != 0 && errno != EEXIST) continue;
        for (unsigned n = 1; n <= 9999; ++n) {
            char path[PATH_LEN];
            /* Named for what it is, numbered so nothing is overwritten. */
            snprintf(path, sizeof(path), "%s/%03u-%s-%04u.%s", dir,
                     gen3_species_national(p.species_internal),
                     gen3_species_name(p.species_internal), n,
                     gen3_any_record_extension(&parsed_save));
            if (access(path, F_OK) == 0) continue;
            if (!write_whole_file(path, record, len)) break;
            if (out && out_size) snprintf(out, out_size, "%s", path);
            return true;
        }
    }
    return false;
}

/*
 * The other half: the files this wrote, listed so one can be put back into a
 * box slot. Only files the length of this save's records are offered, and the
 * writer parses each one before it goes in.
 */
#define RECORD_FILE_MAX 128u

typedef struct RecordFile {
    char name[NAME_LEN];
    char path[PATH_LEN];
} RecordFile;

static size_t record_wanted_size(void) {
    if (record_kind == RECORD_KIND_WONDER_CARD)
        return parsed_save.gba.japanese ? GEN3_WONDER_CARD_BYTES_JP : GEN3_WONDER_CARD_BYTES;
    return gen3_any_record_size(&parsed_save);
}

static RecordFile record_files[RECORD_FILE_MAX];
static uint8_t record_import_buffer[512];
static unsigned record_file_count, record_file_selected;
static unsigned record_target_box, record_target_slot;
static char record_dir[PATH_LEN];

static void scan_record_files(void) {
    record_file_count = 0;
    record_file_selected = 0;
    record_dir[0] = '\0';
    record_target_box = box_index;
    record_target_slot = box_selected;
    if (!fat_available || root_count <= 0) return;
    const size_t want = record_wanted_size();
    if (!want) return;

    for (int pass = 0; pass < root_count && record_file_count == 0; ++pass) {
        const int ri = (current_root + pass) % root_count;
        char dir[PATH_LEN];
        snprintf(dir, sizeof(dir), "%s" RECORD_DIR, roots[ri]);
        DIR *d = opendir(dir);
        if (!d) continue;
        snprintf(record_dir, sizeof(record_dir), "%s", dir);
        struct dirent *e;
        while ((e = readdir(d)) != NULL && record_file_count < RECORD_FILE_MAX) {
            if (e->d_name[0] == '.') continue;
            char path[PATH_LEN];
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            struct stat st;
            /* Only files exactly the size of one of this save's records. */
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            if ((size_t)st.st_size != want) continue;
            RecordFile *f = &record_files[record_file_count++];
            snprintf(f->name, sizeof(f->name), "%s", e->d_name);
            snprintf(f->path, sizeof(f->path), "%s", path);
        }
        closedir(d);
    }
}

static bool import_record_file(const RecordFile *f) {
    if (!f) return false;
    const size_t want = record_wanted_size();
    if (!want || want > sizeof(record_import_buffer)) return false;
    FILE *fp = fopen(f->path, "rb");
    if (!fp) return false;
    /* Exactly the wanted size and no more: a longer file is something else. */
    const bool read_ok = fread(record_import_buffer, 1, want, fp) == want && fgetc(fp) == EOF;
    fclose(fp);
    if (!read_ok) return false;

    if (record_kind == RECORD_KIND_WONDER_CARD)
        return gen3_set_wonder_card(&parsed_save.gba, record_import_buffer, want);
    return gen3_any_set_box_record_raw(&parsed_save, record_target_box, record_target_slot,
                                       record_import_buffer, want);
}

static void show_record_files(void) {
    const bool card = record_kind == RECORD_KIND_WONDER_CARD;
    draw_header(card ? "WONDER CARD FILES" : "POKEMON FILES");
    gui_panel(18, 84, 604, 58, C_PANEL, C_BORDER);
    if (card)
        gui_text(38, 94, 0.7f, C_TEXT, "Replace the Wonder Card in this save");
    else
        gui_textf(38, 94, 0.7f, C_TEXT, "Into box %u slot %u",
                  record_target_box + 1u, record_target_slot + 1u);
    gui_textf(38, 118, 0.55f, C_MUTED, "%s   %u-byte files only",
              record_dir[0] ? record_dir : "no " RECORD_DIR " folder on the card",
              (unsigned)record_wanted_size());

    gui_panel(18, 152, 604, 272, C_PANEL, C_BORDER);
    if (!record_file_count) {
        gui_text(38, 176, 0.75f, C_MUTED, "Nothing to import.");
        if (card)
            gui_text(38, 206, 0.58f, C_MUTED,
                     "Put a Wonder Card file in " RECORD_DIR " on the card.\n"
                     "A card whose checksum does not match is refused.");
        else
            gui_text(38, 206, 0.58f, C_MUTED,
                     "Open a boxed Pokemon and press Z to write it to the card;\n"
                     "it lands in " RECORD_DIR " and shows up here.");
        draw_footer("[B] Back");
        return;
    }
    const unsigned visible = 9u;
    unsigned top = record_file_selected > visible / 2u ? record_file_selected - visible / 2u : 0u;
    if (record_file_count > visible && top > record_file_count - visible)
        top = record_file_count - visible;
    for (unsigned row = 0; row < visible && top + row < record_file_count; ++row) {
        const unsigned i = top + row;
        const float y = 166.0f + row * 25.0f;
        const bool sel = i == record_file_selected;
        if (sel) gui_rect(28, y - 3, 584, 24, C_SELECT);
        char fitted[52];
        fit_text(fitted, sizeof(fitted), record_files[i].name, 48);
        gui_text(44, y, 0.64f, sel ? C_TEXT : C_MUTED, fitted);
    }
    gui_textf(38, 406, 0.55f, C_MUTED, "%u of %u", record_file_selected + 1u, record_file_count);
    draw_footer(card ? "[STICK] File  [DPAD] Jump  [B] Back  [A] Install the card"
                     : "[STICK] File  [DPAD] Jump  [B] Back  [A] Import into the slot");
}

static void handle_record_files(u32 down) {
    if (record_file_count) {
        record_file_selected = nav_index(down, record_file_selected, record_file_count);
        if ((down & PAD_BUTTON_A) && live_edit_allowed()) {
            if (import_record_file(&record_files[record_file_selected])) {
                save_dirty = true;
                set_status("Imported. It is in RAM; save to write it to the card.");
                mode = UI_BOXES;
                return;
            }
            set_status("That file is not a record this save can hold.");
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------------- save check --- */

/*
 * A pass over everything the port can verify without a legality engine: the
 * save's own checksums, every record's checksum, and the handful of values
 * that cannot legitimately hold what they hold.
 */
typedef struct CheckFinding {
    char text[72];
    bool serious;
} CheckFinding;

#define CHECK_MAX_FINDINGS 64u

static CheckFinding check_findings[CHECK_MAX_FINDINGS];
static unsigned check_records_seen, check_records_bad;

static void check_add(bool serious, const char *fmt, ...) {
    if (check_count >= CHECK_MAX_FINDINGS) return;
    CheckFinding *f = &check_findings[check_count++];
    f->serious = serious;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->text, sizeof(f->text), fmt, ap);
    va_end(ap);
}

/* Values a record cannot legitimately hold, which are what a mis-decrypted or
 * hand-edited record usually shows first. */
static void check_record(const Gen3Pokemon *p, const char *where) {
    ++check_records_seen;
    if (!p->checksum_ok) {
        ++check_records_bad;
        check_add(true, "%s: checksum does not match", where);
    }
    const unsigned national = gen3_species_national(p->species_internal);
    if (national == 0 || national > GEN3_DEX_SPECIES)
        check_add(true, "%s: species index %u is not a Pokemon", where, p->species_internal);
    unsigned ev_total = 0;
    for (unsigned i = 0; i < 6u; ++i) {
        ev_total += p->evs[i];
        if (p->ivs[i] > 31u) check_add(true, "%s: IV %u is above 31", where, i);
    }
    if (ev_total > 510u) check_add(false, "%s: EVs total %u, over the 510 cap", where, ev_total);
    if (p->level > 100u) check_add(true, "%s: level %u is above 100", where, p->level);
    if (p->met_level > 100u) check_add(false, "%s: met level %u is above 100", where, p->met_level);
    for (unsigned i = 0; i < 4u; ++i)
        if (p->moves[i] > 354u) check_add(true, "%s: move %u is not a move", where, i + 1u);
}

static void run_save_check(void) {
    check_count = 0;
    check_scroll = 0;
    check_records_seen = check_records_bad = 0;
    check_ran = true;

    if (parsed_save.kind == GEN3_KIND_GBA) {
        for (unsigned slot = 0; slot < 2u; ++slot) {
            const Gen3SlotInfo *info = &parsed_save.gba.slots[slot];
            if (info->checksum_failures)
                check_add(slot == (unsigned)parsed_save.gba.active_slot,
                          "Save slot %c: %u sector checksums bad", 'A' + slot, info->checksum_failures);
        }
        if (gen3_is_pokedex_corrupt(&parsed_save.gba))
            check_add(true, "Security key is all FF; the Pokedex is corrupt");
    } else if (!parsed_save.integrity_ok) {
        check_add(true, "Save checksums do not match (%u failures)", parsed_save.integrity_failures);
    }

    for (unsigned slot = 0; slot < gen3_any_party_count(&parsed_save); ++slot) {
        Gen3Pokemon p;
        if (!gen3_any_party_pokemon(&parsed_save, slot, &p) || !p.present) continue;
        char where[24];
        snprintf(where, sizeof(where), "Party %u", slot + 1u);
        check_record(&p, where);
    }
    for (unsigned box = 0; box < gen3_any_box_count(&parsed_save); ++box) {
        for (unsigned slot = 0; slot < 30u; ++slot) {
            Gen3Pokemon p;
            if (!gen3_any_box_pokemon(&parsed_save, box, slot, &p) || !p.present) continue;
            char where[24];
            snprintf(where, sizeof(where), "Box %u slot %u", box + 1u, slot + 1u);
            check_record(&p, where);
            if (check_count >= CHECK_MAX_FINDINGS) return;
        }
    }
}

static void show_save_check(void) {
    draw_header("SAVE CHECK");
    gui_panel(18, 84, 604, 58, C_PANEL, C_BORDER);
    if (!check_ran) {
        gui_text(38, 100, 0.8f, C_MUTED, "Press A to check this save.");
        draw_footer("[B] Back  [A] Run");
        return;
    }
    gui_textf(38, 96, 0.72f, C_TEXT, "%u records checked, %u with a bad checksum",
              check_records_seen, check_records_bad);
    gui_badge(470, 94, check_count ? "PROBLEMS" : "CLEAN", check_count ? C_RED : C_GREEN, C_BADGE_TEXT);

    gui_panel(18, 152, 604, 272, C_PANEL, C_BORDER);
    if (!check_count) {
        gui_text(38, 172, 0.8f, C_GREEN, "Nothing wrong found.");
        gui_text(38, 200, 0.58f, C_MUTED,
                 "This checks structure, not legality: it will not tell you whether a\n"
                 "Pokemon could have been obtained legitimately.");
    } else {
        const unsigned visible = 10u;
        if (check_scroll + visible > check_count)
            check_scroll = check_count > visible ? check_count - visible : 0u;
        for (unsigned row = 0; row < visible && check_scroll + row < check_count; ++row) {
            const CheckFinding *f = &check_findings[check_scroll + row];
            gui_text(38, 164.0f + row * 23.0f, 0.6f, f->serious ? C_RED : C_YELLOW, f->text);
        }
        if (check_count > visible)
            gui_textf(38, 406, 0.55f, C_MUTED, "%u of %u", check_scroll + 1u, check_count);
        if (check_count >= CHECK_MAX_FINDINGS)
            gui_text(400, 406, 0.55f, C_MUTED, "list truncated");
    }
    draw_footer("[STICK] Scroll  [DPAD] Jump  [B] Back  [A] Run again");
}

static void handle_save_check(u32 down) {
    if (down & PAD_BUTTON_A) run_save_check();
    if (check_count) {
        if ((down & PAD_BUTTON_UP) && check_scroll) --check_scroll;
        if ((down & PAD_BUTTON_DOWN) && check_scroll + 1u < check_count) ++check_scroll;
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------------- shadow table --- */

static void show_shadows(void) {
    draw_header("SHADOW TABLE");
    const unsigned count = gen3_any_shadow_count(&parsed_save);
    if (!count) { gui_text(60, 120, 0.9f, C_MUTED, "This save has no shadow table."); draw_footer("[B] Back"); return; }
    if (shadow_index >= count) shadow_index = 0;

    gui_panel(18, 84, 280, 340, C_PANEL, C_BORDER);
    const unsigned visible = 19u;
    unsigned top = shadow_index > visible / 2u ? shadow_index - visible / 2u : 0u;
    if (count > visible && top > count - visible) top = count - visible;
    for (unsigned row = 0; row < visible && top + row < count; ++row) {
        const unsigned i = top + row;
        const float y = 94.0f + row * 16.0f;
        const bool sel = i == shadow_index;
        if (sel) gui_rect(26, y - 2, 262, 16, C_SELECT);
        Gen3ShadowEntry e = {0};
        gen3_any_shadow_entry(&parsed_save, i, &e);
        gui_textf(36, y, 0.5f, C_MUTED, "%3u", i);
        if (!e.present) { gui_text(76, y, 0.5f, C_FAINT, "empty"); continue; }
        gui_text(76, y, 0.5f, sel ? C_TEXT : C_MUTED,
                 gen3_species_name(gen3_species_internal_from_national(e.species)));
        gui_text(216, y, 0.45f, e.purified ? C_GREEN : C_YELLOW, e.purified ? "pure" : "shadow");
    }
    gui_textf(36, 406, 0.55f, C_MUTED, "%u of %u", shadow_index + 1u, count);

    gui_panel(312, 84, 310, 340, C_PANEL, C_BORDER);
    Gen3ShadowEntry e = {0};
    gen3_any_shadow_entry(&parsed_save, shadow_index, &e);
    if (!e.present) {
        gui_textf(330, 96, 0.86f, C_ACCENT, "Entry %u", shadow_index);
        gui_text(330, 140, 0.8f, C_MUTED, "No Shadow Pokemon here.");
    } else {
        gui_pokemon_sprite(330, 96, 72, 72, e.species);
        gui_text(414, 104, 0.86f, C_TEXT,
                 gen3_species_name(gen3_species_internal_from_national(e.species)));
        gui_badge(414, 134, e.purified ? "PURIFIED" : "SHADOW", e.purified ? C_GREEN : C_YELLOW, C_BADGE_TEXT);
        gui_badge(520, 134, e.snagged ? "SNAGGED" : "WILD", e.snagged ? C_ACCENT : C_BADGE_OFF, C_BADGE_TEXT);
        gui_textf(330, 186, 0.66f, C_MUTED, "PID %08lX", (unsigned long)e.pid);
        /* The gauge counts up to zero, so it reads as how far there is to go. */
        gui_textf(330, 212, 0.7f, C_TEXT, "Heart gauge %ld", (long)e.purification);
        gui_textf(330, 238, 0.62f, C_MUTED, "Shadow EXP %lu", (unsigned long)e.experience);
        gui_text(330, 270, 0.6f, C_MUTED, "IVs");
        static const char *const stat_names[6] = { "HP", "Atk", "Def", "Spe", "SpA", "SpD" };
        for (unsigned i = 0; i < 6u; ++i) {
            const float y = 292.0f + i * 20.0f;
            gui_text(330, y, 0.58f, C_MUTED, stat_names[i]);
            gui_textf(392, y, 0.58f, C_TEXT, "%u", e.ivs[i]);
        }
    }
    draw_footer("[STICK] Entry  [DPAD] Jump  [LEFT]/[RIGHT] Gauge  [B] Back  [X] Purified");
}

static void handle_shadows(u32 down) {
    const unsigned count = gen3_any_shadow_count(&parsed_save);
    if (!count) { if (down & PAD_BUTTON_B) mode = UI_TOOLS; return; }
    shadow_index = nav_index(down, shadow_index, count);

    if (live_edit_allowed()) {
        Gen3ShadowEntry e = {0};
        if (gen3_any_shadow_entry(&parsed_save, shadow_index, &e) && e.present) {
            int direction = 0;
            long long step = 1;
            if (down & PAD_BUTTON_LEFT) direction = -1;
            if (down & PAD_BUTTON_RIGHT) direction = 1;
            step = nav_step(down, 1, 100);
            if (direction) {
                /* The gauge runs from a negative starting value up to zero. */
                const long long next = clamp_ll((long long)e.purification + direction * step, -32768, 0);
                if (gen3_any_set_shadow_purification(&parsed_save, shadow_index, (int32_t)next))
                    save_dirty = true;
            }
            if (down & PAD_BUTTON_X) {
                if (gen3_any_set_shadow_purified(&parsed_save, shadow_index, !e.purified)) save_dirty = true;
            }
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* --------------------------------------------------------- game clock --- */

static void show_clock(void) {
    draw_header("GAME CLOCK");
    gui_panel(60, 96, 520, 280, C_PANEL, C_BORDER);
    static const char *const labels[8] = {
        "Started: days", "Started: hours", "Started: minutes", "Started: seconds",
        "Elapsed: days", "Elapsed: hours", "Elapsed: minutes", "Elapsed: seconds",
    };
    Gen3Clock initial = {0}, elapsed = {0};
    gen3_clock(&parsed_save.gba, false, &initial);
    gen3_clock(&parsed_save.gba, true, &elapsed);
    const unsigned values[8] = {
        initial.day, initial.hour, initial.minute, initial.second,
        elapsed.day, elapsed.hour, elapsed.minute, elapsed.second,
    };
    for (unsigned i = 0; i < 8u; ++i) {
        const float y = 112.0f + i * 32.0f;
        const bool sel = i == clock_field;
        if (sel) gui_rect(72, y - 3, 496, 29, C_SELECT);
        gui_text(90, y, 0.74f, sel ? C_TEXT : C_MUTED, labels[i]);
        gui_textf(420, y, 0.78f, C_TEXT, "%u", values[i]);
        if (i == 3) gui_rect(72, y + 28, 496, 1, C_BORDER);
    }
    gui_text(90, 384, 0.55f, C_MUTED,
             "Ruby, Sapphire and Emerald only. The berry timer and several events\nrun off the elapsed clock.");
    draw_footer("[STICK] Field/+/-  [DPAD] Jump  [B] Back");
}

static void handle_clock(u32 down) {
    clock_field = nav_index(down, clock_field, 8u);

    int direction = 0;
    const bool coarse = (down & UI_COARSE) != 0;
    if (down & PAD_BUTTON_LEFT) direction = -1;
    if (down & PAD_BUTTON_RIGHT) direction = 1;
    if (direction && live_edit_allowed()) {
        const bool is_elapsed = clock_field >= 4u;
        Gen3Clock c = {0};
        if (gen3_clock(&parsed_save.gba, is_elapsed, &c)) {
            const long long step = (long long)direction * (coarse ? 10 : 1);
            switch (clock_field % 4u) {
                case 0: c.day = (uint16_t)clamp_ll((long long)c.day + step, 0, 65535); break;
                case 1: c.hour = (uint8_t)clamp_ll((long long)c.hour + step, 0, 23); break;
                case 2: c.minute = (uint8_t)clamp_ll((long long)c.minute + step, 0, 59); break;
                default: c.second = (uint8_t)clamp_ll((long long)c.second + step, 0, 59); break;
            }
            if (gen3_set_clock(&parsed_save.gba, is_elapsed, &c)) save_dirty = true;
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ---------------------------------------------------------- box layout --- */

#define LAYOUT_ROWS 11u

static void show_box_layout(void) {
    draw_header("BOX LAYOUT");
    const unsigned boxes = gen3_any_box_count(&parsed_save);
    if (!boxes) { gui_text(60, 120, 0.9f, C_MUTED, "This save has no storage boxes."); draw_footer("[B] Back"); return; }

    gui_panel(18, 84, 360, 340, C_PANEL, C_BORDER);
    unsigned top = layout_box > LAYOUT_ROWS / 2u ? layout_box - LAYOUT_ROWS / 2u : 0u;
    if (boxes > LAYOUT_ROWS && top > boxes - LAYOUT_ROWS) top = boxes - LAYOUT_ROWS;
    for (unsigned row = 0; row < LAYOUT_ROWS && top + row < boxes; ++row) {
        const unsigned b = top + row;
        const float y = 96.0f + row * 29.0f;
        const bool sel = b == layout_box;
        if (sel) gui_rect(28, y - 2, 340, 27, C_SELECT);
        gui_textf(40, y, 0.6f, C_MUTED, "%2u", b + 1u);
        char name[28];
        gen3_any_box_name(&parsed_save, b, name, sizeof(name));
        char fitted[24]; fit_text(fitted, sizeof(fitted), name, 20);
        gui_text(78, y, 0.7f, sel ? C_TEXT : C_MUTED, fitted);
    }

    gui_panel(392, 84, 230, 340, C_PANEL, C_BORDER);
    const uint8_t wallpaper = gen3_any_box_wallpaper(&parsed_save, layout_box);
    draw_box_wallpaper(406, 100, 202, 130, wallpaper);
    char name[28];
    gen3_any_box_name(&parsed_save, layout_box, name, sizeof(name));
    gui_textf(406, 244, 0.8f, C_TEXT, "Box %u", layout_box + 1u);
    char fitted[22]; fit_text(fitted, sizeof(fitted), name, 18);
    gui_text(406, 268, 0.72f, C_ACCENT, fitted);
    if (wallpaper < GEN3_WALLPAPER_COUNT)
        gui_text(406, 296, 0.62f, C_MUTED, gen3_wallpaper_name(wallpaper));
    else
        gui_text(406, 296, 0.62f, C_MUTED, "no wallpaper in this format");
    if (parsed_save.kind == GEN3_KIND_RSBOX)
        gui_text(406, 322, 0.55f, C_MUTED, "Two boxes share one grid,\nand one wallpaper.");
    if (!gen3_any_box_name_length(&parsed_save))
        gui_text(406, 366, 0.55f, C_MUTED, "This format has no\neditable box names.");

    draw_footer("[STICK] Box  [DPAD] Wallpaper  [B] Back  [A] Rename");
}

static void handle_box_layout(u32 down) {
    const unsigned boxes = gen3_any_box_count(&parsed_save);
    if (!boxes) { mode = UI_TOOLS; return; }
    layout_box = nav_index(down, layout_box, boxes);
    if ((down & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) && live_edit_allowed()) {
        const int direction = (down & PAD_BUTTON_RIGHT) ? 1 : -1;
        const uint8_t current = gen3_any_box_wallpaper(&parsed_save, layout_box);
        if (current < GEN3_WALLPAPER_COUNT) {
            const uint8_t next = (uint8_t)(((int)current + direction + GEN3_WALLPAPER_COUNT) % GEN3_WALLPAPER_COUNT);
            if (gen3_any_set_box_wallpaper(&parsed_save, layout_box, next)) {
                save_dirty = true;
                set_status(gen3_wallpaper_name(next));
            }
        }
    }
    if ((down & PAD_BUTTON_A) && live_edit_allowed()) {
        /* Only the cartridge saves store their box names in Generation III
         * text; the GameCube ones use UTF-16, which this keyboard cannot type. */
        if (parsed_save.kind != GEN3_KIND_GBA) {
            set_status("Renaming GameCube boxes needs a UTF-16 keyboard.");
        } else {
            uint8_t raw[GEN3_BOX_NAME_LEN];
            gen3_box_name_raw(&parsed_save.gba, layout_box, raw);
            char title[48];
            snprintf(title, sizeof(title), "BOX %u NAME", layout_box + 1u);
            kb_box_index = layout_box;
            kb_load(raw, sizeof(raw), GEN3_BOX_NAME_LEN - 1u, KB_BOX_NAME, UI_BOX_LAYOUT, title);
            return;
        }
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* ------------------------------------------------------------- records --- */

#define RECORD_ROWS 12u

static void show_records(void) {
    draw_header("GAME RECORDS");
    const unsigned count = gen3_record_count(parsed_save.gba.game);
    gui_panel(18, 84, 604, 340, C_PANEL, C_BORDER);
    /* Keep the cursor roughly centred until the list runs out at either end. */
    unsigned top = record_selected > RECORD_ROWS / 2u ? record_selected - RECORD_ROWS / 2u : 0u;
    if (count > RECORD_ROWS && top > count - RECORD_ROWS) top = count - RECORD_ROWS;
    for (unsigned row = 0; row < RECORD_ROWS && top + row < count; ++row) {
        const unsigned i = top + row;
        const float y = 96.0f + row * 25.0f;
        const bool sel = i == record_selected;
        if (sel) gui_rect(28, y - 2, 584, 24, C_SELECT);
        gui_textf(40, y, 0.58f, C_MUTED, "%2u", i);
        gui_text(76, y, 0.7f, sel ? C_TEXT : C_MUTED, gen3_record_name(parsed_save.gba.game, i));
        gui_textf(470, y, 0.7f, C_TEXT, "%lu", (unsigned long)gen3_record(&parsed_save.gba, i));
    }
    gui_textf(40, 400, 0.58f, C_MUTED, "%u of %u", record_selected + 1u, count);
    draw_footer("[STICK] Record/+/-  [DPAD] Jump  [B] Back  [X] Zero");
}

static void adjust_record(int direction, bool coarse) {
    if (!live_edit_allowed()) return;
    const long long step = coarse ? 1000 : 1;
    const long long now = (long long)gen3_record(&parsed_save.gba, record_selected);
    const long long next = clamp_ll(now + (long long)direction * step, 0, 0xFFFFFFFFLL);
    if (gen3_set_record(&parsed_save.gba, record_selected, (uint32_t)next)) save_dirty = true;
}

static void handle_records(u32 down) {
    const unsigned count = gen3_record_count(parsed_save.gba.game);
    if (!count) { mode = UI_TOOLS; return; }
    record_selected = nav_index(down, record_selected, count);
    if (down & PAD_BUTTON_LEFT) adjust_record(-1, (down & UI_COARSE) != 0);
    if (down & PAD_BUTTON_RIGHT) adjust_record(1, (down & UI_COARSE) != 0);

    if ((down & PAD_BUTTON_X) && live_edit_allowed()) {
        if (gen3_set_record(&parsed_save.gba, record_selected, 0)) save_dirty = true;
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

/* -------------------------------------------------------- hall of fame --- */

static void show_hall_of_fame(void) {
    draw_header("HALL OF FAME");
    const unsigned entries = gen3_hof_entry_count(&parsed_save.gba);
    gui_panel(18, 84, 604, 60, C_PANEL, C_BORDER);
    if (!entries) {
        gui_text(38, 100, 0.86f, C_MUTED, "This save has never entered the Hall of Fame.");
        draw_footer("[B] Back");
        return;
    }
    gui_textf(38, 96, 0.86f, C_TEXT, "Entry %u of %u", hof_entry + 1u, entries);
    gui_text(38, 122, 0.58f, C_MUTED, "Read-only: the Hall of Fame lives outside the main save blocks.");

    gui_panel(18, 154, 604, 274, C_PANEL, C_BORDER);
    for (unsigned i = 0; i < GEN3_HOF_TEAM_SIZE; ++i) {
        Gen3HofMember m;
        if (!gen3_hof_member(&parsed_save.gba, hof_entry, i, &m)) continue;
        const float x = 34.0f + (i % 3u) * 196.0f;
        const float y = 168.0f + (i / 3u) * 128.0f;
        gui_panel(x, y, 184, 116, C_PANEL2, m.shiny ? C_YELLOW : C_BORDER);
        if (!m.present) { gui_text(x + 14, y + 46, 0.72f, C_MUTED, "empty"); continue; }
        gui_pokemon_sprite(x + 10, y + 8, 60, 60, m.species);
        gui_text(x + 78, y + 14, 0.7f, C_TEXT,
                 gen3_species_name(gen3_species_internal_from_national(m.species)));
        gui_textf(x + 78, y + 38, 0.66f, C_MUTED, "Lv %u", m.level);
        gui_textf(x + 14, y + 76, 0.64f, C_MUTED, "%s", m.nickname);
        gui_textf(x + 14, y + 96, 0.58f, C_MUTED, "ID %05u", m.tid);
        if (m.shiny) gui_text(x + 140, y + 96, 0.58f, C_YELLOW, "SHINY");
    }
    draw_footer("[STICK] Entry  [DPAD] Jump  [B] Back");
}

static void handle_hall_of_fame(u32 down) {
    const unsigned entries = gen3_hof_entry_count(&parsed_save.gba);
    if (entries) {
        if (down & (PAD_BUTTON_UP | PAD_BUTTON_LEFT)) hof_entry = hof_entry ? hof_entry - 1u : entries - 1u;
        if (down & (PAD_BUTTON_DOWN | PAD_BUTTON_RIGHT)) hof_entry = (hof_entry + 1u) % entries;
    }
    if (down & PAD_BUTTON_B) mode = UI_TOOLS;
}

static void show_trainer_edit(void) {
    char title[80]; snprintf(title, sizeof(title), "TRAINER EDITOR - %s", gen3_any_game_name(&parsed_save));
    draw_header(title);
    gui_panel(72, 82, 496, 346, C_PANEL, C_BORDER);
    /* The name used to be a heading here; it is a row of its own now, so the
     * panel is all rows and the badge strip sits below them. */
    unsigned count = trainer_field_count();
    uint64_t play = parsed_save.played_seconds;
    for (unsigned i=0;i<count;++i) {
        const TrainerField f = trainer_field_at(i);
        float y = 96.0f + i * 26.0f;
        bool sel = i == trainer_edit_field;
        if (sel) gui_rect(88, y + 1, 464, 24, C_SELECT);
        gui_text(104, y, 0.72f, sel ? C_TEXT : C_MUTED, trainer_field_label(f));
        switch (f) {
            case TF_NAME: gui_text(340,y,0.76f,C_TEXT,parsed_save.trainer_name[0]?parsed_save.trainer_name:"-"); break;
            case TF_TID: gui_textf(340,y,0.76f,C_TEXT,"%05u",parsed_save.tid); break;
            case TF_SID: gui_textf(340,y,0.76f,C_TEXT,"%05u",parsed_save.sid); break;
            case TF_GENDER: gui_text(340,y,0.76f,C_TEXT,parsed_save.trainer_gender==1?"Female":"Male"); break;
            case TF_HOURS: gui_textf(340,y,0.76f,C_TEXT,"%lu",(unsigned long)(play/3600u)); break;
            case TF_MIN: gui_textf(340,y,0.76f,C_TEXT,"%u",(unsigned)((play/60u)%60u)); break;
            case TF_SEC: gui_textf(340,y,0.76f,C_TEXT,"%u",(unsigned)(play%60u)); break;
            case TF_MONEY: gui_textf(340,y,0.76f,C_TEXT,"$%lu",(unsigned long)parsed_save.money); break;
            case TF_COINS: gui_textf(340,y,0.76f,C_TEXT,"%u",gen3_coins(&parsed_save.gba)); break;
            case TF_BADGES: gui_textf(340,y,0.76f,C_TEXT,"%u of %u",
                                      gen3_any_badge_count(&parsed_save), GEN3_BADGE_COUNT); break;
            case TF_RIVAL: {
                char rn[24];
                gen3_rival_name(&parsed_save.gba, rn, sizeof(rn));
                gui_text(340,y,0.76f,C_TEXT,rn[0]?rn:"-");
                break;
            }
        }
    }
    if (gen3_any_has_pokedex(&parsed_save)) {
        gui_text(104, 396, 0.6f,
                 trainer_field_at(trainer_edit_field) == TF_BADGES ? C_ACCENT : C_MUTED,
                 "BADGES");
        for (unsigned b = 0; b < GEN3_BADGE_COUNT; ++b) {
            const bool got = gen3_any_badge(&parsed_save, b);
            gui_rect(176.0f + b * 26.0f, 394.0f, 20.0f, 18.0f, got ? C_GREEN : C_PANEL2);
            gui_textf(182.0f + b * 26.0f, 395.0f, 0.55f, got ? C_BADGE_TEXT : C_MUTED, "%u", b + 1u);
        }
        gui_textf(400, 396, 0.58f, C_MUTED, "%u of 8",
                  gen3_any_badge_count(&parsed_save));
    }

    if (parsed_save.kind == GEN3_KIND_GBA)
        draw_footer("[STICK] Field/+/-  [DPAD] Jump  [B] Back  [A] Rename  [Y] Inventory  [X] Save");
    else
        draw_footer("[STICK] Field/+/-  [DPAD] Jump  [B] Back  [X] Save");
}

static void adjust_trainer(int direction, bool coarse) {
    long long d = (long long)direction * (coarse ? 10 : 1);
    uint64_t play = parsed_save.played_seconds;
    uint64_t hours = play / 3600u;
    unsigned min = (unsigned)((play / 60u) % 60u), sec = (unsigned)(play % 60u);
    bool changed = false;
    switch (trainer_field_at(trainer_edit_field)) {
        case TF_TID: changed=gen3_any_set_tid(&parsed_save,(uint16_t)clamp_ll((long long)parsed_save.tid+d,0,65535)); break;
        case TF_SID: changed=gen3_any_set_sid(&parsed_save,(uint16_t)clamp_ll((long long)parsed_save.sid+d,0,65535)); break;
        case TF_GENDER: changed=gen3_any_set_trainer_gender(&parsed_save,parsed_save.trainer_gender==1?0:1); break;
        case TF_HOURS: hours=(uint64_t)clamp_ll((long long)hours+d,0,65535); changed=gen3_any_set_played_seconds(&parsed_save,hours*3600u+(uint64_t)min*60u+sec); break;
        case TF_MIN: min=(unsigned)clamp_ll((long long)min+d,0,59); changed=gen3_any_set_played_seconds(&parsed_save,hours*3600u+(uint64_t)min*60u+sec); break;
        case TF_SEC: sec=(unsigned)clamp_ll((long long)sec+d,0,59); changed=gen3_any_set_played_seconds(&parsed_save,hours*3600u+(uint64_t)min*60u+sec); break;
        case TF_MONEY: {
            long long money_step = coarse ? 10000 : 100;
            changed=gen3_any_set_money(&parsed_save,(uint32_t)clamp_ll((long long)parsed_save.money+(long long)direction*money_step,0,9999999));
            break;
        }
        case TF_COINS: if (parsed_save.kind==GEN3_KIND_GBA) {
            long long coin_step = coarse ? 100 : 1;
            gen3_set_coins(&parsed_save.gba,(uint16_t)clamp_ll((long long)gen3_coins(&parsed_save.gba)+(long long)direction*coin_step,0,9999)); changed=true;
        }
        break;
        case TF_BADGES: {
            /* The games award badges in order, so the editable thing is how
             * many are held rather than which. */
            const long long have = (long long)gen3_any_badge_count(&parsed_save);
            const long long want = clamp_ll(have + d, 0, (long long)GEN3_BADGE_COUNT);
            for (unsigned b = 0; b < GEN3_BADGE_COUNT; ++b)
                gen3_any_set_badge(&parsed_save, b, (long long)b < want);
            changed = want != have;
            break;
        }
        case TF_NAME:
        case TF_RIVAL:
        default:
            break;
    }
    if(changed){ if(parsed_save.kind==GEN3_KIND_GBA) refresh_gba_summary(); save_dirty=true; }
}

/* Each format has a different set of pouches, so stepping skips the ones this
 * save does not have rather than landing on an empty screen. */
static void inventory_step_pocket(int direction) {
    for(unsigned tries=0;tries<GEN3_POCKET_COUNT;++tries){
        inventory_pocket=(Gen3Pocket)(((int)inventory_pocket+direction+GEN3_POCKET_COUNT)%GEN3_POCKET_COUNT);
        if(gen3_any_pocket_capacity(&parsed_save,inventory_pocket)) break;
    }
    inventory_slot=0;
}

static void show_inventory_edit(void) {
    char inv_title[64];
    snprintf(inv_title,sizeof(inv_title),"INVENTORY - %s",gen3_any_kind_name(parsed_save.kind));
    draw_header(inv_title);
    gui_panel(22, 92, 596, 338, C_PANEL, C_BORDER);
    gui_text(38, 103, 0.82f, C_ACCENT, gen3_pocket_name(inventory_pocket));
    unsigned cap=gen3_any_pocket_capacity(&parsed_save,inventory_pocket);
    gui_textf(438,106,0.62f,C_MUTED,"%u slots",cap);
    gui_badge(523,101,inventory_field==0?"ITEM":"QTY",C_ACCENT,C_BADGE_TEXT);

    unsigned first=(inventory_slot/10u)*10u;
    for(unsigned r=0;r<10 && first+r<cap;++r){
        unsigned slot=first+r;
        Gen3ItemSlot it={0}; gen3_any_get_item_slot(&parsed_save,inventory_pocket,slot,&it);
        float y=139.0f+r*26.0f;
        bool sel=slot==inventory_slot;
        if(sel) gui_rect(34,y-2,568,22,C_SELECT);
        gui_textf(48,y,0.62f,sel?C_TEXT:C_MUTED,"%02u",slot+1);
        gui_textf(108,y,0.62f,C_TEXT,"%s",gen3_item_name_for(parsed_save.kind,it.item_id));
        gui_textf(390,y,0.62f,C_TEXT,"Qty %u",it.quantity);
        if(it.item_id==0) gui_text(514,y,0.58f,C_MUTED,"empty");
    }
    if (parsed_save.kind == GEN3_KIND_GBA)
        gui_text(38,407,0.52f,C_MUTED,"PC quantities plain; Bag quantities use the save security key where required.");
    else
        gui_textf(38,407,0.52f,C_MUTED,"GameCube pouches store item and quantity big-endian, unmasked. Max %u per slot.",
                  gen3_any_pocket_max_quantity(&parsed_save,inventory_pocket));
    draw_footer("[STICK] Slot/Change  [DPAD] Pocket  [B] Trainer  [Y] Save  [X] Item/Qty");
}

static void adjust_inventory(int direction, bool coarse) {
    Gen3ItemSlot it={0};
    if(!gen3_any_get_item_slot(&parsed_save,inventory_pocket,inventory_slot,&it)) return;
    const long long step=coarse?10:1;
    const long long max_qty=gen3_any_pocket_max_quantity(&parsed_save,inventory_pocket);
    if(inventory_field==0) it.item_id=(uint16_t)clamp_ll((long long)it.item_id+(long long)direction*step,0,376);
    else it.quantity=(uint16_t)clamp_ll((long long)it.quantity+(long long)direction*step,0,max_qty);
    if(gen3_any_set_item_slot(&parsed_save,inventory_pocket,inventory_slot,it.item_id,it.quantity)) save_dirty=true;
}

static const char *pkm_basic_labels[] = {"Species", "Held item", "Experience", "Friendship", "PID", "Trainer ID", "Secret ID", "Level"};
static const char *pkm_move_labels[] = {"Move 1", "Move 2", "Move 3", "Move 4", "PP 1", "PP 2", "PP 3", "PP 4"};
static const char *pkm_iv_labels[] = {"IV HP", "IV Atk", "IV Def", "IV Spe", "IV SpA", "IV SpD"};
static const char *pkm_ev_labels[] = {"EV HP", "EV Atk", "EV Def", "EV Spe", "EV SpA", "EV SpD"};
static const char *pkm_status_labels[] = {"Egg", "Nature", "Shiny", "Shadow", "Shadow ID", "Heart gauge"};
static const char *pkm_origin_labels[] = {"Origin game", "Ball", "Met level", "Met location", "OT gender", "Fateful"};
static const char *pkm_cosmetic_labels[] = {"Language", "Ability slot", "Markings", "Pokerus strain", "Pokerus days", "PP Ups (all)"};
static const char *pkm_contest_labels[] = {"Cool", "Beauty", "Cute", "Smart", "Tough", "Sheen"};
static const char *pkm_name_labels[] = {"Nickname", "OT name"};

#define PKM_PAGE_COUNT 12u
#define PKM_FIELD_NICKNAME 52u
#define PKM_FIELD_OT_NAME 53u
/* Five contest ribbon levels, then the twelve single-bit ribbons split over
 * two pages so every row stays on screen. */
#define PKM_FIELD_CONTEST_RIBBON 54u
#define PKM_FIELD_RIBBON_FLAG 59u
static const unsigned pkm_page_starts[] = {0, 8, 16, 22, 28, 34, 40, 46, 52, 54, 59, 65};
static const unsigned pkm_page_counts[] = {8, 8, 6, 6, 6, 6, 6, 6, 2, 5, 6, 6};
static unsigned page_start(unsigned page) { return pkm_page_starts[page < PKM_PAGE_COUNT ? page : 0]; }
static unsigned page_count(unsigned page) { return pkm_page_counts[page < PKM_PAGE_COUNT ? page : 0]; }

/* Markings are four independent flags; show which are lit rather than a number. */
static void marking_text(uint8_t markings, char *out, size_t n) {
    snprintf(out, n, "%c%c%c%c",
             (markings & 1u) ? 'O' : '-', (markings & 2u) ? 'S' : '-',
             (markings & 4u) ? 'T' : '-', (markings & 8u) ? 'H' : '-');
}

static void pkm_value_text(unsigned field, char *out, size_t n) {
    if (field == 0) snprintf(out,n,"#%03u  %s",gen3_species_national(edit_pkm.species_internal),gen3_species_name(edit_pkm.species_internal));
    else if (field == 1) snprintf(out,n,"%u  %s",edit_pkm.held_item,gen3_item_name_for(parsed_save.kind,edit_pkm.held_item));
    else if (field == 2) snprintf(out,n,"%lu",(unsigned long)edit_pkm.experience);
    else if (field == 3) snprintf(out,n,"%u",edit_pkm.friendship);
    else if (field == 4) snprintf(out,n,"%08lX",(unsigned long)edit_pkm.pid);
    else if (field == 5) snprintf(out,n,"%05u",edit_pkm.tid);
    else if (field == 6) snprintf(out,n,"%05u",edit_pkm.sid);
    else if (field == 7) {
        /* Box records carry no level byte, so show the level their experience
         * puts them at. It is not editable there; the experience field is. */
        if (edit_source==PKM_EDIT_PARTY || gen3_any_has_shadow(parsed_save.kind))
            snprintf(out,n,"%u",edit_pkm.level);
        else
            snprintf(out,n,"%u  from exp",gen3_effective_level(&edit_pkm));
    }
    else if (field >= 8 && field <= 11) snprintf(out,n,"%u  %s",edit_pkm.moves[field-8],gen3_move_name(edit_pkm.moves[field-8]));
    else if (field >= 12 && field <= 15) snprintf(out,n,"%u",edit_pkm.pp[field-12]);
    else if (field >= 16 && field <= 21) snprintf(out,n,"%u",edit_pkm.ivs[field-16]);
    else if (field >= 22 && field <= 27) snprintf(out,n,"%u",edit_pkm.evs[field-22]);
    else if (field == 28) snprintf(out,n,"%s",edit_pkm.is_egg?"Yes":"No");
    else if (field == 29) snprintf(out,n,"%s",gen3_nature_name(gen3_nature(&edit_pkm)));
    else if (field == 30) snprintf(out,n,"%s",gen3_is_shiny(&edit_pkm)?"Yes":"No");
    else if (field == 31) snprintf(out,n,"%s",gen3_any_has_shadow(parsed_save.kind)?(edit_pkm.is_shadow?"Yes":"No"):"N/A");
    else if (field == 32) { if(gen3_any_has_shadow(parsed_save.kind))snprintf(out,n,"%u",edit_pkm.shadow_id);else snprintf(out,n,"N/A"); }
    /* XD keeps the heart gauge in the save's shadow table, not the record. */
    else if (field == 33) { if(parsed_save.kind==GEN3_KIND_COLOSSEUM)snprintf(out,n,"%ld",(long)edit_pkm.purification);else snprintf(out,n,"N/A"); }
    else if (field == 34) snprintf(out,n,"%u  %s",edit_pkm.origin_game,gen3_origin_game_name(edit_pkm.origin_game));
    else if (field == 35) snprintf(out,n,"%u  %s",edit_pkm.ball,gen3_ball_name(edit_pkm.ball));
    else if (field == 36) snprintf(out,n,"%u",edit_pkm.met_level);
    else if (field == 37) snprintf(out,n,"%u  %s",edit_pkm.met_location,
                                   gen3_met_location_name(edit_pkm.met_location,
                                                          parsed_save.kind != GEN3_KIND_GBA));
    else if (field == 38) snprintf(out,n,"%s",edit_pkm.ot_gender?"Female":"Male");
    else if (field == 39) snprintf(out,n,"%s",edit_pkm.fateful?"Yes":"No");
    else if (field == 40) snprintf(out,n,"%u  %s",edit_pkm.language,gen3_language_name(edit_pkm.language));
    else if (field == 41) snprintf(out,n,"%u",edit_pkm.ability_bit?2u:1u);
    else if (field == 42) marking_text(edit_pkm.markings,out,n);
    else if (field == 43) snprintf(out,n,"%u",gen3_pokerus_strain(&edit_pkm));
    else if (field == 44) snprintf(out,n,"%u",gen3_pokerus_days(&edit_pkm));
    else if (field == 45) snprintf(out,n,"%u/%u/%u/%u",
                                  gen3_pp_up_count(&edit_pkm,0),gen3_pp_up_count(&edit_pkm,1),
                                  gen3_pp_up_count(&edit_pkm,2),gen3_pp_up_count(&edit_pkm,3));
    else if (field >= 46 && field <= 51) snprintf(out,n,"%u",edit_pkm.contest[field-46]);
    else if (field == PKM_FIELD_NICKNAME) snprintf(out,n,"%s",edit_pkm.nickname);
    else if (field == PKM_FIELD_OT_NAME) snprintf(out,n,"%s",edit_pkm.ot_name);
    else if (field >= PKM_FIELD_CONTEST_RIBBON && field < PKM_FIELD_RIBBON_FLAG)
        snprintf(out,n,"%s",gen3_contest_ribbon_level_name(
                     gen3_contest_ribbon(&edit_pkm, field - PKM_FIELD_CONTEST_RIBBON)));
    else if (field >= PKM_FIELD_RIBBON_FLAG &&
             field < PKM_FIELD_RIBBON_FLAG + GEN3_RIBBON_FLAG_COUNT)
        snprintf(out,n,"%s",gen3_ribbon_flag(&edit_pkm, field - PKM_FIELD_RIBBON_FLAG) ? "Yes" : "No");
    else snprintf(out,n,"-");
}

static const char *pkm_page_name(unsigned page) {
    static const char *const names[] = {"BASIC", "MOVES / PP", "IVs", "EVs", "STATUS",
                                        "ORIGIN", "COSMETIC", "CONTEST", "NAMES",
                                        "CONTEST RIBBONS", "RIBBONS I", "RIBBONS II"};
    return names[page < PKM_PAGE_COUNT ? page : 0];
}

static const char *pkm_field_label(unsigned page, unsigned index) {
    if(page==0)return pkm_basic_labels[index];
    if(page==1)return pkm_move_labels[index];
    if(page==2)return pkm_iv_labels[index];
    if(page==3)return pkm_ev_labels[index];
    if(page==4)return pkm_status_labels[index];
    if(page==5)return pkm_origin_labels[index];
    if(page==6)return pkm_cosmetic_labels[index];
    if(page==7)return pkm_contest_labels[index];
    if(page==8)return pkm_name_labels[index];
    if(page==9)return gen3_contest_ribbon_name(index);
    return gen3_ribbon_flag_name((page == 10 ? 0u : 6u) + index);
}

/*
 * Pokedex editor. The list is long, so it scrolls a page at a time and shows
 * the sprite for whatever is selected - picking a species out of 386 by name
 * alone on a television is unpleasant.
 */
#define EVENT_ROWS 10u

/*
 * Event flags and constants, by number. PKHeX labels the well-known ones from
 * per-game tables this port does not carry, so this is the equivalent of its
 * "Research" tab: raw, honest, and enough to compare two saves or flip a flag
 * you have already identified elsewhere.
 */
static void show_events(void) {
    const bool work = event_show_work;
    const unsigned total = work ? gen3_any_event_work_count(&parsed_save)
                                : gen3_any_event_flag_count(&parsed_save);
    const Gen3Game game = parsed_save.gba.game;
    char title[80];
    snprintf(title, sizeof(title), "%s - %s", work ? "EVENT CONSTANTS" : "EVENT FLAGS",
             gen3_any_game_name(&parsed_save));
    draw_header(title);
    gui_panel(18, 88, 604, 334, C_PANEL, C_BORDER);

    gui_textf(30, 101, 0.8f, C_ACCENT, "%s 0 - %u", work ? "Constant" : "Flag",
              total ? total - 1u : 0u);
    if (!work)
        gui_textf(360, 103, 0.62f, C_MUTED, "%u set", gen3_any_event_flag_count(&parsed_save) ?
                  gen3_event_flags_set(&parsed_save.gba) : 0u);
    gui_rect(28, 122, 584, 1, C_BORDER);

    if (!total) {
        gui_text(36, 150, 0.8f, C_MUTED, "This save format has no event data.");
        draw_footer("[B] Back  [X] Flags/Constants");
        return;
    }

    unsigned first = event_selected >= EVENT_ROWS / 2u ? event_selected - EVENT_ROWS / 2u : 0u;
    if (first + EVENT_ROWS > total) first = total - EVENT_ROWS;
    for (unsigned row = 0; row < EVENT_ROWS; ++row) {
        const unsigned index = first + row;
        const float y = 134.0f + row * 25.0f;
        const bool sel = index == event_selected;
        if (sel) gui_rect(28, y - 3, 584, 24, C_SELECT);
        gui_textf(38, y, 0.66f, C_MUTED, "%4u", index);

        const Gen3EventName *named;
        if (work) {
            const uint16_t v = gen3_any_event_work(&parsed_save, index);
            gui_textf(96, y, 0.66f, sel ? C_TEXT : C_MUTED, "%5u", v);
            named = gen3_event_work_entry(game, index);
            const char *value_name = gen3_event_work_value_name(game, index, v);
            if (value_name) {
                char shown[64];
                fit_text_px(shown, sizeof(shown), value_name, 120.0f, 0.6f);
                gui_text(166, y + 1, 0.6f, C_GREEN, shown);
            }
        } else {
            const bool on = gen3_any_event_flag(&parsed_save, index);
            gui_text(96, y, 0.66f, on ? C_GREEN : C_MUTED, on ? "SET" : "clear");
            named = gen3_event_flag_entry(game, index);
        }

        /* The label column. PKHeX names most of these; anything it does not
         * stays blank rather than pretending the number means something. */
        const float label_x = work ? 296.0f : 166.0f;
        if (named) {
            char shown[128];
            fit_text_px(shown, sizeof(shown), named->name, 606.0f - label_x, 0.6f);
            gui_text(label_x, y + 1, 0.6f, sel ? C_TEXT : C_MUTED, shown);
        }
    }

    /* The list truncates; the selection gets its whole label, wrapped, under
     * the divider - most of PKHeX's descriptions do not fit on one row. */
    gui_rect(28, 386, 584, 1, C_BORDER);
    const Gen3EventName *sel_named = work ? gen3_event_work_entry(game, event_selected)
                                          : gen3_event_flag_entry(game, event_selected);
    if (sel_named) {
        const char *category = gen3_event_category_name(sel_named->category);
        if (*category) gui_text(30, 393, 0.56f, C_ACCENT, category);
        char lines[2][96];
        const float x = *category ? 30.0f + gui_text_width(category, 0.56f) + 10.0f : 30.0f;
        const unsigned used = wrap_text(sel_named->name, 606.0f - x, 0.58f, lines, 2u);
        for (unsigned i = 0; i < used; ++i)
            gui_text(i ? 30.0f : x, 393.0f + i * 17.0f, 0.58f, C_TEXT, lines[i]);
    } else {
        gui_textf(30, 393, 0.56f, C_MUTED, "0x%03X - not named by PKHeX", event_selected);
    }

    if (live_edit_allowed())
        draw_footer(work ? "[STICK] Value  [DPAD] Page  [B] Back  [X] Flags/Constants"
                         : "[STICK] Select  [DPAD] Page  [B] Back  [A] Toggle  [X] Flags/Constants");
    else
        draw_footer("[STICK] Select  [DPAD] Page  [B] Back  [X] Flags/Constants");
}

#define DEX_ROWS 11u

static void show_pokedex(void) {
    char title[80];
    snprintf(title, sizeof(title), "POKEDEX - %s", gen3_any_game_name(&parsed_save));
    draw_header(title);
    gui_panel(18, 88, 438, 334, C_PANEL, C_BORDER);
    gui_panel(468, 88, 154, 334, C_PANEL, C_BORDER);

    const unsigned seen = gen3_any_dex_seen_count(&parsed_save);
    const unsigned caught = gen3_any_dex_caught_count(&parsed_save);
    gui_textf(30, 101, 0.86f, C_ACCENT, "Seen %u", seen);
    gui_textf(150, 101, 0.86f, C_ACCENT, "Caught %u", caught);
    gui_textf(300, 103, 0.62f, C_MUTED, "of %u", GEN3_DEX_SPECIES);
    gui_rect(28, 122, 410, 1, C_BORDER);

    unsigned first = dex_selected >= DEX_ROWS / 2u ? dex_selected - DEX_ROWS / 2u : 0u;
    if (first + DEX_ROWS > GEN3_DEX_SPECIES) first = GEN3_DEX_SPECIES - DEX_ROWS;
    for (unsigned row = 0; row < DEX_ROWS; ++row) {
        const unsigned national = first + row + 1u;
        const float y = 134.0f + row * 25.0f;
        const bool sel = (national - 1u) == dex_selected;
        if (sel) gui_rect(28, y - 3, 410, 24, C_SELECT);
        const uint16_t internal = gen3_species_internal_from_national(national);
        char nm[22]; fit_text(nm, sizeof(nm), gen3_species_name(internal), 18);
        gui_textf(38, y, 0.62f, C_MUTED, "#%03u", national);
        gui_text(96, y, 0.66f, sel ? C_TEXT : C_MUTED, nm);
        gui_text(300, y, 0.62f, gen3_any_dex_seen(&parsed_save, national) ? C_GREEN : C_FAINT, "SEEN");
        gui_text(370, y, 0.62f, gen3_any_dex_caught(&parsed_save, national) ? C_GREEN : C_FAINT, "CAUGHT");
    }

    const unsigned sel_national = dex_selected + 1u;
    gui_text(480, 101, 0.9f, C_ACCENT, "SELECTED");
    gui_rect(470, 123, 140, 1, C_BORDER);
    gui_pokemon_sprite(505, 140, 80, 80, sel_national);
    gui_textf(480, 228, 0.66f, C_TEXT, "#%03u", sel_national);
    char nm[20];
    fit_text(nm, sizeof(nm), gen3_species_name(gen3_species_internal_from_national(sel_national)), 17);
    gui_text(480, 250, 0.7f, C_TEXT, nm);
    gui_badge(480, 280, gen3_any_dex_seen(&parsed_save, sel_national) ? "SEEN" : "UNSEEN",
              gen3_any_dex_seen(&parsed_save, sel_national) ? C_GREEN : C_PANEL2, C_BADGE_TEXT);
    gui_badge(480, 308, gen3_any_dex_caught(&parsed_save, sel_national) ? "CAUGHT" : "UNCAUGHT",
              gen3_any_dex_caught(&parsed_save, sel_national) ? C_GREEN : C_PANEL2, C_BADGE_TEXT);
    gui_text(480, 344, 0.58f, C_MUTED, "National Dex");
    gui_badge(480, 362, gen3_any_national_dex(&parsed_save) ? "UNLOCKED" : "LOCKED",
              gen3_any_national_dex(&parsed_save) ? C_GREEN : C_PANEL2, C_BADGE_TEXT);

    draw_footer("[STICK] Species  [DPAD] Jump  [B] Back  [A] Seen  [Y] All  [X] Caught  [Z] National");
}

static void show_pkm_edit(void) {
    char editor_title[80]; snprintf(editor_title, sizeof(editor_title), "POKEMON EDITOR - %s", gen3_any_game_name(&parsed_save));
    draw_header(editor_title);
    gui_panel(18,88,180,334,C_PANEL,C_BORDER);
    gui_panel(210,88,412,334,C_PANEL,C_BORDER);
    unsigned nat=gen3_species_national(edit_pkm.species_internal);
    gui_pokemon_sprite(58,120,96,96,nat);
    gui_text(38,226,1.05f,C_TEXT,gen3_species_name(edit_pkm.species_internal));
    gui_textf(38,254,0.8f,C_MUTED,"%s",edit_pkm.nickname);
    gui_textf(38,278,0.68f,C_MUTED,"OT %s",edit_pkm.ot_name);
    unsigned evsum=0; for(unsigned i=0;i<6;++i) evsum+=edit_pkm.evs[i];
    gui_textf(38,302,0.68f,C_MUTED,"Lv %u  EV total %u",gen3_effective_level(&edit_pkm),evsum);
    const Gen3Personal *pers = gen3_personal(nat);
    if (pers) {
        gui_textf(38,322,0.64f,C_MUTED,"%s%s%s",gen3_type_name_full(pers->type1),
                  pers->type2!=pers->type1?" / ":"",
                  pers->type2!=pers->type1?gen3_type_name_full(pers->type2):"");
        gui_textf(38,342,0.62f,C_MUTED,"%s  %s",gen3_gender_name(gen3_gender(&edit_pkm)),
                  gen3_ability_name(gen3_ability_id(&edit_pkm)));
    }
    gui_textf(38,364,0.64f,C_MUTED,"%s%s",gen3_nature_name(gen3_nature(&edit_pkm)),
              gen3_is_shiny(&edit_pkm)?"  SHINY":"");
    gui_textf(38,384,0.62f,C_MUTED,"HP %s %u",gen3_type_name(gen3_hidden_power_type(&edit_pkm)),
              gen3_hidden_power_power(&edit_pkm));
    gui_textf(38,404,0.64f,edit_pkm.checksum_ok?C_GREEN:C_RED,"%s",edit_pkm.checksum_ok?"Checksum OK":"Checksum bad");
    {
        const unsigned ribbons = gen3_ribbon_count(&edit_pkm);
        /* Shares the checksum row; the sidebar panel ends just below it. */
        gui_textf(126,404,0.62f,ribbons?C_ACCENT:C_MUTED,"%u ribbon%s",ribbons,ribbons==1u?"":"s");
    }

    gui_text(228,102,1.0f,C_ACCENT,pkm_page_name(pkm_edit_page));
    gui_controls_text(500,104,0.52f,C_MUTED,"[DPAD] page");
    if (pkm_edit_page==2 || pkm_edit_page==3) gui_text(516,120,0.55f,C_MUTED,"stat");
    unsigned start=page_start(pkm_edit_page), count=page_count(pkm_edit_page);
    /* Base stats and the final figures they add up to, shown alongside the IV
     * and EV pages the way PKHeX's Stats tab lays them out. */
    const Gen3Game stat_game = parsed_save.kind==GEN3_KIND_GBA?parsed_save.gba.game:GEN3_GAME_UNKNOWN;
    uint8_t base[6]; uint16_t stats[6];
    const bool have_stats = gen3_base_stats(nat,stat_game,base) &&
                            gen3_calc_stats(&edit_pkm,stat_game,stats);
    const uint8_t stat_nature = gen3_nature(&edit_pkm);
    const unsigned raised=(unsigned)(stat_nature/5u)+1u, lowered=(unsigned)(stat_nature%5u)+1u;
    for(unsigned i=0;i<count;++i){
        unsigned field=start+i;
        float y=138.0f+i*30.0f;
        bool sel=field==pkm_edit_field;
        if(sel) gui_rect(222,y-5,388,26,C_SELECT);
        const char *label = pkm_field_label(pkm_edit_page,i);
        gui_text(236,y,0.78f,sel?C_TEXT:C_MUTED,label);
        char val[64]; pkm_value_text(field,val,sizeof(val));
        float value_x=pkm_edit_page==1?382.0f:430.0f;
        float value_scale=pkm_edit_page==1?0.68f:0.8f;
        gui_text(value_x,y,value_scale,C_TEXT,val);
        if ((pkm_edit_page==2 || pkm_edit_page==3) && i<6u) {
            if (have_stats) {
                gui_textf(340,y,0.62f,C_MUTED,"base %u",base[i]);
                /* The nature raises one stat and lowers another; colour the
                 * final figure so which is which is visible at a glance. */
                GXColor stat_colour = C_TEXT;
                if (raised!=lowered && i==raised)  stat_colour = C_GREEN;
                if (raised!=lowered && i==lowered) stat_colour = C_RED;
                gui_textf(516,y,0.78f,stat_colour,"%u",stats[i]);
            }
        }
    }
    if (pkm_edit_page == 8)
        draw_footer("[STICK] Field  [DPAD] Page  [B] Cancel  [A] Apply  [Z] Type a name");
    else if (edit_source == PKM_EDIT_BOX)
        draw_footer("[STICK] Field/+/-  [DPAD] Page  [B] Cancel  [A] Apply  [Z] Export");
    else
        draw_footer("[STICK] Field/+/-  [DPAD] Page  [B] Cancel  [A] Apply");
}

static void adjust_pkm(int direction, bool coarse) {
    unsigned f=pkm_edit_field;
    long long step=coarse?10:1;
    if(f==2) step=coarse?10000:100;
    if(f==4) step=coarse?256:1;
    long long d=direction*step;
    if(f==0){
        long long nat=gen3_species_national(edit_pkm.species_internal); nat=clamp_ll(nat+d,1,386);
        edit_pkm.species_internal=gen3_species_internal_from_national((unsigned)nat);
    } else if(f==1) edit_pkm.held_item=(uint16_t)clamp_ll((long long)edit_pkm.held_item+d,0,65535);
    else if(f==2) edit_pkm.experience=(uint32_t)clamp_ll((long long)edit_pkm.experience+d,0,0xFFFFFFFFLL);
    else if(f==3) edit_pkm.friendship=(uint8_t)clamp_ll((long long)edit_pkm.friendship+d,0,255);
    else if(f==4) edit_pkm.pid=(uint32_t)clamp_ll((long long)edit_pkm.pid+d,0,0xFFFFFFFFLL);
    else if(f==5) edit_pkm.tid=(uint16_t)clamp_ll((long long)edit_pkm.tid+d,0,65535);
    else if(f==6) edit_pkm.sid=(uint16_t)clamp_ll((long long)edit_pkm.sid+d,0,65535);
    else if(f==7 && (edit_source==PKM_EDIT_PARTY || gen3_any_has_shadow(parsed_save.kind))) edit_pkm.level=(uint8_t)clamp_ll((long long)edit_pkm.level+d,1,100);
    else if(f>=8 && f<=11) edit_pkm.moves[f-8]=(uint16_t)clamp_ll((long long)edit_pkm.moves[f-8]+d,0,354);
    else if(f>=12 && f<=15) edit_pkm.pp[f-12]=(uint8_t)clamp_ll((long long)edit_pkm.pp[f-12]+d,0,99);
    else if(f>=16 && f<=21) edit_pkm.ivs[f-16]=(uint8_t)clamp_ll((long long)edit_pkm.ivs[f-16]+d,0,31);
    else if(f>=22 && f<=27) edit_pkm.evs[f-22]=(uint8_t)clamp_ll((long long)edit_pkm.evs[f-22]+d,0,255);
    else if(f==28) edit_pkm.is_egg=!edit_pkm.is_egg;
    else if(f==29) {
        /* Nature is the PID modulo 25, so this re-rolls the PID rather than
         * writing a field. Shininess is preserved. */
        const uint8_t current=gen3_nature(&edit_pkm);
        const uint8_t wanted=(uint8_t)(((long long)current+direction+25)%25);
        if(!gen3_set_nature(&edit_pkm,wanted))
            set_status("No PID found for that nature; try another.");
    }
    else if(f==34) edit_pkm.origin_game=(uint8_t)clamp_ll((long long)edit_pkm.origin_game+direction,0,15);
    else if(f==35) edit_pkm.ball=(uint8_t)clamp_ll((long long)edit_pkm.ball+direction,0,12);
    else if(f==36) edit_pkm.met_level=(uint8_t)clamp_ll((long long)edit_pkm.met_level+d,0,127);
    else if(f==37) edit_pkm.met_location=(uint8_t)clamp_ll((long long)edit_pkm.met_location+d,0,255);
    else if(f==38) edit_pkm.ot_gender=edit_pkm.ot_gender?0u:1u;
    else if(f==39) edit_pkm.fateful=!edit_pkm.fateful;
    else if(f==40) edit_pkm.language=(uint8_t)clamp_ll((long long)edit_pkm.language+direction,0,7);
    else if(f==41) edit_pkm.ability_bit=!edit_pkm.ability_bit;
    else if(f==42) edit_pkm.markings=(uint8_t)(((long long)edit_pkm.markings+direction+16)%16);
    else if(f==43) {
        const long long strain=clamp_ll((long long)gen3_pokerus_strain(&edit_pkm)+direction,0,15);
        edit_pkm.pokerus=(uint8_t)((strain<<4)|gen3_pokerus_days(&edit_pkm));
    }
    else if(f==44) {
        const long long days=clamp_ll((long long)gen3_pokerus_days(&edit_pkm)+direction,0,15);
        edit_pkm.pokerus=(uint8_t)(((long long)gen3_pokerus_strain(&edit_pkm)<<4)|days);
    }
    else if(f==45) {
        /* One control for all four slots; per-move PP Ups are rarely uneven. */
        const long long ups=clamp_ll((long long)gen3_pp_up_count(&edit_pkm,0)+direction,0,3);
        for(unsigned m=0;m<4u;++m) gen3_set_pp_up_count(&edit_pkm,m,(unsigned)ups);
    }
    else if(f>=46 && f<=51) edit_pkm.contest[f-46]=(uint8_t)clamp_ll((long long)edit_pkm.contest[f-46]+d,0,255);
    else if(f>=PKM_FIELD_CONTEST_RIBBON && f<PKM_FIELD_RIBBON_FLAG) {
        const unsigned contest=f-PKM_FIELD_CONTEST_RIBBON;
        const long long level=clamp_ll((long long)gen3_contest_ribbon(&edit_pkm,contest)+direction,
                                       0,GEN3_CONTEST_RIBBON_MAX);
        gen3_set_contest_ribbon(&edit_pkm,contest,(uint8_t)level);
    }
    else if(f>=PKM_FIELD_RIBBON_FLAG && f<PKM_FIELD_RIBBON_FLAG+GEN3_RIBBON_FLAG_COUNT) {
        const unsigned index=f-PKM_FIELD_RIBBON_FLAG;
        gen3_set_ribbon_flag(&edit_pkm,index,!gen3_ribbon_flag(&edit_pkm,index));
    }
}

static bool apply_pkm_edit(void) {
    bool ok=false;
    if(edit_source==PKM_EDIT_PARTY) ok=gen3_any_set_party_pokemon(&parsed_save,edit_source_slot,&edit_pkm);
    else if(edit_source==PKM_EDIT_BOX) ok=gen3_any_set_box_pokemon(&parsed_save,edit_source_box,edit_source_slot,&edit_pkm);
    else if(edit_source==PKM_EDIT_DAYCARE) ok=gen3_set_daycare_pokemon(&parsed_save.gba,edit_source_slot,&edit_pkm);
    if(ok){ save_dirty=true; if(parsed_save.kind==GEN3_KIND_GBA) refresh_gba_summary(); set_status("Pokemon changes applied in RAM. Save creates a verified timestamped backup."); }
    else set_status("Could not apply Pokemon edit.");
    return ok;
}

static void show_confirm_save(void) {
    draw_header("SAVE CHANGES");
    gui_panel(74, 120, 492, 250, C_PANEL, C_ACCENT);
    gui_text(96, 142, 1.2f, C_ACCENT, "WRITE THE SAVE?");

    const char *target = loaded_from_cart ? "the Game Boy Advance cartridge"
                       : loaded_from_card ? "the memory card"
                                          : loaded_path;
    char shown[80];
    fit_text_px(shown, sizeof(shown), target, 440.0f, 0.7f);
    gui_text(96, 186, 0.7f, C_TEXT, shown);

    gui_text(96, 224, 0.64f, C_MUTED, "A verified backup is written first, and the new");
    gui_text(96, 246, 0.64f, C_MUTED, "file is read back and compared before this");
    gui_text(96, 268, 0.64f, C_MUTED, "reports success. Nothing is overwritten blind.");

    gui_text(96, 306, 0.66f, save_dirty ? C_YELLOW : C_GREEN,
             save_dirty ? "There are unsaved edits." : "No edits have been made yet.");

    draw_footer("[B] Cancel  [A] Write it");
}

static void show_error(void) {
    draw_header("ERROR");
    gui_panel(74, 126, 492, 230, C_PANEL, C_RED);
    gui_text(96, 148, 1.3f, C_RED, "COULD NOT OPEN SAVE");

    /* Hardware diagnostics deliberately contain short newline-separated
     * fields.  Render them as lines instead of feeding the whole message to
     * fit_text(), which hid everything after the first ~54 characters. */
    const char *p = error_message;
    float y = 194.0f;
    for (unsigned line = 0; line < 5 && p && *p; ++line, y += 22.0f) {
        char msg[80];
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < 68u) { msg[n] = p[n]; ++n; }
        msg[n] = '\0';
        gui_text(96, y, 0.72f, C_TEXT, msg);
        p += n;
        if (*p == '\n') ++p;
        else if (*p) { while (*p && *p != '\n') ++p; if (*p == '\n') ++p; }
    }

    gui_controls_text(96, 308, 0.68f, C_MUTED, "[B] returns to the previous browser.");
    draw_footer("[B] Back  [START] Exit");
}

static void render_current(void) {
    gui_begin(&gui);
    switch (mode) {
        case UI_BROWSER: show_browser(); break;
        case UI_CARD_BROWSER: show_card_browser(); break;
        case UI_BACKUP_BROWSER: show_backup_browser(); break;
        case UI_SUMMARY: show_summary(); break;
        case UI_BOXES: show_boxes(); break;
        case UI_TRAINER_EDIT: show_trainer_edit(); break;
        case UI_INVENTORY_EDIT: show_inventory_edit(); break;
        case UI_PKM_EDIT: show_pkm_edit(); break;
        case UI_POKEDEX: show_pokedex(); break;
        case UI_EVENTS: show_events(); break;
        case UI_TOOLS: show_tools(); break;
        case UI_DAYCARE: show_daycare(); break;
        case UI_ROAMER: show_roamer(); break;
        case UI_MAIL: show_mail(); break;
        case UI_HALL_OF_FAME: show_hall_of_fame(); break;
        case UI_RECORDS: show_records(); break;
        case UI_BOX_LAYOUT: show_box_layout(); break;
        case UI_POKEBLOCKS: show_pokeblocks(); break;
        case UI_SECRET_BASES: show_secret_bases(); break;
        case UI_DECORATIONS: show_decorations(); break;
        case UI_EMERALD_EXTRAS: show_emerald_extras(); break;
        case UI_MEMO: show_memo(); break;
        case UI_GAMECUBE_LINK: show_gamecube_link(); break;
        case UI_LEGALITY: show_legality(); break;
        case UI_FRONTIER: show_frontier(); break;
        case UI_CLOCK: show_clock(); break;
        case UI_SHADOWS: show_shadows(); break;
        case UI_SAVE_CHECK: show_save_check(); break;
        case UI_MISC: show_misc(); break;
        case UI_RECORD_FILES: show_record_files(); break;
        case UI_CONFIRM_SAVE: show_confirm_save(); break;
        case UI_KEYBOARD: show_keyboard(); break;
        case UI_ERROR: show_error(); break;
    }
    gui_end(&gui);
}

static void browser_open_selected(void) {
    if (entry_count <= 0 || selected < 0 || selected >= entry_count) return;
    BrowserEntry *e = &entries[selected];
    char path[PATH_LEN]; make_child_path(path, sizeof(path), current_path, e->name);
    if (e->is_dir) {
        snprintf(current_path, sizeof(current_path), "%s/", path);
        scan_directory();
    } else if (load_save_file(path)) mode = UI_SUMMARY;
    else mode = UI_ERROR;
}

static void handle_browser(u32 down) {
    if (entry_count) selected = (int)nav_index(down, (unsigned)selected, (unsigned)entry_count);
    if (nav_page_prev(down)) { selected -= VISIBLE_ROWS; if (selected < 0) selected = 0; }
    if (nav_page_next(down)) { selected += VISIBLE_ROWS; if (selected >= entry_count) selected = entry_count ? entry_count - 1 : 0; }
    if (down & PAD_BUTTON_A) browser_open_selected();
    if (down & PAD_BUTTON_B) { path_up(); scan_directory(); }
    if (down & PAD_BUTTON_X) switch_root();
    if (down & PAD_BUTTON_Y) {
        /* Open the screen immediately and let the scan happen once the console
         * has settled; mounting here would stall the frame. */
        mode = UI_CARD_BROWSER;
        card_scan_pending = true;
    }
    if (down & PAD_TRIGGER_Z) { backup_root_index=current_root; scan_backups(); mode=UI_BACKUP_BROWSER; }
}

static void handle_card_browser(u32 down) {
    const int rows = card_row_count();
    if (rows > 0) card_selected = (int)nav_index(down, (unsigned)card_selected, (unsigned)rows);
    if (card_selected >= rows) card_selected = rows - 1;
    if (down & PAD_BUTTON_Y) {
        card_scan_pending = false;
        scan_memory_cards();
    }
    if (down & PAD_BUTTON_A) {
        bool ok;
        if (card_selected < card_entry_count) ok = load_card_entry(&card_entries[card_selected]);
        else ok = read_gba_cart_save();
        mode = ok ? UI_SUMMARY : UI_ERROR;
    }
    if (down & PAD_TRIGGER_Z) { backup_root_index=current_root; scan_backups(); mode=UI_BACKUP_BROWSER; }
    if (down & PAD_BUTTON_B) mode = UI_BROWSER;
}

static void handle_backup_browser(u32 down) {
    if(backup_entry_count)backup_selected=(int)nav_index(down,(unsigned)backup_selected,(unsigned)backup_entry_count);
    if(nav_page_prev(down) &&root_count>1){backup_root_index=backup_root_index?backup_root_index-1:root_count-1;scan_backups();}
    if(nav_page_next(down) &&root_count>1){backup_root_index=(backup_root_index+1)%root_count;scan_backups();}
    if(down&PAD_BUTTON_Y)scan_backups();
    if((down&PAD_BUTTON_A)&&backup_entry_count){if(!open_backup_path(backup_entries[backup_selected].path))mode=UI_ERROR;}
    if((down&PAD_BUTTON_X)&&backup_entry_count)restore_backup_path(backup_entries[backup_selected].path);
    if(down&PAD_BUTTON_B)mode=UI_BROWSER;
}

static void handle_summary(u32 down) {
    unsigned count = gen3_any_party_count(&parsed_save);
    if (count) {
        unsigned col=party_selected%3, row=party_selected/3;
        if (down & PAD_BUTTON_LEFT) col=col?col-1:2;
        if (down & PAD_BUTTON_RIGHT) col=col<2?col+1:0;
        if (down & PAD_BUTTON_UP) row=row?row-1:1;
        if (down & PAD_BUTTON_DOWN) row=row<1?row+1:0;
        unsigned candidate=row*3+col; if(candidate<count) party_selected=candidate;
    }
    if ((down & PAD_BUTTON_A) && live_edit_allowed() && count) begin_party_edit();
    if ((down & PAD_BUTTON_Y) && gen3_any_box_count(&parsed_save)) mode = UI_BOXES;
    if (down & PAD_BUTTON_X) { tool_selected=0; mode=UI_TOOLS; }
    if (down & PAD_TRIGGER_Z) { backup_root_index=current_root; scan_backups(); mode=UI_BACKUP_BROWSER; }
    if (down & PAD_BUTTON_B) mode = loaded_from_backup ? UI_BACKUP_BROWSER : ((loaded_from_card || loaded_from_cart) ? UI_CARD_BROWSER : UI_BROWSER);
}

static void handle_pokedex(u32 down) {
    dex_selected = nav_index(down, dex_selected, GEN3_DEX_SPECIES);
    if (down & PAD_BUTTON_LEFT)  dex_selected = dex_selected >= DEX_ROWS ? dex_selected - DEX_ROWS : 0u;
    if (down & PAD_BUTTON_RIGHT) {
        dex_selected += DEX_ROWS;
        if (dex_selected >= GEN3_DEX_SPECIES) dex_selected = GEN3_DEX_SPECIES - 1u;
    }
    if (!live_edit_allowed()) {
        if (down & PAD_BUTTON_B) mode = dex_return;
        return;
    }

    const unsigned national = dex_selected + 1u;
    if (down & PAD_BUTTON_A) {
        gen3_any_set_dex_seen(&parsed_save, national, !gen3_any_dex_seen(&parsed_save, national));
        save_dirty = true;
    }
    if (down & PAD_BUTTON_X) {
        gen3_any_set_dex_caught(&parsed_save, national, !gen3_any_dex_caught(&parsed_save, national));
        save_dirty = true;
    }
    if (down & PAD_BUTTON_Y) {
        /* Fill both, since a caught species the dex has not seen is not a
         * state the games produce. */
        const bool fill = gen3_any_dex_caught_count(&parsed_save) < GEN3_DEX_SPECIES;
        for (unsigned n = 1; n <= GEN3_DEX_SPECIES; ++n) {
            gen3_any_set_dex_seen(&parsed_save, n, fill);
            gen3_any_set_dex_caught(&parsed_save, n, fill);
        }
        save_dirty = true;
        set_status(fill ? "Whole Pokedex marked seen and caught." : "Pokedex cleared.");
    }
    if (down & PAD_TRIGGER_Z) {
        gen3_any_set_national_dex(&parsed_save, !gen3_any_national_dex(&parsed_save));
        save_dirty = true;
        set_status(gen3_any_national_dex(&parsed_save)
                       ? "National Dex unlocked (flag, work value and mode byte)."
                       : "National Dex locked again.");
    }
    if (down & PAD_BUTTON_B) mode = dex_return;
}

static void handle_events(u32 down) {
    const bool work = event_show_work;
    const unsigned total = work ? gen3_any_event_work_count(&parsed_save)
                                : gen3_any_event_flag_count(&parsed_save);
    if (down & PAD_BUTTON_X) {
        event_show_work = !event_show_work;
        event_selected = 0;
        return;
    }
    if (down & PAD_BUTTON_B) { mode = event_return; return; }
    if (!total) return;

    event_selected = nav_index(down, event_selected, total);
    if (nav_page_prev(down))   event_selected = event_selected >= EVENT_ROWS ? event_selected - EVENT_ROWS : 0u;
    if (nav_page_next(down)) {
        event_selected += EVENT_ROWS;
        if (event_selected >= total) event_selected = total - 1u;
    }
    if (!live_edit_allowed()) return;

    if (work) {
        /* A stick nudge of one, and A for 256 so a 16-bit value is reachable.
         * Left and right on the D-pad page the list, so they do not also
         * change the value under the cursor. */
        const int fine = nav_fine(down);
        long long v = gen3_any_event_work(&parsed_save, event_selected);
        v += fine;
        if (down & PAD_BUTTON_A)     v += 256;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        if (fine || (down & PAD_BUTTON_A)) {
            gen3_any_set_event_work(&parsed_save, event_selected, (uint16_t)v);
            save_dirty = true;
        }
    } else if (down & PAD_BUTTON_A) {
        gen3_any_set_event_flag(&parsed_save, event_selected,
                                !gen3_any_event_flag(&parsed_save, event_selected));
        save_dirty = true;
    }
}

/*
 * Moving a record between slots. Picking one up remembers where it came from
 * rather than emptying the slot, so nothing is lost if the console dies or the
 * user backs out; placing it writes both ends and only then clears the source.
 * Placing onto an occupied slot swaps the two.
 */
static void box_pick_up_or_place(void) {
    Gen3Pokemon here = {0};
    gen3_any_box_pokemon(&parsed_save, box_index, box_selected, &here);

    if (!box_holding) {
        if (!here.present) { set_status("That slot is empty."); return; }
        box_held = here;
        box_held_box = box_index;
        box_held_slot = box_selected;
        box_holding = true;
        set_status("Picked up. X places it, B puts it back.");
        return;
    }

    if (box_index == box_held_box && box_selected == box_held_slot) {
        box_holding = false;
        set_status("Put back.");
        return;
    }

    /* Write the destination first: if that fails nothing has moved yet. */
    if (!gen3_any_set_box_pokemon(&parsed_save, box_index, box_selected, &box_held)) {
        set_status("Could not place it there.");
        return;
    }
    bool ok;
    if (here.present)
        ok = gen3_any_set_box_pokemon(&parsed_save, box_held_box, box_held_slot, &here);
    else
        ok = gen3_any_clear_box_slot(&parsed_save, box_held_box, box_held_slot);
    if (!ok) {
        /* Put the destination back the way it was rather than leaving a copy. */
        if (here.present) gen3_any_set_box_pokemon(&parsed_save, box_index, box_selected, &here);
        else gen3_any_clear_box_slot(&parsed_save, box_index, box_selected);
        set_status("Could not clear the old slot; nothing moved.");
        return;
    }
    box_holding = false;
    save_dirty = true;
    set_status(here.present ? "Swapped." : "Moved.");
}

static void handle_boxes(u32 down) {
    unsigned n = gen3_any_box_count(&parsed_save);
    if (!n) { mode = UI_SUMMARY; return; }
    if (nav_page_prev(down)) { box_index = box_index == 0 ? n - 1 : box_index - 1; box_selected = 0; }
    if (nav_page_next(down)) { box_index = (box_index + 1) % n; box_selected = 0; }
    unsigned col = box_selected % 6, row = box_selected / 6;
    if (down & PAD_BUTTON_LEFT)  col = col == 0 ? 5 : col - 1;
    if (down & PAD_BUTTON_RIGHT) col = col == 5 ? 0 : col + 1;
    if (down & PAD_BUTTON_UP)    row = row == 0 ? 4 : row - 1;
    if (down & PAD_BUTTON_DOWN)  row = row == 4 ? 0 : row + 1;
    box_selected = row * 6 + col;
    if ((down & PAD_BUTTON_A) && live_edit_allowed() && !box_holding) begin_box_edit();
    if (down & PAD_BUTTON_Y) request_save();
    if ((down & PAD_BUTTON_X) && live_edit_allowed()) box_pick_up_or_place();
    if (down & PAD_TRIGGER_Z) { backup_root_index=current_root; scan_backups(); mode=UI_BACKUP_BROWSER; }
    if (down & PAD_BUTTON_B) {
        /* While carrying a record, B puts it back rather than leaving the
         * screen with it in limbo. */
        if (box_holding) { box_holding = false; set_status("Put back."); }
        else mode = UI_SUMMARY;
    }
}

static void handle_trainer_edit(u32 down) {
    unsigned fields=trainer_field_count();
    trainer_edit_field=nav_index(down,trainer_edit_field,fields);
    if (down & PAD_BUTTON_LEFT) adjust_trainer(-1,(down & UI_COARSE) != 0);
    if (down & PAD_BUTTON_RIGHT) adjust_trainer(1,(down & UI_COARSE) != 0);

    if ((down & PAD_BUTTON_A) && live_edit_allowed()) {
        const TrainerField f = trainer_field_at(trainer_edit_field);
        if (f == TF_NAME) {
            const unsigned len = gen3_any_trainer_name_length(&parsed_save);
            if (!len) { set_status("This save's trainer name is not editable."); return; }
            if (gen3_any_name_is_utf16(&parsed_save))
                kb_load_ascii(parsed_save.trainer_name, len, KB_GC_TRAINER_NAME,
                              UI_TRAINER_EDIT, "TRAINER NAME");
            else
                kb_load(parsed_save.gba.small, len, len, KB_TRAINER_NAME,
                        UI_TRAINER_EDIT, "TRAINER NAME");
            return;
        }
        if (f == TF_RIVAL && parsed_save.kind == GEN3_KIND_GBA) {
            uint8_t raw[GEN3_RIVAL_NAME_LEN];
            gen3_rival_name_raw(&parsed_save.gba, raw);
            kb_load(raw, sizeof(raw), GEN3_RIVAL_NAME_LEN - 1u, KB_RIVAL_NAME, UI_TRAINER_EDIT, "RIVAL NAME");
            return;
        }
    }
    if (down & PAD_BUTTON_Y) { inventory_pocket=GEN3_POCKET_ITEMS; inventory_slot=0; inventory_field=0; mode=UI_INVENTORY_EDIT; }
    if (down & PAD_BUTTON_X) request_save();
    if (down & PAD_BUTTON_B) mode=UI_TOOLS;
}

static void handle_inventory_edit(u32 down) {
    unsigned cap=gen3_any_pocket_capacity(&parsed_save,inventory_pocket);
    if(!cap){ inventory_step_pocket(1); cap=gen3_any_pocket_capacity(&parsed_save,inventory_pocket); }
    if(!cap){ mode=UI_TRAINER_EDIT; return; }
    inventory_slot=nav_index(down,inventory_slot,cap);
    if(nav_page_prev(down)) inventory_step_pocket(-1);
    if(nav_page_next(down)) inventory_step_pocket(1);
    if(down & PAD_BUTTON_X) inventory_field^=1u;
    const int fine = nav_fine(down);
    if(fine) adjust_inventory(fine, false);
    if(down & PAD_BUTTON_Y) request_save();
    if(down & PAD_BUTTON_B) mode=UI_TRAINER_EDIT;
}

static void handle_pkm_edit(u32 down) {
    unsigned start=page_start(pkm_edit_page), count=page_count(pkm_edit_page);
    unsigned local=pkm_edit_field-start; if(local>=count){local=0;pkm_edit_field=start;}
    local=nav_index(down,local,count);
    pkm_edit_field=start+local;
    const int fine = nav_fine(down);
    if (fine) adjust_pkm(fine, false);

    if (nav_page_prev(down)) { pkm_edit_page=pkm_edit_page?pkm_edit_page-1:PKM_PAGE_COUNT-1u; pkm_edit_field=page_start(pkm_edit_page); }
    if (nav_page_next(down)) { pkm_edit_page=(pkm_edit_page+1)%PKM_PAGE_COUNT; pkm_edit_field=page_start(pkm_edit_page); }
    if (down & PAD_TRIGGER_Z) {
        if (pkm_edit_field == PKM_FIELD_NICKNAME) {
            kb_load(edit_pkm.nickname_raw, sizeof(edit_pkm.nickname_raw), 10u,
                    KB_NICKNAME, UI_PKM_EDIT, "NICKNAME");
            return;
        }
        if (pkm_edit_field == PKM_FIELD_OT_NAME) {
            kb_load(edit_pkm.ot_raw, sizeof(edit_pkm.ot_raw), 7u,
                    KB_OT_NAME, UI_PKM_EDIT, "ORIGINAL TRAINER");
            return;
        }
        /* Anywhere else, Z writes this record to the card as a file. */
        if (edit_source == PKM_EDIT_BOX) {
            char written[PATH_LEN];
            if (export_box_record(edit_source_box, edit_source_slot, written, sizeof(written))) {
                char msg[PATH_LEN + 32];
                snprintf(msg, sizeof(msg), "Saved %s", written);
                set_status(msg);
            } else {
                set_status("Could not write the record to the card.");
            }
            return;
        }
    }
    const UiMode back = edit_source==PKM_EDIT_BOX ? UI_BOXES
                      : (edit_source==PKM_EDIT_DAYCARE ? UI_DAYCARE : UI_SUMMARY);
    if (down & PAD_BUTTON_A) { if(apply_pkm_edit()) mode=back; }
    if (down & PAD_BUTTON_B) mode=back;
}


static void handle_confirm_save(u32 down) {
    if (down & PAD_BUTTON_A) { save_in_place(); mode = confirm_return; return; }
    if (down & PAD_BUTTON_B) mode = confirm_return;
}

static void handle_error(u32 down) {
    if (down & PAD_BUTTON_B) mode = fat_available ? UI_BROWSER : UI_CARD_BROWSER;
}

/* Merge all four controller ports. The analog stick reports as UI_STICK_*
 * rather than as the D-pad's bits, so the two are separate inputs: the stick
 * moves one step, the D-pad ten. The edge and repeat state machine lives in
 * uinput.c so it can be tested on the host; this only gathers hardware state. */
static UiInput ui_input = {
    .repeatable = PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT |
                  UI_STICK_ANY,
    .previous_nav = 0u, .last_held = 0u, .carry_block = 0u, .repeat_frames = 0u,
    .dir_up = PAD_BUTTON_UP, .dir_down = PAD_BUTTON_DOWN,
    .dir_left = PAD_BUTTON_LEFT, .dir_right = PAD_BUTTON_RIGHT,
};
static bool last_cstick_active;

static u32 poll_input(void) {
    PAD_ScanPads();
    u32 edges = 0;
    u32 held = 0;
    bool cstick_active = false;
    for (int port = 0; port < 4; ++port) {
        edges |= PAD_ButtonsDown(port);
        held |= PAD_ButtonsHeld(port);
        s8 sx = PAD_StickX(port);
        s8 sy = PAD_StickY(port);
        s8 cx = PAD_SubStickX(port);
        s8 cy = PAD_SubStickY(port);
        if (sx <= -40) held |= UI_STICK_LEFT;
        if (sx >=  40) held |= UI_STICK_RIGHT;
        if (sy >=  40) held |= UI_STICK_UP;
        if (sy <= -40) held |= UI_STICK_DOWN;
        /* A deliberate C-stick movement is a screenshot gesture. A 55-count
         * threshold is high enough to ignore normal analog center noise. */
        if (cx <= -55 || cx >= 55 || cy <= -55 || cy >= 55)
            cstick_active = true;
    }
    last_cstick_active = cstick_active;
    return ui_input_step(&ui_input, edges, held);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!gui_init(&gui)) return 1;

    /*
     * libfat first, CARD_Init second.
     *
     * Both walk the EXI bus: fatInitDefault() probes for an SD adapter in the
     * memory card slots as well as SP2, and CARD_Init() claims the same slots
     * for the memory card driver. Starting the card driver first leaves the
     * probe contending with it for the bus, which is an intermittent hang at
     * startup rather than a reliable one. Mount the filesystem while nothing
     * else holds EXI, then hand the slots to the card driver.
     */
    fat_available = fatInitDefault();
    CARD_Init(NULL, NULL);
    if (fat_available) {
        detect_roots();
        if (root_count) scan_directory();
        else fat_available = false;
    }
    app_start_time = gettime();
    if (!fat_available) {
        /* No SD, so the memory cards are the only source; scan once the
         * console has settled rather than immediately. */
        card_scan_pending = true;
        mode = UI_CARD_BROWSER;
    }

    bool screenshot_latched = false;
    while (SYS_MainLoop()) {
        u32 down = poll_input();
        const UiMode mode_before = mode;
        bool screenshot_now = last_cstick_active && !screenshot_latched;
        screenshot_latched = last_cstick_active;
        if (down & PAD_BUTTON_START) break;

        if (card_scan_pending && mode == UI_CARD_BROWSER &&
            (unsigned)diff_msec(app_start_time, gettime()) >= CARD_SETTLE_MS) {
            card_scan_pending = false;
            snprintf(status_message, sizeof(status_message), "Scanning memory cards...");
            status_frames = 120;
            render_current();
            scan_memory_cards();
            status_message[0] = '\0';
            status_frames = 0;
        }

        switch (mode) {
            case UI_BROWSER: handle_browser(down); break;
            case UI_CARD_BROWSER: handle_card_browser(down); break;
            case UI_BACKUP_BROWSER: handle_backup_browser(down); break;
            case UI_SUMMARY: handle_summary(down); break;
            case UI_BOXES: handle_boxes(down); break;
            case UI_EVENTS: handle_events(down); break;
            case UI_TRAINER_EDIT: handle_trainer_edit(down); break;
            case UI_INVENTORY_EDIT: handle_inventory_edit(down); break;
            case UI_PKM_EDIT: handle_pkm_edit(down); break;
        case UI_POKEDEX: handle_pokedex(down); break;
            case UI_TOOLS: handle_tools(down); break;
            case UI_DAYCARE: handle_daycare(down); break;
            case UI_ROAMER: handle_roamer(down); break;
            case UI_MAIL: handle_mail(down); break;
            case UI_HALL_OF_FAME: handle_hall_of_fame(down); break;
            case UI_RECORDS: handle_records(down); break;
            case UI_BOX_LAYOUT: handle_box_layout(down); break;
            case UI_POKEBLOCKS: handle_pokeblocks(down); break;
            case UI_SECRET_BASES: handle_secret_bases(down); break;
            case UI_DECORATIONS: handle_decorations(down); break;
            case UI_EMERALD_EXTRAS: handle_emerald_extras(down); break;
            case UI_MEMO: handle_memo(down); break;
            case UI_GAMECUBE_LINK: handle_gamecube_link(down); break;
            case UI_LEGALITY: handle_legality(down); break;
            case UI_FRONTIER: handle_frontier(down); break;
            case UI_CLOCK: handle_clock(down); break;
            case UI_SHADOWS: handle_shadows(down); break;
            case UI_SAVE_CHECK: handle_save_check(down); break;
            case UI_MISC: handle_misc(down); break;
            case UI_RECORD_FILES: handle_record_files(down); break;
            case UI_CONFIRM_SAVE: handle_confirm_save(down); break;
            case UI_KEYBOARD: handle_keyboard(down); break;
            case UI_ERROR: handle_error(down); break;
        }
        if (mode != mode_before) ui_input_screen_changed(&ui_input);

        /* Draw and present one complete 640x480 frame every VSync. */
        render_current();
        if (status_frames) --status_frames;
        /* Capture after presentation so the confirmation banner is not baked into the shot. */
        if (screenshot_now) take_screenshot();
    }
    reset_loaded_save();
    gui_shutdown(&gui);
    return 0;
}
