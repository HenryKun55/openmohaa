# OpenMoHAA

[![Build](https://github.com/openmoh/openmohaa/actions/workflows/branches-build.yml/badge.svg?branch=main)](https://github.com/openmoh/openmohaa/actions/workflows/branches-build.yml) [![Release](https://img.shields.io/github/v/release/openmoh/openmohaa)](https://github.com/openmoh/openmohaa/releases) [![Downloads](https://img.shields.io/github/downloads/openmoh/openmohaa/total)](https://github.com/openmoh/openmohaa/releases)

![License](https://img.shields.io/github/license/openmoh/openmohaa) ![Commits](https://img.shields.io/github/commit-activity/t/openmoh/openmohaa)

![Discord](https://img.shields.io/discord/596049712579215361?logo=discord&logoColor=white&color=5865F2)

![logo](misc/openmohaa-text-sm.png)

## What is OpenMoHAA?

OpenMoHAA is an open-source project aimed at preserving and enhancing **Medal of Honor: Allied Assault** (including Spearhead and Breakthrough expansions) by providing more features and bugfixes, across modern platforms and architectures.

Powered by [ioquake3](https://github.com/ioquake/ioq3) and the [F.A.K.K SDK](https://code.idtech.space/ritual/fakk2-sdk), OpenMoHAA provides:
- Full compatibility with the original game: assets, scripts and multiplayer
- Better support for modern systems
- Cross-platform support (Linux, Windows, macOS)
- Support for both single-player and multiplayer modes
- Includes all fixes from Spearhead 2.15 and Breakthrough 2.40b
- More fixes and features, such as bots and a ban system

*OpenMoHAA is an independent project and is not affiliated with or endorsed by Electronic Arts.*

## Getting started

- 📦 [Installing OpenMoHAA](docs/markdown/01-intro/01-installation.md)
- ▶️ [How to play: Launching the game, expansions & file locations](docs/markdown/02-running/01-running.md)
- ❓ [FAQ & Troubleshooting](docs/markdown/02-running/03-faq.md)
- 🌐 [Setting up a game server](docs/markdown/02-running/02-running-server.md)

## Reporting Issues

> [!NOTE]
> OpenMoHAA hasn't hit version 1.0.0 yet. Think of it like a beta build from the golden age of LAN parties. Features are being added, bugs are getting squashed, and more things are being tweaked. Things might change, break, or get even better over time.
> 
> If that sounds like your kind of mission, gear up, frag some bots, and help level up OpenMoHAA!

If you encounter a bug or a problem, you can do one of the following:
- Submit an [issue](https://github.com/openmoh/openmohaa/issues) on GitHub (use the template).
- Join the [OpenMoHAA Discord](https://discord.gg/NYtH58R) for a quick help.

## Additional documentation

- 📖 [Documentation](https://openmoh.github.io/openmohaa)
- ⚙️ [Game settings & configuration](docs/markdown/03-configuration/01-configuration.md)
- 📝 [Code & Scripting reference](docs/markdown/04-coding/02-coding.md)
- 📜 [Contributing guidelines](CONTRIBUTING.md)

## Current state

- 🧰 [List of differences](docs/markdown/01-intro/04-differences.md)

### Single-player

The entire single-player campaign should work (Allied Assault, Spearhead and Breakthrough). If you encounter any bug, please create a new [GitHub issue](https://github.com/openmoh/openmohaa/issues) describing them.

### Multiplayer

- Almost fully stable
- All official game modes are supported, including those from Spearhead and Breakthrough:
  - Free-For-All
  - Team-Deathmatch
  - Round-based match
  - Objective match
  - Tug-of-War (Spearhead)
  - Liberation (Breakthrough)
- Popular mods like **Freeze-Tag** are supported
- Built-in bots for offline practice and for testing
  - 🔧 [Setting up bots](docs/markdown/02-running/01-running.md#Playing-with-bots)

You can host your own [OpenMoHAA server](docs/markdown/02-running/02-running-server.md#) or join others using OpenMoHAA.

## Console homebrew ports (PS Vita / Nintendo Switch)

> [!WARNING]
> **Console ports — version `0.0.1` · experimental TEST build.**
> The Vita/Switch ports are at a very early stage and **everything here is still
> a test / work in progress**. They are already **functional enough to play and,
> more importantly, to surface bugs** — which is exactly the goal right now.
> **Expect** crashes, freezes, missing audio/effects, performance dips and rough
> edges. Treat this as a tool to *find and report problems*, **not** a finished
> release. No warranty — back up your SD card / saves before using it.

This fork (`vita-port` branch) adds experimental homebrew ports to the **PlayStation Vita** (vitaGL / vitasdk) and the **Nintendo Switch** (libnx / devkitPro), alongside the regular desktop builds. All console-specific code is guarded behind `__vita__` / `__SWITCH__`, so the Linux/Windows/macOS builds are unaffected. The Switch build links the engine, game and cgame into a single NRO with per-module symbol isolation (it has no runtime code loader).

> [!IMPORTANT]
> **You supply your own game data.** OpenMoHAA ships only the open-source engine — **no game assets are included or distributed here**. Copy a `main/` data set from your **own** retail *Medal of Honor: Allied Assault* install (the `Pak*.pk3` files from your CD1/CD2, plus the loose `sound/` and `music/`).

| Platform | Current state (`v0.0.1` — expect bugs) |
|----------|------------------------------------------|
| **macOS** | The reference desktop build — believed to work well, but **not guaranteed**: it's `0.0.1` like the rest, so **anything can still happen**. Please report what you hit. |
| **PS Vita** | **Playable** — single-player runs — but it **can still crash**, and performance work (VBO world draw, GPU skinning, faster loads) is ongoing. |
| **Nintendo Switch** | **Playable on real hardware** — the campaign runs after the briefing-skip, script loop-guard and loose-sound fixes — but it **can still freeze at certain moments and hit crashes that aren't mapped yet**. This is exactly why it's `0.0.1`. |

### Getting the game data (you must own it)

OpenMoHAA ships **no game assets** — you need the data from a copy of *Medal of
Honor: Allied Assault* that **you own**. Whatever the source, you are after the
game's **`main/` folder** and the files inside it.

**Where it can come from:**

- **GOG — "Medal of Honor: Allied Assault War Chest":** the easiest legit source
  today. The bundle (base game + **Spearhead** + **Breakthrough**) is sold on
  GOG; after installing, the data sits in the game folder as
  `main/` + `mainta/` + `maintt/`.
- **Steam — same "War Chest" bundle:** also distributed on Steam; the data lives
  under `…\Steam\steamapps\common\Medal of Honor Allied Assault War Chest\`.
  (Store availability has shifted over time — check your own library; the folder
  layout is identical.)
- **Retail discs (CD1 + CD2):** install on a Windows PC; the install folder
  (e.g. `…\EA GAMES\MOHAA\`) contains `main/` (+ `mainta/` / `maintt/` if the
  expansions are installed).
- **EA App / Origin:** not currently sold there.
- **Demo:** the official AA / Spearhead / Breakthrough demos also work on
  desktop (`+set com_target_demo 1`).

> ⚠️ Don't grab "the game files" from random download sites — that's piracy, and
> those paks are frequently modified or incomplete. Use the discs or the store
> copy **you own**.

**What the engine actually needs (minimum, base game):**

- `main/Pak0.pk3` … `main/Pak5.pk3` — the game data. `Pak0`–`Pak3` are the base
  game; `Pak4`/`Pak5` come from the official **v1.11 patch**. Localized releases
  also add a **language pak** (e.g. `Pak6Uk.pk3` = English, `Pak6Es.pk3` =
  Spanish — the trailing letters are the language). **Copy every `Pak*.pk3` you
  have** — e.g. `Pak2.pk3` holds most of the world textures, so without it
  levels render grey/untextured.
- *(optional, recommended)* `main/music/*.mp3` and any loose `main/sound/…`
  files — music and ambient/vehicle sounds that aren't packed inside the paks
  (these are what the loose-sound note below, and `prepare-data.sh`, handle).

> **Expansions:** **Spearhead** uses a separate `mainta/` folder and
> **Breakthrough** a `maintt/` folder (desktop: `+set com_target_game 1` / `2`).
> The Vita/Switch ports currently target the **base game (`main/`)**.

### Where the files go

On every platform the **engine binary** and the **game data** are separate. Drop the data `main/` folder in the per-platform location below:

| Platform | Engine binary | Game data (`main/`) |
|----------|---------------|---------------------|
| **macOS** | the built `openmohaa` app / binary | `~/Library/Application Support/openmohaa/main/` |
| **PS Vita** | install `OpenMoHAA.vpk` with VitaShell | `ux0:data/openmohaa/main/` |
| **Nintendo Switch** | `OpenMoHAA.nro` at `sdmc:/switch/OpenMoHAA.nro` (run from hbmenu) — or a [forwarder NSP](misc/switch/forwarder/README.md) for a HOME-menu icon | `sdmc:/switch/openmohaa/main/` |

A complete `main/` looks like this:

```
main/
├── Pak0.pk3 … Pak5.pk3   # base-game data (Pak0–3 = game, Pak4–5 = v1.11 patch)
├── music/                # *.mp3 (loose, optional)
├── sound/                # loose sounds that are NOT inside the Pak*.pk3
│   ├── amb/              # amb_* , wind_*        (ambient)
│   ├── environment/      # wind_*
│   ├── mechanics/        # mec_* , shortwave* , static*
│   └── vehicle/          # truck_* , veh_* , m1_* , plane4
└── autoexec.cfg          # console builds: misc/<platform>/main/autoexec.cfg
```

> [!WARNING]
> **Switch / Vita loose-sound gotcha:** any sound that is *not* packed inside a `Pak*.pk3` must sit in the exact sub-folder and **lowercase** filename the engine asks for (e.g. `sound/amb/amb_rainint_01.wav`). Retail data sometimes ships these flat and capitalized directly in `sound/`, which the case-sensitive lookup never finds — the result is a stutter/freeze as the engine retries the missing file every frame (notably the rain on Mission 5). The helper script below fixes this for you.

### Preparing your data set

[`misc/console/prepare-data.sh`](misc/console/prepare-data.sh) turns your own retail `main/` into a correctly-laid-out `main/` ready to copy onto the device — it copies the paks + music and routes every loose sound into its proper sub-folder (lowercase), then drops in the right `autoexec.cfg`:

```sh
misc/console/prepare-data.sh /path/to/retail/main ./out-main switch
# then copy ./out-main to  sdmc:/switch/openmohaa/main/   (Switch)
#                     or to ux0:data/openmohaa/main/       (Vita: pass "vita")
```

### Running on Switch — with or without a forwarder NSP

There are two ways to launch the Switch port; **both play identically** — the
difference is only how you start it:

- **Without an NSP (default):** put `OpenMoHAA.nro` at `sdmc:/switch/OpenMoHAA.nro`
  and open it from **hbmenu** (Album → homebrew). Nothing to install.
- **With a forwarder NSP (optional):** build a tiny forwarder so OpenMoHAA gets
  its own **icon on the HOME menu**, which just launches that same NRO. The
  forwarder contains **only the launcher — no game data**. See
  [`misc/switch/forwarder/README.md`](misc/switch/forwarder/README.md).

Either way the game data lives on the SD card at `sdmc:/switch/openmohaa/main/`.

### Switch — controls, what we tested on, and config

**Controls** come from [`misc/switch/main/autoexec.cfg`](misc/switch/main/autoexec.cfg)
(installed automatically by `prepare-data.sh`). Default layout:

| Input | Action |
|-------|--------|
| **Left stick** | Move (forward / back / strafe) |
| **Right stick** | Look / aim |
| **ZR** | Fire |
| **ZL** | Secondary fire |
| **A** | Use / interact |
| **B** | Jump |
| **Y** | Crouch |
| **X** | Reload |
| **L / R** | Previous / next weapon |
| **L3 / R3** | Lean left / right |
| **D-pad** ↑ / ↓ | Use / crouch |
| **D-pad** ← / → | Previous / next weapon |
| **+** (Plus) | Menu |
| **−** (Minus) | Scoreboard · **double-tap** = on-screen dev/perf menu |

That config only sets the **hardware essentials** the game can't default on
Switch — gamepad on, analog-look feel, the button map, native 720p, sound on.
Everything else (volume, sensitivity, etc.) is left to the in-game menu so your
choices persist in `configs/omconfig.cfg` across reboots.

**Tested on:**
- **Real Nintendo Switch** on custom firmware (Atmosphère) — the primary target
  and the source of truth for crashes.
- **Ryujinx `1.3.3`** (the community-maintained fork) — runs with **stock,
  default emulator settings: we made no config changes at all and it worked out
  of the box.**

> Reminder: this is `v0.0.1`. The Switch build can still **freeze at certain
> moments or hit crashes that aren't mapped yet**. When that happens,
> `sdmc:/switch/openmohaa/main/boot.log` is where to look — on real hardware it
> captures the faulting address so the crash can be traced.

## Screenshots

|                                                                                   |                                                                            |
|-----------------------------------------------------------------------------------|----------------------------------------------------------------------------|
| ![](docs/assets/images/v0.60.0-x86_64/mohdm1_1.png)                                      | ![](docs/assets/images/v0.60.0-x86_64/training_1.png)                               |
| ![](docs/assets/images/v0.60.0-x86_64/flughafen_1.png)                                   | ![](docs/assets/images/v0.60.0-x86_64/flughafen_2.png)                            |
| ![](docs/assets/images/v0.60.0-x86_64/mohdm2_1.png "Playing Freeze-Tag mode with bots")  | ![](docs/assets/images/v0.60.0-x86_64/training_3.png "Single-Player training")    |

*More screenshots [here](docs/assets/images)*

## Development & Compiling

- 💻 [Building from source](docs/markdown/04-coding/01-compiling.md)

## Third party librairies

The following third party tools and libraries are used by the project

- [Flex](https://github.com/westes/flex)
- [Bison](https://savannah.gnu.org/projects/bison/)
- [SDL](http://www.libsdl.org/)
- [OpenAL](https://www.openal.org/)
- [LibMAD](http://www.underbit.com/products/mad/)
- [cURL](https://curl.se/)
- [Libogg](https://github.com/gcp/libogg)
- [Libvorbis](https://xiph.org/vorbis/)
- [Libopus](https://opus-codec.org/)

## Resources

- 🔗 [GitHub Repository](https://github.com/openmoh/openmohaa/)
- 🌐 [MOH-DB](https://www.moh-db.com/)
- 🕹️ [333networks](https://333networks.com/)
- 📂 [ModDB](https://www.moddb.com/games/medal-of-honor-allied-assault)
- 📂 [GameBanana](https://gamebanana.com/games/720)
- 💬 [Join us on Discord](https://discord.gg/NYtH58R)
