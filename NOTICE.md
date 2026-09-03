# Attribution

PKHeX-GC is an unofficial GameCube port of [PKHeX](https://github.com/kwsch/PKHeX)
by Kaphotics, and is licensed under **GPLv3** because PKHeX is.

No PKHeX source is copied here. The Generation III save and Pokémon formats —
the rotating sector layout, PK3 encryption and substructure shuffle, the
Colosseum and XD ciphers, Pokémon Box block reconstruction, species index
conversion — are reimplemented in C for devkitPPC/libogc, written against
PKHeX's source as the reference. The GameCube interface, the memory-card
reader, and the GBA link-cable transport are original.

The box sprites are PKHeX's own artwork, downloaded at build time. None of it
is stored in this repository. The Pokémon artwork and data are
© Nintendo / Creatures Inc. / GAME FREAK Inc.

Other work this depends on, none of it copied:

- **devkitPro / libogc** — GameCube toolchain and runtime.
- **[gba-link-cable-dumper](https://github.com/FIX94/gba-link-cable-dumper)**
  (FIX94, MIT) and **SendSave** (Chishm) — the JOY-bus multiboot handshake and
  the cartridge save-memory command sequences.
- **[Inter](https://github.com/rsms/inter)** (SIL OFL 1.1) — the UI font the
  embedded glyph texture was rendered from. No font file is redistributed.

Not affiliated with or endorsed by PKHeX or Nintendo. Full provenance in
[`SOURCES.md`](SOURCES.md); font and sprite specifics in [`FONT.md`](FONT.md)
and [`SPRITES.md`](SPRITES.md).
