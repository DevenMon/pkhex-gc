#!/usr/bin/env bash
#
# PKHeX-GC build script.
#
#   ./build.sh                 build pkhex-gc.dol (native devkitPPC, else Docker)
#   ./build.sh --tests         run the host test suite only, no toolchain needed
#   ./build.sh --clean         remove build output first
#   ./build.sh --docker        force the devkitPPC container
#   ./build.sh --native        force the locally installed devkitPPC
#   ./build.sh --no-network    build the sprite atlas as a blank placeholder
#
# Output: ./pkhex-gc.dol  (copy it to your SD card and launch it from Swiss)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

IMAGE="${DEVKITPPC_IMAGE:-devkitpro/devkitppc:20260503}"
MODE=auto
CLEAN=0
TESTS_ONLY=0
SPRITE_ARGS=()

for arg in "$@"; do
  case "$arg" in
    --tests|--tests-only) TESTS_ONLY=1 ;;
    --clean)              CLEAN=1 ;;
    --docker)             MODE=docker ;;
    --native)             MODE=native ;;
    --no-network|--placeholder-sprites) SPRITE_ARGS+=(--placeholder) ;;
    -h|--help)            sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "build.sh: unknown option '$arg' (try --help)" >&2; exit 2 ;;
  esac
done

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- host steps

say "Generating the Pokemon sprite atlas"
python3 tools/build_sprites.py --self-test
python3 tools/build_sprites.py "${SPRITE_ARGS[@]+"${SPRITE_ARGS[@]}"}"

say "Building the GBA save agent"
if command -v clang >/dev/null 2>&1 && command -v ld.lld >/dev/null 2>&1; then
  python3 tools/build_gba_agent.py
elif command -v zig >/dev/null 2>&1; then
  python3 tools/build_gba_agent.py
else
  echo "No ARM toolchain (clang+ld.lld or zig); using the committed data/gba_agent.bin."
  python3 tools/build_gba_agent.py --check || \
    die "data/gba_agent.bin could not be verified and cannot be rebuilt here."
fi

say "Running host tests"
./tests/run_host_tests.sh

if [ "$TESTS_ONLY" -eq 1 ]; then
  say "Host tests passed (--tests: stopping before the DOL build)"
  exit 0
fi

# --------------------------------------------------------------- DOL build

have_native() {
  [ -n "${DEVKITPRO:-}" ] && [ -f "${DEVKITPRO:-/nonexistent}/libogc2/gamecube_rules" ]
}
have_docker() { command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; }

if [ "$MODE" = auto ]; then
  if have_native; then MODE=native
  elif have_docker; then MODE=docker
  else
    die "No way to build the DOL.

Install devkitPPC and libogc2, and export DEVKITPRO
(see https://devkitpro.org/wiki/Getting_Started and
https://github.com/extremscorner/libogc2#readme),
or start Docker so this script can use the ${IMAGE} container.

Everything that does not need the GameCube toolchain already passed:
run './build.sh --tests' to re-check just that part."
  fi
fi

[ "$CLEAN" -eq 1 ] && { say "Cleaning"; rm -rf build pkhex-gc.elf pkhex-gc.dol; }

case "$MODE" in
  native)
    have_native || die "--native was requested but DEVKITPRO/libogc2 was not found.
Install libogc2: https://github.com/extremscorner/libogc2#readme"
    say "Building pkhex-gc.dol with the local devkitPPC ($DEVKITPRO)"
    make
    ;;
  docker)
    have_docker || die "--docker was requested but the Docker daemon is not reachable."
    say "Building pkhex-gc.dol in $IMAGE"
    docker run --rm -v "$ROOT:/project" -w /project "$IMAGE" \
      bash -lc './tools/install_libogc2.sh && make'
    ;;
esac

[ -s pkhex-gc.dol ] || die "make finished but pkhex-gc.dol is missing or empty."

say "Built $(cd "$ROOT" && ls -l pkhex-gc.dol | awk '{print $9, $5" bytes"}')"
cat <<'DONE'

Copy pkhex-gc.dol to your SD card and launch it from Swiss.
DONE
