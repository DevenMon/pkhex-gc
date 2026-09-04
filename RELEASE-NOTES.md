# PKHeX-GC 1.0.3

Two inventory bugs, both of which destroyed data. **If you have edited a
Colosseum or XD inventory in an earlier build, check it against your backup.**

## Scrolling a list no longer edits it

Each analog stick axis was tested on its own, so pushing the stick down and
even slightly sideways reported **down and right at the same time**. On the
inventory screen up and down move the cursor while left and right change the
value under it — so scrolling through a pocket quietly rewrote the entries it
passed. The stick's gate is octagonal and the threshold was a third of full
deflection, so this was not an unusual grip; it is how a thumb normally pushes
down.

The stick now reports at most one direction: the axis it leans along wins
outright. The D-pad was never affected — both of its directions mean
navigation, never a value change.

## Colosseum and XD items are no longer destroyed

Item ids are two ranges. Every game shares the cartridge items at 0–376, and
Colosseum and XD keep their own — every key item, cologne and disc — at 500 and
up, with 377–499 meaning nothing at all.

The editor clamped to 0–376 regardless of the save. So a single press on any
Colosseum or XD item above 500 dropped it to 376, and the item was gone.
Combined with the bug above, scrolling a Colosseum pocket was enough to do it
without ever pressing sideways deliberately.

Stepping now walks both ranges as one list: every id stays reachable, the dead
range in between is skipped rather than landed on, and each game stops at the
end of its own item list.

Both fixes have regression tests that were confirmed to fail against the old
code.

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
