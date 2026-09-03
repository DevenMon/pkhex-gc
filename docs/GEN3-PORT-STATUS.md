# Generation III port status

Goal: everything PKHeX exposes for Generation III, working on GameCube.

This inventory is taken from the PKHeX source, not from screenshots — mainly
`PKHeX.Core/PKM/PK3.cs`, `PKHeX.Core/Saves/SAV3{,RS,E,FRLG}.cs`,
`PKHeX.Core/Saves/Substructures/Gen3/`, and the Gen3 editors under
`PKHeX.WinForms/Subforms/Save Editors/`. `SAV3*` exposes 94 public members;
they are all accounted for below.

Status keys: **done** — implemented and covered by host tests; **data** — the
save/record layer handles it but no UI reaches it yet; **todo** — not started.

As of v0.7.18 everything in this inventory is done or partly done except
legality checking, which is deliberately out of scope. What remains partial is
listed at the end.

## 1. The Pokémon record (PK3)

Every field of PK3 is now read and written, and `tests/test_pk3.c` round-trips
all of them through the shuffle-and-encrypt.

| PKHeX field | Where | Data | UI |
| --- | --- | --- | --- |
| PID | 0x00 | done | done |
| TID / SID | 0x04 | done | done |
| Nickname | 0x08 | done | done (on-screen keyboard) |
| Language | 0x12 | done | done |
| Egg / BadEgg / HasSpecies flags | 0x13 | done | egg done; the other two are not user-settable |
| OT name | 0x14 | done | done (on-screen keyboard) |
| Markings | 0x1B | done | done |
| Checksum / Sanity | 0x1C | done | n/a |
| Species, Held item, EXP | Growth | done | done |
| PP Ups | Growth 0x08 | done | done |
| Friendship | Growth 0x09 | done | done |
| Moves 1–4, PP 1–4 | Attacks | done | done |
| EVs (6) | EVs 0x00 | done | done |
| Contest stats (Cool/Beauty/Cute/Smart/Tough/Sheen) | EVs 0x06 | done | done |
| Pokérus strain + days | Misc 0x00 | done | done |
| Met location | Misc 0x01 | done | done, named |
| Met level, Origin game, Ball, OT gender | Misc 0x02 | done | done |
| IVs (6), Is Egg, Ability bit | Misc 0x04 | done | done |
| Ribbons + Fateful Encounter | Misc 0x08 | done | done |
| Battle stats, Status, Level, Held mail ID | party only | level done | level done |

Derived values PKHeX computes rather than stores, now implemented:

- **Nature** (`PID % 25`) with names, and a setter that finds a PID with the
  requested nature *without* changing shininess — a shiny gets a constructed
  PID rather than a random walk, because shiny PIDs are far too rare to find
  by search.
- **Shiny** (`TID ^ SID ^ PIDhi ^ PIDlo < 8`).
- **Hidden Power** type and power from the IV low bits.
- **Pokérus** strain/days, **PP Ups** per move slot.

Verified against PKHeX rather than assumed:

- **Species index conversion.** The internal-to-National mapping is a table,
  not an offset; both directions are covered by tests over the full dex.
- **Substructure shuffle.** All 24 orders round-trip, checked equivalent to
  PKHeX's `BlockPosition` table (PKHeX-GC stores the inverse permutation, so
  the two tables look different but produce identical results).

Still missing at the record level:

- ~~**Gender** and **ability name**~~ — done in v0.7.4 via the generated
  personal table.
- ~~**Met location names**~~ — done in v0.7.7 from PKHeX's location tables,
  with a separate table for the GameCube games, which reuse the indices.
- ~~**Nickname / OT name editing**~~ — done in v0.7.5. The keyboard edits the
  stored Gen III bytes rather than an ASCII copy, so characters this build
  renders as `?` survive an edit to another field untouched.

## 2. The save (SAV3)

| PKHeX capability | Status |
| --- | --- |
| Rotating 14-sector layout, slot selection, checksums | done |
| Trainer name / gender / TID / SID / play time | done |
| Money, Coins | done |
| Item pouches (PC, Items, Key Items, Balls, TMs, Berries) | done |
| Party read/write | done |
| Box read/write (14 boxes × 30) | done |
| Box names | done |
| Box wallpaper (`Get/SetBoxWallpaper`) | done |
| Pokédex seen/caught (`Get/SetSeen`, `Get/SetCaught`, `MirrorSeenFlags`) | done |
| Pokédex `NationalDex` flag | done |
| Event flags (`Get/SetEventFlag`, `EventFlagCount`) | done |
| Event constants / work (`Get/SetWork`, `EventWorkCount`) | done |
| Hall of Fame (`Get/SetHallOfFameData`) | read — viewer only, the block sits outside the rotating layout |
| Mail (`IsMail`) | done — read and clear; composing needs the easy-chat word tables |
| Daycare (`GetDaycareSlot`, `GetDaycareEXP`, `IsEggAvailable`, seed) | done |
| Records (`Get/SetRecord`, `Record3` per-game IDs) | done |
| Roamer (`Roamer3`) | done, including the non-Emerald single-IV-byte encounter |
| Gift ribbons (`GiftRibbons`, import/clear) | data — read and written, no UI yet |
| E-Reader berry (`EBerryName`, `IsEBerryEngima`) | done |
| Battle video / `SetExtraDataSentinelBattleVideo` | todo |
| RTC (`RTC3`) — RS/E | done |
| Secret Base (`SecretBase3`, teams, decorations) — RS/E | read and clear, with the defending team |
| Decorations (`Decoration3`, `DecorationInventory3`) — RS/E | data — read and written by category, no UI yet |
| PokéBlocks (`PokeBlock3`, case) — RS/E | done |
| Battle Frontier (`BattleFrontier3`) — E | done: streaks, symbols, Frontier Pass and Battle Points. Trainer Hill todo |
| Swarm | done. Paintings read only. Trendy word and Strategy Memo todo |
| Mystery Gift (`Gen3MysteryData`, Wonder Card) | read and clear; importing a card is todo |
| Rival name, Trainer Card icons, continue flag (`SAV_Misc3`) | rival name done; card icons and continue flag todo |
| `IsCorruptPokedexFF` | done, as part of Save Check |
| Legality checking | out of scope for now — it is most of PKHeX.Core |

## 3. Editors PKHeX offers for Gen3

| PKHeX editor | Status |
| --- | --- |
| Pokémon: Main / Met / Stats / Moves / Cosmetic / OT-Misc | done |
| Inventory editor | done (per-pouch, with security-key masking) |
| Simple Trainer editor | done (name, TID/SID, money, play time, badges) |
| Simple Pokédex editor | done |
| Event Flags (+ constants, research) | done — by number, with the ferry and island flags PKHeX names labelled |
| Hall of Fame viewer | done |
| Mail Box editor | done (view and clear) |
| Misc3 (Joyful, Records, Battle Frontier, Ferry, Paintings) | done except the ticket-granting button |
| Roamer editor | done |
| Secret Base editor | done (viewer with the defending team, and clearing) |
| RTC editor | done |
| Box Layout (names + wallpaper) | done — its own screen under Tools |
| Save Box Data / Verify Checksums / Verify All PKMs / Export Backup | done — Save Check for verification, Z in the editor to export a record, Pokemon Files to import one |

## 4. Plan

Ordered so each stage is independently testable and useful.

1. ~~**Pokémon editor completion.**~~ Done in v0.7.4: eight pages covering
   nature (re-rolling the PID), ability slot, language, markings, contest
   stats, Pokérus, met data, origin game, ball, fateful and PP Ups, with
   nature, shininess and Hidden Power shown alongside the sprite.
2. ~~**Personal table.**~~ Done in v0.7.4: gender, ability names, types and the
   base-stat column. Final stats, and the level a box record's experience puts
   it at, followed in v0.7.5.
3. ~~**Pokédex editor.**~~ Done in v0.7.4. Setting the National Dex flag is
   still open: it needs the event-flag and work accessors from stage 4.
4. ~~**Event flags and constants.**~~ Done in v0.7.4, by number. PKHeX labels
   the well-known ones from per-game tables that are not ported; this is the
   equivalent of its Research tab.
5. ~~**Box layout.**~~ Wallpapers in v0.7.4, box names in v0.7.5, and in
   v0.7.11 both moved into one Box Layout screen, with box-to-box moves on the
   storage screen.
6. ~~**Trainer extras.**~~ Badges in v0.7.4; rival name and the game records
   in v0.7.7. Trainer card icons are still open.
7. ~~**Hall of Fame, Mail, Daycare, Roamer.**~~ Done in v0.7.6, reached from
   a new Tools menu on `Y`. The Hall of Fame is a viewer: it lives in the two
   sectors past the main save, which this port copies through untouched.
8. ~~**Game-specific blocks.**~~ Done in v0.7.12: PokéBlocks, secret bases,
   the decoration inventory, the Battle Frontier and the two clocks. Secret
   base teams and Trainer Hill are still open.
9. ~~**Name entry.**~~ Done in v0.7.5: a controller keyboard over the Gen III
   character set, reached with A from the trainer editor's name rows and with
   Z from the Pokémon editor's NAMES page.

Legality checking is deliberately last and may stay out of scope; it is the
largest part of PKHeX.Core and does not fit a controller UI well.

## 5. The GameCube saves

PKHeX also opens the three GameCube Generation III saves. Their status here:

| Format | Read | Write |
| --- | --- | --- |
| Pokémon Colosseum (`SAV3Colosseum`) | trainer, party, three boxes, full CK3 records with shadow state, six-pouch bag | yes, with the SHA-1 chain cipher and both checksums rebuilt |
| Pokémon XD (`SAV3XD`) | trainer, party, eight boxes, full XK3 records, seven-pouch bag | yes, with the Genius Sonority cipher and both checksums rebuilt |
| Pokémon Box RS (`SAV3RSBox`) | box layout, names, wallpapers, per-block checksums, all 50 boxes of PK3 records | yes, repacking the 23 blocks and re-checksumming |

XD's record was only partly read before v0.7.8. It now covers every field the
port models: met data, ball, OT gender, Pokérus, markings, the ability and egg
flags, language, fateful encounter, moves with PP Ups, the six EVs and IVs in
XD's own stat order, contest stats and both kinds of ribbon. Writing preserves
everything it does not model, including the Shadow ID and both names.

Pokémon Box lays its storage out as 12×5 grids rather than the 6×5 the
cartridge games use, so two consecutive boxes share one grid — the even box is
its left half, the odd box its right — and each slot carries four extra bytes
naming the trainer who deposited it. Writes go through the ordinary PK3 writer
and leave those four bytes alone.

Colosseum's record was also only partly read before v0.7.10. It now covers the
origin game, language, met data, ball, OT gender, PP Ups, contest stats,
contest ribbon levels, the twelve flag ribbons, Pokérus, the ability and egg
flags and markings. Its fateful-encounter flag lives in a different byte per
region and reads as the two disagreeing, which is how PKHeX models it.

XD's shadow table landed in v0.7.13. A record only names its Shadow ID; the
table says whether that Pokémon is still a Shadow, how far its heart gauge has
come, what IVs it was generated with and how much Shadow experience it holds,
so reading a record now cross-references it. Colosseum keeps the same
information in the record itself and has no table.

Trainer and box names are editable on every format as of v0.7.14. The
GameCube saves store UTF-16, so the keyboard works on plain characters there
and on Generation III bytes on a cartridge; Pokémon Box stores Generation III
text but outside a `Gen3Save`, so it takes the character path and is
re-encoded on commit.

Still open on the GameCube side: Colosseum's Strategy Memo and Japanese bonus
disc flags, and XD's purifier chamber.

## 6. What is still partial

- **Legality checking, the encounter half.** Tools → **Legality check** now
  checks everything that can be decided from the record itself: the PID and IV
  pair against the Generation III RNG methods, moves against the level-up, egg,
  machine and tutor learnsets, experience against the growth curve, the effort
  value cap, the second ability slot, Pokérus, and the enumerations. What it
  does *not* have is PKHeX's encounter database — whether a species could be
  met where it says it was, at that level, in that ball. That is the larger
  half by data volume and the smaller half by how many edited Pokémon it
  catches.
- **XD's purifier chamber.** PKHeX does not implement this either — the
  offset is present in `SAV3XD` and commented out — so there is nothing to
  port until someone maps it.
- **Trainer card icons and the continue flag.** Likewise: PKHeX has no
  Generation III trainer card code at all. Listed here because it was asked
  for, not because it is a gap against the thing being ported.
- **Battle videos.** PKHeX has `BattleVideo3`, which parses the teams out of a
  saved video. Reading one would mean a whole second record format for a
  screen that shows six Pokemon nobody can edit, so it stays out for now.
