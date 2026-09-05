#!/usr/bin/env bash
#
# Install libogc2 into a devkitPro environment.
#
# PKHeX-GC links against libogc2 rather than stock libogc for one reason:
# VIDEO_GetPreferredMode. Stock libogc decides the video mode from the
# component-cable detect alone, so it forces 480p on any console with a
# digital AV cable plugged in - whether or not the user actually turned
# progressive scan on - and leaves anyone running 480i with no picture.
# libogc2 requires SYS_GetProgressiveScan() as well, which is the setting
# the user chose at boot.
#
# Safe to re-run: it exits immediately once libogc2 is present.
#
# See https://github.com/extremscorner/libogc2#readme
set -euo pipefail

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
RULES="$DEVKITPRO/libogc2/gamecube_rules"

if [ -f "$RULES" ]; then
  echo "libogc2 already present at $DEVKITPRO/libogc2"
  exit 0
fi

# The signing key for Extrems' package repository.
KEY=C8A2759C315CFBC3429CC2E422B803BA8AA3D7CE

# pacman -Sy synchronises every configured repository, so a hiccup on any of
# them - including devkitPro's own, which rate-limits CI with a 403 - fails
# the install even when the repository we actually need is fine. Keyservers
# are no more reliable. Neither is worth failing a build over, so retry.
retry() {
  n=1
  delay=5
  while true; do
    if "$@"; then return 0; fi
    if [ "$n" -ge 5 ]; then
      echo "install_libogc2.sh: '$*' failed after $n attempts." >&2
      return 1
    fi
    echo "install_libogc2.sh: '$*' failed (attempt $n); retrying in ${delay}s..." >&2
    sleep "$delay"
    n=$((n + 1))
    delay=$((delay * 2))
  done
}

PACMAN=$(command -v dkp-pacman || command -v pacman || true)
KEYTOOL=$(command -v dkp-pacman-key || command -v pacman-key || true)
if [ -z "$PACMAN" ] || [ -z "$KEYTOOL" ]; then
  echo "install_libogc2.sh: no devkitPro pacman found; cannot install libogc2." >&2
  echo "Install it manually: https://github.com/extremscorner/libogc2#readme" >&2
  exit 1
fi

CONF="$DEVKITPRO/pacman/etc/pacman.conf"
[ -f "$CONF" ] || CONF=/etc/pacman.conf

retry "$KEYTOOL" --recv-keys "$KEY" --keyserver keyserver.ubuntu.com
"$KEYTOOL" --lsign-key "$KEY"

if ! grep -q '^\[libogc2-devkitpro\]' "$CONF"; then
  {
    echo
    echo '[libogc2-devkitpro]'
    echo 'Server = https://packages.libogc2.org/devkitpro/linux/$arch'
    echo 'Server = https://packages.extremscorner.org/devkitpro/linux/$arch'
  } >> "$CONF"
fi

# libogc2-libfat rather than libogc2-libdvm: the FAT API and this project's
# use of it are unchanged, and it avoids exFAT entirely.
if ! retry "$PACMAN" -Sy --noconfirm libogc2 libogc2-libfat; then
  # -Sy refuses to proceed unless every configured repository refreshes, and
  # devkitPro's own answers CI with a 403 often enough to stop a release -
  # while the libogc2 repository beside it downloads perfectly. The container
  # already ships devkitPro's databases, and a failed refresh still commits
  # the ones that did arrive, so the libogc2 database is there by now. Install
  # from what is on disk rather than demanding the network agree twice.
  echo "install_libogc2.sh: refresh failed; installing from the synced databases." >&2
  retry "$PACMAN" -S --noconfirm libogc2 libogc2-libfat
fi

[ -f "$RULES" ] || { echo "install_libogc2.sh: $RULES still missing after install." >&2; exit 1; }
echo "libogc2 installed at $DEVKITPRO/libogc2"
