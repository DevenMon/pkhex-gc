# Moving a Pokémon between two saves

Research only — nothing here is implemented. The question is what it would
take to move a Pokémon from one open save to another the way a trade does, and
what could go wrong.

## What a Generation III trade actually changes

Almost nothing in the record. This is the surprising part and it shapes
everything else.

Generation III does **not** rewrite the original trainer's name, ID, secret ID
or the PID when a Pokémon is traded. The record crosses intact. "This Pokémon
was traded to me" is not a stored fact — the game works it out at run time by
comparing the record's OT name, trainer ID and OT gender against the save's
own trainer, which is also how the obedience rule and the boosted experience
gain are decided.

So a trade is, to a first approximation, **a byte-for-byte copy of the record
into the other save**. The consequences are all on the receiving side:

- the species is marked **seen and caught** in the receiving Pokédex;
- the receiving save owns the slot and its box layout changes;
- if the record is an egg, it hatches on the receiving trainer's step count;
- held **mail** must not travel — the games refuse to trade a Pokémon holding
  mail, because mail lives in a separate block indexed by slot, not in the
  record.

## What this port already has

More than I expected. Every one of the four save formats is parsed into and
written from one `Gen3Pokemon` struct — `gen3_any_box_pokemon()` reads from a
cartridge save, Colosseum, XD or Pokémon Box, and `gen3_any_set_box_pokemon()`
writes into any of them. The format differences are already absorbed.

So the mechanical core of a trade — read from A, write into B — exists today.
What is missing is everything around it.

## What is missing, in the order it would bite

**1. Two saves open at once.** The whole application holds exactly one
`parsed_save`. This is the real work: a second slot, a second set of box
screens, and a way to move a cursor between them. Every other item here is
small by comparison.

**2. Pokédex on arrival.** `gen3_any_set_dex_seen()` and `set_dex_caught()`
exist. A trade that does not set them produces a save the games would never
have made — the Pokémon is in the box and the Dex has never heard of it.

**3. Mail.** A record holding mail must have the mail moved with it or the item
cleared. Moving a mail-holding record between saves as it stands would leave
the mail behind in the source's mailbox and point the destination at a mail
slot belonging to a different save. The games sidestep this by refusing the
trade; the simplest correct behaviour here is to refuse it too, and say why.

**4. GameCube text encoding is region-dependent.** This is the one I would
have got wrong. PKHeX's `CK3.ConvertToPK3` and `PK3.ConvertToCK3` do not just
copy the name bytes — they call `RemapGlyphs3GBA` and `RemapGlyphs3GC`, because
Colosseum and XD encode some characters differently between NTSC-J, NTSC-U and
PAL, and a name that crosses without remapping comes out corrupted. This port
decodes GameCube text as UTF-16 and does no region remapping at all, which is
fine while a save is only read and written in its own format and is not fine
the moment a name crosses between a GameCube and a cartridge save.

**5. Shadow Pokémon.** A Colosseum or XD record that is still shadow carries
purification state that has no meaning in a cartridge save. PKHeX's `CK3` and
`XK3` are `IShadowCapture` for exactly this reason. A shadow Pokémon should be
refused until purified, which is what the games enforce.

**6. The presence flag and the checksum.** `ConvertToPK3` ends with
`FlagHasSpecies = SpeciesInternal != 0` and `RefreshChecksum()`. This port
already sets the flag on write — it is the bug that filled the boxes with
nameless entries — and recomputes the checksum. Worth stating because getting
either wrong produces a record that is invisible in-game rather than
obviously broken.

**7. Party stats.** `ConvertToCK3` calls `ResetPartyStats()`. A boxed record
does not carry current HP and stats; a party record does. Moving between the
two needs them recomputed, and the port already computes derived stats.

## What would make a traded record illegal

Nothing about the copy itself. A FireRed-only species sitting in a Ruby save is
exactly what trading is for, and its met location and origin game stay as they
were — that is how the receiving game knows it is foreign.

What would be illegal is what an editor could do *around* the copy: a record
whose origin game cannot have produced it, a met location that does not exist
in the origin game, or a species that no Generation III game can legitimately
hold. That is legality checking, not trade simulation, and it is being built
separately.

## My recommendation

Two saves open at once is a substantial change to the shape of the
application, and the payoff over "export a record to SD from one save, import
it into another" — which works today — is convenience rather than capability.

If it is wanted, the order I would do it in is: region glyph remapping first
(it is a correctness bug that already exists whenever a GameCube name is
written to a cartridge save), then the refusals for mail and shadow state,
then the Pokédex update, then the second save slot.
