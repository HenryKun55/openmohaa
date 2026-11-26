# vita-bench

Repeatable performance runs of the Vita build in Vita3K (the fork with the PS Vita speed
model, branch `henry/cpu-throttle`): each scenario in `scenarios.json` picks the
campaign, starts the level with the player invulnerable (`devmap`, `god`), skips
CONTINUE (`vita_autocontinue`), and averages the game's perf log over time windows
counted from the moment the player is in the level (`[VITA] in game` in boot.log), with
a screenshot of the game window at the end of each window.

    python3 tools/vita-bench/bench.py                      # every scenario
    python3 tools/vita-bench/bench.py boat --set sv_vita_thread_test=2
    python3 tools/vita-bench/bench.py boat --profile host   # no CPU/GPU throttle

Needs: the vpk unzipped into Vita3K's `ux0/app/OMHA00001`, the game data in
`ux0/data/openmohaa` (`main`, and `mainta` / `maintt` for the expansion scenarios), and
`r_vita_perflog 1` in its config. Paths: `VITA3K_BIN`, `VITA3K_UX0`.

`VITA3K_STDOUT=<file>` keeps the emulator's log, which has the module load addresses
("Loaded module segment 0 @") needed to symbolize a `VITA3K_PROFILE` guest profile of
game.suprx / cgame.suprx.

The Vita3K data folder is left as it was: `vita_test.cfg` is removed and the campaign
choice restored. Only temporary cvars are set.

## Calibration

The `vita` profile (`VITA3K_CPU_MHZ`, `VITA3K_GPU_SCALE`) is meant to give the frame
times of a PS Vita 1000 at its default clocks. Reference (device, m3l1 landing craft,
commit 1fa8f558): start of the level ~38 ms a frame (sv 12, cl 25, render thread 26),
near the beach ~75 ms (sv 24, cl 46, render thread 40).

## Aim assist and touch aim

    python3 tools/vita-bench/aimtest.py --map m4l3 --out <folder>

Starts a level with god + notarget and drives it with no button presses: the right
stick through Vita3K's keyboard mapping (`vinput.swift` sends keys to the Vita3K process
only, the Mac's own keyboard and mouse are left alone) and front touch swipes through
`vita_touch_test` (a temporary cvar replacing the panel's reports: Vita3K turns a mouse
drag into touch only while its window has the focus). `vita_aim_debug 2` shows the aim
assist overlay and writes an `[AIM]` line a second into boot.log; the script sums them
up (`selected` / `applied` = frames where the assist found a visible enemy and moved the
aim). Levels with enemies in reach of the start: m4l3, m2l2b, m6l3c.
