# PKHeX-GC 1.0.2

The rest of the 480i fix. **If 1.0.1 still gave you a blank screen, this is
the build to try.**

## Following the console instead of guessing

1.0 forced NTSC 480p and showed nothing on a console that was not running in
progressive scan. 1.0.1 switched to `VIDEO_GetPreferredMode`, which was the
right call to make — but against stock libogc that function decides from the
component-cable detect alone:

```c
if (VIDEO_HaveComponentCable()) rmode = &TVNtsc480Prog;
```

So plugging in a digital AV cable forced 480p whether or not progressive scan
was ever switched on, and the blank screen survived on exactly the consoles
that report a cable. It also refuses progressive to anyone whose detect
circuit has a fault, however their console is set.

PKHeX-GC now links [libogc2](https://github.com/extremscorner/libogc2), whose
version of the same call also requires `SYS_GetProgressiveScan()` — the
setting chosen at boot — and picks an interlaced mode otherwise. The video
mode now follows what the console was actually told to do.

Thanks to **Extrems** for pointing at the real cause.

## Building

libogc2 is now a build requirement. `tools/install_libogc2.sh` installs it
through devkitPro's package manager, and both `./build.sh` paths run it, so
there is nothing extra to do by hand.

Everything else is unchanged from 1.0.1: the interface is still drawn in a
640×480 design space scaled onto the active mode's framebuffer, interlaced
modes enable the vertical filter to stop thin horizontal edges shimmering,
and screenshots follow the real framebuffer size.

---

PKHeX-GC is a GameCube port of [PKHeX](https://github.com/kwsch/PKHeX) for
Generation III Pokémon saves.

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
