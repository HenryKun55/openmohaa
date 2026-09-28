# OpenMoHAA on the PlayStation Vita

Single-player **Medal of Honor: Allied Assault** running natively on the PS Vita
as a homebrew app, built on OpenMoHAA's SDL2 / OpenGL renderer through vitaGL.

> This is a fan port of [OpenMoHAA](https://github.com/openmoh/openmohaa), which is
> built on [ioquake3](https://github.com/ioquake/ioq3). You need your own copy of
> the original game: no game data is included.

## Download

Get `OpenMoHAA.vpk` from the [Releases](https://github.com/HenryKun55/openmohaa/releases) page. Every release also
carries the source code of that exact build, as the GPL requires.

## What works

- The Allied Assault single-player campaign, level transitions and saves.
- Spearhead and Breakthrough, if their pk3s are present (less tested).
- Dual-analog controls, touch-free menus (analog cursor, Cross clicks).
- A render thread on its own CPU core, GPU skinning for characters, a world
  vertex buffer, and a performance menu to tune quality live.
- Around 30-45 FPS in combat on m1l1 on a stock Vita (444 MHz), more in lighter
  scenes.

## Install on the Vita

You need a Vita with custom firmware (HENkaku / h-encore / Enso) and VitaShell.

1. Copy `OpenMoHAA.vpk` to the root of the memory card (`ux0:`), by FTP
   (VitaShell: SELECT) or USB, and install it in VitaShell (Cross -> Install).
2. Copy the game files from your Medal of Honor: Allied Assault install:
   ```
   ux0:data/openmohaa/main/        Pak0.pk3 ... Pak5.pk3 (and music/, sound/, video/ if loose)
   ux0:data/openmohaa/mainta/      Spearhead pk3s (optional)
   ux0:data/openmohaa/maintt/      Breakthrough pk3s (optional)
   ```
3. Optional, faster loading: build a pre-resampled sound pack on a PC and copy it
   next to the paks (saves several seconds per level load):
   ```sh
   tools/snd_pak.py /path/to/main/Pak*.pk3 -o snd_out   # needs ffmpeg
   (cd snd_out && zip -r ../Pak8_snd.pk3 .)
   ```
   then copy `Pak8_snd.pk3` to `ux0:data/openmohaa/main/`.
4. Launch it from the LiveArea bubble.

A level load takes about a minute: the game reads ~60 MB of data per level and
the memory card's throughput is the limit.

## Controls

| Vita | Action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| R | Fire |
| L | Secondary attack |
| Cross | Use / click in menus / skip cutscenes and videos |
| Circle | Crouch |
| Square | Reload |
| Triangle | Jump |
| D-pad left / right | Previous / next weapon |
| D-pad up | Use |
| Start | Menu |
| Select (hold) | Objectives / scores |
| Select (double tap) | Vita settings menu (in game and in the 2D menus) |

The Controls menu changes these; they are saved in `ux0:data/openmohaa/main/configs/omconfig.cfg`.

## Settings menu

Double-tap Select, in game or in the main menus (or type `vitasettings` in the
console). It is in English, or Portuguese when the Vita's system language is
Portuguese, and every item has a one-line description. L / R or D-pad left /
right change tab, D-pad up / down select, Cross changes, Circle / Select / Start
close.

- **GRAPHICS**: presets (Performance / Balanced / Quality, Quality = every option
  at its highest) and texture, model, distant, curve, effect and terrain detail,
  texture filter, shadows (Off / Blob / Precise), dynamic lights, lens flares and
  decals.
- **DISPLAY**: FPS counter, HUD, crosshair, weapon model, blood.
- **CONTROLS**: look sensitivity and crosshair, separately for hip and aim.
- **SYSTEM**: restore the default settings, open the debug menu, close.

Settings marked `*` are applied when the menu closes (some restart the video).

The **debug menu** (SYSTEM tab, or `vitadebug`) has the renderer switches
(render thread, GPU skinning, world VBO, server thread...), world parts, cheats,
a level list and the profilers.

## Your settings and saves

Installing a new `.vpk` only replaces the app in `ux0:app/OMHA00001`. Your
settings (`configs/omconfig.cfg`), saves and game files live in
`ux0:data/openmohaa/` and are never touched by an update. The default settings
are only applied the first time the game starts on a Vita with no config; after
that, only your own choices are used.

## Performance notes

Measured on hardware with the in-game profiler (lines in
`ux0:data/openmohaa/main/boot.log`, every 60 frames):

- `FRAME-PROF`: main thread breakdown (server, client, cgame, scene building) and
  render thread busy time.
- `RT-PROF` / `RT-PROF2`: render thread by command and surface type, batches,
  vertexes, GPU vs CPU skinned surfaces.
- `CG-PROF`: cgame breakdown (entities, shadows, effects...).
- `G-PROF`: game logic (AI, scripts, player, other entities).
- `LOAD-PROF`: level load phases, file reads, images, models, sounds.

Main optimizations in this port: the renderer backend runs on its own core, the
render thread no longer waits for the GPU every frame, character skinning runs
on the GPU (including faces while not animating), entity light visibility and
shadow marks are cached, and shadow cost is bounded.

## Known issues

- Level loads take ~60-70 s (memory card throughput).
- Compressed (.dds) textures crash vitaGL on hardware, so they stay disabled.
- Multiplayer is not available (no networking layer on the Vita build).
- Vita3K runs the game, but is not representative of hardware performance.

## Debugging

- `ux0:data/openmohaa/main/boot.log`: engine log with the profiler lines.
- `ux0:data/openmohaa/main/crashlog.txt`: written on internal `Com_Error` failures.
- Crash dumps land in `ux0:data/psp2core-*.psp2dmp`; symbolicate them with
  [vita-parse-core](https://github.com/xyzz/vita-parse-core) against the matching
  ELF (`eboot`, `game.suprx` or `cgame.suprx` module).
- Vita3K log (macOS): `~/Library/Application Support/Vita3K/Vita3K/vita3k.log`.

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
