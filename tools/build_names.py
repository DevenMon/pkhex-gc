#!/usr/bin/env python3
"""Generate source/gen3_names.c from PKHeX's name lists.

Moves, items and species were hand-transcribed into gen3.c, and hand
transcription drifts: five entries had come out with the wrong spelling, the
Colosseum and XD key items were missing entirely, and the item lookup returned
a pointer into one shared static buffer, so naming two items in a single call
would have shown the same name twice.

Sources, all under PKHeX.Core/Resources/text:

    other/en/text_Moves_en.txt        indexed by move id
    other/en/text_Species_en.txt      indexed by National Dex number
    items/gen3/text_ItemsG3_en.txt    indexed by Generation III item id
    items/gen3/text_ItemsG3Colosseum_en.txt
    items/gen3/text_ItemsG3XD_en.txt

The two GameCube lists are supplements, not replacements: PKHeX concatenates
them onto the cartridge list starting at index 500, which is where Colosseum
and XD put their own key items.

Decoration names come from the Decoration3 enum in
Saves/Substructures/Gen3/Decoration3.cs rather than a text file - PKHeX has no
localised list for them - so SMALL_DESK becomes "Small Desk". Emerald's trendy
words come from TrendyWord3E.cs the same way, but keep the enum's own spelling:
they are Easy Chat words and guessing where the spaces go in KTHXBYE would be
inventing text rather than porting it.

Usage: tools/build_names.py <path to a PKHeX checkout>
"""
from __future__ import annotations
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "source" / "gen3_names.c"

# Generation III has 354 moves, 386 species, and item ids up to 376.
MOVE_COUNT = 355
SPECIES_COUNT = 386
ITEM_COUNT = 377
CXD_ITEM_BASE = 500

# The UI font is ASCII. PKHeX's English lists use these beyond it.
TRANSLITERATE = {
    "é": "e", "♂": "(M)", "♀": "(F)",
    "’": "'", "‘": "'", "“": '"', "”": '"',
    "–": "-", "—": "-", "…": "...", " ": " ",
}


def read_lines(path: pathlib.Path) -> list[str]:
    """PKHeX's text resources are a mix of UTF-16 with a BOM and plain UTF-8."""
    raw = path.read_bytes()
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8-sig")
    return text.splitlines()


def ascii_only(text: str, where: str) -> str:
    out = []
    for ch in text:
        if ch in TRANSLITERATE:
            out.append(TRANSLITERATE[ch])
        elif 0x20 <= ord(ch) <= 0x7E:
            out.append(ch)
        else:
            raise SystemExit(f"{where}: no ASCII spelling for U+{ord(ch):04X} ({ch!r})")
    return "".join(out).strip()


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(out, name: str, values: list[str], per_line: int = 4) -> None:
    out.write(f"static const char *const {name}[] = {{\n")
    for i in range(0, len(values), per_line):
        out.write("    " + " ".join(c_string(v) + "," for v in values[i:i + per_line]) + "\n")
    out.write("};\n\n")


# Words the enum spells in a way title case would mangle.
DECORATION_WORDS = {
    "TV": "TV", "CD": "CD", "PIKA": "Pika", "POKEMON": "Pokemon",
    "MR": "Mr.", "NOTE": "Note",
}


def enum_entries(root: pathlib.Path, rel: str, enum: str) -> list[str]:
    """The bare identifiers of one C# enum, in declaration order.

    The enum is located by name rather than by taking the first brace in the
    file: Decoration3.cs holds a second enum of category names after the
    decorations themselves, and reading to the last brace swallowed it.
    """
    source = root / rel
    text = source.read_text(encoding="utf-8")
    m = re.search(r"\benum\s+" + re.escape(enum) + r"\b[^{]*\{", text)
    if not m:
        raise SystemExit(f"{source}: no enum {enum}")
    depth, i = 1, m.end()
    while depth:
        if text[i] == "{": depth += 1
        elif text[i] == "}": depth -= 1
        i += 1
    body = text[m.end():i - 1]
    entries = []
    for line in body.splitlines():
        entry = line.strip().rstrip(",").split("=")[0].strip()
        if not entry or entry.startswith("//"):
            continue
        if entry[0].isalpha() and entry.replace("_", "").isalnum():
            entries.append(entry)
    if not entries:
        raise SystemExit(f"{source}: enum {enum} has no entries")
    return entries


def decoration_names(root: pathlib.Path) -> list[str]:
    names = []
    for entry in enum_entries(root, "PKHeX.Core/Saves/Substructures/Gen3/Decoration3.cs",
                              "Decoration3"):
        if entry == "NONE":
            names.append("-")
        else:
            names.append(" ".join(DECORATION_WORDS.get(w, w.capitalize())
                                  for w in entry.split("_")))
    if len(names) < 100:
        raise SystemExit(f"only found {len(names)} decorations")
    return names


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    text = pathlib.Path(sys.argv[1]) / "PKHeX.Core/Resources/text"
    if not text.is_dir():
        raise SystemExit(f"not a PKHeX checkout: {text} missing")

    def load(rel: str, count: int | None = None, skip: int = 0) -> list[str]:
        path = text / rel
        lines = read_lines(path)
        if count is not None and len(lines) < skip + count:
            raise SystemExit(f"{path}: wanted {skip + count} lines, found {len(lines)}")
        wanted = lines[skip:skip + count] if count is not None else lines[skip:]
        return [ascii_only(v, f"{path.name}:{i + skip + 1}") for i, v in enumerate(wanted)]

    moves = load("other/en/text_Moves_en.txt", MOVE_COUNT)
    moves[0] = "-"                      # PKHeX writes an em-dash run here
    items = load("items/gen3/text_ItemsG3_en.txt", ITEM_COUNT)
    items[0] = "Empty"                  # what the editor calls an empty slot
    # Species are National Dex ordered with "Egg" at index 0, which is not a
    # species; the table below is 1-based on purpose.
    species = load("other/en/text_Species_en.txt", SPECIES_COUNT, skip=1)
    colo = load("items/gen3/text_ItemsG3Colosseum_en.txt")
    xd = load("items/gen3/text_ItemsG3XD_en.txt")
    root = pathlib.Path(sys.argv[1])
    decorations = decoration_names(root)
    trendy = enum_entries(root, "PKHeX.Core/Saves/Substructures/Gen3/TrendyWord3E.cs",
                          "TrendyWord3E")
    hill = [w.capitalize() for w in
            enum_entries(root, "PKHeX.Core/Saves/Substructures/Gen3/TrainerHillMode3E.cs",
                         "TrainerHillMode3E")]

    with OUT.open("w") as out:
        out.write("/*\n"
                  " * Generated by tools/build_names.py - do not edit.\n"
                  " *\n"
                  " * Move, item and species names, straight from PKHeX's name lists so they\n"
                  " * cannot drift out of step with it. Colosseum and XD add their own key\n"
                  " * items on top of the cartridge list, starting at id 500.\n"
                  " */\n"
                  '#include "gen3.h"\n'
                  '#include "gen3_all.h"\n\n')
        emit(out, "move_names", moves)
        emit(out, "item_names", items, per_line=3)
        emit(out, "species_names", species, per_line=4)
        emit(out, "colosseum_item_names", colo, per_line=3)
        emit(out, "xd_item_names", xd, per_line=3)
        emit(out, "decoration_names", decorations, per_line=3)
        emit(out, "trendy_word_names", trendy, per_line=4)
        emit(out, "trainer_hill_mode_names", hill, per_line=4)

        out.write(f"""#define COUNT(a) ((unsigned)(sizeof (a) / sizeof (a)[0]))
#define CXD_ITEM_BASE {CXD_ITEM_BASE}u

const char *gen3_move_name(uint16_t move_id) {{
    return move_id < COUNT(move_names) ? move_names[move_id] : "Unknown move";
}}

const char *gen3_item_name(uint16_t item_id) {{
    return item_id < COUNT(item_names) ? item_names[item_id] : "???";
}}

/*
 * Colosseum and XD share the cartridge item ids and add their own above 500.
 * Naming one of those through gen3_item_name would report "???" for every key
 * item those games have.
 */
const char *gen3_item_name_for(Gen3SaveKind kind, uint16_t item_id) {{
    if (item_id >= CXD_ITEM_BASE) {{
        const unsigned offset = (unsigned)item_id - CXD_ITEM_BASE;
        if (kind == GEN3_KIND_COLOSSEUM && offset < COUNT(colosseum_item_names))
            return colosseum_item_names[offset];
        if (kind == GEN3_KIND_XD && offset < COUNT(xd_item_names))
            return xd_item_names[offset];
        return "???";
    }}
    return gen3_item_name(item_id);
}}

const char *gen3_decoration_name(uint8_t decoration_id) {{
    return decoration_id < COUNT(decoration_names) ? decoration_names[decoration_id] : "???";
}}

/* Easy Chat words, kept as PKHeX spells them - see the note at the top. */
const char *gen3_trendy_word_name(unsigned word) {{
    return word < COUNT(trendy_word_names) ? trendy_word_names[word] : "?";
}}

const char *gen3_trainer_hill_mode_name(unsigned mode) {{
    return mode < COUNT(trainer_hill_mode_names) ? trainer_hill_mode_names[mode] : "?";
}}

const char *gen3_species_name(uint16_t internal_species) {{
    const unsigned national = gen3_species_national(internal_species);
    if (national < 1u || national > COUNT(species_names)) return "Unknown";
    return species_names[national - 1u];
}}
""")

    print(f"wrote {OUT} (moves {len(moves)}, items {len(items)}, species {len(species)}, "
          f"Colosseum +{len(colo)}, XD +{len(xd)}, decorations {len(decorations)}, "
          f"trendy words {len(trendy)}, Trainer Hill modes {len(hill)})")


if __name__ == "__main__":
    main()
