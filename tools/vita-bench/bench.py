#!/usr/bin/env python3
"""
vita-bench: repeatable performance runs of the Vita build in Vita3K.

Each scenario (scenarios.json) picks the campaign, starts a level with the player
invulnerable (cheats + god), waits for the player to be in the level ("[VITA] in game"
in boot.log), and averages the game's perf log (FRAME-PROF, RT-PROF) over time windows
counted from there, with a screenshot of the game's window at the end of each window.
A run that stops before its last window is reported as a crash.

    python3 tools/vita-bench/bench.py                  # every scenario, Vita speed profile
    python3 tools/vita-bench/bench.py boat --set sv_vita_thread_test=2
    python3 tools/vita-bench/bench.py boat --profile host  # no CPU/GPU throttle

The "vita" profile runs the Vita3K fork (branch henry/cpu-throttle) with its CPU and GPU
speed model (VITA3K_CPU_MHZ, VITA3K_GPU_SCALE...), calibrated so the frame times match a
real PS Vita 1000 (see CALIBRATION in README.md). Results: a table on stdout, and
report.md, results.json and the screenshots in --out (default
../openmohaa-bench-results/<date-time>, outside the repository).

The Vita3K data folder is left as it was: vita_test.cfg is removed and the campaign
choice (vita_game.txt) restored, whatever happens. Only temporary cvars are set (cheats,
thereisnomonkey, vita_autocontinue, vita_levelcmd and the --set ones, which must not be
saved settings).

Needs the game installed in Vita3K (the vpk unzipped into ux0/app/OMHA00001), the
game data in ux0/data/openmohaa, and r_vita_perflog 1 in its config.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
HOME = os.path.expanduser("~")

VITA3K = os.environ.get(
    "VITA3K_BIN",
    os.path.join(HOME, "Documents/workspace/games/Vita3K/build/macos-ninja/bin/Release/Vita3K.app/Contents/MacOS/Vita3K"),
)
UX0 = os.environ.get("VITA3K_UX0", os.path.join(HOME, "Library/Application Support/Vita3K/Vita3K/fs/ux0"))
DATA = os.path.join(UX0, "data/openmohaa")
MAIN = os.path.join(DATA, "main")
BOOT_LOG = os.path.join(MAIN, "boot.log")
TEST_CFG = os.path.join(MAIN, "vita_test.cfg")
GAME_CHOICE = os.path.join(DATA, "vita_game.txt")
TITLE = "OMHA00001"

# Speed profiles: environment for the Vita3K fork. "vita" is calibrated against a PS Vita
# 1000 at the default clocks (README.md, CALIBRATION).
PROFILES = {
    "vita": {"VITA3K_CPU_MHZ": "333", "VITA3K_CORES": "3", "VITA3K_GPU_SCALE": "37"},
    "host": {},
}

LOAD_TIMEOUT = 420  # seconds from launch to the player in the level
KV = re.compile(r"([a-z0-9:]+)=(-?[0-9.]+)")


def winid_tool():
    """The small Swift tool that finds a process's window id (screenshots of the game
    window only, never the rest of the screen); built once into a cache folder."""
    cache = os.path.join(HOME, "Library/Caches/vita-bench")
    exe = os.path.join(cache, "winid")
    src = os.path.join(HERE, "winid.swift")
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        os.makedirs(cache, exist_ok=True)
        subprocess.run(["swiftc", "-O", src, "-o", exe], check=True, capture_output=True)
    return exe


def screenshot(pid, path):
    try:
        wid = subprocess.run([winid_tool(), str(pid)], capture_output=True, text=True, timeout=10).stdout.strip()
        if wid:
            subprocess.run(["screencapture", "-x", "-o", "-l" + wid, path], timeout=10)
            return os.path.exists(path)
    except (subprocess.SubprocessError, OSError):
        pass
    return False


def kill_vita3k():
    subprocess.run(["pkill", "-9", "-f", f"Vita3K.app.*-r {TITLE}"], capture_output=True)
    time.sleep(1)


def parse_line(line):
    """('frame' | 'rt' | 'rt2' | None, {key: value})"""
    if line.startswith("FRAME-PROF"):
        return "frame", {k: float(v) for k, v in KV.findall(line.split("|")[0])}
    if line.startswith("RT-PROF (ms/frame)"):
        head, _, rest = line.partition("|")
        vals = {k: float(v) for k, v in KV.findall(head)}
        m = re.search(r"(\d+) batches (\d+) verts", rest)
        if m:
            vals["batches"], vals["verts"] = float(m.group(1)), float(m.group(2))
        for k, v in KV.findall(rest.split("verts", 1)[-1]):
            vals[k] = float(v)
        return "rt", vals
    if line.startswith("RT-PROF2"):
        vals = {}
        m = re.search(r"skin surfaces gpu=(\d+) cpu=(\d+)", line)
        if m:
            vals["skin_gpu"], vals["skin_cpu"] = float(m.group(1)), float(m.group(2))
        m = re.search(r"static surfs=(\d+) draws=(\d+)", line)
        if m:
            vals["static_surfs"], vals["static_draws"] = float(m.group(1)), float(m.group(2))
        return "rt2", vals
    return None, None


def mean(rows, key):
    vals = [r[key] for r in rows if key in r]
    return round(statistics.mean(vals), 1) if vals else None


def summarize(samples):
    """Averages over the perf lines of one window (each line averages ~1 s of frames)."""
    frames = [v for kind, v in samples if kind == "frame" and v.get("frame", 0) > 0]
    rts = [v for kind, v in samples if kind == "rt" and v.get("3d", 0) > 0]
    rt2s = [v for kind, v in samples if kind == "rt2"]
    if not frames:
        return None
    frame_ms = mean(frames, "frame")
    out = {
        "fps": round(1000.0 / frame_ms, 1) if frame_ms else None,
        "frame_ms": frame_ms,
        "worst_ms": round(max(v["frame"] for v in frames), 1),
        "main_sv": mean(frames, "sv"),
        "main_cl": mean(frames, "cl"),
        "main_cg": mean(frames, "cg"),
        "rthread": mean(frames, "rthread"),
        "rt_3d": mean(rts, "3d"),
        "rt_swap": mean(rts, "swap"),
        "batches": mean(rts, "batches"),
        "samples": len(frames),
    }
    for key in ("tess:skel", "draw:skel", "draw:static", "draw:other"):
        out[key] = mean(rts, key)
    for key in ("skin_gpu", "skin_cpu", "static_draws"):
        out[key] = mean(rt2s, key)
    return out


def run_scenario(name, sc, profile, sets, outdir):
    print(f"== {name}: {sc['map']} (game {sc['game']}), profile {profile}", flush=True)
    result = {"scenario": name, "map": sc["map"], "game": sc["game"], "profile": profile, "sets": sets,
              "windows": {}, "status": "ok"}
    saved_choice = open(GAME_CHOICE).read() if os.path.exists(GAME_CHOICE) else None
    env = dict(os.environ, **PROFILES[profile])
    proc = None
    try:
        for attempt in range(4):
            kill_vita3k()
            if os.path.exists(BOOT_LOG):
                os.remove(BOOT_LOG)
            with open(GAME_CHOICE, "w") as f:
                f.write(f"{sc['game']}\n")
            with open(TEST_CFG, "w") as f:
                f.write("set thereisnomonkey 1\nset cheats 1\n")
                for k, v in sets.items():
                    f.write(f'set {k} "{v}"\n')
                # start the level without waiting for CONTINUE, then run the commands (cl_ui.cpp)
                f.write("set vita_autocontinue 1\n")
                f.write(f'set vita_levelcmd "{sc.get("commands", "")}"\n')
                f.write(f"devmap {sc['map']}\n")  # devmap: cheats on (god)
            proc = subprocess.Popen([VITA3K, "-r", TITLE], env=env, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL, start_new_session=True)
            t_launch = time.time()
            pos, t0, retry = 0, None, False
            samples = []  # (seconds since in game, kind, values)
            last_end = max(w["to"] for w in sc["windows"])
            shots = {w["name"]: False for w in sc["windows"]}
            while True:
                time.sleep(0.5)
                now = time.time()
                if os.path.exists(BOOT_LOG):
                    with open(BOOT_LOG, "r", errors="replace") as f:
                        f.seek(pos)
                        chunk = f.read()
                        pos = f.tell()
                    for line in chunk.splitlines():
                        if 'Unknown command "devmap"' in line:
                            retry = True
                        if t0 is None and line.startswith("[VITA] in game"):
                            t0 = now
                            print(f"   in the level after {now - t_launch:.0f} s", flush=True)
                        if t0 is not None:
                            kind, vals = parse_line(line)
                            if kind:
                                samples.append((now - t0, kind, vals))
                if os.path.exists(TEST_CFG) and t0 is not None:
                    os.remove(TEST_CFG)  # read at start-up only
                if retry:
                    break
                if proc.poll() is not None:
                    result["status"] = "crash" if t0 else "crash before the level"
                    break
                if t0 is None:
                    if now - t_launch > LOAD_TIMEOUT:
                        result["status"] = "did not reach the level"
                        break
                    continue
                elapsed = now - t0
                for w in sc["windows"]:
                    if not shots[w["name"]] and elapsed >= w["to"]:
                        shots[w["name"]] = True
                        path = os.path.join(outdir, f"{name}-{w['name']}.png")
                        if screenshot(proc.pid, path):
                            result.setdefault("screenshots", {})[w["name"]] = os.path.basename(path)
                if elapsed >= last_end + 1.5:
                    break
            if not retry:
                break
            print("   'map' ran before it existed, starting again", flush=True)
        for w in sc["windows"]:
            win = [(kind, v) for t, kind, v in samples if w["from"] <= t <= w["to"]]
            result["windows"][w["name"]] = summarize(win)
    finally:
        if proc and proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        kill_vita3k()
        if os.path.exists(TEST_CFG):
            os.remove(TEST_CFG)
        if saved_choice is None:
            if os.path.exists(GAME_CHOICE):
                os.remove(GAME_CHOICE)
        else:
            with open(GAME_CHOICE, "w") as f:
                f.write(saved_choice)
        if os.path.exists(BOOT_LOG):
            shutil.copy(BOOT_LOG, os.path.join(outdir, f"{name}-boot.log"))
    return result


COLUMNS = [("fps", "FPS"), ("frame_ms", "frame"), ("worst_ms", "worst"), ("main_sv", "sv"), ("main_cl", "cl"),
           ("rthread", "rthread"), ("rt_3d", "3d"), ("batches", "draws"), ("tess:skel", "skin"),
           ("draw:static", "static"), ("draw:other", "other")]


def report(results):
    lines = ["| scenario | window | status | " + " | ".join(c[1] for c in COLUMNS) + " |",
             "|---|---|---|" + "---|" * len(COLUMNS)]
    for r in results:
        for wname, s in r["windows"].items():
            cells = [str(s.get(k)) if s and s.get(k) is not None else "-" for k, _ in COLUMNS]
            lines.append(f"| {r['scenario']} | {wname} | {r['status']} | " + " | ".join(cells) + " |")
        if not r["windows"]:
            lines.append(f"| {r['scenario']} | - | {r['status']} |" + " - |" * len(COLUMNS))
    return "\n".join(lines)


def main():
    with open(os.path.join(HERE, "scenarios.json")) as f:
        scenarios = {k: v for k, v in json.load(f).items() if not k.startswith("_")}
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenarios", nargs="*", help="scenario names (default: all): " + ", ".join(scenarios))
    ap.add_argument("--profile", choices=PROFILES, default="vita")
    ap.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                    help="temporary cvar for the run (e.g. sv_vita_thread_test=2, r_vita_staticmerge=0)")
    ap.add_argument("--out", help="results folder")
    args = ap.parse_args()

    names = args.scenarios or list(scenarios)
    for n in names:
        if n not in scenarios:
            sys.exit(f"unknown scenario '{n}' (have: {', '.join(scenarios)})")
    sets = dict(s.split("=", 1) for s in args.set)
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    outdir = args.out or os.path.join(os.path.dirname(REPO), "openmohaa-bench-results", stamp)
    os.makedirs(outdir, exist_ok=True)

    results = []
    try:
        for n in names:
            results.append(run_scenario(n, scenarios[n], args.profile, sets, outdir))
    except KeyboardInterrupt:
        print("interrupted", flush=True)
    table = report(results)
    head = (f"# vita-bench {stamp}\n\nprofile {args.profile} {PROFILES[args.profile]}, set {sets}, "
            f"commit {subprocess.run(['git', '-C', REPO, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()}\n\n")
    with open(os.path.join(outdir, "report.md"), "w") as f:
        f.write(head + table + "\n")
    with open(os.path.join(outdir, "results.json"), "w") as f:
        json.dump(results, f, indent=1)
    print()
    print(table)
    print(f"\n{outdir}")


if __name__ == "__main__":
    main()
