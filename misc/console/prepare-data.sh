#!/usr/bin/env bash
#
# prepare-data.sh — assemble a correctly laid-out OpenMoHAA `main/` data set
# for the homebrew console ports (PS Vita / Nintendo Switch) from YOUR OWN
# retail Medal of Honor: Allied Assault data (installed from your CD1/CD2).
#
# This script contains and downloads NO game assets — you supply them. It only
# copies and re-organises files you already own.
#
# Why it exists:
#   - The engine reads sounds at fixed lowercase paths (e.g.
#     sound/amb/amb_rainint_01.wav). Retail data often ships LOOSE sounds (the
#     ones not packed inside Pak*.pk3) flat and Capitalized directly in sound/.
#     On the case-sensitive console lookups those are never found, and a looping
#     ambient that misses every frame stutters/freezes the level (e.g. m5l1 rain).
#   - This routes each loose sound into the sub-folder + lowercase name the
#     engine asks for, copies the paks + music verbatim, and drops in the
#     platform autoexec.cfg (controller binds, etc.).
#
# Usage:
#   misc/console/prepare-data.sh <src-main> <dst-main> [switch|vita]
#
#     <src-main>   your retail MoHAA `main/`  (Pak0.pk3 … Pak5.pk3, sound/, music/)
#     <dst-main>   output folder to create    (copy this onto the device)
#     platform     which autoexec.cfg to install (default: switch)
#
# Then copy <dst-main> to:
#     Switch :  sdmc:/switch/openmohaa/main/
#     Vita   :  ux0:data/openmohaa/main/
#
set -euo pipefail

SRC="${1:?usage: prepare-data.sh <src-main> <dst-main> [switch|vita]}"
DST="${2:?usage: prepare-data.sh <src-main> <dst-main> [switch|vita]}"
PLATFORM="${3:-switch}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"

[ -d "$SRC" ] || { echo "error: src main/ not found: $SRC" >&2; exit 1; }
mkdir -p "$DST"

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# --------------------------------------------------------------------------
# 1. Pak*.pk3 — copied verbatim (the engine indexes pak contents
#    case-insensitively, so paks need no fix-up).
# --------------------------------------------------------------------------
echo ">> copying Pak*.pk3"
shopt -s nullglob nocaseglob
found_pak=0
for p in "$SRC"/Pak*.pk3; do cp -v "$p" "$DST/"; found_pak=1; done
[ "$found_pak" = 1 ] || echo "   warning: no Pak*.pk3 found in $SRC"
shopt -u nocaseglob

# --------------------------------------------------------------------------
# 2. music/ — loose mp3 (optional)
# --------------------------------------------------------------------------
if [ -d "$SRC/music" ]; then
  echo ">> copying music/"
  mkdir -p "$DST/music"
  cp -R "$SRC/music/." "$DST/music/"
fi

# --------------------------------------------------------------------------
# 3. sound/ — lay loose sounds out where the engine asks for them (lowercase).
# --------------------------------------------------------------------------
if [ -d "$SRC/sound" ]; then
  echo ">> laying out loose sounds (lowercase, correct sub-folders)"

  # 3a. Sounds already in a sub-folder: keep the sub-folder, just lowercase.
  while IFS= read -r -d '' f; do
    rel="${f#"$SRC"/sound/}"
    low="$(lower "$rel")"
    mkdir -p "$DST/sound/$(dirname "$low")"
    cp "$f" "$DST/sound/$low"
  done < <(find "$SRC/sound" -mindepth 2 -type f -print0)

  # 3b. Flat files sitting directly in sound/ — route by name prefix into the
  #     correct sub-folder. (Prefixes verified against the pak sound scripts.)
  while IFS= read -r -d '' f; do
    low="$(lower "$(basename "$f")")"
    case "$low" in
      amb_*)                       subs="amb" ;;
      wind_*)                      subs="amb environment" ;;
      mec_*|shortwave*|static*)    subs="mechanics" ;;
      m1_*|truck_*|veh_*|plane4*)  subs="vehicle" ;;
      *)                           subs="." ;;   # unknown -> leave at sound/ root
    esac
    for s in $subs; do
      mkdir -p "$DST/sound/$s"
      cp "$f" "$DST/sound/$s/$low"
    done
  done < <(find "$SRC/sound" -maxdepth 1 -type f -print0)
fi

# --------------------------------------------------------------------------
# 4. autoexec.cfg for the target platform (controller binds, joystick, etc.)
# --------------------------------------------------------------------------
case "$PLATFORM" in
  switch) AE="$REPO/misc/switch/main/autoexec.cfg" ;;
  vita)   AE="$REPO/misc/vita/main/autoexec.cfg" ;;
  *)      AE="" ; echo "   note: unknown platform '$PLATFORM', skipping autoexec.cfg" ;;
esac
if [ -n "$AE" ] && [ -f "$AE" ]; then
  echo ">> autoexec.cfg ($PLATFORM)"
  cp -v "$AE" "$DST/autoexec.cfg"
elif [ -n "$AE" ]; then
  echo "   warning: $AE not found, skipping autoexec.cfg"
fi

echo ""
echo ">> done -> $DST"
echo "   copy it to:"
echo "     Switch :  sdmc:/switch/openmohaa/main/"
echo "     Vita   :  ux0:data/openmohaa/main/"
