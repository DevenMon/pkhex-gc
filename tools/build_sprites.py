#!/usr/bin/env python3
"""Build a compact GameCube RGB5A3 atlas for National Dex #001-386.

Uses the same box sprites PKHeX itself draws, so a Pokemon looks the same here
as it does on the desktop: PKHeX.Drawing.PokeSprite's "Big Pokemon Sprites",
one 68x56 PNG per species (b_<dex>.png). Each is trimmed to its visible pixels,
scaled into a 32x32 cell, and packed into a 1024x512 GX_TF_RGB5A3 tiled texture
(data/gen3_sprites.bin).

The PNGs are fetched at build time and cached under assets/cache/; none are
stored in this repository. See SPRITES.md.

No Pillow/ImageMagick dependency: PNG decoding uses only Python's stdlib.
"""
from __future__ import annotations

import re
import struct
import sys
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "assets" / "cache" / "pkhex-sprites"
OUT = ROOT / "data" / "gen3_sprites.bin"
NAMES_C = ROOT / "source" / "gen3_names.c"
SPRITE_URL = ("https://raw.githubusercontent.com/kwsch/PKHeX/master/"
              "PKHeX.Drawing.PokeSprite/Resources/img/Big%20Pokemon%20Sprites/b_{dex}.png")
MAX_DEX = 386
SRC_W, SRC_H = 68, 56
ATLAS_W, ATLAS_H, CELL = 1024, 512, 32


def fetch(url: str, path: Path) -> None:
    """Download to path unless it is already cached. Callers announce progress;
    386 individual lines would drown the build log."""
    if path.exists() and path.stat().st_size > 32:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "PKHeX-GC sprite builder/0.7"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    path.write_bytes(data)


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def decode_png(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("sprite sheet is not a PNG")
    off = 8
    width = height = bit_depth = color_type = None
    palette = None
    trans = None
    compressed = bytearray()
    while off + 12 <= len(data):
        n = struct.unpack_from(">I", data, off)[0]
        typ = data[off+4:off+8]
        payload = data[off+8:off+8+n]
        off += 12 + n
        if typ == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", payload)
            if bit_depth != 8 or comp != 0 or filt != 0 or interlace != 0:
                raise ValueError(f"unsupported PNG format: depth={bit_depth} interlace={interlace}")
        elif typ == b"PLTE":
            palette = [tuple(payload[i:i+3]) for i in range(0, len(payload), 3)]
        elif typ == b"tRNS":
            trans = bytes(payload)
        elif typ == b"IDAT":
            compressed.extend(payload)
        elif typ == b"IEND":
            break
    if width is None or height is None:
        raise ValueError("PNG missing IHDR")
    channels = {0:1, 2:3, 3:1, 4:2, 6:4}.get(color_type)
    if channels is None:
        raise ValueError(f"unsupported PNG color type {color_type}")
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    expected = height * (stride + 1)
    if len(raw) != expected:
        raise ValueError(f"unexpected PNG stream size {len(raw)} != {expected}")
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        ft = raw[pos]; pos += 1
        cur = bytearray(raw[pos:pos+stride]); pos += stride
        for i in range(stride):
            left = cur[i-channels] if i >= channels else 0
            up = prev[i]
            ul = prev[i-channels] if i >= channels else 0
            if ft == 1: cur[i] = (cur[i] + left) & 0xff
            elif ft == 2: cur[i] = (cur[i] + up) & 0xff
            elif ft == 3: cur[i] = (cur[i] + ((left + up) >> 1)) & 0xff
            elif ft == 4: cur[i] = (cur[i] + paeth(left, up, ul)) & 0xff
            elif ft != 0: raise ValueError(f"unsupported PNG filter {ft}")
        rows.append(cur); prev = cur

    rgba = bytearray(width * height * 4)
    for y, row in enumerate(rows):
        for x in range(width):
            i = x * channels
            if color_type == 6:
                r,g,b,a = row[i:i+4]
            elif color_type == 2:
                r,g,b = row[i:i+3]; a = 255
            elif color_type == 3:
                idx = row[i]
                if palette is None or idx >= len(palette): raise ValueError("bad palette index")
                r,g,b = palette[idx]; a = trans[idx] if trans is not None and idx < len(trans) else 255
            elif color_type == 4:
                g,a = row[i:i+2]; r=b=g
            else:
                g = row[i]; r=b=g; a=255
            j = (y * width + x) * 4
            rgba[j:j+4] = bytes((r,g,b,a))
    return width, height, rgba


def species_names() -> list[str]:
    """The National Dex names, read out of the table tools/build_names.py writes.

    Scraped rather than duplicated so the atlas and the UI can never disagree
    about which sprite belongs to which species.
    """
    text = NAMES_C.read_text(encoding="utf-8")
    m = re.search(r"static const char \*const species_names\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise ValueError(f"could not find species_names[] in {NAMES_C}")
    names = re.findall(r'"([^"]+)"', m.group(1))
    if len(names) != 386:
        raise ValueError(f"expected 386 species names, found {len(names)}")
    return names




def get_px(rgba: bytearray, width: int, x: int, y: int):
    i = (y * width + x) * 4
    return rgba[i],rgba[i+1],rgba[i+2],rgba[i+3]


def put_px(dst: bytearray, x: int, y: int, p):
    if not (0 <= x < ATLAS_W and 0 <= y < ATLAS_H): return
    i=(y*ATLAS_W+x)*4; dst[i:i+4]=bytes(p)


def place_icon(dst: bytearray, src: bytearray, sw: int, sh: int, sx: int, sy: int, index: int):
    # Find visible content inside the PokéSprite 68x56 cell.
    minx,miny,maxx,maxy = SRC_W,SRC_H,-1,-1
    for y in range(SRC_H):
        yy=sy+y
        if yy >= sh: break
        for x in range(SRC_W):
            xx=sx+x
            if xx >= sw: break
            if get_px(src,sw,xx,yy)[3] > 8:
                minx=min(minx,x); maxx=max(maxx,x); miny=min(miny,y); maxy=max(maxy,y)
    if maxx < minx: return
    bw=maxx-minx+1; bh=maxy-miny+1
    scale=min(30.0/bw,30.0/bh)
    dw=max(1,min(30,int(round(bw*scale))))
    dh=max(1,min(30,int(round(bh*scale))))
    dx0=(index % (ATLAS_W//CELL))*CELL + (CELL-dw)//2
    dy0=(index // (ATLAS_W//CELL))*CELL + (CELL-dh)//2
    for dy in range(dh):
        srcy=sy+miny+min(bh-1,(dy*bh)//dh)
        for dx in range(dw):
            srcx=sx+minx+min(bw-1,(dx*bw)//dw)
            put_px(dst,dx0+dx,dy0+dy,get_px(src,sw,srcx,srcy))


def rgb5a3_pixel(r,g,b,a):
    if a < 8:
        return 0
    if a >= 224:
        return 0x8000 | ((r*31//255)<<10) | ((g*31//255)<<5) | (b*31//255)
    return ((a*7//255)<<12) | ((r*15//255)<<8) | ((g*15//255)<<4) | (b*15//255)


def to_rgb5a3_tiled(rgba: bytearray) -> bytes:
    out=bytearray()
    for ty in range(0,ATLAS_H,4):
        for tx in range(0,ATLAS_W,4):
            for y in range(4):
                for x in range(4):
                    i=((ty+y)*ATLAS_W+(tx+x))*4
                    v=rgb5a3_pixel(*rgba[i:i+4])
                    out.extend(struct.pack(">H",v))
    return bytes(out)


def write_placeholder(reason: str) -> None:
    """Emit a fully transparent atlas of the right shape.

    The DOL links against this blob, so a build with no network access has to
    produce something rather than failing. The UI simply draws no icons.
    """
    packed = to_rgb5a3_tiled(bytearray(ATLAS_W * ATLAS_H * 4))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(packed)
    print(f"sprites: WARNING - {reason}")
    print(f"sprites: wrote placeholder {OUT.relative_to(ROOT)} "
          f"({len(packed)} bytes, no icons). Re-run with network access for artwork.")


def build():
    placeholder = "--placeholder" in sys.argv
    if not placeholder:
        try:
            paths = fetch_sprites()
        except Exception as exc:  # network, TLS, proxy, upstream layout change
            write_placeholder(f"could not fetch PKHeX sprites ({exc})")
            return
    else:
        write_placeholder("--placeholder requested")
        return

    atlas = bytearray(ATLAS_W * ATLAS_H * 4)
    drawn = 0
    for dex in range(1, MAX_DEX + 1):
        path = paths.get(dex)
        if path is None:
            continue
        sw, sh, src = decode_png(path)
        if (sw, sh) != (SRC_W, SRC_H):
            raise ValueError(f"b_{dex}.png is {sw}x{sh}, expected {SRC_W}x{SRC_H}")
        place_icon(atlas, src, sw, sh, 0, 0, dex - 1)
        drawn += 1

    if drawn < MAX_DEX:
        raise ValueError(f"only {drawn} of {MAX_DEX} sprites were available")

    packed = to_rgb5a3_tiled(atlas)
    if len(packed) != ATLAS_W * ATLAS_H * 2:
        raise AssertionError("bad atlas size")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(packed)
    print(f"sprites: wrote {OUT.relative_to(ROOT)} ({len(packed)} bytes, {drawn} icons "
          f"from PKHeX)")


def fetch_sprites() -> dict[int, Path]:
    """Download one box sprite per species, caching each on disk."""
    CACHE.mkdir(parents=True, exist_ok=True)
    paths: dict[int, Path] = {}
    missing = 0
    for dex in range(1, MAX_DEX + 1):
        path = CACHE / f"b_{dex}.png"
        if not (path.exists() and path.stat().st_size > 32):
            if missing == 0:
                print(f"sprites: downloading {MAX_DEX} PKHeX box sprites "
                      f"(cached in {CACHE.relative_to(ROOT)})")
            missing += 1
            fetch(SPRITE_URL.format(dex=dex), path)
        paths[dex] = path
    return paths

def self_test():
    px = rgb5a3_pixel(255, 0, 0, 255)
    assert px == 0xFC00, hex(px)
    blank = bytearray(ATLAS_W * ATLAS_H * 4)
    packed = to_rgb5a3_tiled(blank)
    assert len(packed) == ATLAS_W * ATLAS_H * 2 and not any(packed)
    assert len(species_names()) == MAX_DEX

    # Every species must land in its own cell, and the last one must fit.
    cells = ATLAS_W // CELL * (ATLAS_H // CELL)
    assert cells >= MAX_DEX, f"atlas holds {cells} cells, need {MAX_DEX}"
    last = MAX_DEX - 1
    assert (last // (ATLAS_W // CELL)) * CELL + CELL <= ATLAS_H

    # A fully opaque 68x56 source should fill its cell edge to edge.
    src = bytearray(b"\xff" * (SRC_W * SRC_H * 4))
    atlas = bytearray(ATLAS_W * ATLAS_H * 4)
    place_icon(atlas, src, SRC_W, SRC_H, 0, 0, 0)
    drawn = sum(1 for y in range(CELL) for x in range(CELL)
                if get_px(atlas, ATLAS_W, x, y)[3] > 8)
    assert drawn >= 30 * 24, f"icon placement covered only {drawn} pixels"

    print("sprite builder self-test: PASS")


if __name__ == "__main__":
    if "--self-test" in sys.argv: self_test()
    else: build()
