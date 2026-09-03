#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT1="${TMPDIR:-/tmp}/pkhex-gc-test-gen3"
OUT2="${TMPDIR:-/tmp}/pkhex-gc-test-allgen3"
OUT3="${TMPDIR:-/tmp}/pkhex-gc-test-recovery"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3.c" "$ROOT/source/gen3_blocks.c" "$ROOT/source/gen3_locations.c" "$ROOT/source/gen3_records.c" "$ROOT/source/gen3_event_names.c" "$ROOT/source/gen3_names.c" "$ROOT/tests/test_gen3.c" -o "$OUT1"
"$OUT1"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3.c" "$ROOT/source/sha1.c" "$ROOT/source/gen3_all.c" "$ROOT/source/gen3_blocks.c" "$ROOT/source/gen3_locations.c" "$ROOT/source/gen3_records.c" "$ROOT/source/gen3_event_names.c" "$ROOT/source/gen3_names.c" \
  "$ROOT/tests/test_allgen3.c" -lm -o "$OUT2"
"$OUT2"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/recovery.c" "$ROOT/tests/test_recovery.c" -o "$OUT3"
"$OUT3"
OUT5="${TMPDIR:-/tmp}/pkhex-gc-test-pk3"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3.c" "$ROOT/source/gen3_personal.c" "$ROOT/source/gen3_exp.c" "$ROOT/source/gen3_species.c" "$ROOT/source/gen3_blocks.c" "$ROOT/source/gen3_locations.c" "$ROOT/source/gen3_records.c" "$ROOT/source/gen3_event_names.c" "$ROOT/source/gen3_names.c" "$ROOT/tests/test_pk3.c" -o "$OUT5"
"$OUT5"
OUT7="${TMPDIR:-/tmp}/pkhex-gc-test-gen3-blocks"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3.c" "$ROOT/source/gen3_blocks.c" "$ROOT/source/gen3_locations.c" "$ROOT/source/gen3_records.c" "$ROOT/source/gen3_event_names.c" "$ROOT/source/gen3_names.c" "$ROOT/tests/test_gen3_blocks.c" -o "$OUT7"
"$OUT7"
OUT12="${TMPDIR:-/tmp}/pkhex-gc-test-legality"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3_legality.c" "$ROOT/source/gen3_learnsets.c" "$ROOT/source/gen3_personal.c" \
  "$ROOT/source/gen3_species.c" "$ROOT/source/gen3_exp.c" "$ROOT/source/gen3.c" \
  "$ROOT/source/gen3_names.c" "$ROOT/source/gen3_blocks.c" "$ROOT/source/gen3_locations.c" \
  "$ROOT/source/gen3_records.c" "$ROOT/source/gen3_event_names.c" \
  "$ROOT/tests/test_legality.c" -o "$OUT12"
"$OUT12"
OUT10="${TMPDIR:-/tmp}/pkhex-gc-test-names"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3.c" "$ROOT/source/gen3_names.c" "$ROOT/source/gen3_blocks.c" \
  "$ROOT/source/gen3_locations.c" "$ROOT/source/gen3_records.c" "$ROOT/source/gen3_event_names.c" \
  "$ROOT/tests/test_names.c" -o "$OUT10"
"$OUT10"
OUT9="${TMPDIR:-/tmp}/pkhex-gc-test-event-names"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/gen3_event_names.c" "$ROOT/tests/test_event_names.c" -o "$OUT9"
"$OUT9"
OUT8="${TMPDIR:-/tmp}/pkhex-gc-test-uinput"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/source/uinput.c" "$ROOT/tests/test_uinput.c" -o "$OUT8"
"$OUT8"
OUT4="${TMPDIR:-/tmp}/pkhex-gc-test-gbalink-wire"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/tests/test_gbalink_wire.c" -o "$OUT4"
"$OUT4"
# The build script runs these before anything is compiled, and one of them
# scrapes a generated table out of source/. Moving that table between files
# broke the build without breaking a single test here, so they run here too.
python3 "$ROOT/tests/test_source_hygiene.py"
python3 "$ROOT/tools/build_sprites.py" --self-test
python3 "$ROOT/tools/build_gba_agent.py" --check
python3 "$ROOT/tests/test_png.py"
python3 "$ROOT/tests/test_layout.py"
python3 "$ROOT/tests/test_controls.py"
python3 "$ROOT/tests/test_palette.py"
# Compile for real rather than -fsyntax-only: unused statics and other
# codegen-stage diagnostics only surface once an object is produced, and the
# devkitPPC build treats them as errors. -O0 avoids glibc fortify's
# format-truncation notes, which do not apply to the newlib target.
OBJDIR="${TMPDIR:-/tmp}/pkhex-gc-objs"
rm -rf "$OBJDIR" && mkdir -p "$OBJDIR"
for unit in main gui png_writer gbalink joyboot uinput gen3_event_names gen3_names gen3_legality gen3_learnsets; do
  # -Wno-format-truncation: the browser paths truncate deliberately, and this
  # host's glibc headers model them differently from the newlib target.
  cc -std=c11 -O0 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-format-truncation \
    -I"$ROOT/tests/stubs" -I"$ROOT/include" \
    -c "$ROOT/source/$unit.c" -o "$OBJDIR/$unit.o"
done
echo "GameCube frontend + GBA link driver compile: PASS"

python3 "$ROOT/tools/build_gba_agent.py" --check
python3 "$ROOT/tests/test_joyboot.py"
