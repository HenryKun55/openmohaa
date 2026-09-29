# OpenMoHAA for PlayStation Vita

**Medal of Honor: Allied Assault** single-player, running natively on the PS Vita:
up to 60 FPS in lighter scenes, around 30-45 FPS in heavy combat.

**Download:** [OpenMoHAA.vpk](https://github.com/HenryKun55/openmohaa/releases/latest)
from the Releases page.

You need your own copy of the original game. No game data is included.

## Install

1. Make sure the Vita has **libshacccg.suprx** (the shader compiler most Vita ports
   need). If not, install and run [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D)
   once: it extracts it for you.
2. On the Vita, open **VitaShell** and press **SELECT** to start its FTP server.
3. From a computer, connect to the address it shows (for example with FileZilla)
   and copy `OpenMoHAA.vpk` to `ux0:/`.
4. In VitaShell, press **Cross** on `OpenMoHAA.vpk` and choose **Install**.
5. Copy the game files in the table below to `ux0:data/openmohaa/main/`.
6. Start it from the LiveArea.

The game files come from your game **installed on a PC**: the GOG edition (the only
store that sells it; it is not on Steam) or the discs installed with the official
1.11 patch. Take them from its `main` folder:

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
| `mainta`, `maintt` | no | expansions, not supported yet |
| `configs`, `.cfg`, `.exe`, `.dll` | no | PC settings and programs |

Each `.pk3` on the Vita must be the same size as on your PC. A menu with only white
outlines means a missing `libshacccg.suprx` or a pak that did not copy completely.

Step-by-step guide (which files, GOG or discs, USB, updating):
[docs/PORTING-VITA.md](docs/PORTING-VITA.md#install-on-the-vita). Updating never
touches your settings or saves.

## Controls

| Button | In game |
|---|---|
| Left stick | Move |
| Right stick | Look |
| R | Fire |
| L | Secondary attack |
| Cross | Use |
| Circle | Crouch |
| Square | Reload |
| Triangle | Jump |
| D-pad left / right | Previous / next weapon |
| D-pad up | Use |
| Start | Pause menu |
| Select (hold) | Objectives |
| Select (tap twice) | Vita settings |

In menus: left stick moves the cursor, Cross clicks, Circle goes back. Buttons can
be changed in the game's Controls menu.

**Vita settings** (tap Select twice, in game or in the menus): graphics presets
and detail, HUD, crosshair, look sensitivity, and the **language of the game's
text** (Portuguese so far; the voices stay as they are). Want your language?
See [docs/TRANSLATING.md](docs/TRANSLATING.md).

## Limits

- **Save often** from the game's menu (Start -> Save Game), especially before
  leaving the game or putting the Vita to sleep.
- Not available: multiplayer, and the Spearhead and Breakthrough expansions.
- Not tested yet: the PS button and coming back, sleep with the power button,
  closing and reopening the game, the whole campaign, Vita 2000 and PS TV.
  [Details](docs/PORTING-VITA.md#limits).

Something went wrong? [Report it](https://github.com/HenryKun55/openmohaa/issues/new/choose)
(English or Portuguese), with `ux0:data/openmohaa/main/boot.log` copied right
after the problem.

This branch also has experimental Switch and macOS code
([docs/CONSOLE-PORTS.md](docs/CONSOLE-PORTS.md)), without builds.

## How it is made

Developed with AI (Claude, through Claude Code) and manual work: the AI writes
most of the code and the profilers, the maintainer decides what to do, tests every
build on a real Vita and approves each change. Every change is its own commit, so
any regression can be found and reverted.

## Credits and license

A fork of [OpenMoHAA](https://github.com/openmoh/openmohaa), built on
[ioquake3](https://github.com/ioquake/ioq3). GPL v2 (see [COPYING.txt](COPYING.txt));
each release includes its source code.

Medal of Honor is a trademark of Electronic Arts. Not affiliated with Electronic
Arts, Sony or Nintendo.
