#!/bin/sh
# Switch static-module symbol isolation.
#
# libnx has no runtime code loader, so the game / cgame modules are linked
# statically into the single NRO. They share class names with the engine
# (ScriptVariable, Listener, Event, Class, con_set, ...) but compile them with
# DIFFERENT layouts — e.g. ScriptVariable has a `short3 key` member only under
# GAME_DLL — so the copies are NOT interchangeable. A plain link would either
# collide (multiple definition) or, with --allow-multiple-definition, silently
# merge mismatched layouts and corrupt memory at runtime.
#
# This script gives each module its own symbol namespace:
#   1. partial-link every archive member into ONE relocatable object so all
#      internal cross-references are resolved in-place;
#   2. rename every DEFINED symbol to a per-module prefix EXCEPT the module's
#      API entry point, which the engine resolves by name.
# Renaming the (defined) COMDAT signature symbols too means the module's C++
# vtable/typeinfo groups no longer match the engine's, so the final link keeps
# both copies instead of discarding one and leaving dangling relocations.
#
# args: CC NM OBJCOPY ARCHIVE KEEPSYM PREFIX OUT
set -e
CC="$1"; NM="$2"; OBJCOPY="$3"; AR="$4"; KEEP="$5"; PFX="$6"; OUT="$7"
TMP="$OUT.combined.o"
REDEF="$OUT.redef.txt"

# 1. merge all members into one relocatable object (resolves internal refs)
"$CC" -r -nostdlib -Wl,--whole-archive "$AR" -Wl,--no-whole-archive -o "$TMP"

# 2. build the rename table: every defined external (incl. weak/COMDAT) symbol
#    except the API entry point, mapped to PREFIX+name. Undefined symbols (libc,
#    libstdc++, engine import table) are left untouched so they still resolve.
"$NM" --defined-only --extern-only "$TMP" \
    | awk '{print $NF}' \
    | sort -u \
    | grep -vx "$KEEP" \
    | awk -v p="$PFX" '{print $1" "p$1}' > "$REDEF"

"$OBJCOPY" --redefine-syms="$REDEF" "$TMP" "$OUT"
