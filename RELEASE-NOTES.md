# PKHeX-GC 1.0.4

Inventory fixes, mostly Colosseum and XD.

**Changing an item now takes two steps.** Press A to open a slot, change it,
then A to keep it or B to throw it away. Before, left and right changed
whatever the cursor was on, so scrolling a pocket could rewrite it.

**Colosseum and XD items are no longer destroyed.** Their items are numbered
from 500 up, but the editor only allowed 0-376, so one press turned any key
item, cologne or disc into something else.

**The stick reports one direction at a time.** A push that was slightly
diagonal used to count as two.

If you edited a Colosseum or XD pocket in 1.0.2, check it against the backups
in `pkhex-gc-backups/`.

Copy `pkhex-gc.dol` to your SD card and launch it through Swiss.
