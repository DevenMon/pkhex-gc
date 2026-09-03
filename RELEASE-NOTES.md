# PKHeX-GC 1.0.1

A fix for consoles that are not set to progressive scan.

## Every video mode now works

1.0 forced the NTSC 480p render mode. On a console that is not running in
progressive scan — which is the normal case for composite, S-Video and RGB
SCART — that produced no picture at all. PKHeX-GC started and ran, but the TV
never showed anything.

It now uses whatever mode the console reports, so 480i works, and so do the
PAL modes. A component cable set to progressive still gets 480p. Nothing needs
configuring.

Two details that come with that:

- The interface is still drawn in a 640×480 design space and is scaled onto
  the framebuffer of the active mode, so every screen stays fully visible
  rather than being cropped by a shorter or taller framebuffer.
- Interlaced modes enable the vertical filter, which stops thin horizontal
  edges — table rules, text stems — from shimmering between fields.
  Progressive keeps the sharper unfiltered copy.

Screenshots taken with the C-stick now follow the real framebuffer size, so a
PAL capture is the whole PAL frame rather than the top 480 lines of it.

Everything else is unchanged from 1.0.

---

The first release of PKHeX-GC, a GameCube port of
[PKHeX](https://github.com/kwsch/PKHeX) for Generation III Pokémon saves.

Copy `pkhex-gc.dol` to your SD card and launch it through Swiss.

## What it does

- Reads and edits Ruby, Sapphire, Emerald, FireRed, LeafGreen, Pokémon
  Colosseum, Pokémon XD and Pokémon Box: Ruby & Sapphire saves.
- Loads saves from SD, from a GameCube memory card in either slot, and from a
  GBA cartridge over a DOL-011 link cable.
- Edits every field in a Generation III Pokémon record across twelve pages,
  with an on-screen keyboard for text fields.
- Save-wide editors under **Tools**: trainer data, inventory, Pokédex, event
  flags, box layout, daycare, roaming Pokémon, mail, Hall of Fame, game
  records, PokéBlocks, secret bases, Battle Frontier data, game clock, Pokémon
  XD shadow data, save validation, swarm data, e-Reader berry, museum
  paintings and Mystery Gift.
- Never overwrites anything without first writing a verified timestamped
  backup, and never reports a save as successful until the result has been
  read back and compared byte-for-byte.

Legality checking is deliberately not included.

## Game Boy Player

PKHeX-GC cannot pull a save off a cartridge sitting in a Game Boy Player. Use
[Game Boy Interface](https://www.gc-forever.com/wiki/index.php?title=Game_Boy_Interface)
to dump the save to your SD card and open that file here.

Reading the cartridge directly through the Player was attempted at length and
could not be made to work — the README explains how far it got and where it
stopped. That code is not in this release.

## Known limits

- Cartridge write-back over the link cable is implemented but has not been
  exercised against physical hardware. Read a cartridge before writing one,
  and keep the backup.
- Box wallpapers are drawn by PKHeX-GC rather than taken from the cartridge
  ROM, so they do not match the artwork the games use.

Built by **DevenMon**, with the help of **Claude** and **ChatGPT**.
