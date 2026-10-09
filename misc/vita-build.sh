#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'INNER'
Usage: ./build-vita.sh [--clean] [--help]

Helper script for building OpenMoHAA for PlayStation Vita.

Options:
  --clean   Remove the existing build-vita directory before configuring.
  --help    Show this help message.
INNER
}

CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --clean)
      CLEAN=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $arg" >&2
      usage
      exit 1
      ;;
  esac
done

VITASDK="${VITASDK:-}"
if [ -z "$VITASDK" ]; then
  if [ -d "$HOME/vitasdk" ]; then
    VITASDK="$HOME/vitasdk"
  elif [ -d "/usr/local/vitasdk" ]; then
    VITASDK="/usr/local/vitasdk"
  fi
fi

if [ -z "$VITASDK" ]; then
  echo "Error: VITASDK is not set."
  echo "Set VITASDK to your VitaSDK root, e.g. export VITASDK=\$HOME/vitasdk"
  exit 1
fi

TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
  echo "Error: Vita toolchain file not found: $TOOLCHAIN_FILE"
  exit 1
fi

if [ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]; then
  echo "Error: Vita SDK compiler not found: $VITASDK/bin/arm-vita-eabi-gcc"
  exit 1
fi

BUILD_DIR="build-vita"
if [ "$CLEAN" -eq 1 ]; then
  echo "Removing existing $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

if [ ! -f "$VITASDK/arm-vita-eabi/lib/librt.a" ]; then
  echo "Creating missing librt stub in Vita SDK..."
  tmpdir="$(mktemp -d)"
  cat > "$tmpdir/librt_stub.c" <<'STUB'
void __vita_librt_stub(void) {}
STUB
  "$VITASDK/bin/arm-vita-eabi-gcc" -c "$tmpdir/librt_stub.c" -o "$tmpdir/librt_stub.o"
  "$VITASDK/bin/arm-vita-eabi-ar" rcs "$VITASDK/arm-vita-eabi/lib/librt.a" "$tmpdir/librt_stub.o"
  rm -rf "$tmpdir"
fi

cd "$BUILD_DIR"
cmake -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel

echo "Build complete. The OpenMoHAA.vpk package is generated in $PWD."
