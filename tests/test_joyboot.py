#!/usr/bin/env python3
"""Cross-check source/joyboot.c against an independent model of the handshake.

The model below is written straight from the documented GBA JOY-bus multiboot
sequence rather than from the C, so a transcription slip in either one shows up
as a mismatch in the encrypted stream.
"""
from __future__ import annotations
import pathlib, random, struct, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
M32 = 0xFFFFFFFF


def ref_key(send_size: int) -> int:
    size = (send_size - 0x200) >> 3
    res = ((size & 0x3F80) << 1) | ((size & 0x4000) << 2) | (size & 0x7F) | 0x380000
    res3 = ((res >> 8) + (res >> 16) + res) & M32
    res3 = ((res3 << 24) & M32) | res | 0x80808080
    xor = (0x4B, 0x61, 0x77, 0x61) if (res3 & 0x200) == 0 else (0x73, 0x65, 0x64, 0x6F)
    out = 0
    for i, x in enumerate(xor):
        out |= (((res3 >> (8 * i)) & 0xFF) ^ x) << (24 - 8 * i)
    return out & M32


def ref_crc(crc: int, val: int) -> int:
    for _ in range(32):
        if (crc ^ val) & 1:
            crc = (crc >> 1) ^ 0xA1C1
        else:
            crc >>= 1
        val >>= 1
    return crc & M32


def ref_session(raw: int) -> int:
    return int.from_bytes((raw ^ 0x7365646F).to_bytes(4, "big"), "little")


def ref_stream(image: bytes, raw_session: int):
    """Return (our_key, [encrypted body words], final crc word)."""
    send_size = (len(image) + 7) & ~7
    image = image + b"\0" * (send_size - len(image))
    key = ref_session(raw_session)
    words, crc = [], 0x15A0
    for i in range(0xC0, send_size, 4):
        plain = struct.unpack_from("<I", image, i)[0]
        crc = ref_crc(crc, plain)
        key = (key * 0x6177614B + 1) & M32
        enc = plain ^ key
        enc ^= (~(i + (0x20 << 20)) + 1) & M32
        enc ^= 0x20796220
        words.append(enc & M32)
    crc = (crc | (send_size << 16)) & M32
    key = (key * 0x6177614B + 1) & M32
    final = crc ^ key
    final ^= (~(send_size + (0x20 << 20)) + 1) & M32
    final ^= 0x20796220
    return ref_key(send_size), words, final & M32


DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "joyboot.h"

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    static unsigned char image[0x40000];
    size_t n = fread(image, 1, sizeof(image), f);
    fclose(f);
    unsigned raw = (unsigned)strtoul(argv[2], NULL, 16);

    unsigned send_size = (unsigned)((n + 7u) & ~7u);
    memset(image + n, 0, send_size - n);

    printf("%08X\n", joyboot_key(send_size));
    unsigned key = joyboot_session_key(raw);
    unsigned crc = JOYBOOT_CRC_SEED, i;
    for (i = 0xC0u; i < send_size; i += 4u) {
        unsigned plain = (unsigned)image[i] | ((unsigned)image[i+1] << 8) |
                         ((unsigned)image[i+2] << 16) | ((unsigned)image[i+3] << 24);
        crc = joyboot_crc_step(crc, plain);
        printf("%08X\n", joyboot_encrypt(&key, plain, i));
    }
    crc |= (send_size << 16);
    printf("%08X\n", joyboot_finish(&key, crc, i));
    return 0;
}
"""


def main() -> None:
    rng = random.Random(0x504B4147)
    with tempfile.TemporaryDirectory(prefix="pkhex-joyboot-") as td_name:
        td = pathlib.Path(td_name)
        (td / "driver.c").write_text(DRIVER)
        exe = td / "driver"
        subprocess.run(
            ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "include"), str(td / "driver.c"),
             str(ROOT / "source" / "joyboot.c"), "-o", str(exe)],
            check=True)

        cases = [len(( ROOT / "data" / "gba_agent.bin").read_bytes()), 0x200, 0x208, 0x1000, 0x3FF8]
        for size in cases:
            image = bytes(rng.randrange(256) for _ in range(size))
            img_path = td / "image.bin"
            img_path.write_bytes(image)
            raw_session = rng.randrange(1 << 32)

            out = subprocess.run([str(exe), str(img_path), f"{raw_session:08X}"],
                                 check=True, capture_output=True, text=True)
            got = [int(x, 16) for x in out.stdout.split()]

            key, words, final = ref_stream(image, raw_session)
            expected = [key] + words + [final]
            if got != expected:
                for idx, (g, e) in enumerate(zip(got, expected)):
                    if g != e:
                        raise SystemExit(
                            f"joyboot mismatch (image {size} bytes) at word {idx}: "
                            f"C {g:08X} != model {e:08X}")
                raise SystemExit(f"joyboot length mismatch: {len(got)} vs {len(expected)}")
            print(f"joyboot: {size} byte image, {len(words)} body words match")

    # The real agent must be a legal multiboot image.
    agent = (ROOT / "data" / "gba_agent.bin").read_bytes()
    assert len(agent) % 8 == 0, "agent size must be a multiple of 8"
    assert 0x200 <= len(agent) <= 0x40000, "agent size out of multiboot range"
    assert agent[0xB2] == 0x96, "missing fixed header byte"
    assert (-0x19 - sum(agent[0xA0:0xBD])) & 0xFF == agent[0xBD], "bad header checksum"
    for name, off in (("ROM", 0x00), ("RAM", 0xC0), ("JOYBUS", 0xE0)):
        word = struct.unpack_from("<I", agent, off)[0]
        assert word >> 24 == 0xEA, f"{name} entry at 0x{off:02X} is not an ARM branch"
    print("joyboot: agent multiboot header OK")
    print("JoyBoot handshake tests: PASS")


if __name__ == "__main__":
    main()
