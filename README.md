# OpenMoHAA for PlayStation Vita

**Medal of Honor: Allied Assault** single-player, running natively on the PS Vita
as a homebrew app.

> [!NOTE]
> **In development.** There is no release right now: the next `.vpk` will be
> published on the [Releases](https://github.com/HenryKun55/openmohaa/releases)
> page when the current round of work is finished.

You need your own copy of the original game. No game data is included here.

## What it does

- The Allied Assault campaign, with level transitions and saves.
- Dual-analog controls and menus used with the pad (no touch needed).
- A settings menu in English or Portuguese (it follows the Vita's language), with
  quality presets and a debug menu for testing.
- Built for the Vita's hardware: the renderer runs on its own CPU core, characters
  are skinned on the GPU, the world is drawn from a vertex buffer, and light and
  shadow work is cached. Around 30-45 FPS in combat on the first mission on a
  stock Vita, more in lighter scenes.
- Multiplayer is not available on the Vita.

## Install, controls and building

Everything is in **[docs/PORTING-VITA.md](docs/PORTING-VITA.md)**: installing the
`.vpk`, where to put your game files, controls, the settings menu, how settings
and saves are kept, and how to build it yourself.

Updating the app never touches your settings or saves: they live in
`ux0:data/openmohaa/`, and an install only replaces the app itself.

## How this port is made

This port is developed with the help of AI (Claude, through Claude Code) together
with manual work:

- The AI writes and changes most of the code, reads the engine to find the
  causes of crashes and slowdowns, and builds the profilers used to measure them.
- The maintainer decides what gets done, tests every build by hand on a real Vita
  and on Vita3K, reports what they see (crashes, glitches, frame rate, load times)
  and approves each change.
- Each change is a separate commit, so any regression can be found and reverted
  on its own.
- Performance work is based on measurements taken on the real hardware, not on
  the emulator.

## Based on OpenMoHAA

This is a fork of [OpenMoHAA](https://github.com/openmoh/openmohaa), the
open-source engine for Medal of Honor: Allied Assault, which is built on
[ioquake3](https://github.com/ioquake/ioq3). All the engine work is theirs; this
repository adds the Vita port on the `vita-port` branch. The Vita code is kept
behind `__vita__`, so the desktop builds are unchanged.

For the PC version, multiplayer, servers and everything else about OpenMoHAA
itself, go to the [OpenMoHAA project](https://github.com/openmoh/openmohaa).

This branch also contains an experimental Nintendo Switch port and notes on
preparing the game data for consoles: see
[docs/CONSOLE-PORTS.md](docs/CONSOLE-PORTS.md).

## License

GPL v2, like OpenMoHAA and ioquake3 (see [LICENSE.txt](LICENSE.txt)). Each
release includes the source code of that exact build.

Medal of Honor is a trademark of Electronic Arts. This project is not affiliated
with or endorsed by Electronic Arts or Sony.
