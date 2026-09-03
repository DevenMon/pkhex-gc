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

PACMAN=$(command -v dkp-pacman || command -v pacman || true)
KEYTOOL=$(command -v dkp-pacman-key || command -v pacman-key || true)
if [ -z "$PACMAN" ] || [ -z "$KEYTOOL" ]; then
  echo "install_libogc2.sh: no devkitPro pacman found; cannot install libogc2." >&2
  echo "Install it manually: https://github.com/extremscorner/libogc2#readme" >&2
  exit 1
fi

CONF="$DEVKITPRO/pacman/etc/pacman.conf"
[ -f "$CONF" ] || CONF=/etc/pacman.conf

"$KEYTOOL" --recv-keys "$KEY" --keyserver keyserver.ubuntu.com
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
"$PACMAN" -Sy --noconfirm libogc2 libogc2-libfat

[ -f "$RULES" ] || { echo "install_libogc2.sh: $RULES still missing after install." >&2; exit 1; }
echo "libogc2 installed at $DEVKITPRO/libogc2"
