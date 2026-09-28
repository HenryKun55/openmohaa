# OpenMoHAA for PlayStation Vita

**Medal of Honor: Allied Assault** single-player, running natively on the PS Vita.

**Download:** [OpenMoHAA.vpk](https://github.com/HenryKun55/openmohaa/releases/latest)
from the Releases page.

You need your own copy of the original game. No game data is included.

## Install

1. On the Vita, open **VitaShell** and press **SELECT** to start its FTP server.
2. From a computer, connect to the address it shows (for example with FileZilla)
   and copy `OpenMoHAA.vpk` to `ux0:/`.
3. In VitaShell, press **Cross** on `OpenMoHAA.vpk` and choose **Install**.
4. Copy `Pak0.pk3` ... `Pak5.pk3` from your PC game's `main` folder to
   `ux0:data/openmohaa/main/`.
5. Start it from the LiveArea.

Step-by-step guide (USB, optional files, updating):
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
and detail, HUD, crosshair, look sensitivity. English or Portuguese, following
the Vita's language.

## Not available

Multiplayer, and the Spearhead and Breakthrough expansions: this version plays the
Allied Assault campaign.

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
