# PKHeX-GC 1.0.4

**Use this instead of 1.0.3.** That release narrowed the inventory bug without
closing it: browsing a pocket could still change the slot under the cursor.

## A slot has to be opened before anything about it can change

Left and right adjusted whatever the cursor was sitting on, at any time, while
scrolling a pocket. Nothing gated it, nothing confirmed it and nothing could
undo it — and on a Colosseum or XD save the item was destroyed outright rather
than merely nudged. 1.0.3 stopped the stick reporting two directions at once,
which removed one way to trigger it and left the rest.

Opening a slot is now its own screen:

- The pocket list only browses. Up and down move the cursor, the D-pad changes
  pocket, **A** opens the slot under the cursor. The list screen contains no
  code that can write an item, so no input it receives can alter the save.
- The slot editor changes a working copy. **X** switches between the item and
  the quantity, the stick changes the selected one, the D-pad changes it by
  ten. **A** applies the change; **B** cancels it.

Nothing is written to the save until you press A, so backing out of a slot
leaves the bytes exactly as they were.

A build test now enforces this rather than trusting it: the pocket list is
checked for any call that could write a slot, and the adjustment itself is
checked to be reachable only from the slot editor. It was confirmed to fail
against the old arrangement.

## Also still in this release, from 1.0.3

Colosseum and XD item ids live at 500 and up, while the cartridge items every
game shares are at 0–376 and 377–499 are not items at all. The editor clamped
to 0–376 whatever the save, so one press on a GameCube item destroyed it.
Stepping now walks both ranges as one list, skipping the dead range between
them and stopping at the end of each game's own item list.

The analog stick also still reports at most one direction, so a lean off-axis
can no longer count as two.

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
