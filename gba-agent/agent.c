/*
 * PKHeX-GC Game Boy Advance cartridge save agent.
 *
 * This is the ARM7TDMI program that PKHeX-GC uploads to a Game Boy Advance
 * over the GameCube-GBA Link Cable (DOL-011) using JOY-bus multiboot.  Once it
 * is running the GBA is a save-memory peripheral: the GameCube asks it to
 * identify the inserted cartridge, to read the cartridge save out, and - only
 * after the GameCube has confirmed that a complete, checksum-matched image
 * arrived - to program that image back.  See SOURCES.md for provenance of the
 * multiboot and save-memory sequences.
 *
 * Deliberate scope: SRAM 32 KiB and Flash 64/128 KiB are read and written.
 * EEPROM is detected and reported, but never read or programmed - every
 * Generation III Pokemon cartridge is 128 KiB Flash, so an untested EEPROM
 * programming path would be pure risk for no benefit to this application.
 */
#include <stdint.h>

#include "gba_save_checksum.h"

typedef volatile uint8_t  vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;

#define REG_SOUNDCNT_X (*(vu16 *)0x04000084u)
#define REG_SOUNDBIAS  (*(vu16 *)0x04000088u)
#define REG_DMA3SAD    (*(vu32 *)0x040000D4u)
#define REG_DMA3DAD    (*(vu32 *)0x040000D8u)
#define REG_DMA3CNT    (*(vu32 *)0x040000DCu)
#define REG_JOYCNT     (*(vu16 *)0x04000140u)
#define REG_JOY_RECV   (*(vu32 *)0x04000150u)
#define REG_JOY_TRANS  (*(vu32 *)0x04000154u)
#define REG_IME        (*(vu16 *)0x04000208u)
#define REG_WAITCNT    (*(vu16 *)0x04000204u)

/* JOYCNT flags are written back as 1 to acknowledge them. */
#define JOYCNT_RESET_FLAG 0x0001u
#define JOYCNT_RECV_FLAG  0x0002u /* the GameCube wrote a word to us */
#define JOYCNT_SEND_FLAG  0x0004u /* the GameCube read our staged word */
#define JOYCNT_ACK        (JOYCNT_RECV_FLAG | JOYCNT_SEND_FLAG)

#define ROM_BASE   ((vu8 *)0x08000000u)
#define ROM_BASE32 ((vu32 *)0x08000000u)
#define SRAM_BASE  ((vu8 *)0x0E000000u)
#define FLASH_CMD0 (*(vu8 *)0x0E005555u)
#define FLASH_CMD1 (*(vu8 *)0x0E002AAAu)
#define FLASH_BANK (*(vu8 *)0x0E000000u)

/* Wire protocol.  Every word below is a GBA-native uint32_t; the GameCube
 * side byte-swaps scalars and passes save payload words through untouched. */
#define AGENT_MAGIC  0x504B4147u /* 'PKAG' - agent is idle and ready */
#define AGENT_PONG   0x504B4F4Bu /* 'PKOK' - reply to CMD_PING */

#define CMD_PING     0x50494E47u
#define CMD_IDENTIFY 0x49444E54u
#define CMD_READ     0x52454144u
#define CMD_WRITE    0x57524954u

#define ST_OK          0u
#define ST_NO_CART     1u
#define ST_NO_SAVE     2u
#define ST_UNSUPPORTED 3u
#define ST_SIZE_ERROR  4u
#define ST_WRITE_ERROR 5u
#define ST_ABORTED     6u
/*
 * A chip erase is the point of no return: once it starts, the save that was
 * on the cartridge is gone whatever happens next. These are split from
 * ST_WRITE_ERROR so the host can say which side of that line it failed on,
 * because "the cartridge did not accept the write" is a very different
 * sentence depending on the answer.
 */
#define ST_ERASE_ERROR   7u   /* erase did not confirm; the save may be gone */
#define ST_PROGRAM_ERROR 8u   /* erased, then programming failed: it IS gone */
#define ST_VERIFY_ERROR  9u   /* programmed, then read back wrong */

/* Published while the agent is doing something too slow to keep the JOY-bus
 * handshake lock-stepped; the host polls until it reads anything else. */
#define ST_BUSY        0xB5B5B5B5u

#define SAVE_NONE        0u
#define SAVE_EEPROM_512  1u
#define SAVE_EEPROM_8K   2u
#define SAVE_SRAM_32K    3u
#define SAVE_FLASH_64K   4u
#define SAVE_FLASH_128K  5u

#define MAX_SAVE_SIZE 0x20000u

/*
 * Bounded so a dead or absent Flash chip can never hang the console. Each spin
 * is a volatile SRAM read at eight waitstates, so roughly fourteen cycles:
 * eight million of them is about seven seconds at 16.78 MHz.
 *
 * Erase gets its own, much longer limit. Programming a byte is tens of
 * microseconds and seven seconds is enormously generous; a chip erase is
 * seconds by design, and datasheet maxima for the parts in these cartridges
 * run well past seven. Timing out an erase that was actually still working
 * would report a failure for a cartridge whose save is already gone, which is
 * the worst answer available.
 */
#define FLASH_POLL_LIMIT 8000000u
#define FLASH_ERASE_POLL_LIMIT 40000000u

static uint8_t save_buffer[MAX_SAVE_SIZE] __attribute__((aligned(4)));

/* ---------------------------------------------------------------- JOY bus */

static uint32_t joy_recv(void)
{
    while ((REG_JOYCNT & JOYCNT_RECV_FLAG) == 0) { }
    REG_JOYCNT |= JOYCNT_ACK;
    return REG_JOY_RECV;
}

/*
 * Lock-stepped word exchange.  It is only correct while the agent can stage
 * the next word within a few instructions of the previous one being read,
 * which the host's inter-transfer delay comfortably covers.  Anything slower
 * must go through joy_publish_busy()/joy_publish() instead.
 */
static void joy_send(uint32_t value)
{
    REG_JOY_TRANS = value;
    while ((REG_JOYCNT & JOYCNT_SEND_FLAG) == 0) { }
    REG_JOYCNT |= JOYCNT_ACK;
}

/*
 * Announce "working on it".  REG_JOY_TRANS answers the host's reads from
 * hardware, so the host can poll it while this CPU is busy elsewhere.  It
 * deliberately does not touch the flags: acknowledging here could swallow a
 * host write that has already landed.
 */
static void joy_publish_busy(void)
{
    REG_JOY_TRANS = ST_BUSY;
}

/* Publish a result the host is polling for and wait for its acknowledgement,
 * which also clears the flags left set by the polling reads and puts both
 * sides back in lock-step. */
static void joy_publish(uint32_t value)
{
    REG_JOY_TRANS = value;
    (void)joy_recv();
}

/* The GameCube speaks to the agent over the JOY bus; these names keep the
 * command protocol below independent of the transport primitives above. */
static uint32_t wire_recv(void)
{
    return joy_recv();
}

static void wire_send(uint32_t value)
{
    joy_send(value);
}

static void wire_publish_busy(void)
{
    joy_publish_busy();
}

static void wire_publish(uint32_t value)
{
    joy_publish(value);
}

/* ------------------------------------------------------- cartridge probing */

/* Beyond the end of a cartridge the AGB ROM bus floats to a value derived
 * from the address, so a region that reads back as its own halfword index is
 * past the last populated mirror.  Sizes below 1 MiB do not exist. */
static uint32_t rom_size(void)
{
    uint32_t size;
    for (size = (1u << 20); size < (1u << 25); size <<= 1)
    {
        const vu16 *probe = (vu16 *)(0x08000000u + size);
        unsigned j;
        int past_end = 1;
        for (j = 0; j < 0x1000u; ++j)
        {
            if (probe[j] != (uint16_t)j) { past_end = 0; break; }
        }
        if (past_end) break;
    }
    return size;
}

static int cart_present(void)
{
    /* First word of the Nintendo logo in the cartridge header. */
    return *(vu32 *)0x08000004u == 0x51AEFF24u;
}

/* Every retail Generation III Pokemon RPG uses 1 Mbit (128 KiB) Flash.  The
 * three-byte product-code prefixes are stable across regions/revisions:
 * AXV Ruby, AXP Sapphire, BPE Emerald, BPR FireRed, BPG LeafGreen.  Prefer
 * this header fact before doing a full ROM library-string scan; it makes the
 * exact use-case PKHeX-GC cares about independent of long ROM-bus probing. */
static int pokemon_gen3_flash1m(void)
{
    const vu8 *code = (const vu8 *)0x080000ACu;
    if (code[0] == 'A' && code[1] == 'X' && (code[2] == 'V' || code[2] == 'P'))
        return 1;
    if (code[0] == 'B' && code[1] == 'P' &&
        (code[2] == 'E' || code[2] == 'R' || code[2] == 'G'))
        return 1;
    return 0;
}

static uint32_t cartridge_rom_size(void)
{
    /* All five retail Gen III Pokemon RPGs are 16 MiB.  Avoid walking the
     * entire ROM bus for the primary PKHeX-GC use-case; the generic scanner
     * remains for other cartridges supported through the cable backend. */
    if (pokemon_gen3_flash1m()) return 0x01000000u;
    return rom_size();
}

/*
 * Every official cartridge embeds the save-library identifier string of the
 * backup memory it was built against ("FLASH1M_", "FLASH_", "FLASH512_",
 * "EEPROM_", "SRAM_").  Scanning the ROM for it is the standard way to learn
 * the save type without writing anything to the cartridge.
 */
static uint32_t detect_save_type(uint32_t rom_bytes)
{
    if (pokemon_gen3_flash1m()) return SAVE_FLASH_128K;
    if (rom_bytes == 0) return SAVE_NONE;
    uint32_t words = rom_bytes / 4u;
    if (words == 0) return SAVE_NONE;

    for (uint32_t x = words - 1u; x > 0u; --x)
    {
        const uint32_t id = ROM_BASE32[x];
        const uint32_t next = ROM_BASE32[x + 1u];
        if (id == 0x53414C46u) /* "FLAS" */
        {
            if (next == 0x5F4D3148u) return SAVE_FLASH_128K; /* "H1M_" */
            if (next == 0x32313548u) return SAVE_FLASH_64K;  /* "H512" */
            if ((next & 0x0000FFFFu) == 0x00005F48u) return SAVE_FLASH_64K; /* "H_" */
        }
        else if (id == 0x52504545u) /* "EEPR" */
        {
            if ((next & 0x00FFFFFFu) == 0x005F4D4Fu) return SAVE_EEPROM_8K; /* "OM_" */
        }
        else if (id == 0x4D415253u) /* "SRAM" */
        {
            if ((next & 0x000000FFu) == 0x0000005Fu) return SAVE_SRAM_32K; /* "_" */
        }
    }
    return SAVE_NONE;
}

static uint32_t save_size_for(uint32_t save_type)
{
    switch (save_type)
    {
        case SAVE_EEPROM_512: return 0x200u;
        case SAVE_EEPROM_8K:  return 0x2000u;
        case SAVE_SRAM_32K:   return 0x8000u;
        case SAVE_FLASH_64K:  return 0x10000u;
        case SAVE_FLASH_128K: return 0x20000u;
        default:              return 0u;
    }
}

static int save_type_supported(uint32_t save_type)
{
    return save_type == SAVE_SRAM_32K ||
           save_type == SAVE_FLASH_64K ||
           save_type == SAVE_FLASH_128K;
}

/* --------------------------------------------------------------- Flash I/O */

static void flash_command(uint8_t code)
{
    FLASH_CMD0 = 0xAAu;
    FLASH_CMD1 = 0x55u;
    FLASH_CMD0 = code;
}

static void flash_reset(void)
{
    flash_command(0x90u);
    FLASH_BANK = 0xF0u;
}

static void flash_select_bank(uint8_t bank)
{
    flash_command(0xB0u);
    FLASH_BANK = bank;
}

/*
 * DQ7 data polling: the chip returns the complement of the written bit while
 * an erase or program cycle is in progress and the true value once it has
 * finished.  Waiting for the expected byte therefore both synchronises and
 * verifies, unlike a bare toggle-bit poll.
 */
static int flash_wait(const vu8 *cell, uint8_t expected)
{
    for (uint32_t spin = 0; spin < FLASH_POLL_LIMIT; ++spin)
        if (*cell == expected) return 1;
    return 0;
}

static int flash_wait_limit(const vu8 *cell, uint8_t expected, uint32_t limit)
{
    for (uint32_t spin = 0; spin < limit; ++spin)
        if (*cell == expected) return 1;
    return 0;
}

static int flash_erase_chip(void)
{
    flash_command(0x80u);
    flash_command(0x10u);
    return flash_wait_limit(SRAM_BASE, 0xFFu, FLASH_ERASE_POLL_LIMIT);
}

/*
 * Erase leaves every byte 0xFF and programming can only clear bits, so a byte
 * that wants to be 0xFF is already there. Skipping those is not just faster:
 * it shortens the window in which the cartridge holds no valid save, and on a
 * Generation III save most of the trailing sectors are erased anyway.
 */
static int flash_program_bank(const uint8_t *src)
{
    for (uint32_t i = 0; i < 0x10000u; ++i)
    {
        if (src[i] == 0xFFu) continue;
        flash_command(0xA0u);
        SRAM_BASE[i] = src[i];
        if (!flash_wait(SRAM_BASE + i, src[i])) return 0;
    }
    return 1;
}

/* Read every byte back on the cartridge itself, so a bad write is caught here
 * rather than only after the whole image has crossed the link again. */
static int flash_verify_bank(const uint8_t *src)
{
    for (uint32_t i = 0; i < 0x10000u; ++i)
        if (SRAM_BASE[i] != src[i]) return 0;
    return 1;
}

static void flash_read_bank(uint8_t *dst)
{
    for (uint32_t i = 0; i < 0x10000u; ++i)
        dst[i] = SRAM_BASE[i];
}

/* ---------------------------------------------------------- save read/write */

static uint32_t save_read(uint32_t save_type, uint8_t *dst)
{
    const uint32_t size = save_size_for(save_type);
    switch (save_type)
    {
        case SAVE_SRAM_32K:
            for (uint32_t i = 0; i < size; ++i) dst[i] = SRAM_BASE[i];
            return ST_OK;
        case SAVE_FLASH_64K:
            flash_reset();
            flash_read_bank(dst);
            return ST_OK;
        case SAVE_FLASH_128K:
            flash_reset();
            flash_select_bank(0u);
            flash_read_bank(dst);
            flash_select_bank(1u);
            flash_read_bank(dst + 0x10000u);
            flash_select_bank(0u);
            return ST_OK;
        default:
            return ST_UNSUPPORTED;
    }
}

static uint32_t save_write(uint32_t save_type, const uint8_t *src)
{
    switch (save_type)
    {
        case SAVE_SRAM_32K:
            /* Static RAM: no erase, nothing to lose, and a byte-for-byte
             * check costs nothing. */
            for (uint32_t i = 0; i < 0x8000u; ++i) SRAM_BASE[i] = src[i];
            for (uint32_t i = 0; i < 0x8000u; ++i)
                if (SRAM_BASE[i] != src[i]) return ST_VERIFY_ERROR;
            return ST_OK;

        case SAVE_FLASH_64K:
            flash_reset();
            if (!flash_erase_chip()) return ST_ERASE_ERROR;
            /* Past this line the old save no longer exists. */
            if (!flash_program_bank(src)) return ST_PROGRAM_ERROR;
            if (!flash_verify_bank(src)) return ST_VERIFY_ERROR;
            flash_reset();
            return ST_OK;

        case SAVE_FLASH_128K:
            flash_reset();
            flash_select_bank(0u);
            if (!flash_erase_chip()) return ST_ERASE_ERROR;
            /* Past this line the old save no longer exists, in both banks:
             * a chip erase clears the whole part, not the selected bank. */
            flash_select_bank(0u);
            if (!flash_program_bank(src)) return ST_PROGRAM_ERROR;
            if (!flash_verify_bank(src)) return ST_VERIFY_ERROR;
            flash_select_bank(1u);
            if (!flash_program_bank(src + 0x10000u)) return ST_PROGRAM_ERROR;
            if (!flash_verify_bank(src + 0x10000u)) return ST_VERIFY_ERROR;
            flash_select_bank(0u);
            flash_reset();
            return ST_OK;

        default:
            return ST_UNSUPPORTED;
    }
}

/* ------------------------------------------------------------------ command */

static void send_block(const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 4u)
        wire_send(*(const uint32_t *)(data + i));
}

static void recv_block(uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 4u)
        *(uint32_t *)(data + i) = wire_recv();
}

static uint32_t probe_status(uint32_t type)
{
    if (save_type_supported(type)) return ST_OK;
    return type == SAVE_NONE ? ST_NO_SAVE : ST_UNSUPPORTED;
}

static void do_identify(void)
{
    wire_publish_busy();
    if (!cart_present()) { wire_publish(ST_NO_CART); return; }

    const uint32_t bytes = cartridge_rom_size();
    const uint32_t type = detect_save_type(bytes);

    wire_publish(probe_status(type));
    wire_send(bytes);
    wire_send(type);
    wire_send(save_size_for(type));
    send_block((const uint8_t *)0x08000000u, 0xC0u); /* cartridge header */
}

static void do_read(void)
{
    wire_publish_busy();
    if (!cart_present()) { wire_publish(ST_NO_CART); return; }

    const uint32_t type = detect_save_type(cartridge_rom_size());
    const uint32_t size = save_size_for(type);
    if (!save_type_supported(type)) { wire_publish(probe_status(type)); return; }

    const uint32_t status = save_read(type, save_buffer);
    wire_publish(status);
    if (status != ST_OK) return;

    wire_send(size);
    send_block(save_buffer, size);
    wire_send(gba_save_checksum(save_buffer, size));
}

static void do_write(void)
{
    wire_publish_busy();
    if (!cart_present()) { wire_publish(ST_NO_CART); return; }

    const uint32_t type = detect_save_type(cartridge_rom_size());
    const uint32_t size = save_size_for(type);
    if (!save_type_supported(type)) { wire_publish(probe_status(type)); return; }

    wire_publish(ST_OK);
    wire_send(size);

    /* The host announces the image size it is about to stream. */
    if (wire_recv() != size) { wire_send(ST_SIZE_ERROR); return; }
    wire_send(ST_OK);

    recv_block(save_buffer, size);
    wire_send(gba_save_checksum(save_buffer, size));

    /*
     * Nothing has been written to the cartridge yet.  The host compares the
     * checksum above against the image it sent and only authorises the
     * erase/program cycle if they match, so a truncated or corrupted transfer
     * can never destroy the existing save.
     */
    /*
     * Stage the busy sentinel before waiting for the verdict.  Until this
     * runs REG_JOY_TRANS still holds the checksum above, and the host must
     * never be able to poll that and mistake it for the write result.  The
     * host does not read again until after it has sent the verdict, so this
     * window is closed.
     */
    wire_publish_busy();
    const uint32_t commit = wire_recv();
    wire_publish(commit == 1u ? save_write(type, save_buffer) : ST_ABORTED);
}

void agent_main(void)
{
    REG_IME = 0;
    REG_SOUNDCNT_X = 0;
    REG_SOUNDBIAS = 0;
    /* SRAM/Flash need 8 wait states; ROM and EEPROM use the SDK defaults. */
    REG_WAITCNT = 0x0317u;

    REG_JOY_TRANS = AGENT_MAGIC;
    REG_JOYCNT |= JOYCNT_RESET_FLAG | JOYCNT_ACK;

    for (;;)
    {
        wire_send(AGENT_MAGIC);
        switch (wire_recv())
        {
            case CMD_PING:     wire_publish(AGENT_PONG); break;
            case CMD_IDENTIFY: do_identify(); break;
            case CMD_READ:     do_read(); break;
            case CMD_WRITE:    do_write(); break;
            default: break;
        }
    }
}
