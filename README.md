# PKHeX-GC

**PKHeX-GC** is a GameCube port of [PKHeX](https://github.com/kwsch/PKHeX) for
Generation III Pokémon saves. It runs as a `.dol` through Swiss on real
GameCube hardware and is designed to be used entirely with a GameCube
controller.

This is not the PKHeX desktop application running on a GameCube. That isn't
really possible. Instead, the Generation III save and Pokémon formats have
been reimplemented in C using PKHeX's source as the reference, with a new
640×480 interface built specifically for the GameCube.

This is **version 1.0**.

If PKHeX-GC and desktop PKHeX disagree about something, assume PKHeX is right
and file a bug here.

The Pokémon sprites are PKHeX's own box sprites, so Pokémon should look the
same here as they do in desktop PKHeX.

## What works

| | Read | Edit |
| --- | --- | --- |
| Ruby / Sapphire / Emerald / FireRed / LeafGreen | yes | yes |
| Pokémon Colosseum | yes | yes |
| Pokémon XD | yes | yes |
| Pokémon Box: Ruby & Sapphire | yes | yes |
| GameCube Memory Card, Slot A/B | yes | yes |
| GBA cartridge over a link cable | yes | yes *(untested on hardware)* |

### Using a Game Boy Player? Use GBI

PKHeX-GC cannot pull a save off a cartridge sitting in a Game Boy Player. If
that is your setup, use
**[Game Boy Interface](https://www.gc-forever.com/wiki/index.php?title=Game_Boy_Interface)**
to dump the save to your SD card, then open that file in PKHeX-GC like any
other save. GBI does this well and PKHeX-GC does not try to compete with it.

I did try. Reading the cartridge directly through the Player — no link cable,
no GBI — was attempted at length and I could not get it working. The idea was
to reach the Player over the GameCube's High-Speed Port, power its cartridge
slot, wake its internal GBA over the Player's serial link, and multiboot
PKHeX-GC's own save agent into it.

Part of that worked. The Player's Control register answers, and it reliably
reports whether a cartridge is inserted and whether it is a Game Boy or a Game
Boy Advance one. The serial side never did. Across many rounds of hardware
probing — address maps, ARAM mapping variants, burst lane layouts, and thirty
different boot/serial strategies — nothing on that bus ever answered the GBA
multiboot handshake, so the agent could never be uploaded and no save was ever
read this way.

That code has been removed rather than left in as a dead experiment. Without
GBI, reading a cartridge needs a DOL-011 link cable.

PKHeX-GC recognizes the following save formats:

- `.sav`
- `.fla`
- `.srm`
- `.bin`
- `.dat`
- `.gci`
- `.sv`
- `.sv1` through `.sv9`
- `.sa1`
- `.sa2`

That includes the numbered save extensions used by flash carts and emulators,
such as R4, No$GBA, and VisualBoyAdvance.

The Pokémon editor covers every field stored in a Generation III Pokémon
record across twelve pages, including:

- species and EXP
- moves and PP
- IVs and EVs, including the resulting stats
- nature and ability
- met information
- Poké Ball
- markings
- Pokérus
- contest stats
- ribbons
- nickname
- OT name

There is also an on-screen keyboard for editing text fields.

Press <img src="assets/buttons/x.svg" alt="X" height="20"> from the Summary
screen to open **Tools**. That contains the save-wide editors and utilities,
including:

- trainer data
- inventory
- Pokédex
- event flags
- box layout
- daycare
- roaming Pokémon
- mail
- Hall of Fame
- game records
- PokéBlocks
- secret bases
- Battle Frontier data
- game clock
- Pokémon XD shadow data
- save validation
- swarm data
- e-Reader berry
- museum paintings
- Mystery Gift

[`docs/GEN3-PORT-STATUS.md`](docs/GEN3-PORT-STATUS.md) tracks the port
field-by-field against PKHeX.

The one major feature deliberately left out for now is legality checking.

## Getting it

You do not need to build PKHeX-GC yourself. Every push automatically produces
a `.dol`.

1. Open [Actions](../../actions).
2. Click the newest successful green run.
3. Download the **pkhex-gc-dol** artifact at the bottom.
4. Unzip it.
5. Copy `pkhex-gc.dol` to your SD card.
6. Launch it through Swiss.

## Building

```sh
./build.sh
```

The build script generates the sprite atlas, rebuilds the GBA agent, runs the
tests, and then builds the GameCube DOL.

If `DEVKITPPC` is set, it uses your local devkitPPC installation.

If Docker is running instead, it builds inside the
`devkitpro/devkitppc` container.

If neither is available, the script stops before compiling the DOL, but the
earlier generation and test steps still run.

| | |
| --- | --- |
| `./build.sh --tests` | Run the tests only. No GameCube toolchain required. |
| `./build.sh --clean` | Remove previous build output first. |
| `./build.sh --no-network` | Build with a blank sprite atlas for offline use. |

On Windows with Docker Desktop:

```powershell
.\build-docker.ps1
```

## Your saves are backed up

PKHeX-GC is intentionally conservative about writing save data.

It will not overwrite anything unless it can first create a verified backup,
and it will not report a save as successful until the result has been read
back and compared byte-for-byte.

- Before modifying a file, PKHeX-GC writes a timestamped copy to
  `pkhex-gc-backups/`.
- If the backup cannot be written, the original file is not touched.
- After saving, the file is opened again and parsed again.
- If the readback does not match what was supposed to be written, PKHeX-GC
  rolls the change back.
- Backups contain recovery metadata so **Backups → Restore** knows where they
  originally came from.
- Cartridge metadata is included as well, preventing a cartridge backup from
  being restored to the wrong game.
- GameCube memory-card writes are performed one sector at a time and then
  receive the same byte-for-byte verification.

Even with all of that, keep your backup until you have loaded the edited save
in the actual game and confirmed that everything is working.

## Controls

The basic navigation is built around two controls.

<img src="assets/buttons/stick.svg" alt="Stick" height="20"> **Stick — move one step.**

Use the stick for normal movement. When editing a value, left and right change
it by one.

<img src="assets/buttons/dpad.svg" alt="D-pad" height="20"> **D-pad — move further.**

Up and down jump ten rows at a time. Left and right move between pages, boxes,
pockets, categories, or whatever larger grouping makes sense for the current
screen.

That is the entire navigation scheme.
<img src="assets/buttons/l.svg" alt="L" height="20">
<img src="assets/buttons/r.svg" alt="R" height="20"> are analog triggers, so
they are deliberately left unused.

<img src="assets/buttons/b.svg" alt="B" height="20"> **B always means Back.**

The button tables are also arranged to match the physical GameCube controller:
<img src="assets/buttons/b.svg" alt="B" height="20"> appears to the left of
<img src="assets/buttons/a.svg" alt="A" height="20">, and
<img src="assets/buttons/y.svg" alt="Y" height="20"> appears to the left of
<img src="assets/buttons/x.svg" alt="X" height="20">.

### Files

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select |
| <img src="assets/buttons/dpad.svg" alt="D-pad" height="20"> | Jump ten rows / change page |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Up one folder |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Open |
| <img src="assets/buttons/y.svg" alt="Y" height="20"> | Hardware |
| <img src="assets/buttons/x.svg" alt="X" height="20"> | Switch device |
| <img src="assets/buttons/z.svg" alt="Z" height="20"> | Backups |

### Hardware

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select a source |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Files |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Open selected source |
| <img src="assets/buttons/y.svg" alt="Y" height="20"> | Rescan memory cards |
| <img src="assets/buttons/z.svg" alt="Z" height="20"> | Backups |

The Hardware screen puts hardware save sources in one place, including
GameCube memory-card saves and a cartridge in a linked Game Boy Advance.

### Summary

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select party slot |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Back |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Edit |
| <img src="assets/buttons/y.svg" alt="Y" height="20"> | Boxes |
| <img src="assets/buttons/x.svg" alt="X" height="20"> | Tools |
| <img src="assets/buttons/z.svg" alt="Z" height="20"> | Backups |

### Boxes

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select slot |
| <img src="assets/buttons/dpad.svg" alt="D-pad" height="20"> | Previous / next box |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Summary |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Edit |
| <img src="assets/buttons/y.svg" alt="Y" height="20"> | Save |
| <img src="assets/buttons/x.svg" alt="X" height="20"> | Pick up / place |

### Pokémon editor

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select or adjust field |
| <img src="assets/buttons/dpad.svg" alt="D-pad" height="20"> | Previous / next page |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Cancel |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Apply |
| <img src="assets/buttons/z.svg" alt="Z" height="20"> | Export |

### Pokédex

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select species |
| <img src="assets/buttons/dpad.svg" alt="D-pad" height="20"> | Jump ten rows / change page |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Back |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Toggle Seen |
| <img src="assets/buttons/y.svg" alt="Y" height="20"> | Fill / clear all |
| <img src="assets/buttons/x.svg" alt="X" height="20"> | Toggle Caught |
| <img src="assets/buttons/z.svg" alt="Z" height="20"> | National Dex |

### Keyboard

| | |
| :-: | --- |
| <img src="assets/buttons/stick.svg" alt="Stick" height="20"> | Select key |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Cancel |
| <img src="assets/buttons/a.svg" alt="A" height="20"> | Type |
| <img src="assets/buttons/y.svg" alt="Y" height="20"> | Change case |
| <img src="assets/buttons/x.svg" alt="X" height="20"> | Delete |
| <img src="assets/buttons/z.svg" alt="Z" height="20"> | Accept |

### Anywhere

| | |
| :-: | --- |
| <img src="assets/buttons/b.svg" alt="B" height="20"> | Back |
| <img src="assets/buttons/start.svg" alt="START" height="20"> | Exit |
| <img src="assets/buttons/cstick.svg" alt="C-stick" height="20"> | Take PNG screenshot |

Saving the overall game save is deliberately **not** assigned to a controller
button.

Use **Tools → Save changes to file**.

PKHeX-GC will show the name of the file it is about to overwrite and ask for
confirmation. It then creates the verified backup, writes the new data, reads
the file back, and compares it before reporting that the save succeeded.

## Testing

```sh
./tests/run_host_tests.sh
```

The host tests cover:

- save reconstruction and checksums
- PK3 encryption across all 24 substructure orders
- round-trip testing for every PK3 field
- species index conversion across the full Pokédex
- backup metadata
- the GBA link protocol and byte order
- JoyBoot handshake arithmetic against an independent model

Passing tests are not the same thing as testing on hardware.

The link-cable transport has not yet been used against a physical cartridge.
Read a cartridge before writing one, and keep the SD backup PKHeX-GC makes.

## Contributors

PKHeX-GC is written by **DevenMon**, with the help of **Claude** and
**ChatGPT**. There are no other contributors.

## Credits

PKHeX-GC builds on a lot of work done by other projects. None of their code is
copied directly into this repository.

- **[PKHeX](https://github.com/kwsch/PKHeX)** by Kaphotics — the reference for
  Generation III save and Pokémon formats, and the source of the box sprites.
  PKHeX is GPLv3, which is why PKHeX-GC is GPLv3 as well.

- **[devkitPro / libogc](https://github.com/devkitPro/libogc)** — the GameCube
  toolchain and runtime.

- **[gba-link-cable-dumper](https://github.com/FIX94/gba-link-cable-dumper)**
  by FIX94 and **SendSave** by Chishm — references for the GBA link-cable and
  cartridge save-memory protocols.

Pokémon sprites and data are © Nintendo / Creatures Inc. / GAME FREAK Inc.

No Pokémon artwork is stored in this repository. The build downloads the
sprites when needed.

PKHeX-GC is an unofficial project and is not affiliated with or endorsed by
PKHeX or Nintendo.

See [`SOURCES.md`](SOURCES.md) for the full provenance ledger and
[`NOTICE.md`](NOTICE.md) for attribution.
