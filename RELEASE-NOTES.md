# PKHeX-GC 1.0

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
