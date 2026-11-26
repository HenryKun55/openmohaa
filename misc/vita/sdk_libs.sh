#!/usr/bin/env bash
# OpenMoHAA for PS Vita: the two libraries the vdpm packages get wrong for this port.
# Run once after installing the vdpm packages (docs/PORTING-VITA.md), and again after any
# `vdpm vitaGL` / `vdpm libmad`, which put the unusable versions back. The build checks
# both (cmake/platforms/vita.cmake) and stops with a pointer here when they are wrong.
#
# - vitaGL: the GPU skinning (renderergl1/tr_vita_skin.c) draws through the vgl* pipeline
#   (vglVertexAttribPointerMapped, vglDrawObjects), which vitaGL compiles only with
#   ENABLE_LEGACY_PIPELINE=1. The vdpm package does not set it: vglDrawObjects is then an
#   empty function and every animated model (soldiers, the weapon in hand) is missing,
#   with no error anywhere. Pinned to the vitaGL the released builds use.
# - libmad: the vdpm package is built with the ARM EABI default -fshort-enums, the engine
#   with 32-bit enums: every MP3 (music, ambience, video sound) fails to open.
#
# The replaced files are kept next to them as *.vdpm.
set -euo pipefail

: "${VITASDK:?set VITASDK to the vitasdk install}"
export PATH="$VITASDK/bin:$PATH"
VITAGL_COMMIT=38d2f9704b6b241965ed7086aeaacd69f044f2c5
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
LIB="$VITASDK/arm-vita-eabi/lib"
INC="$VITASDK/arm-vita-eabi/include"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

keep() { [ -e "$1" ] && [ ! -e "$1.vdpm" ] && cp "$1" "$1.vdpm" || true; }

echo "== vitaGL $VITAGL_COMMIT (ENABLE_LEGACY_PIPELINE=1)"
git clone -q https://github.com/Rinnegatamante/vitaGL.git "$WORK/vitaGL"
git -C "$WORK/vitaGL" checkout -q "$VITAGL_COMMIT"
make -C "$WORK/vitaGL" NO_DEBUG=1 ENABLE_LEGACY_PIPELINE=1 -j"$(getconf _NPROCESSORS_ONLN)" >"$WORK/vitaGL.log" 2>&1 ||
    { tail -20 "$WORK/vitaGL.log"; exit 1; }
keep "$LIB/libvitaGL.a"
keep "$INC/vitaGL.h"
cp "$WORK/vitaGL/libvitaGL.a" "$LIB/"
cp "$WORK/vitaGL/source/vitaGL.h" "$INC/"

echo "== libmad (32-bit enums, generic fixed point)"
# Not FPM_ARM: its imdct_l_arm.S breaks ARM/Thumb interworking (crash at boot).
cmake -S "$REPO/code/thirdparty/libmad" -B "$WORK/libmad" \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DOPTIMIZE=SPEED -DASO=OFF -DFPM_ARM=OFF -DFPM_DEFAULT=ON \
    -DCMAKE_C_FLAGS=-fno-short-enums >"$WORK/libmad.log" 2>&1
cmake --build "$WORK/libmad" -j"$(getconf _NPROCESSORS_ONLN)" >>"$WORK/libmad.log" 2>&1 ||
    { tail -20 "$WORK/libmad.log"; exit 1; }
keep "$LIB/libmad.a"
keep "$INC/mad.h"
cp "$WORK/libmad/libmad.a" "$LIB/"
cp "$WORK/libmad/mad.h" "$INC/"

echo "Done. Reconfigure the build (cmake ..) so it checks them again."
