# OpenMoHAA — Nintendo Switch HOME-menu forwarder

By default the Switch build is launched from **hbmenu** (Album → homebrew).
A *forwarder NSP* makes OpenMoHAA appear as a normal **icon on the HOME menu**
that, when opened, just launches `sdmc:/switch/OpenMoHAA.nro`.

> **The forwarder contains only the launcher — no game data, no engine assets.**
> It is a tiny title (a few hundred KB) whose only job is "run that NRO". The
> game data still lives on the SD card at `sdmc:/switch/openmohaa/main/` (see the
> main README for the data layout and `misc/console/prepare-data.sh`).

You need a Switch running custom firmware (Atmosphère) with **sigpatches**
installed — then a self-signed forwarder NSP installs and boots fine.

---

## Inputs (the same for either method)

| Field            | Value                                  |
|------------------|----------------------------------------|
| Target NRO path  | `sdmc:/switch/OpenMoHAA.nro`           |
| Title / app name | `OpenMoHAA`                            |
| Author           | `OpenMoHAA homebrew`                   |
| Icon (256×256)   | `misc/switch/icon.jpg` (reuse the NRO icon) |
| Title ID         | any **unused** 16-hex id, e.g. `0100BADC0DE00000` (must be unique on your console) |

---

## Method A — web generator (easiest, no local tools/keys)

1. Open the **Nintendo Homebrew "NSP Forwarder Generator"**
   (https://nh-server.github.io/nsp-forwarder/ — community tool).
2. Set:
   - **ROM/NRO path** → `/switch/OpenMoHAA.nro`
   - **Title** → `OpenMoHAA`, **Author** → `OpenMoHAA homebrew`
   - **Icon** → upload `misc/switch/icon.jpg`
3. Download the generated `.nsp`.
4. Copy it to the SD and install with **Goldleaf**, **DBI** or **Tinfoil**.
5. The OpenMoHAA icon appears on the HOME menu.

This signs the NSP for you, so you don't need to dump your console keys.

---

## Method B — local build with hacBrewPack (full control, scriptable)

Use this if you want a reproducible, offline build.

**You provide:**
- [`hacBrewPack`](https://github.com/The-4n/hacBrewPack) in your `PATH`.
- Your own console keys at `~/.switch/prod.keys` (dump from *your* console with
  Lockpick_RCM — required to pack the NSP).
- A small **forwarder exefs** (`main` + `main.npdm`): a homebrew stub that
  next-launches a target NRO. Drop it in `misc/switch/forwarder/exefs/`.
  Any of the public "nsp-forwarder" templates work; they read the target path
  from `romfs:/nextNroPath`.

Then run:

```sh
misc/switch/forwarder/make-forwarder.sh
# -> misc/switch/forwarder/out/OpenMoHAA-forwarder.nsp
```

Override defaults via env vars, e.g.:

```sh
TITLEID=0100BADC0DE00000 NAME=OpenMoHAA make-forwarder.sh
```

Install the resulting `.nsp` with Goldleaf / DBI / Tinfoil.

> `make-forwarder.sh` is a scaffold: the exact hacBrewPack flags and the
> forwarder stub's path convention (`nextNroPath` vs `nextArgv`) depend on the
> template you drop into `exefs/`. Adjust to match your template — the script
> documents each step inline.

---

## Notes

- A forwarder is **not** required to play — it is pure convenience (HOME icon
  vs launching through hbmenu). Everything works from hbmenu without it.
- Keep your Title ID unique; reusing an ID already installed on the console
  causes an install conflict.
- The forwarder never needs rebuilding when you update the game: it just points
  at `sdmc:/switch/OpenMoHAA.nro`, so replacing that NRO updates the game.
