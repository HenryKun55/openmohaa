# OpenMoHAA for PlayStation Vita

**Medal of Honor: Allied Assault** single-player, running natively on the PS Vita.
The same code also builds for **Nintendo Switch** and **macOS**.

> [!NOTE]
> **In development.** No release right now. The next one will be on the
> [Releases](https://github.com/HenryKun55/openmohaa/releases) page with builds
> for Vita (`.vpk`), Switch (`.nro`) and macOS.

You need your own copy of the original game. No game data is included.

## Install (Vita)

1. Install `OpenMoHAA.vpk` with VitaShell.
2. Copy your game's `Pak0.pk3` ... `Pak5.pk3` to `ux0:data/openmohaa/main/`.
3. Start it from the LiveArea.

Full guide: [docs/PORTING-VITA.md](docs/PORTING-VITA.md). Updating never touches
your settings or saves.

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

## Switch and macOS

- **Switch:** `OpenMoHAA.nro` in `sdmc:/switch/`, game files in
  `sdmc:/switch/openmohaa/main/`. See [docs/CONSOLE-PORTS.md](docs/CONSOLE-PORTS.md).
- **macOS:** game files in `~/Library/Application Support/openmohaa/main/`.

Multiplayer is not available on Vita or Switch.

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
