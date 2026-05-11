# OpenMoHAA on the PlayStation Vita

Single-player Medal of Honor: Allied Assault running natively on the
PS Vita as a homebrew app, built on top of OpenMoHAA's existing
SDL2 / OpenGL renderer.

## What works

- Full engine bring-up: filesystem (pk3 mounting), config exec, client
  initialisation, renderer, audio, scripting (TIKI), AI.
- Single-player campaign (Allied Assault base game).
- Spearhead and Breakthrough expansions, if their pk3s are present.
- Native dual-analog control via SDL_GameController.
- 960x544 fullscreen, vsync enabled, vitaGL (OpenGL ES → GXM).

## What's stubbed (multiplayer + dev tooling)

The following subsystems are intentionally disabled because the Vita
SDK either does not ship the underlying API (BSD sockets) or because
the feature has no meaningful counterpart on the platform. Single-
player gameplay is unaffected.

| Subsystem | Reason | Source of stubs |
|---|---|---|
| GameSpy SDK (~30 funcs) | No BSD sockets, GameSpy masters defunct since 2014 | `code/gamespy/gamespy_vita_stub.c` |
| `net_ip.c` (UDP/TCP) | vitasdk uses `sceNet`, not BSD; SP needs no networking | `code/qcommon/net_vita.c` |
| `cl_uiserverlist.cpp` | Pulls GameSpy types | gated in `cmake/client.cmake` |
| `libmumblelink.c` (voice) | Needs `sys/mman.h` (no mmap on Vita) | gated in `cmake/client.cmake` |
| Launcher binaries | Vita has no multi-binary picker concept | gated in `CMakeLists.txt` |
| 8 desktop GL legacy entry points | vitaGL is GL ES based; never called by GL1 path | `code/sdl/vita_gl_stubs.c` |
| `mkfifo` (Sys_Mkfifo) | dedicated-server console FIFO, not built on Vita | gated in `code/sys/sys_unix.c` |
| `_kill_r` for arbitrary signals | vitasdk newlib only handles SIGINT/SIGTERM | `Sys_PIDIsRunning` returns true if pid==self |

## Build

### Host requirements

- macOS, Linux, or Windows with WSL
- CMake ≥ 3.25
- Bison ≥ 3.5.1, Flex ≥ 2.6.4
- A vitasdk install (sets `$VITASDK`)

### vitasdk + libs

```sh
git clone https://github.com/vitasdk/vdpm
cd vdpm
export VITASDK=$HOME/vitasdk         # or /usr/local/vitasdk
./bootstrap-vitasdk.sh
# Install runtime deps (vitaGL, SDL2, OpenAL, codecs, ...)
for pkg in zlib bzip2 libpng libjpeg-turbo \
           sdl2 openal-soft openssl curl \
           libogg libvorbis opus opusfile libmad \
           libmathneon vitaShaRK vitaGL SceShaccCgExt \
           kubridge taihen vita-rss-libdl ; do
  ./vdpm -f $pkg
done
```

### librt stub

vitasdk does not ship `librt` (POSIX realtime — `clock_gettime` lives
in `libc` here). pkg-config files for `ogg`/`vorbis`/`opus` inherit
`-lrt` from upstream Linux builds, which the linker then can't find.
Drop a small empty stub once:

```sh
echo 'void __vita_librt_stub(void) {}' > /tmp/librt_stub.c
$VITASDK/bin/arm-vita-eabi-gcc -c /tmp/librt_stub.c -o /tmp/librt_stub.o
$VITASDK/bin/arm-vita-eabi-ar rcs $VITASDK/arm-vita-eabi/lib/librt.a /tmp/librt_stub.o
```

### Configure + build

```sh
mkdir build-vita && cd build-vita
cmake -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      ..
cmake --build . -j$(nproc)
```

The `OpenMoHAA.vpk` lands in `build-vita/`.

## Install on the Vita

1. Vita CFW required (HENkaku/h-encore/Enso).
2. Copy `OpenMoHAA.vpk` to the Vita (FTP via VitaShell or USB).
3. Install in VitaShell (X → Install).
4. Copy your legitimate MoH:AA install to:
   ```
   ux0:data/openmohaa/main/        Pak0..Pak5.pk3, music/, sound/, video/
   ux0:data/openmohaa/mainta/      Spearhead pk3s (optional)
   ux0:data/openmohaa/maintt/      Breakthrough pk3s (optional)
   ```
5. Launch from the LiveArea bubble.

The stock Vita CD/DVD ROM dumps work — install via Wine or extract
with `unshield` from `data1.hdr` + `data1.cab` + `data2.cab` + `data3.cab`,
plus the loose `Pak2.pk3` from disc 2.

## Controls

Default bindings shipped in `app0:/main/autoexec.cfg`:

| Vita | Action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| Cross | Fire |
| Square | Jump |
| Circle | Crouch |
| Triangle | Reload / use |
| L | Walk (vs run) |
| R | Iron sights / zoom |
| L3 (left stick press) | Sprint |
| R3 (right stick press) | Lean |
| Select | Show scores |
| Start | Open menu |
| D-pad | Weapon select |

Override in `ux0:data/openmohaa/main/autoexec.cfg` if you want a
different layout.

## Known issues

- **In-game gameplay crashes at script compilation.** The menu, audio,
  cinematics and renderer are all working on real hardware. Selecting
  Single Player → a mission successfully loads `game.suprx` (statically
  linked into the eboot — see `cmake/basegame.cmake` and
  `code/sys/new/sys_main_new.c`) and `G_InitGame` runs cleanly. The
  briefing map then crashes inside `ClassDef::GetDef(int)` at
  `this->m_pResponseDefs[event]` with `m_pResponseDefs == NULL`,
  called from `ScriptCompiler::EmitField`.

  Root cause: `class ClassDef` and `class Listener` in `corepp/` have
  layout-affecting members and virtuals gated behind
  `#ifdef WITH_SCRIPT_ENGINE`. fgame is compiled with the define;
  cgame and the engine aren't. On upstream's SHARED build each
  binary has its own private corepp and the layouts never need to
  agree. In our static-link build the same `ClassDef::classlist`
  ends up linked into one chain with instances constructed by two
  different layouts, so `m_pResponseDefs` lands at a different offset
  depending on whose code reads it.

  The fix needs `WITH_SCRIPT_ENGINE` (and matching `ARCHIVE_SUPPORTED`)
  to be uniform across the engine + cgame + game compilation. A
  straightforward `add_compile_definitions(WITH_SCRIPT_ENGINE)` in
  `vita.cmake` cascades into errors from fgame headers that depend
  on `GAME_DLL` being set as well — specifically
  `fgame/g_utils.h:212` accesses `g_entities[].entity` which only
  exists when `GAME_DLL` is defined. A proper fix is one of:
    - Split corepp into its own STATIC lib built with
      `WITH_SCRIPT_ENGINE ARCHIVE_SUPPORTED`, shared between engine
      and game module, plus enough header reorganisation so that
      compiling corepp in isolation doesn't pull in
      `fgame/g_utils.h` (it currently does through
      `script/scriptvm.h` → `fgame/gamescript.h`).
    - Or build only the engine with `WITH_SCRIPT_ENGINE` and rely on
      the fact that cgame doesn't construct fgame's ClassDef
      instances at runtime (only iterates them via the shared
      `classlist`).
- **Vita3K compatibility**: the binary boots and reaches `vglInitExtended`,
  but Vita3K's GXM emulation is incomplete and the renderer hangs there.
  Real Vita hardware works because vitaGL talks to GXM directly.
- **Performance**: Cortex-A9 quad @ 444 MHz is below OpenMoHAA's
  recommended (Cortex-A9 800 MHz). Expect 20-30 FPS in light scenes,
  lower in busy ones. Overclocking to 500 MHz via PSVshell helps.
- **GXM enum width warning**: linker warns about `32-bit enums vs
  variable-size enums` on a few `.o` files. The Sce stub libs are
  built with `-fshort-enums`; our app uses fixed 32-bit enums. They
  agree on every value we care about, but if you see weird sce*
  return-value handling that's the first place to look.
- **Multiplayer is gone.** Direct-IP play would be possible by writing
  a real `net_psp2.c` against `sceNet`; the existing stubs make every
  send/recv a no-op.

## Debugging

- VitaShell → SELECT → Show log dumps the current process log to
  `ux0:data/`.
- `ux0:data/openmohaa/main/crashlog.txt` is written by the engine on
  internal `Com_Error` failures.
- For Vita3K dev iterations the log lives at:
  `~/Library/Application Support/Vita3K/Vita3K/vita3k.log` (macOS).

## Layout of the Vita-specific changes

```
cmake/platforms/vita.cmake          # toolchain, deps, VPK packaging
code/sys/sys_vita.c                 # heap/stack budget, Sce module loads
code/qcommon/net_vita.c             # net_ip.c stub
code/sdl/vita_gl_stubs.c            # legacy GL entry points vitaGL omits
code/gamespy/gamespy_vita_stub.c    # GameSpy SDK no-op layer
misc/vita/sce_sys/                  # icon0, LiveArea bg, template.xml
misc/vita/main/autoexec.cfg         # default bindings + render tuning
```

In-tree files patched with `#ifdef __vita__` guards:

- `code/qcommon/q_platform.h` — Vita OS detection
- `code/gamespy/common/gsPlatform.h` — Vita treated as `_UNIX` (then stubbed)
- `code/sys/sys_unix.c` — `Sys_Exec`, paths, `Sys_GetCurrentUser`,
  `Sys_Basename`/`Dirname`, `Sys_Mkfifo`, `Sys_PIDIsRunning`,
  `Sys_PlatformInit/Exit`
- `code/sys/new/sys_unix_new.c` — backtrace gated out
- `code/sdl/sdl_glimp.c` — Vita branch in `GLimp_SetMode` calls
  `vglInitExtended` and skips the desktop GL context loop
- `code/renderergl1/tr_image.c` — removed `JPEG_INTERNALS` (unused)
- `cmake/shared_sources.cmake`, `cmake/client.cmake` — gating
- `cmake/platforms/all.cmake`, `CMakeLists.txt` — wire-in
