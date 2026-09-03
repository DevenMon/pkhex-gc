#ifndef PKHEX_GC_JOYBOOT_H
#define PKHEX_GC_JOYBOOT_H

#include <stdint.h>

/*
 * Pure arithmetic of the GBA JOY-bus multiboot ("JoyBoot") handshake.
 *
 * The GBA BIOS accepts a multiboot image only if the host answers its session
 * key correctly and streams the body encrypted with a key stream derived from
 * it, terminated by a checksum over the plaintext.  None of this touches
 * hardware, so it lives here and is covered by host tests; source/gbalink.c
 * supplies the serial transport.
 */

/* Derived from the padded image size and sent to the GBA before the body. */
uint32_t joyboot_key(uint32_t send_size);

/* Running checksum over the *plaintext* body words, seeded with 0x15A0. */
#define JOYBOOT_CRC_SEED 0x15A0u
uint32_t joyboot_crc_step(uint32_t crc, uint32_t plain_word);

/* The GBA announces its session key as a raw word; this turns it into the
 * initial key-stream state. */
uint32_t joyboot_session_key(uint32_t raw_word);

/* Advance the key stream and encrypt one body word.  offset is the byte
 * offset of the word within the image (0xC0 for the first body word). */
uint32_t joyboot_encrypt(uint32_t *session_key, uint32_t plain_word,
                         uint32_t offset);

/* Encrypt the terminating checksum word.  crc must already have the image
 * size folded into its high halfword; offset is the padded image size. */
uint32_t joyboot_finish(uint32_t *session_key, uint32_t crc, uint32_t offset);

#endif
