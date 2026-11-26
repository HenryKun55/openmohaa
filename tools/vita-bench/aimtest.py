#!/usr/bin/env python3
"""Aim assist / touch aim check in Vita3K, no button presses.

Starts `training` (player free, no cutscene) with god + notarget, spawns a German soldier
in front of the player, then drives the game through vinput (input sent to the Vita3K
process only: right stick = I/J/K/L; front touch swipes through vita_touch_test) and
screenshots the
aim assist debug overlay (vita_aim_debug, a temporary cvar) after each step.

    VITA3K_BIN=... VITA3K_UX0=... python3 tools/vita-bench/aimtest.py --out <folder>

Only temporary cvars are set; vita_test.cfg is removed and the campaign choice restored.
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402

KEY = {"S": 1, "I": 34, "J": 38, "K": 40, "L": 37}
# Front touch swipes come from vita_touch_test (sdl_input.c): Vita3K only turns a mouse drag
# into touch while its window has the focus, and taking the focus would disturb whoever uses
# the Mac. "start x0 y0 x1 y1 ms" in raw panel units, start from the first gameplay frame.
DEFAULT_TOUCH = ("14000 1300 544 1460 544 600|"   # right half: turn right across the soldier
                 "17000 1460 544 1300 544 600|"   # and back
                 "20000 300 544 460 544 600")     # left half: touch aim ignores it
DEFAULT_STEPS = ("shot:spawned,key:S:900,wait:500,shot:backed-off,key:L:150,wait:300,shot:stick-right,"
                 "key:J:150,wait:300,shot:stick-left,wait:2500,shot:touch-right,wait:2500,shot:touch-back,"
                 "wait:2500,shot:touch-left-half")


def vinput_tool():
    exe = os.path.join(bench.HOME, "Library/Caches/vita-bench/vinput")
    src = os.path.join(bench.HERE, "vinput.swift")
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        subprocess.run(["swiftc", "-O", src, "-o", exe], check=True, capture_output=True)
    return exe


def summary():
    """The aim assist debug lines ([AIM], vita_aim_debug 2) of this run, summed up."""
    if not os.path.exists(bench.BOOT_LOG):
        return
    lines = [l for l in open(bench.BOOT_LOG, errors="replace") if l.startswith("[AIM]")]
    vals = [dict(re.findall(r"(\w+)=(-?[\d.]+)", l)) for l in lines]
    if not vals:
        print("no [AIM] lines")
        return
    best = lambda k: max(float(v.get(k, 0)) for v in vals)
    near = [v for v in vals if v.get("nearest", "-1") != "-1"]
    print(f"[AIM] enemies<={best('enemies'):.0f} eligible<={best('eligible'):.0f} cone<={best('cone'):.0f} "
          f"visible<={best('visible'):.0f} lines-with-nearest={len(near)} | totals: {lines[-1].split('frames=')[1].strip()}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--map", default="training")
    ap.add_argument("--levelcmd", default="god; notarget; spawn Actor model models/human/german_wehrmact_soldier.tik")
    ap.add_argument("--touch", default=DEFAULT_TOUCH, help="vita_touch_test swipes")
    ap.add_argument("--steps", default=DEFAULT_STEPS,
                    help="comma list: shot:<name> key:<S|I|J|K|L>:<ms> drag:x0:y0:x1:y1:ms (after focus) focus wait:<ms>")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    tool = vinput_tool()

    saved_choice = open(bench.GAME_CHOICE).read() if os.path.exists(bench.GAME_CHOICE) else None
    proc = None
    shots = []
    try:
        bench.kill_vita3k()
        if os.path.exists(bench.BOOT_LOG):
            os.remove(bench.BOOT_LOG)
        with open(bench.GAME_CHOICE, "w") as f:
            f.write("0\n")
        with open(bench.TEST_CFG, "w") as f:
            f.write("set thereisnomonkey 1\nset cheats 1\n")
            f.write('set vita_aim_debug "2"\nset vita_touch_debug "1"\n')  # 2: [AIM] lines in boot.log
            f.write(f'set vita_touch_test "{args.touch}"\n')
            f.write("set vita_autocontinue 1\n")
            f.write(f'set vita_levelcmd "{args.levelcmd}"\n')
            f.write(f"devmap {args.map}\n")
        proc = subprocess.Popen([bench.VITA3K, "-r", bench.TITLE], stdout=subprocess.DEVNULL,
                                stderr=subprocess.STDOUT, start_new_session=True)
        t_launch = time.time()
        while True:
            time.sleep(0.5)
            if proc.poll() is not None:
                print("crash before the level")
                return 1
            if os.path.exists(bench.BOOT_LOG) and "[VITA] in game" in open(bench.BOOT_LOG, errors="replace").read():
                break
            if time.time() - t_launch > bench.LOAD_TIMEOUT:
                print("did not reach the level")
                return 1
        print(f"in the level after {time.time() - t_launch:.0f} s", flush=True)
        os.remove(bench.TEST_CFG)
        time.sleep(8)  # level commands (god, notarget, spawn) and the soldier settle

        def shot(name):
            path = os.path.join(args.out, f"{len(shots):02d}-{name}.png")
            if bench.screenshot(proc.pid, path):
                shots.append(path)
                print("  screenshot", os.path.basename(path), flush=True)

        def run(*cmd):
            subprocess.run([tool, str(proc.pid), *map(str, cmd)], check=True, timeout=60)

        for step in args.steps.split(","):
            kind, *rest = step.split(":")
            if kind == "shot":
                shot(rest[0])
            elif kind == "key":
                run("key", KEY[rest[0]], rest[1])
            elif kind == "drag":
                run("drag", *rest)
            elif kind == "focus":
                run("focus")
            elif kind == "wait":
                run("wait", rest[0])
        if proc.poll() is not None:
            print("Vita3K exited during the test")
            return 1
        summary()
        print("ok")
        return 0
    finally:
        if proc and proc.poll() is None:
            os.killpg(proc.pid, 9)
        bench.kill_vita3k()
        for p in (bench.TEST_CFG, bench.EARLY_CFG):
            if os.path.exists(p):
                os.remove(p)
        if saved_choice is not None:
            with open(bench.GAME_CHOICE, "w") as f:
                f.write(saved_choice)
        if os.path.exists(bench.BOOT_LOG):
            import shutil
            shutil.copy(bench.BOOT_LOG, os.path.join(args.out, "boot.log"))


if __name__ == "__main__":
    sys.exit(main())
