#!/usr/bin/env python3
"""Build the PKHeX-GC GBA cartridge save agent (multiboot image).

The agent is a freestanding ARM7TDMI program, so it is built with clang/lld
(or Zig's bundled clang) rather than requiring devkitARM alongside devkitPPC.
"""
from __future__ import annotations
import pathlib, shutil, struct, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "gba-agent"
OUT = ROOT / "data" / "gba_agent.bin"
SIZE_HDR = ROOT / "include" / "gba_agent_size.h"

# The BIOS verifies this logo before it will start a multiboot image.
LOGO = bytes.fromhex(
    "24 FF AE 51 69 9A A2 21 3D 84 82 0A 84 E4 09 AD "
    "11 24 8B 98 C0 81 7F 21 A3 52 BE 19 93 09 CE 20 "
    "10 46 4A 4A F8 27 31 EC 58 C7 E8 33 82 E3 CE BF "
    "85 F4 DF 94 CE 4B 09 C1 94 56 8A C0 13 72 A7 FC "
    "9F 84 4D 73 A3 CA 9A 61 58 97 A3 27 FC 03 98 76 "
    "23 1D C7 61 03 04 AE 56 BF 38 84 00 40 A7 0E FD "
    "FF 52 FE 03 6F 95 30 F1 97 FB C0 85 60 D6 80 25 "
    "A9 63 BE 03 01 4E 38 E2 F9 A2 34 FF BB 3E 03 44 "
    "78 00 90 CB 88 11 3A 94 65 C0 7C 63 87 F0 3C AF "
    "D6 25 E4 8B 38 0A AC 72 21 D4 F8 07"
)

# JoyBoot's session-key derivation is only defined for images of at least
# 0x200 bytes, and the transfer is word-pair based, so pad to a multiple of 8.
MIN_SIZE = 0x200
MAX_SIZE = 0x40000


def header() -> bytearray:
    h = bytearray(0xC0)
    h[:4] = struct.pack("<I", 0xEA00002E)  # b 0x020000C0
    h[4:0xA0] = LOGO
    h[0xA0:0xAC] = b"PKHEXGC AGT "
    h[0xAC:0xB0] = b"PKAG"
    h[0xB0:0xB2] = b"02"
    h[0xB2] = 0x96
    h[0xBD] = (-0x19 - sum(h[0xA0:0xBD])) & 0xFF
    return h


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> None:
    check_only = "--check" in sys.argv[1:]
    clang, lld = shutil.which("clang"), shutil.which("ld.lld")
    zig = shutil.which("zig")
    if not zig and shutil.which("python3"):
        probe = subprocess.run([sys.executable, "-m", "ziglang", "version"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if probe.returncode == 0:
            zig = f"{sys.executable}\0-m\0ziglang"

    with tempfile.TemporaryDirectory(prefix="pkhex-gba-agent-") as td_name:
        td = pathlib.Path(td_name)
        c_o, s_o = td / "agent.o", td / "start.o"
        elf, raw = td / "agent.elf", td / "agent.raw"
        common = ["--target=arm-none-eabi", "-mcpu=arm7tdmi", "-marm",
                  "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-Os"]
        if clang and lld:
            run([clang, *common, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "include"), "-c", str(SRC / "agent.c"), "-o", str(c_o)])
            run([clang, *common, "-c", str(SRC / "start.S"), "-o", str(s_o)])
            run([lld, "-T", str(SRC / "link.ld"), "--oformat=binary",
                 str(s_o), str(c_o), "-o", str(raw)])
        elif zig:
            z = zig.split("\0")
            target = ["-target", "arm-freestanding-eabi", "-mcpu=arm7tdmi"]
            run([*z, "cc", *target, "-marm", "-ffreestanding", "-fno-builtin",
                 "-fno-stack-protector", "-Os", "-std=c11", "-Wall", "-Wextra",
                 "-Werror", "-nostdlib", "-I", str(ROOT / "include"), str(SRC / "start.S"), str(SRC / "agent.c"),
                 f"-Wl,-T,{SRC / 'link.ld'}", "-o", str(elf)])
            run([*z, "objcopy", "-O", "binary", str(elf), str(raw)])
        elif check_only:
            print("GBA agent: no ARM toolchain available, skipping rebuild check")
            return
        else:
            raise SystemExit("clang/lld or Zig is required")
        payload = header() + raw.read_bytes()

    payload.extend(b"\0" * ((-len(payload)) & 7))
    if len(payload) < MIN_SIZE:
        payload.extend(b"\0" * (MIN_SIZE - len(payload)))
    if len(payload) > MAX_SIZE:
        raise SystemExit(f"agent too large for a multiboot transfer: {len(payload)} bytes")
    assert len(payload) % 8 == 0

    if check_only:
        current = OUT.read_bytes() if OUT.exists() else b""
        if bytes(payload) != current:
            raise SystemExit(
                f"{OUT.relative_to(ROOT)} is stale: rebuild it with "
                f"tools/build_gba_agent.py and commit the result")
        print(f"GBA agent: {OUT.relative_to(ROOT)} matches gba-agent/ sources "
              f"({len(payload)} bytes)")
        return

    OUT.write_bytes(payload)
    SIZE_HDR.write_text(
        "#ifndef PKHEX_GC_GBA_AGENT_SIZE_H\n#define PKHEX_GC_GBA_AGENT_SIZE_H\n"
        f"#define GBA_AGENT_BIN_SIZE {len(payload)}u\n#endif\n", encoding="ascii")
    print(f"GBA agent: {OUT.relative_to(ROOT)} ({len(payload)} bytes), "
          f"header checksum 0x{payload[0xBD]:02X}")


if __name__ == "__main__":
    main()
