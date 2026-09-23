#!/usr/bin/env python3
"""Symbolize a Vita3K guest profile (VITA3K_PROFILE=<file>, speed mode) against the Vita ELF.

Usage: misc/vita/vita3k_profile.py [-b baseline.txt] <profile.txt> [elf] [top]
  elf defaults to build-vita/Release/openmohaa (not stripped, linked at 0x81000000 = load base).
  -b subtracts an earlier copy of the (cumulative) profile, e.g. taken after the level loaded,
     so only gameplay is counted:  cp /tmp/omhaa-profile.txt /tmp/base.txt  ... play ...

Weights are guest instructions (plus billed HLE time) at Vita speed, so percentages
approximate share of CPU time on the device.
"""
import bisect
import os
import re
import subprocess
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
args = sys.argv[1:]
baseline = None
if args[:1] == ["-b"]:
    baseline, args = args[1], args[2:]
prof_path = args[0]
elf = args[1] if len(args) > 1 else os.path.join(REPO, "build-vita/Release/openmohaa")
top = int(args[2]) if len(args) > 2 else 30
nm = os.path.join(os.environ.get("VITASDK", os.path.expanduser("~/vitasdk")), "bin/arm-vita-eabi-nm")

VITA3K_LOG = os.path.expanduser("~/Library/Application Support/Vita3K/Vita3K/vita3k.log")
BUILD = os.path.dirname(elf)
if os.path.basename(BUILD) == "Release":
    BUILD = os.path.dirname(BUILD)
# Separate modules (game.suprx / cgame.suprx) load at their own base; their ELFs are linked
# at 0x81000000 like the eboot. Map each loaded range back to its ELF.
MODULE_ELFS = {"game.suprx": "game.elf", "cgame.suprx": "cgame.elf"}


def read_syms(path):
    out = []
    for line in subprocess.run([nm, "-n", "-C", path], capture_output=True, text=True).stdout.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[0] and parts[1] in ("t", "T", "w", "W"):
            out.append((int(parts[0], 16), parts[2]))
    return out


# (start, end, link_base, syms, addrs, tag)
modules = []
eboot_syms = read_syms(elf)
modules.append((0x81000000, 0x81000000 + 0x4000000, 0x81000000, eboot_syms, [a for a, _ in eboot_syms], ""))
if os.path.exists(VITA3K_LOG):
    seen = {}
    with open(VITA3K_LOG, "rb") as f:
        for raw in f:
            if b"Loaded module segment 0 @" not in raw:
                continue
            line = raw.decode("utf-8", "replace")
            for mod, elf_name in MODULE_ELFS.items():
                if line.rstrip().endswith(mod):
                    m = re.search(r"@ \[(0x[0-9A-Fa-f]+) - (0x[0-9A-Fa-f]+) / (0x[0-9A-Fa-f]+)\]", line)
                    if m:
                        start, end, link = (int(x, 16) for x in m.groups())
                        seen[mod] = (start, end, link, elf_name)
    for mod, (start, end, link, elf_name) in seen.items():
        path = os.path.join(BUILD, elf_name)
        if os.path.exists(path):
            ms = read_syms(path)
            modules.insert(0, (start, end, link, ms, [a for a, _ in ms], mod.split(".")[0] + ":"))


def sym(addr):
    addr &= ~1
    for start, end, link, ms, ma, tag in modules:
        if start <= addr < end:
            a = addr - start + link
            i = bisect.bisect_right(ma, a) - 1
            return tag + (ms[i][1] if i >= 0 else f"?{addr:x}")
    return f"?{addr:x}"


self_w = defaultdict(int)
caller_w = defaultdict(int)
thread_w = defaultdict(int)
total = 0
def load(path):
    rows = {}
    for line in open(path):
        t, pc, lr, w = line.split()
        rows[(t, int(pc, 16))] = (int(lr, 16), int(w))
    return rows


rows = load(prof_path)
if baseline:
    for key, (_, w0) in load(baseline).items():
        if key in rows:
            lr, w = rows[key]
            rows[key] = (lr, w - w0)

for (t, pc), (lr, w) in rows.items():
    if w <= 0:
        continue
    total += w
    thread_w[t] += w
    f = sym(pc)
    self_w[f] += w
    caller_w[(f, sym(lr))] += w

print(f"total weight {total:,} guest instructions")
print("\n== threads ==")
for t, w in sorted(thread_w.items(), key=lambda x: -x[1])[:8]:
    print(f"{100 * w / total:6.2f}%  thread {t}")
print(f"\n== top {top} functions (self) ==")
for f, w in sorted(self_w.items(), key=lambda x: -x[1])[:top]:
    print(f"{100 * w / total:6.2f}%  {f[:110]}")
print(f"\n== top {top} function <- caller (by LR) ==")
for (f, c), w in sorted(caller_w.items(), key=lambda x: -x[1])[:top]:
    print(f"{100 * w / total:6.2f}%  {f[:60]}  <-  {c[:60]}")
