#!/usr/bin/env python3
"""Regenerate the two PK3 slot fixtures in tests/test_pk3.c.

The presence test needs a pair of eighty-byte box records: one a Pokemon was
withdrawn from, which keeps its PID, trainer ID, nickname and a body that
still checksums, and one actually holding a Pokemon. Real saves are full of
the first kind, so the test was originally written against a cartridge dump -
which meant carrying somebody's trainer name and IDs around in the repository.
These are built instead, to the same shape, from values that belong to nobody.

Encryption, shuffling and the checksum follow PKHeX's PK3 (see SOURCES.md).

Usage: tools/build_pk3_fixture.py [--stdout]
"""
from __future__ import annotations
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "test_pk3.c"

# The 24 substructure orders, indexed by PID % 24.
ORDER = ["GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA",
         "AGEM", "AGME", "AEGM", "AEMG", "AMGE", "AMEG",
         "EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG",
         "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"]


def enc_char(c: str) -> int:
    if "0" <= c <= "9":
        return 0xA1 + ord(c) - ord("0")
    if "A" <= c <= "Z":
        return 0xBB + ord(c) - ord("A")
    if "a" <= c <= "z":
        return 0xD5 + ord(c) - ord("a")
    if c == " ":
        return 0x00
    raise SystemExit(f"no Gen III byte for {c!r}")


def text(s: str, n: int) -> bytes:
    b = [enc_char(c) for c in s][:n]
    return bytes(b + [0xFF] * (n - len(b)))


def u16(v: int) -> bytes:
    return v.to_bytes(2, "little")


def u32(v: int) -> bytes:
    return v.to_bytes(4, "little")


def build(pid: int, otid: int, nickname: str, ot: str, flags: int, species: int,
          item: int, exp: int, friendship: int, ivs: list[int],
          moves: list[int]) -> bytes:
    growth = u16(species) + u16(item) + u32(exp) + bytes([0, friendship]) + b"\x00\x00"
    attacks = b"".join(u16(m) for m in moves) + bytes([20, 20, 20, 20])
    evs = bytes(12)
    iv_word = 0
    for i, v in enumerate(ivs):
        iv_word |= (v & 31) << (i * 5)
    misc = bytes(2) + u16(0) + u32(iv_word) + u32(0)
    parts = {"G": growth, "A": attacks, "E": evs, "M": misc}
    for key, value in parts.items():
        assert len(value) == 12, (key, len(value))

    body = b"".join(parts[ch] for ch in ORDER[pid % 24])
    checksum = 0
    for i in range(0, 48, 2):
        checksum = (checksum + int.from_bytes(body[i:i + 2], "little")) & 0xFFFF

    head = bytearray(32)
    head[0x00:0x04] = u32(pid)
    head[0x04:0x08] = u32(otid)
    head[0x08:0x12] = text(nickname, 10)
    head[0x12] = 2  # English
    head[0x13] = flags
    head[0x14:0x1B] = text(ot, 7)
    head[0x1B] = 0  # no markings
    head[0x1C:0x1E] = u16(checksum)

    key = pid ^ otid
    enc = bytearray()
    for i in range(0, 48, 4):
        enc += (int.from_bytes(body[i:i + 4], "little") ^ key).to_bytes(4, "little")
    return bytes(head) + bytes(enc)


def emit(name: str, record: bytes, note: str) -> str:
    lines = [f"/* {note} */", f"static const uint8_t {name}[80] = {{"]
    for i in range(0, 80, 12):
        lines.append("    " + " ".join("0x%02X," % b for b in record[i:i + 12]))
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> None:
    # A slot the player withdrew from. The game cleared the flag byte and the
    # species; the PID, the trainer ID and the nickname stayed behind.
    residue = build(0x2C6A1D05, 0x11112222, "RHYHORN", "TESTER", 0x00,
                    0, 37, 21970, 70, [20, 21, 22, 23, 24, 25], [33, 45, 0, 0])
    # A slot actually holding something: Rayquaza, internal index 406.
    real = build(0x51A03E14, 0x33334444, "RAYQUAZA", "TESTER", 0x02,
                 406, 0, 428750, 70, [31, 30, 29, 28, 27, 26], [200, 63, 239, 0])
    blocks = {
        "residue_slot": emit("residue_slot", residue, "flag byte clear: the slot is free"),
        "real_slot": emit("real_slot", real, "flag byte 0x02: Has Species"),
    }

    if "--stdout" in sys.argv[1:]:
        print("\n".join(blocks.values()))
        return

    source = OUT.read_text()
    for name, block in blocks.items():
        pattern = re.compile(
            r"/\* [^\n]*\*/\nstatic const uint8_t " + name + r"\[80\] = \{.*?\n\};\n",
            re.S)
        source, count = pattern.subn(lambda _m, b=block: b, source, count=1)
        if count != 1:
            raise SystemExit(f"could not find {name} in {OUT}")
    OUT.write_text(source)
    print(f"wrote {OUT} (residue_slot, real_slot)")


if __name__ == "__main__":
    main()
