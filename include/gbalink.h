#ifndef PKHEX_GC_GBALINK_H
#define PKHEX_GC_GBALINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBALINK_MAX_SAVE_SIZE 0x20000u
#define GBALINK_HEADER_SIZE   0xC0u

typedef void (*GbaLinkProgressFn)(const char *stage, size_t done, size_t total,
                                  void *user);

typedef enum GbaLinkSaveType {
    GBALINK_SAVE_NONE = 0,
    GBALINK_SAVE_EEPROM_512,
    GBALINK_SAVE_EEPROM_8K,
    GBALINK_SAVE_SRAM_32K,
    GBALINK_SAVE_FLASH_64K,
    GBALINK_SAVE_FLASH_128K,
} GbaLinkSaveType;

typedef struct GbaLinkCart {
    int chan;                  /* SI channel the agent answered on */
    uint32_t rom_size;
    uint32_t save_size;        /* 0 when the cartridge has no backup memory */
    GbaLinkSaveType save_type;
    bool writable;             /* PKHeX-GC can program this save type */
    char title[13];            /* header 0xA0, NUL-terminated */
    char gamecode[5];          /* header 0xAC, NUL-terminated */
    char maker[3];             /* header 0xB0, NUL-terminated */
    uint8_t header[GBALINK_HEADER_SIZE];
} GbaLinkCart;

const char *gbalink_save_type_name(GbaLinkSaveType type);

/* Find a Game Boy Advance on any GameCube serial port.  Returns the channel
 * in *chan_out.  A GameCube controller on another port is left alone. */
/*
 * What is actually on the console's serial and EXI buses.
 *
 * gbalink_find_gba() looked at one bit of SI_GetType and threw the rest away,
 * so a device that answers JoyBus without setting that bit was skipped
 * silently. This records what every SI channel and EXI device reports instead.
 */
#define GBALINK_PORTS 4
#define GBALINK_EXI_CHANNELS 3
#define GBALINK_EXI_DEVICES 3

typedef struct GbaLinkPortScan {
    uint32_t type[GBALINK_PORTS];        /* raw SI_GetType, kept whatever it says */
    bool responded[GBALINK_PORTS];
    uint8_t status[GBALINK_PORTS][3];    /* the JoyBus identify reply */
    bool status_ok[GBALINK_PORTS];
    bool looks_gba[GBALINK_PORTS];       /* device id 0004: a Game Boy Advance */
    bool multiboot[GBALINK_PORTS];       /* and sitting in its BIOS boot wait */
    uint32_t exi_id[GBALINK_EXI_CHANNELS][GBALINK_EXI_DEVICES];
    bool exi_ok[GBALINK_EXI_CHANNELS][GBALINK_EXI_DEVICES];
    int gba_chan;                        /* -1 when nothing answered as a GBA */
} GbaLinkPortScan;

void gbalink_scan_ports(GbaLinkPortScan *out);

bool gbalink_find_gba(int *chan_out, char *error, size_t error_size);

/* Upload the embedded save agent to a GBA sitting in its BIOS multiboot
 * wait state.  Safe to call again if the GBA has since been reset. */
bool gbalink_boot_agent(int chan, char *error, size_t error_size,
                        GbaLinkProgressFn progress, void *user);

/* Ask the running agent what cartridge is inserted. */
bool gbalink_identify(int chan, GbaLinkCart *out, char *error, size_t error_size);

/* Read the cartridge save.  out_size must be at least cart->save_size. */
bool gbalink_read_save(const GbaLinkCart *cart, uint8_t *out, size_t out_size,
                       char *error, size_t error_size,
                       GbaLinkProgressFn progress, void *user);

/* Stream an image to the agent, confirm it arrived intact, and only then
 * authorise the erase/program cycle.  A transfer error aborts before the
 * cartridge is touched. */
bool gbalink_write_save(const GbaLinkCart *cart, const uint8_t *data, size_t size,
                        char *error, size_t error_size,
                        GbaLinkProgressFn progress, void *user);

#endif
