#!/usr/bin/env bash
#
# make-forwarder.sh — build a HOME-menu forwarder NSP for OpenMoHAA (Switch).
#
# The forwarder contains ONLY the launcher; it next-launches the target NRO
# (sdmc:/switch/OpenMoHAA.nro). It bundles NO game data and NO engine assets.
#
# This is a SCAFFOLD: hacBrewPack's exact flags and the forwarder stub's path
# convention depend on the forwarder template you drop into exefs/. Read README.md
# in this folder first. For most users the web generator (Method A) is easier.
#
# Requirements you provide:
#   - hacbrewpack in PATH        https://github.com/The-4n/hacBrewPack
#   - ~/.switch/prod.keys        your own console keys (Lockpick_RCM)
#   - exefs/main (+ main.npdm)   a forwarder stub that reads romfs:/nextNroPath
#
# Usage:   misc/switch/forwarder/make-forwarder.sh
# Env overrides: TITLEID, NAME, AUTHOR, TARGET_NRO, ICON, OUT
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

TITLEID="${TITLEID:-0100BADC0DE00000}"            # any UNUSED 16-hex id; keep unique
NAME="${NAME:-OpenMoHAA}"
AUTHOR="${AUTHOR:-OpenMoHAA homebrew}"
TARGET_NRO="${TARGET_NRO:-sdmc:/switch/OpenMoHAA.nro}"
ICON="${ICON:-$REPO/misc/switch/icon.jpg}"
OUT="${OUT:-$HERE/out}"
KEYS="${KEYS:-$HOME/.switch/prod.keys}"

err() { echo "error: $*" >&2; exit 1; }
command -v hacbrewpack >/dev/null || err "hacbrewpack not found in PATH (see README.md)"
[ -f "$KEYS" ]            || err "keys not found: $KEYS (dump prod.keys with Lockpick_RCM)"
[ -f "$HERE/exefs/main" ] || err "missing forwarder stub: $HERE/exefs/main (see README.md)"
[ -f "$ICON" ]            || err "icon not found: $ICON"

echo ">> forwarder for: $TARGET_NRO"
echo ">> titleid=$TITLEID  name=$NAME"

# Lay out the NSP source tree
BUILD="$HERE/build"
rm -rf "$BUILD" "hacbrewpack_nsp"
mkdir -p "$BUILD/exefs" "$BUILD/romfs" "$BUILD/logo" "$OUT"
cp "$HERE/exefs/"* "$BUILD/exefs/"

# The forwarder stub reads its target from romfs. (Template-specific: some use
# nextNroPath, some nextArgv — match yours.)
printf '%s' "$TARGET_NRO" > "$BUILD/romfs/nextNroPath"
printf '%s' "$TARGET_NRO" > "$BUILD/romfs/nextArgv"

hacbrewpack \
  --titleid       "$TITLEID" \
  --titlename     "$NAME" \
  --titleauthor   "$AUTHOR" \
  --titlepublisher "$AUTHOR" \
  --nopatchnacplogo \
  -k "$KEYS" \
  --exefsdir "$BUILD/exefs" \
  --romfsdir "$BUILD/romfs" \
  --logodir  "$BUILD/logo" \
  --icon     "$ICON"

# hacBrewPack writes to ./hacbrewpack_nsp/<titleid>.nsp
nsp="$(ls hacbrewpack_nsp/*.nsp 2>/dev/null | head -1 || true)"
[ -n "$nsp" ] || err "hacbrewpack produced no .nsp — check flags / template"
mv "$nsp" "$OUT/OpenMoHAA-forwarder.nsp"

echo ""
echo ">> done -> $OUT/OpenMoHAA-forwarder.nsp"
echo "   install with Goldleaf / DBI / Tinfoil (CFW + sigpatches required)"
