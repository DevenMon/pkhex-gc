# Upstream source / provenance log

This file records the upstream projects that PKHeX-GC currently depends on, reimplements logic from, adapts implementation patterns from, or derives build-time assets from.

The categories matter: inclusion here does **not** mean source code from every repository was copied into PKHeX-GC. Where logic was reimplemented from an upstream project, that is called out explicitly. Where a project is only a build/runtime dependency or compatibility reference, that is stated separately.

Last audited for PKHeX-GC v0.7.0, 2026-09-02.

## 1. PKHeX — primary save-format and Pokémon-format reference

- Repository: https://github.com/kwsch/PKHeX
- Project: PKHeX
- License: GNU GPL v3.0
- Relationship: **logic/format behavior reimplemented and ported** into native C for GameCube.

PKHeX is the primary upstream reference for Generation III save structures and Pokémon structures. PKHeX-GC does not embed or run the desktop C#/.NET UI; it reimplements the relevant save/Pokémon behavior in C for libogc.

Areas currently informed by / ported from PKHeX include:

- Generation III GBA rotating 14-sector save layout and redundant-save selection.
- Ruby/Sapphire, Emerald, and FireRed/LeafGreen save-block offsets and checksums.
- PK3 encryption/decryption, substructure permutation, checksum behavior, party/box record handling.
- Generation III trainer/security-key handling used for encrypted money, coins, and Bag quantities.
- Pokémon Colosseum save-slot selection, encryption/decryption, trainer layout, CK3 structure, party and box storage.
- Pokémon XD save-slot selection, Genius Sonority cipher/substructure handling, XK3 structure, party and box storage.
- Pokémon Box save-block reconstruction and paired-box PK3 storage behavior.
- GameCube memory-card save identification/format dispatch.
- Species/item/move identifiers and other Gen III enumerated data used by the editor UI.
- Per-game offsets for the daycare, roaming Pokemon, mailbox, Hall of Fame and
  gift ribbons, from `SaveBlock3LargeRS`, `SaveBlock3LargeE` and
  `SaveBlock3LargeFRLG`, with the record layouts from `Roamer3`, `Mail3` and
  `HallFame3`.
- Met location names from `Resources/text/locations/gen3` and game record IDs
  from `Record3.cs`, both turned into C tables by `tools/build_locations.py`
  and `tools/build_records.py`.
- GameCube bag layouts from `PlayerBag3Colosseum` and `PlayerBag3XD`, and the
  XD save checksums from `XDCrypto`/`GeniusCrypto`.
- Generation III legality analysis, in the parts that can be checked from the
  record: the linear congruential generator and the PID/IV method search from
  `Legality/RNG/MethodFinder` and `LCRNG`, the level-up, egg, machine and tutor
  learnsets from `Resources/byte/levelup`, `byte/eggmove` and `byte/personal`,
  the machine and tutor move orders from `PersonalInfo3` and `LearnSource3E`,
  and move PP from `MoveInfo3`. Turned into C tables by
  `tools/build_learnsets.py`. The encounter database PKHeX's full engine rests
  on is not ported; see `include/gen3_legality.h` for what that excludes.
- Move, item and species names from `Resources/text/other/en/text_Moves_en.txt`,
  `text_Species_en.txt` and `Resources/text/items/gen3/text_ItemsG3_en.txt`,
  with the Colosseum and XD key items from `text_ItemsG3Colosseum_en.txt` and
  `text_ItemsG3XD_en.txt` appended at id 500 the way `GameStrings.GetG3CXD`
  does, all turned into C tables by `tools/build_names.py`.
- The external event block Colosseum, XD and Pokemon Box write into a
  cartridge save, from `SAV3`'s Colosseum coupon and RSBOX properties.
- The Colosseum and XD Strategy Memo from `StrategyMemo`/`StrategyMemoEntry`,
  and Emerald's Trainer Hill records, trendy word flags and Walda wallpaper
  from `SaveBlock3LargeE`.
- Scripted event flag and event constant labels from
  `Resources/text/script/gen3/{flags,const}_{rs,e,frlg}_en.txt`, parsed the way
  `EventLabelParsing` parses them and turned into C tables by
  `tools/build_event_names.py`.

Relevant upstream areas/classes include, depending on current PKHeX tree organization:

- `PKHeX.Core/Saves/SAV3*`
- `PKHeX.Core/Saves/SAV3Colosseum*`
- `PKHeX.Core/Saves/SAV3XD*`
- `PKHeX.Core/Saves/SAV3Box*`
- `PKHeX.Core/Saves/MemoryCard/SAV3GCMemoryCard*`
- `PKHeX.Core/Saves/Encryption/XDCrypto*`
- `PKHeX.Core/PKM/PK3*`
- `PKHeX.Core/PKM/CK3*`
- `PKHeX.Core/PKM/XK3*`
- `PKHeX.Core/PKM/Util/PokeCrypto*`
- `PKHeX.Core/Saves/Util/SaveUtil*`

For v0.7.1 the Generation III record layer was checked field by field against
`PKHeX.Core/PKM/PK3.cs`: the plaintext header (PID, ID32, nickname, language,
flag byte, OT name, markings, checksum), the four shuffled substructures, the
Origins bit packing (met level 0-6, game 7-10, ball 11-14, OT gender 15), the
IV32 packing with its egg and ability bits, and the ribbon word with fateful
encounter in bit 31. The derived Nature, shiny, Hidden Power and Pokerus rules
come from the same source. `docs/GEN3-PORT-STATUS.md` records what is ported
and what is not.

Because PKHeX-GC is a derivative/reimplementation informed by GPLv3 PKHeX source, PKHeX-GC itself is distributed under GPLv3.

## 2. devkitPro/libogc — GameCube runtime/API dependency

- Repository: https://github.com/devkitPro/libogc
- Project: libogc
- License: zlib-style/libogc license plus notices for incorporated components.
- Relationship: **runtime/library dependency; API usage, not vendored libogc source**.

PKHeX-GC uses libogc for the native GameCube environment, including:

- GX graphics and texture rendering.
- VI/video mode and framebuffer management.
- PAD controller input.
- physical GameCube memory-card APIs (`CARD_*`).
- console/system facilities used by the homebrew frontend.
- libfat integration exposed by the devkitPro GameCube environment for FAT/SD access.

No complete libogc source tree is copied into this repository. The DOL is linked against the GameCube libraries supplied by the devkitPro build environment.

The v0.5.5 physical-card correction was checked against libogc's `CARD_Write` implementation and public `CARD_GetSectorSize` API. PKHeX-GC now submits one aligned write per physical card sector before performing its independent whole-file reread comparison.

For v0.7.0, `ogc/si.h` is also used directly: `SI_GetType` identifies a Game Boy Advance on a controller port (`SI_GBA`), and `SI_Transfer` carries every JOY-bus transaction. `libogc/si.c` was read to confirm that `SI_Transfer` moves its buffers through CPU register writes rather than DMA - so no cache maintenance is needed - and that it returns 0 when a channel already has a packet queued, which `source/gbalink.c` retries rather than ignoring. libogc supplies no GBA multiboot support, so `source/joyboot.c` is PKHeX-GC's own.

## 3. devkitPro/gamecube-examples — frontend implementation patterns

- Repository: https://github.com/devkitPro/gamecube-examples
- Project: devkitPro GameCube examples
- Relationship: **implementation patterns/examples adapted**, especially for GX/video/input setup.

The v0.4+ frontend rewrite used the current devkitPro GameCube GX examples as the reference for stable GameCube rendering behavior, including:

- GX initialization order.
- 640×480 framebuffer setup.
- double-buffered XFB presentation.
- `GX_CopyDisp` / draw-done / VSync ordering.
- sprite/texture rendering state setup.
- controller polling within the frame loop.

The application UI itself is original PKHeX-GC code; this repository is logged because those official examples were explicitly used to correct the earlier broken GX presentation path.

## 4. devkitPro/devkitppc-rules — GameCube Makefile/build rules

- Repository: https://github.com/devkitPro/devkitppc-rules
- Project: devkitPPC build rules
- Relationship: **build-system dependency; included by Makefile from installed devkitPPC**.

PKHeX-GC's `Makefile` includes devkitPPC's `gamecube_rules`, which supplies the standard GameCube compilation/linking/DOL conversion rules and paths for libogc.

No copy of `gamecube_rules` is vendored in this repository; it comes from the devkitPPC installation inside the build environment.

## 5. devkitPro/docker — reproducible devkitPPC container environment

- Repository: https://github.com/devkitPro/docker
- Docker image used by the project: `devkitpro/devkitppc:20260503`
- License of Dockerfile repository: GNU GPL v3.0
- Relationship: **build environment only**.

`build-docker.ps1`, `build-docker.sh`, and the GitHub Actions workflow use the official devkitPro devkitPPC container image so the GameCube DOL can be built without installing the toolchain directly on the host.

The devkitPro Docker repository is not copied into PKHeX-GC and its source is not linked into the application; it defines the environment used to compile it.

## 6a. kwsch/PKHeX — Generation III personal (species) table

- Repository: https://github.com/kwsch/PKHeX
- Path: `PKHeX.Core/Resources/byte/personal/personal_{rs,e,fr,lg}`
- License: GNU GPL v3.0
- Relationship: **generated data table**.

`tools/build_personal.py` reads PKHeX's 0x1C-byte-per-species records and emits
`source/gen3_personal.c` with the fields this port shows: base stats, types,
gender ratio and the two abilities. Ability names come from
`Resources/text/other/en/text_Abilities_en.txt`.

The four Generation III games agree on every one of those fields except
Deoxys, whose form - and so its base stats - differs per game, so that single
entry is emitted four times. The personal type indices run 0-16 and are not the
same ordering as Hidden Power's, which is why the two have separate name
tables.

Species data is © Nintendo / Creatures Inc. / GAME FREAK Inc.

## 6. kwsch/PKHeX — Pokémon box sprite artwork

- Repository: https://github.com/kwsch/PKHeX
- Path: `PKHeX.Drawing.PokeSprite/Resources/img/Big Pokemon Sprites/b_<dex>.png`
- License: GNU GPL v3.0
- Relationship: **build-time asset source**.

`tools/build_sprites.py` downloads one 68×56 box sprite per National Dex number
1-386 and packs them into the GameCube texture the UI draws, so PKHeX-GC shows
the same icons as PKHeX. The PNGs are cached under `assets/cache/` and are not
stored in this repository; neither is the generated atlas.

The depicted artwork is separately identified by upstream as © Nintendo /
Creatures Inc. / GAME FREAK Inc. See `SPRITES.md`.

This supersedes the previous PokéSprite spritesheet source.

## 8. rsms/inter — UI font design used to rasterize the embedded atlas

- Repository: https://github.com/rsms/inter
- Project: Inter
- License: SIL Open Font License 1.1
- Relationship: **font design used to generate a rasterized UI texture**.

The v0.5+ UI font atlas was rasterized from Inter for improved legibility at 640×480. PKHeX-GC source archives do **not** include or redistribute Inter `.ttf`, `.otf`, `.woff`, or other font files. They contain only the rendered GameCube texture pixels (`data/ui_font.bin`) and generated glyph advance metrics.

See `FONT.md` for the font-specific notice.

## 9. FIX94/gba-link-cable-dumper — GBA link-cable protocol reference

- Repository: https://github.com/FIX94/gba-link-cable-dumper
- Project: GBA Link Cable Dumper
- License: MIT
- Relationship: **protocol reference; logic reimplemented, no source copied**.

This is the reference implementation of the GameCube-side path PKHeX-GC v0.7.0
uses, and the source that establishes the working details as facts rather than
guesses:

- the JOY-bus command set over `SI_Transfer` (`0x00` status, `0x14` read,
  `0x15` write, `0xFF` reset) and the ~50 µs inter-transfer delay;
- waiting on status byte 2 bit 4 for the GBA BIOS multiboot state;
- the JoyBoot handshake: image size padded to 8 bytes, the key derived from
  it, the session key the GBA offers, the key-stream advance
  `key = key * 0x6177614B + 1`, the per-word XOR chain, and the `0xA1C1`
  checksum seeded at `0x15A0`;
- the byte order in each direction across a big-endian host.

PKHeX-GC reimplements these in `source/joyboot.c`, `source/gbalink.c` and
`include/gba_link_wire.h`, with a different command protocol, a different
safety model (checksum-gated erase, byte-for-byte read-back, rollback) and its
own agent. `source/joyboot.c` is checked against an independent model of the
handshake in `tests/test_joyboot.py` rather than against this source.

## 10. Chishm's SendSave / libSave — GBA save-memory routines

- Project: SendSave (`libSave.c`), by Chishm; reused by `gba-link-cable-dumper`
- Relationship: **hardware command sequences reimplemented, no source copied**.

The cartridge-side save routines in `gba-agent/agent.c` follow the save-memory
command sequences this library established:

- reading the cartridge's own save-library identifier string (`FLASH1M_`,
  `FLASH512_`, `FLASH_`, `EEPROM_`, `SRAM_`) out of ROM to determine the save
  type without writing to the cartridge;
- Flash unlock (`0xAA` at `0x0E005555`, `0x55` at `0x0E002AAA`), reset
  (`0x90`/`0xF0`), bank select (`0xB0`), chip erase (`0x80`/`0x10`) and byte
  program (`0xA0`);
- 128 KiB Flash being two banked 64 KiB halves.

PKHeX-GC deliberately differs in two places. It polls for the expected byte
(DQ7 data polling) with a bounded spin instead of an unbounded toggle-bit
wait, so a dead chip cannot hang the console and each byte is verified as it is
written. And it implements no EEPROM read or program path at all.

## 11. GBA JOY-bus multiboot references

PKHeX-GC uploads `gba-agent` to a linked Game Boy Advance with Nintendo's
JOY-bus multiboot ("JoyBoot") handshake. The handshake arithmetic and its
framing were cross-checked against these public implementations and traces:

- `afska/gba-link-connection` (`docs/multiboot.md` and `LinkCableMultiboot.hpp`,
  MIT): protocol sequence and BIOS header/handshake semantics;
- `jojolebarjos/gba-multiboot` (MIT): multiboot hardware behaviour;
- `akkera102/gba_01_multiboot` (`src/multiboot.c`): behavioural reference for
  the wire framing, including the encrypted and final-CRC phases.

Relationship: **protocol/behaviour reference; logic reimplemented, no source
copied**. `source/joyboot.c` is PKHeX-GC's own arithmetic, cross-checked
against an independent model in `tests/test_joyboot.py`.

## 12. Retired: direct Game Boy Player save extraction

Earlier revisions (through v0.21.5) attempted to read a Game Boy Advance
cartridge through a Game Boy Player with no link cable, by reaching the
Player's High-Speed Port over an ARAM DMA, powering the cartridge slot through
the Player's Control register, and multibooting `gba-agent` into the Player's
internal Game Boy Advance over its serial link.

Dolphin's `Source/Core/Core/HW/HSP/HSP_DeviceGBPlayer.cpp` (GPLv2+) was the
behavioural reference for that register model, and Game Boy Interface was cited
only as an existence proof that a cable-free path is possible — GBI is closed
source and nothing from it was ever disassembled, copied or redistributed here.

The cartridge-presence side of that worked; the serial side never did, so no
save was ever read this way. All of that code has been removed, and neither
project is a source for anything that ships in PKHeX-GC now.

## Compatibility/reference projects not currently used as source code

These may be relevant to supported workflows, but are **not currently counted as code/asset upstreams** unless that changes in a future revision:

- Swiss: PKHeX-GC is launched from Swiss, but Swiss source code is not incorporated into PKHeX-GC.

If future code is adapted from either project, its exact repository and affected source files must be promoted into the main provenance sections above.

## Maintenance rule

When adding code, format logic, build infrastructure, or assets based on another repository:

1. add the repository here in the same change;
2. state whether the relationship is copied/ported logic, adapted pattern, runtime dependency, build dependency, or asset source;
3. state which PKHeX-GC files/features it affects;
4. record the upstream license when known; and
5. update `NOTICE.md` if attribution/license obligations require it.


## v0.7.0 implementation notes

**Why the erase is gated.** The GBA agent receives a save image into RAM,
returns a checksum of what it received, and waits. The GameCube compares that
against the image it sent and only then authorises the erase/program cycle. A
cable knocked loose mid-transfer therefore ends with the cartridge exactly as
it was, rather than half-erased.

**Why slow work is polled rather than handshaked.** The JOY-bus word handshake
is only correct while the agent can stage its next word within a few
instructions of the previous one being read - the host's inter-transfer delay
covers that gap, but not a 128 KiB Flash read or a chip erase. Before any such
operation the agent publishes a busy sentinel into `REG_JOY_TRANS`, which
answers the host's reads from hardware while its CPU is elsewhere. The host
polls that until the real result appears, then acknowledges, which also clears
the flags the polling reads left set and puts both sides back in lock-step.

**Why the multiboot image has two entry points.** The BIOS enters a multiboot
image at `0x020000C0` after a Normal/Multiplay-mode transfer and at
`0x020000E0` after a JOY-bus transfer. PKHeX-GC boots over the JOY bus, but
`gba-agent/start.S` branches to the same start code from both slots because the
header layout the BIOS checks reserves both.

**Why EEPROM is left alone.** Every Generation III Pokémon cartridge uses
128 KiB Flash. An EEPROM programming path could not be exercised by this
application's own use case, so shipping one would mean shipping untested code
whose failure mode is a destroyed save on somebody else's cartridge.
