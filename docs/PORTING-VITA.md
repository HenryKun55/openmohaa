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
- Dual-analog controls, touch-free menus (analog cursor, Cross clicks).
- A render thread on its own CPU core, GPU skinning for characters, a world
  vertex buffer, and a performance menu to tune quality live.
- Up to 60 FPS in lighter scenes and around 30-45 FPS in heavy combat on the first
  mission, on a stock Vita (444 MHz).

## Install on the Vita

### What you need

- A PS Vita (or PS TV) with custom firmware (HENkaku, h-encore or Enso) and
  **VitaShell** installed.
- **libshacccg.suprx**, the Vita's shader compiler, extracted on the console. The
  game needs it to draw. Most Vita ports need it too, so you may already have it.
  If not: install [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D) (also on
  VitaDB), open it and let it extract and decrypt the file; it saves it to
  `ur0:data/libshacccg.suprx`.
- A Wi-Fi network shared by the Vita and a computer (or a USB cable).
- Your own copy of **Medal of Honor: Allied Assault**: the GOG "War Chest" edition
  or the original discs. You need its `main` folder (see step 3).
- About 1.5 GB free on the memory card.

### 1. Send the files to the Vita

The easy way is VitaShell's FTP server:

1. Open **VitaShell** on the Vita and press **SELECT**. It shows an address
   like `ftp://192.168.1.6:1337`. Keep that screen open while copying.
2. On the computer, open an FTP client (for example **FileZilla**, free) and
   connect to that address: host `192.168.1.6`, port `1337`, no user or password.
   (On Windows you can also type `ftp://192.168.1.6:1337` in the File Explorer
   address bar.)

Or by USB: in VitaShell press **START** -> **USB device**, connect the cable, and
the memory card shows up on the computer as a drive (`ux0:` is its root).

### 2. Install the app

1. Copy `OpenMoHAA.vpk` (from the [Releases](https://github.com/HenryKun55/openmohaa/releases)
   page) to the **root** of the memory card: `ux0:/OpenMoHAA.vpk`.
2. In VitaShell, go to `ux0:`, select `OpenMoHAA.vpk`, press **Cross** and choose
   **Install**. If it asks about extended permissions, answer **Yes**.
3. You can delete `ux0:/OpenMoHAA.vpk` afterwards.

### 3. Copy the game files

**Where they come from.** The files come from the game **installed on a PC**, not
straight from the discs: on the CDs they are packed inside the installer.

- **GOG** ("Medal of Honor: Allied Assault War Chest", the only store that sells it
  today; it is not on Steam): install it and use its `main` folder, e.g.
  `C:\GOG Games\Medal of Honor Allied Assault War Chest\main`. It is already
  patched. The GOG installer is for Windows; on macOS or Linux you can unpack it
  with [innoextract](https://constexpr.org/innoextract/).
- **Discs (CD1 + CD2)**: install the game on a Windows PC, then the official
  **1.11 patch** (it adds `Pak4.pk3` and `Pak5.pk3`). Use the `main` folder of the
  install, e.g. `C:\Program Files (x86)\EA GAMES\MOHAA\main`.

**What to copy** from that `main` folder to `ux0:data/openmohaa/main/`:

| Copy from the PC's `main` folder | Needed | Notes |
|---|---|---|
| `Pak0.pk3` | yes | |
| `Pak1.pk3` | yes | |
| `Pak2.pk3` | yes | |
| `Pak3.pk3` | yes | |
| `Pak4.pk3` | yes | from the 1.11 patch |
| `Pak5.pk3` | yes | from the 1.11 patch |
| `Pak6EnUk.pk3`, `pak7.pk3` | yes, if you have them | GOG only |
| any other `.pk3` | yes, if you have it | e.g. a language pak |
| `sound` folder | recommended | copy the whole folder as it is: music, dialogue, ambient |
| `video` folder | optional | intro videos |
| `mainta`, `maintt` | optional | the Spearhead and Breakthrough expansions: copy each whole folder next to `main` (see below) |
| `configs`, `.cfg`, `.exe`, `.dll` | no | PC settings and programs |

The result:

```
ux0:data/openmohaa/main/
    Pak0.pk3  Pak1.pk3  Pak2.pk3  Pak3.pk3  Pak4.pk3  Pak5.pk3
    Pak6EnUk.pk3  pak7.pk3   (GOG only)
    sound/    (music, dialogue, amb_stereo and other folders, plus some loose .wav files)
    video/    (optional)
```

Keep `sound` exactly as the PC has it: do not rename or move files inside it. Some
ambient sounds sit directly in `sound/` on the PC; the game finds them there
(from v0.1.1; v0.1 missed them).

**Check the copy.** Each `.pk3` on the Vita must have exactly the same size as on
the PC (sizes differ between editions, so compare with your own PC files, not
someone else's). A menu that shows only white outlines means the game cannot draw
properly: check first that `ur0:data/libshacccg.suprx` exists (see "What you need"),
then that no pak stopped copying halfway. `boot.log` also lists, near
the top, every pak the game found and how many files it has.

**Optional, faster loading:** build a pre-resampled sound pack on a PC and copy it
next to the paks (saves several seconds per level load):
```sh
tools/snd_pak.py /path/to/main/Pak*.pk3 -o snd_out   # needs ffmpeg
(cd snd_out && zip -r ../Pak8_snd.pk3 .)
```
then copy `Pak8_snd.pk3` to `ux0:data/openmohaa/main/`.

### 4. Play

Start **Medal of Honor: Allied Assault** from its LiveArea bubble. A level load takes about a minute: the game reads ~60 MB
per level and the memory card's speed is the limit.

### Updating

Install the new `.vpk` the same way. Your settings and saves in
`ux0:data/openmohaa/` are kept.

### Tested with

- Game: English discs with the 1.11 patch (`Pak0.pk3` to `Pak5.pk3`). The GOG
  edition and other languages should work but are not tested yet.
- Console: PS Vita 1000 (OLED). Vita 2000 and PS TV are not tested yet.

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
console). It is in the Text Language chosen in DISPLAY (English, Português, Español,
Français, Deutsch, Italiano; the first start follows the Vita's system language), and
every item has a one-line description. L / R or D-pad left /
right change tab, D-pad up / down select, Cross changes, Circle / Select / Start
close.

- **GRAPHICS**: presets (Performance / Balanced / Quality, Quality = every option
  at its highest) and texture, model, distant, curve, effect and terrain detail,
  texture filter, shadows (Off / Blob / Precise), dynamic lights, lens flares and
  decals.
- **DISPLAY**: FPS counter, HUD, crosshair, weapon model, blood, subtitles, and the
  Text Language of the game, its subtitles and this menu. The menu pictures change too
  when that language's `lang_<code>.pk3` is in `ux0:data/openmohaa/main/` (see
  [TRANSLATING.md](TRANSLATING.md)).
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

## Limits

**Save often.** Use the game's own menu (Start -> Save Game), especially before
leaving the game, closing it or putting the Vita to sleep. Saves have no thumbnail
picture on the Vita (taking it froze the game for almost a second).

**Not available:**

- Multiplayer (no networking on the Vita build); its door in the main menu shows
  a notice.

**Not tested yet**, so they may fail (please [report it](https://github.com/HenryKun55/openmohaa/issues/new/choose)
if they do):

- Pressing the PS button to go to the home screen and coming back to the game.
- Putting the Vita to sleep with the power button and waking it during a game.
- Closing the game from the LiveArea and starting it again.
- The whole campaign from start to end: not every mission has been played through.
- Vita 2000 and PS TV (tested on a Vita 1000).

**Known issues:**

- Level loads take ~60-70 s (memory card throughput).
- Compressed (.dds) textures crash vitaGL on hardware, so they stay disabled.
- Vita3K runs the game, but is not representative of hardware performance.

## Reporting a problem

Open an issue with the [Vita problem form](https://github.com/HenryKun55/openmohaa/issues/new/choose),
in English or Portuguese. Attach `ux0:data/openmohaa/main/boot.log`, copied right
after the problem (every start overwrites it), and, after a crash, the newest
`ux0:data/psp2core-*.psp2dmp` in a .zip.

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
misc/vita/main/vita_autoexec.cfg    # start-up settings (pad, video mode, intro skip), always run
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
