#!/usr/bin/env python3
import binascii, struct, subprocess, sys, tempfile, zlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = Path(tempfile.gettempdir()) / "pkhex-gc-test.png"
exe = Path(tempfile.gettempdir()) / "pkhex-gc-test-png-writer"
subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I"+str(root/"include"), str(root/"source/png_writer.c"),
                str(root/"tests/test_png.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe), str(out)], check=True)
b = out.read_bytes()
assert b[:8] == b"\x89PNG\r\n\x1a\n"
pos=8; chunks=[]; idat=b""
while pos < len(b):
    n=struct.unpack(">I", b[pos:pos+4])[0]; typ=b[pos+4:pos+8]; data=b[pos+8:pos+8+n]
    crc=struct.unpack(">I", b[pos+8+n:pos+12+n])[0]
    assert (binascii.crc32(typ+data)&0xffffffff)==crc
    chunks.append(typ)
    if typ==b"IDAT": idat += data
    pos += 12+n
assert chunks == [b"IHDR", b"IDAT", b"IEND"]
assert zlib.decompress(idat) == bytes([
    0, 255,0,0, 0,255,0,
    0, 0,0,255, 255,255,255,
])
print("PNG writer test: PASS")
