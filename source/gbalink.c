/*
 * PKHeX-GC GameCube <-> Game Boy Advance link-cable transport.
 *
 * The GameCube reaches a Game Boy Advance through its serial interface (SI)
 * with a DOL-011 GameCube-GBA Link Cable.  A GBA sitting in its BIOS multiboot
 * wait state answers the JOY-bus commands below, which is how PKHeX-GC uploads
 * the save agent in gba-agent/ and then talks to it.
 *
 * Wire notes.  A JOY-bus word arrives as four bytes in cartridge order.  A
 * scalar therefore has to be assembled little-endian on this big-endian host,
 * while save payload bytes are passed straight through so a dump is
 * byte-identical to the cartridge.  Both directions are spelled out
 * explicitly below rather than relying on a byte-swap intrinsic.
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/exi.h>
#include <ogc/si.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gba_link_wire.h"
#include "gba_save_checksum.h"
#include "gbalink.h"
#include "joyboot.h"
#include "gba_agent_size.h"

extern const unsigned char gba_agent_bin[];

/* 50 us is the shortest inter-transfer delay that is reliable in practice, and
 * it is also what keeps the agent's lock-stepped word handshake correct. */
#define SI_TRANS_DELAY_US 50u
#define SI_TIMEOUT_MS     250u

/* JOY-bus commands. */
#define JOY_CMD_STATUS 0x00u
#define JOY_CMD_READ   0x14u
#define JOY_CMD_WRITE  0x15u
#define JOY_CMD_RESET  0xFFu

/* Status byte 2, bit 4: the GBA is in its BIOS multiboot wait state. */
#define JOY_STATUS_MULTIBOOT 0x10u

/* Must match gba-agent/agent.c. */
#define AGENT_MAGIC  0x504B4147u
#define AGENT_PONG   0x504B4F4Bu
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
/* Split out of ST_WRITE_ERROR: which side of the chip erase it failed on
 * decides whether the cartridge still holds a save. Kept in step with
 * gba-agent/agent.c, which is where they are raised. */
#define ST_ERASE_ERROR   7u
#define ST_PROGRAM_ERROR 8u
#define ST_VERIFY_ERROR  9u
#define ST_BUSY        0xB5B5B5B5u

#define AGENT_SAVE_NONE       0u
#define AGENT_SAVE_EEPROM_512 1u
#define AGENT_SAVE_EEPROM_8K  2u
#define AGENT_SAVE_SRAM_32K   3u
#define AGENT_SAVE_FLASH_64K  4u
#define AGENT_SAVE_FLASH_128K 5u

static uint8_t si_out[32] ATTRIBUTE_ALIGN(32);
static uint8_t si_in[32] ATTRIBUTE_ALIGN(32);
static volatile uint32_t si_completions;

static void set_error(char *error, size_t error_size, const char *msg)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", msg ? msg : "GBA link transfer failed.");
}

static void report(GbaLinkProgressFn progress, void *user, const char *stage,
                   size_t done, size_t total)
{
    if (progress) progress(stage, done, total, user);
}

static void si_callback(s32 chan, u32 ret)
{
    (void)chan;
    (void)ret;
    ++si_completions;
}

/* One SI transaction.  The completion counter (rather than a flag) means a
 * transfer that timed out cannot be mistaken for the next one completing. */
static bool si_run(int chan, uint32_t out_len, uint32_t in_len)
{
    const uint32_t expected = si_completions + 1u;
    const u64 start = gettime();

    while (!SI_Transfer(chan, si_out, out_len, si_in, in_len,
                        si_callback, SI_TRANS_DELAY_US))
        if (diff_msec(start, gettime()) >= SI_TIMEOUT_MS) return false;

    while (si_completions != expected)
        if (diff_msec(start, gettime()) >= SI_TIMEOUT_MS) return false;

    return true;
}

static bool joy_reset(int chan)
{
    si_out[0] = JOY_CMD_RESET;
    return si_run(chan, 1u, 3u);
}

static bool joy_status(int chan, uint8_t status[3])
{
    memset(si_in, 0, 4);
    si_out[0] = JOY_CMD_STATUS;
    if (!si_run(chan, 1u, 3u)) return false;
    memcpy(status, si_in, 3);
    return true;
}

/* Read one word; raw[] receives the four bytes in cartridge order. */
static bool joy_read(int chan, uint8_t raw[4])
{
    memset(si_in, 0, 8);
    si_out[0] = JOY_CMD_READ;
    if (!si_run(chan, 1u, 5u)) return false;
    memcpy(raw, si_in, 4);
    return true;
}

static bool joy_write(int chan, const uint8_t raw[4])
{
    si_out[0] = JOY_CMD_WRITE;
    memcpy(si_out + 1, raw, 4);
    si_in[0] = 0;
    return si_run(chan, 5u, 1u);
}

static bool link_recv(int chan, uint32_t *value)
{
    uint8_t raw[4];
    if (!joy_read(chan, raw)) return false;
    *value = gba_wire_to_scalar(raw);
    return true;
}

static bool link_send(int chan, uint32_t value)
{
    uint8_t raw[4];
    gba_scalar_to_wire(raw, value);
    return joy_write(chan, raw);
}

/*
 * Wait for a result the agent publishes while it is busy with the cartridge.
 * REG_JOY_TRANS answers from GBA hardware, so polling it is safe even though
 * the agent's CPU is not servicing the handshake; the acknowledgement write
 * afterwards puts both sides back in lock-step.
 */
static bool link_poll(int chan, uint32_t *value, uint32_t timeout_ms,
                     GbaLinkProgressFn progress, void *user, const char *stage)
{
    const u64 start = gettime();
    uint32_t last_tick = 0;
    for (;;)
    {
        uint32_t word;
        if (!link_recv(chan, &word)) return false;
        if (word != ST_BUSY && word != AGENT_MAGIC)
        {
            *value = word;
            return link_send(chan, 0u);
        }

        const uint32_t elapsed = diff_msec(start, gettime());
        if (elapsed >= timeout_ms) return false;
        /* Erasing and programming Flash takes tens of seconds; keep the
         * caller's UI alive rather than letting it look hung. */
        if (progress && elapsed - last_tick >= 500u)
        {
            last_tick = elapsed;
            report(progress, user, stage, elapsed / 1000u, 0);
        }
    }
}

/* Resynchronise with the agent's idle state and hand it a command. */
static bool link_command(int chan, uint32_t command)
{
    for (unsigned tries = 0; tries < 64u; ++tries)
    {
        uint32_t word;
        if (!link_recv(chan, &word)) return false;
        if (word == AGENT_MAGIC) return link_send(chan, command);
    }
    return false;
}

/* ------------------------------------------------------------- device setup */

const char *gbalink_save_type_name(GbaLinkSaveType type)
{
    switch (type)
    {
        case GBALINK_SAVE_EEPROM_512: return "EEPROM 512 B";
        case GBALINK_SAVE_EEPROM_8K:  return "EEPROM 8 KiB";
        case GBALINK_SAVE_SRAM_32K:   return "SRAM 32 KiB";
        case GBALINK_SAVE_FLASH_64K:  return "Flash 64 KiB";
        case GBALINK_SAVE_FLASH_128K: return "Flash 128 KiB";
        default:                      return "none";
    }
}

static GbaLinkSaveType save_type_from_agent(uint32_t value)
{
    switch (value)
    {
        case AGENT_SAVE_EEPROM_512: return GBALINK_SAVE_EEPROM_512;
        case AGENT_SAVE_EEPROM_8K:  return GBALINK_SAVE_EEPROM_8K;
        case AGENT_SAVE_SRAM_32K:   return GBALINK_SAVE_SRAM_32K;
        case AGENT_SAVE_FLASH_64K:  return GBALINK_SAVE_FLASH_64K;
        case AGENT_SAVE_FLASH_128K: return GBALINK_SAVE_FLASH_128K;
        default:                    return GBALINK_SAVE_NONE;
    }
}

/*
 * Every serial channel and every EXI device, recorded rather than judged.
 *
 * gbalink_find_gba() accepted a channel only when SI_GetType set the SI_GBA
 * bit, and told nobody what the other three said. That is the wrong test for
 * anything that is not a link cable: the JoyBus identify command is what
 * actually names a device, and a Game Boy Advance answers it with device id
 * 0x0004 whatever SI_GetType decided to report. Status bit 4 of the reply then
 * says whether it is sitting in its BIOS multiboot wait, which is the state the
 * agent can be uploaded into.
 *
 * Everything here is read-only: a JoyBus reset and a status request, and an EXI
 * ID read. Nothing is written to any device.
 */
void gbalink_scan_ports(GbaLinkPortScan *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->gba_chan = -1;

    for (int chan = SI_CHAN0; chan < SI_MAX_CHAN && chan < GBALINK_PORTS; ++chan)
    {
        const u32 type = SI_GetType(chan);
        out->type[chan] = type;
        out->responded[chan] = (type & SI_ERROR_NO_RESPONSE) == 0;

        /* Ask anyway. A device that does not set SI_GBA may still answer the
         * JoyBus identify, and that is the whole point of recording this. */
        (void)joy_reset(chan);
        uint8_t status[3] = {0, 0, 0};
        if (joy_status(chan, status))
        {
            out->status_ok[chan] = true;
            memcpy(out->status[chan], status, 3);
            out->looks_gba[chan] = status[0] == 0x00u && status[1] == 0x04u;
            out->multiboot[chan] = (status[2] & JOY_STATUS_MULTIBOOT) != 0;
            if (out->looks_gba[chan] && out->gba_chan < 0) out->gba_chan = chan;
        }
    }

    for (int chan = 0; chan < GBALINK_EXI_CHANNELS; ++chan)
        for (int dev = 0; dev < GBALINK_EXI_DEVICES; ++dev)
        {
            u32 id = 0;
            if (EXI_GetID(chan, dev, &id)) {
                out->exi_ok[chan][dev] = true;
                out->exi_id[chan][dev] = id;
            }
        }
}

/*
 * Find a Game Boy Advance on any serial channel.
 *
 * The SI_GBA bit of SI_GetType is a hint, not the test. What names a device is
 * the JoyBus identify reply: a Game Boy Advance answers device id 0x0004, and
 * bit 4 of the third byte says it is sitting in its BIOS multiboot wait. This
 * used to accept a channel only when SI_GetType set SI_GBA and skip everything
 * else without recording it, so anything that answers JoyBus without setting
 * that bit was invisible.
 *
 * The type bit is still tried first, because on a plain link cable it is the
 * cheap answer. A channel that fails it is then asked directly.
 */
bool gbalink_find_gba(int *chan_out, char *error, size_t error_size)
{
    if (!chan_out) return false;

    for (int chan = SI_CHAN0; chan < SI_MAX_CHAN; ++chan)
    {
        const u32 type = SI_GetType(chan);
        if (type & SI_ERROR_NO_RESPONSE) continue;
        if (type & SI_GBA)
        {
            *chan_out = chan;
            return true;
        }
    }

    /* Nothing declared itself. Ask every channel what it is. */
    for (int chan = SI_CHAN0; chan < SI_MAX_CHAN; ++chan)
    {
        (void)joy_reset(chan);
        uint8_t status[3] = {0, 0, 0};
        if (!joy_status(chan, status)) continue;
        if (status[0] == 0x00u && status[1] == 0x04u)
        {
            *chan_out = chan;
            return true;
        }
    }

    set_error(error, error_size,
              "No Game Boy Advance answered on any serial channel.\n"
              "Neither the port type nor a JOY-bus identify found one.");
    return false;
}

/* ---------------------------------------------------------------- multiboot */

bool gbalink_boot_agent(int chan, char *error, size_t error_size,
                        GbaLinkProgressFn progress, void *user)
{
    report(progress, user, "Waiting for GBA BIOS", 0, 0);

    uint8_t status[3] = {0, 0, 0};
    const u64 wait_start = gettime();
    while ((status[2] & JOY_STATUS_MULTIBOOT) == 0)
    {
        if (!joy_reset(chan) || !joy_status(chan, status))
        {
            set_error(error, error_size,
                      "Lost contact with the GBA while waiting for its BIOS.");
            return false;
        }
        if (diff_msec(wait_start, gettime()) >= 10000u)
        {
            set_error(error, error_size,
                      "The GBA never entered multiboot mode.\n"
                      "Switch it on with NO cartridge inserted, or hold Start+Select\n"
                      "while switching it on to stop the cartridge from booting.");
            return false;
        }
    }

    /*
     * From here to the final checksum the multiboot stream must run without a
     * long pause, and this application's progress callback presents a frame
     * and waits for VSync.  So report before the stream starts and after it
     * ends, and nothing in between - the upload is a few kilobytes anyway.
     */
    report(progress, user, "Uploading GBA agent", 0, 0);

    /* JoyBoot's key derivation is defined over an image padded to 8 bytes. */
    const uint32_t send_size = (GBA_AGENT_BIN_SIZE + 7u) & ~7u;
    const uint32_t our_key = joyboot_key(send_size);

    uint32_t raw_session = 0;
    uint8_t raw[4];
    if (!joy_read(chan, raw))
    {
        set_error(error, error_size, "The GBA did not offer a multiboot session key.");
        return false;
    }
    /* The session key is folded in as the word came off the wire. */
    raw_session = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
                  ((uint32_t)raw[2] << 8) | (uint32_t)raw[3];
    uint32_t session_key = joyboot_session_key(raw_session);

    /*
     * The key is the one word the BIOS itself consumes rather than the program
     * we are uploading, and it wants the opposite byte order to everything
     * else on this link: most significant byte first.
     */
    const uint8_t key_raw[4] = {
        (uint8_t)(our_key >> 24), (uint8_t)(our_key >> 16),
        (uint8_t)(our_key >> 8), (uint8_t)our_key,
    };
    if (!joy_write(chan, key_raw))
    {
        set_error(error, error_size, "Could not send the multiboot session key.");
        return false;
    }

    /* The 0xC0-byte cartridge header travels unencrypted. */
    for (uint32_t i = 0; i < 0xC0u; i += 4u)
    {
        if (!joy_write(chan, gba_agent_bin + i))
        {
            set_error(error, error_size, "Multiboot header transfer failed.");
            return false;
        }
    }

    uint32_t crc = JOYBOOT_CRC_SEED;
    uint32_t i;
    for (i = 0xC0u; i < send_size; i += 4u)
    {
        const uint32_t plain = gba_wire_to_scalar(gba_agent_bin + i);
        crc = joyboot_crc_step(crc, plain);
        if (!link_send(chan, joyboot_encrypt(&session_key, plain, i)))
        {
            set_error(error, error_size, "Multiboot payload transfer failed.");
            return false;
        }
    }

    crc |= (send_size << 16);
    if (!link_send(chan, joyboot_finish(&session_key, crc, i)))
    {
        set_error(error, error_size, "Multiboot checksum transfer failed.");
        return false;
    }
    /* The GBA echoes its own checksum; the BIOS has already accepted or
     * rejected the image by this point, so it is read only to stay in step. */
    (void)joy_read(chan, raw);

    report(progress, user, "Starting GBA agent", 0, 0);

    /* Give the BIOS time to hand control to the agent, then prove it is up. */
    const u64 boot_start = gettime();
    for (;;)
    {
        uint32_t pong = 0;
        if (link_command(chan, CMD_PING) && link_poll(chan, &pong, 2000u, NULL, NULL, NULL) &&
            pong == AGENT_PONG)
            return true;
        if (diff_msec(boot_start, gettime()) >= 5000u) break;
    }

    set_error(error, error_size,
              "The GBA accepted the multiboot upload but the agent never answered.");
    return false;
}

/* ------------------------------------------------------------------ commands */

static const char *status_message(uint32_t status)
{
    switch (status)
    {
        case ST_NO_CART:
            return "No Game Boy Advance cartridge is inserted.";
        case ST_NO_SAVE:
            return "That cartridge reports no save memory.";
        case ST_UNSUPPORTED:
            return "That cartridge uses EEPROM save memory, which PKHeX-GC does not\n"
                   "read or write. Every Generation III Pokemon cartridge is Flash.";
        case ST_SIZE_ERROR:
            return "The GBA rejected the image size.";
        case ST_WRITE_ERROR:
            return "The cartridge did not accept the erase/program cycle.\n"
                   "The save may have been erased. Do not play the cartridge;\n"
                   "restore the backup from Backups first.";
        case ST_ERASE_ERROR:
            return "The Flash erase did not confirm within its time limit.\n"
                   "It may still have erased the save. Do not play the cartridge;\n"
                   "restore the backup from Backups, or retry the write.";
        case ST_PROGRAM_ERROR:
            return "The Flash was erased and then failed to program.\n"
                   "The cartridge now holds NO valid save. Restore the backup\n"
                   "from Backups before playing it.";
        case ST_VERIFY_ERROR:
            return "The cartridge did not read back what was written to it.\n"
                   "Its save is not trustworthy. Restore the backup from Backups.";
        case ST_ABORTED:
            return "The transfer was abandoned before the cartridge was touched.";
        default:
            return "The GBA agent reported an unexpected status.";
    }
}

static void copy_header_field(char *dst, size_t dst_size, const uint8_t *src, size_t len)
{
    size_t n = 0;
    for (; n < len && n + 1 < dst_size; ++n)
    {
        const uint8_t c = src[n];
        dst[n] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
    }
    while (n > 0 && dst[n - 1] == ' ') --n;
    dst[n] = '\0';
}

bool gbalink_identify(int chan, GbaLinkCart *out, char *error, size_t error_size)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->chan = chan;

    if (!link_command(chan, CMD_IDENTIFY))
    {
        set_error(error, error_size, "The GBA agent did not accept the identify command.");
        return false;
    }

    uint32_t status = 0;
    if (!link_poll(chan, &status, 45000u, NULL, NULL, NULL))
    {
        set_error(error, error_size, "The GBA agent stopped responding while reading the cartridge.");
        return false;
    }
    if (status == ST_NO_CART)
    {
        set_error(error, error_size, status_message(status));
        return false;
    }

    uint32_t rom_size = 0, type = 0, save_size = 0;
    if (!link_recv(chan, &rom_size) || !link_recv(chan, &type) ||
        !link_recv(chan, &save_size))
    {
        set_error(error, error_size, "Truncated cartridge description from the GBA agent.");
        return false;
    }
    for (uint32_t i = 0; i < GBALINK_HEADER_SIZE; i += 4u)
    {
        if (!joy_read(chan, out->header + i))
        {
            set_error(error, error_size, "Truncated cartridge header from the GBA agent.");
            return false;
        }
    }

    out->rom_size = rom_size;
    out->save_type = save_type_from_agent(type);
    out->save_size = save_size;
    out->writable = (status == ST_OK) && save_size != 0 &&
                    save_size <= GBALINK_MAX_SAVE_SIZE;
    copy_header_field(out->title, sizeof(out->title), out->header + 0xA0, 12);
    copy_header_field(out->gamecode, sizeof(out->gamecode), out->header + 0xAC, 4);
    copy_header_field(out->maker, sizeof(out->maker), out->header + 0xB0, 2);

    if (status != ST_OK) set_error(error, error_size, status_message(status));
    return status == ST_OK;
}

bool gbalink_read_save(const GbaLinkCart *cart, uint8_t *out, size_t out_size,
                       char *error, size_t error_size,
                       GbaLinkProgressFn progress, void *user)
{
    if (!cart || !out || !cart->save_size || cart->save_size > out_size)
    {
        set_error(error, error_size, "Invalid Game Boy Advance save read request.");
        return false;
    }
    const int chan = cart->chan;

    if (!link_command(chan, CMD_READ))
    {
        set_error(error, error_size, "The GBA agent did not accept the read command.");
        return false;
    }

    report(progress, user, "Reading cartridge save", 0, cart->save_size);
    uint32_t status = 0;
    if (!link_poll(chan, &status, 60000u, NULL, NULL, NULL))
    {
        set_error(error, error_size, "The GBA agent stopped responding while reading the save.");
        return false;
    }
    if (status != ST_OK)
    {
        set_error(error, error_size, status_message(status));
        return false;
    }

    uint32_t size = 0;
    if (!link_recv(chan, &size) || size != cart->save_size)
    {
        set_error(error, error_size, "The GBA agent announced an unexpected save size.");
        return false;
    }

    for (uint32_t i = 0; i < size; i += 4u)
    {
        if (!joy_read(chan, out + i))
        {
            set_error(error, error_size, "The save transfer was cut short.");
            return false;
        }
        if ((i & 0xFFFu) == 0) report(progress, user, "Reading cartridge save", i, size);
    }

    uint32_t their_sum = 0;
    if (!link_recv(chan, &their_sum))
    {
        set_error(error, error_size, "The GBA agent did not send a save checksum.");
        return false;
    }
    if (their_sum != gba_save_checksum(out, size))
    {
        set_error(error, error_size,
                  "The save arrived with a checksum mismatch. Nothing was written\n"
                  "anywhere; reseat the link cable and read again.");
        return false;
    }

    report(progress, user, "Reading cartridge save", size, size);
    return true;
}

bool gbalink_write_save(const GbaLinkCart *cart, const uint8_t *data, size_t size,
                        char *error, size_t error_size,
                        GbaLinkProgressFn progress, void *user)
{
    if (!cart || !data || !cart->writable || size != cart->save_size)
    {
        set_error(error, error_size, "Invalid Game Boy Advance save write request.");
        return false;
    }
    const int chan = cart->chan;

    if (!link_command(chan, CMD_WRITE))
    {
        set_error(error, error_size, "The GBA agent did not accept the write command.");
        return false;
    }

    uint32_t status = 0;
    if (!link_poll(chan, &status, 45000u, NULL, NULL, NULL))
    {
        set_error(error, error_size, "The GBA agent stopped responding before the write.");
        return false;
    }
    if (status != ST_OK)
    {
        set_error(error, error_size, status_message(status));
        return false;
    }

    uint32_t agent_size = 0;
    if (!link_recv(chan, &agent_size) || agent_size != size)
    {
        set_error(error, error_size,
                  "The cartridge save size changed since it was read. Nothing was written.");
        return false;
    }
    uint32_t accepted = 0;
    if (!link_send(chan, (uint32_t)size) || !link_recv(chan, &accepted))
    {
        set_error(error, error_size,
                  "Lost contact with the GBA agent. The cartridge was NOT written.");
        return false;
    }
    if (accepted != ST_OK)
    {
        set_error(error, error_size, status_message(accepted));
        return false;
    }

    report(progress, user, "Sending save to GBA", 0, size);
    for (uint32_t i = 0; i < size; i += 4u)
    {
        if (!joy_write(chan, data + i))
        {
            set_error(error, error_size,
                      "The save transfer was cut short. The cartridge was NOT written.");
            return false;
        }
        if ((i & 0xFFFu) == 0) report(progress, user, "Sending save to GBA", i, size);
    }

    /*
     * The agent has the image in RAM and has touched nothing yet.  Authorise
     * the erase/program cycle only if its checksum matches ours.
     */
    uint32_t their_sum = 0;
    if (!link_recv(chan, &their_sum))
    {
        set_error(error, error_size,
                  "The GBA agent did not confirm the transfer. The cartridge was NOT written.");
        return false;
    }
    const bool intact = their_sum == gba_save_checksum(data, (uint32_t)size);
    if (!link_send(chan, intact ? 1u : 0u))
    {
        set_error(error, error_size,
                  "Could not send the write authorisation. The cartridge was NOT written.");
        return false;
    }
    if (!intact)
    {
        (void)link_poll(chan, &status, 5000u, NULL, NULL, NULL);
        set_error(error, error_size,
                  "The image arrived corrupted, so the write was refused.\n"
                  "The cartridge save is untouched. Reseat the cable and retry.");
        return false;
    }

    report(progress, user, "Programming cartridge", size, size);
    if (!link_poll(chan, &status, 120000u, progress, user, "Programming cartridge"))
    {
        set_error(error, error_size,
                  "The GBA stopped responding during programming.\n"
                  "Do NOT switch the cartridge off; retry the write before playing.");
        return false;
    }
    if (status != ST_OK)
    {
        set_error(error, error_size, status_message(status));
        return false;
    }
    return true;
}
