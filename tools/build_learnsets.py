#!/usr/bin/env python3
"""Generate source/gen3_learnsets.c: how a Generation III Pokemon can know a move.

Four sources, all from PKHeX's binary resources:

  byte/levelup/lvlmove_{rs,e,fr,lg}.pkl   level-up moves, per game
  byte/eggmove/eggmove_rs.pkl             egg moves, shared by every Gen III game
  byte/personal/hmtm_g3.pkl               which TMs and HMs a species can learn
  byte/personal/tutors_g3.pkl             move tutor compatibility

The .pkl containers are what PKHeX calls a BinLinkerAccessor: a two-byte
identifier, a 16-bit entry count, then one (start,end) offset pair per entry
and the data they point into. Level-up and egg move containers use 16-bit
offsets; the machine and tutor ones use 32-bit.

A level-up entry is `move[] then level[]` - the moves as 16-bit values and the
levels as bytes, so the entry is three bytes per move. An egg move entry is a
flat array of 16-bit move ids. Machine and tutor entries are bit flags, one
bit per machine in order.

Usage: tools/build_learnsets.py <path to a PKHeX checkout>
"""
from __future__ import annotations
import pathlib, struct, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "source" / "gen3_learnsets.c"
MAX_DEX = 386
GAMES = {"rs": "rs", "e": "emerald", "fr": "frlg"}   # LeafGreen matches FireRed

# The 58 machines, in the order their bits appear, and which move each teaches.
# From PersonalInfo3.MachineMovesTechnical and MachineMovesHidden.
TM_COUNT = 50
HM_COUNT = 8
MACHINE_MOVES = [
    264, 337, 352, 347,  46,  92, 258, 339, 331, 237,
    241, 269,  58,  59,  63, 113, 182, 240, 202, 219,
    218,  76, 231,  85,  87,  89, 216,  91,  94, 247,
    280, 104, 115, 351,  53, 188, 201, 126, 317, 332,
    259, 263, 290, 156, 213, 168, 211, 285, 289, 315,
    15, 19, 57, 70, 148, 249, 127, 291,
]

# The tutor bit order, from LearnSource3E.Tutor_E: the fifteen FireRed and
# LeafGreen tutors first, then the fifteen Emerald added. A species' tutor
# flags are indexed by this list.
TUTOR_MOVES = [
    5,  14,  25,  34,  38,  68,  69, 102, 118, 135, 138,  86, 153, 157, 164,
    223, 205, 244, 173, 196, 203, 189,   8, 207, 214, 129, 111,   9,   7, 210,
]


def unpack(data: bytes, width: int) -> list[bytes]:
    """Entries from a BinLinkerAccessor container.

    The offset pairs OVERLAP: PKHeX reads a single wide value at
    `4 + index * width` and splits it, so an entry's end is the next entry's
    start. Striding by the pair size instead of the offset size gives entries
    several times too long, which is exactly as wrong as it sounds and shows
    up as a species with two hundred level-up moves.
    """
    count = struct.unpack_from("<H", data, 2)[0]
    fmt = "<HH" if width == 2 else "<II"
    out = []
    for i in range(count):
        start, end = struct.unpack_from(fmt, data, 4 + i * width)
        out.append(data[start:end])
    return out


def level_up(entry: bytes) -> list[tuple[int, int]]:
    """(level, move) pairs, in the order the game teaches them."""
    count = len(entry) // 3
    if not count:
        return []
    moves = struct.unpack_from(f"<{count}H", entry, 0)
    levels = entry[count * 2: count * 3]
    return [(lv, mv) for mv, lv in zip(moves, levels)]


def flags(entry: bytes, count: int) -> list[bool]:
    return [bool(entry[i >> 3] & (1 << (i & 7))) if (i >> 3) < len(entry) else False
            for i in range(count)]


def emit_pairs(out, name: str, per_species: list[list[tuple[int, int]]]) -> None:
    """One flat move array plus an index, so the table is a pair of arrays
    rather than 387 separate ones."""
    flat: list[tuple[int, int]] = []
    index: list[tuple[int, int]] = []
    for entry in per_species:
        index.append((len(flat), len(entry)))
        flat.extend(entry)

    out.write(f"static const Gen3LevelMove {name}_moves[] = {{\n")
    for i in range(0, len(flat), 6):
        out.write("    " + " ".join(f"{{{lv:3d},{mv:3d}}}," for lv, mv in flat[i:i + 6]) + "\n")
    out.write("};\n\n")
    out.write(f"static const Gen3LearnIndex {name}_index[] = {{\n")
    for i in range(0, len(index), 8):
        out.write("    " + " ".join(f"{{{s:5d},{n:2d}}}," for s, n in index[i:i + 8]) + "\n")
    out.write("};\n\n")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    res = pathlib.Path(sys.argv[1]) / "PKHeX.Core/Resources/byte"
    if not res.is_dir():
        raise SystemExit(f"not a PKHeX checkout: {res} missing")

    level = {}
    for key, tag in GAMES.items():
        entries = unpack((res / f"levelup/lvlmove_{key}.pkl").read_bytes(), 2)
        level[tag] = [level_up(entries[d]) if d < len(entries) else [] for d in range(MAX_DEX + 1)]

    egg_entries = unpack((res / "eggmove/eggmove_rs.pkl").read_bytes(), 2)
    eggs = []
    for d in range(MAX_DEX + 1):
        raw = egg_entries[d] if d < len(egg_entries) else b""
        eggs.append(list(struct.unpack_from(f"<{len(raw)//2}H", raw, 0)) if raw else [])

    machine_entries = unpack((res / "personal/hmtm_g3.pkl").read_bytes(), 4)
    tutor_entries = unpack((res / "personal/tutors_g3.pkl").read_bytes(), 4)

    # Move PP, from MoveInfo3.PP, indexed by move id.
    pp_text = (pathlib.Path(sys.argv[1]) / "PKHeX.Core/Moves/MoveInfo3.cs").read_text()
    body = pp_text.split("PP =>", 1)[1].split("[", 1)[1].split("]", 1)[0]
    move_pp = [int(v) for v in body.replace("\n", " ").split(",") if v.strip()]
    if len(move_pp) != 355:
        raise SystemExit(f"MoveInfo3.PP has {len(move_pp)} entries, expected 355")

    with OUT.open("w") as out:
        out.write("/*\n"
                  " * Generated by tools/build_learnsets.py - do not edit.\n"
                  " *\n"
                  " * How a Generation III Pokemon can come to know a move: the level-up\n"
                  " * lists per game, the egg moves (shared by all three), and the machine\n"
                  " * and tutor compatibility bits. From PKHeX's binary resources.\n"
                  " *\n"
                  " * Level-up moves are stored as one flat array with a per-species index\n"
                  " * into it, rather than 387 arrays, because 387 pointers cost more than\n"
                  " * the moves do.\n"
                  " */\n"
                  '#include "gen3_learnsets.h"\n\n')

        for tag in ("rs", "emerald", "frlg"):
            emit_pairs(out, f"level_{tag}", level[tag])

        flat_eggs: list[int] = []
        egg_index: list[tuple[int, int]] = []
        for e in eggs:
            egg_index.append((len(flat_eggs), len(e)))
            flat_eggs.extend(e)
        out.write("static const unsigned short egg_moves[] = {\n")
        for i in range(0, len(flat_eggs), 12):
            out.write("    " + " ".join(f"{m:3d}," for m in flat_eggs[i:i + 12]) + "\n")
        out.write("};\n\nstatic const Gen3LearnIndex egg_index[] = {\n")
        for i in range(0, len(egg_index), 8):
            out.write("    " + " ".join(f"{{{s:5d},{n:2d}}}," for s, n in egg_index[i:i + 8]) + "\n")
        out.write("};\n\n")

        out.write("static const unsigned short machine_moves[] = {\n")
        for i in range(0, len(MACHINE_MOVES), 10):
            out.write("    " + " ".join(f"{m:3d}," for m in MACHINE_MOVES[i:i + 10]) + "\n")
        out.write("};\n\nstatic const unsigned short tutor_moves[] = {\n")
        for i in range(0, len(TUTOR_MOVES), 10):
            out.write("    " + " ".join(f"{m:3d}," for m in TUTOR_MOVES[i:i + 10]) + "\n")
        out.write("};\n\nstatic const unsigned char move_pp[] = {\n")
        for i in range(0, len(move_pp), 16):
            out.write("    " + " ".join(f"{v:3d}," for v in move_pp[i:i + 16]) + "\n")
        out.write("};\n\n")

        for name, entries, count in (("machine", machine_entries, TM_COUNT + HM_COUNT),
                                     ("tutor", tutor_entries, len(TUTOR_MOVES))):
            width = (count + 7) // 8
            out.write(f"static const unsigned char {name}_flags[][{width}] = {{\n")
            for d in range(MAX_DEX + 1):
                raw = entries[d] if d < len(entries) else b""
                padded = (bytes(raw) + bytes(width))[:width]
                out.write("    { " + ", ".join(f"0x{b:02X}" for b in padded) + " },\n")
            out.write("};\n\n")

        out.write(f"""#define COUNT(a) ((unsigned)(sizeof (a) / sizeof (a)[0]))
#define MAX_DEX {MAX_DEX}u

static const Gen3LearnIndex *level_index_for(Gen3Game game, unsigned national,
                                             const Gen3LevelMove **moves) {{
    if (national < 1u || national > MAX_DEX) return NULL;
    switch (game) {{
        case GEN3_GAME_EMERALD: *moves = level_emerald_moves; return &level_emerald_index[national];
        case GEN3_GAME_FRLG:    *moves = level_frlg_moves;    return &level_frlg_index[national];
        default:                *moves = level_rs_moves;      return &level_rs_index[national];
    }}
}}

unsigned gen3_level_moves(Gen3Game game, unsigned national,
                          const Gen3LevelMove **out) {{
    const Gen3LevelMove *moves = NULL;
    const Gen3LearnIndex *idx = level_index_for(game, national, &moves);
    if (!idx || !out) return 0;
    *out = moves + idx->start;
    return idx->count;
}}

unsigned gen3_egg_moves(unsigned national, const unsigned short **out) {{
    if (national < 1u || national > MAX_DEX || !out) return 0;
    *out = egg_moves + egg_index[national].start;
    return egg_index[national].count;
}}

bool gen3_learns_machine(unsigned national, unsigned machine) {{
    if (national < 1u || national > MAX_DEX || machine >= {TM_COUNT + HM_COUNT}u) return false;
    return (machine_flags[national][machine >> 3] & (1u << (machine & 7u))) != 0u;
}}

bool gen3_learns_tutor(unsigned national, unsigned tutor) {{
    if (national < 1u || national > MAX_DEX || tutor >= COUNT(tutor_moves)) return false;
    return (tutor_flags[national][tutor >> 3] & (1u << (tutor & 7u))) != 0u;
}}

unsigned gen3_machine_count(void) {{ return COUNT(machine_moves); }}
unsigned gen3_tutor_count(void) {{ return COUNT(tutor_moves); }}

unsigned gen3_machine_move(unsigned machine) {{
    return machine < COUNT(machine_moves) ? machine_moves[machine] : 0u;
}}

unsigned gen3_tutor_move(unsigned tutor) {{
    return tutor < COUNT(tutor_moves) ? tutor_moves[tutor] : 0u;
}}

/* Base PP, before PP Ups. Zero for a move id the games do not have. */
unsigned gen3_move_pp(unsigned move) {{
    return move < COUNT(move_pp) ? move_pp[move] : 0u;
}}
""")

    totals = {t: sum(len(x) for x in level[t]) for t in level}
    print(f"wrote {OUT} (level-up {totals}, egg {sum(len(e) for e in eggs)}, "
          f"machines and tutors for {MAX_DEX} species)")


if __name__ == "__main__":
    main()
