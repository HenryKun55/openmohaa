#!/usr/bin/env python3
"""
snd_pak.py -- offline WAV conditioner for the OpenMoHAA PS Vita port.

Pre-resamples the game's .wav sounds down to the runtime mixer rate (default
22050 Hz, mono, 16-bit PCM) so the engine does NOT have to resample them at load
time. On the Vita this is a big win: ~half of MoHAA's ~1200 ubersound sounds are
44100 Hz, and S_LoadSound (snd_mem_new.cpp) resamples every one whose rate is
above s_khz via DownSampleWav_MILES -- ~20 s of CPU during level load. Feeding the
engine sounds already at 22050 Hz makes S_LoadSound's `realKhz < info.rate` check
false, so it skips the resample entirely (and the files are ~half the size, so the
FS_Read is cheaper too). No engine change; original paks stay as the fallback.

Only sounds ABOVE the target rate are converted -- ones already <= target load
fine untouched. Output is loose files mirroring the source paths; drop them into
the Vita "main/" (or zip into a high-sort pak, e.g. Pak8_snd.pk3) and the loader
prefers them.

Requires ffmpeg on PATH.

Usage:
  tools/snd_pak.py <src> [<src> ...] -o <outdir> [--rate 22050] [--jobs N] [--dry-run]
    <src>   a directory tree and/or a .pk3 (zip) archive containing .wav files.
"""

import argparse
import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

FFMPEG = shutil.which("ffmpeg")


def wav_rate(data):
    """Parse a WAV header's sample rate (0 if not a PCM WAV)."""
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return 0
    i = data.find(b"fmt ")
    if i < 0 or len(data) < i + 16:
        return 0
    return struct.unpack_from("<I", data, i + 12)[0]


def convert(src_bytes, dst_path, rate):
    """Resample src WAV bytes to <rate> Hz mono s16 PCM WAV at dst_path."""
    os.makedirs(os.path.dirname(dst_path) or ".", exist_ok=True)
    fd, tin = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        with open(tin, "wb") as f:
            f.write(src_bytes)
        # -map_metadata -1 + -flags +bitexact strip ffmpeg's LIST/INFO chunk so the
        # output is a canonical RIFF/WAVE with just fmt+data -- some embedded WAV
        # parsers (MoHAA's GetWavinfo) don't skip unknown chunks.
        cmd = [FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
               "-i", tin, "-ar", str(rate), "-ac", "1", "-sample_fmt", "s16",
               "-map_metadata", "-1", "-flags", "+bitexact", "-f", "wav", dst_path]
        subprocess.check_call(cmd)
    finally:
        try:
            os.remove(tin)
        except OSError:
            pass
    # sanity: output must be a PCM WAV at the requested rate
    with open(dst_path, "rb") as f:
        head = f.read(64)
    return wav_rate(head) == rate


def gather(srcs):
    """Yield (kind, ref, rel, data) for every .wav in the sources."""
    for s in srcs:
        if os.path.isdir(s):
            for dp, _d, files in os.walk(s):
                for n in files:
                    if n.lower().endswith(".wav"):
                        ab = os.path.join(dp, n)
                        rel = os.path.relpath(ab, s).replace(os.sep, "/")
                        yield ("dir", ab, rel)
        elif zipfile.is_zipfile(s):
            with zipfile.ZipFile(s) as z:
                for m in z.namelist():
                    if m.lower().endswith(".wav"):
                        yield ("pk3", (s, m), m.replace("\\", "/"))
        else:
            print(f"warning: skipping '{s}' (not a dir or .pk3)", file=sys.stderr)


def read_src(kind, ref):
    if kind == "dir":
        with open(ref, "rb") as f:
            return f.read()
    pk3, member = ref
    with zipfile.ZipFile(pk3) as z:
        return z.read(member)


def process(job, outdir, rate, dry):
    kind, ref, rel = job
    try:
        data = read_src(kind, ref)
    except Exception as e:
        return rel, "fail", f"read: {e}"
    r = wav_rate(data)
    if r == 0:
        return rel, "skip", "not a PCM wav"
    if r <= rate:
        return rel, "skip", f"{r}Hz <= target"
    if dry:
        return rel, "ok", f"{r}Hz -> {rate}Hz"
    dst = os.path.join(outdir, rel)
    try:
        if convert(data, dst, rate):
            return rel, "ok", f"{r}Hz -> {rate}Hz"
        return rel, "fail", "output rate mismatch"
    except Exception as e:
        return rel, "fail", f"ffmpeg: {e}"


def main():
    if not FFMPEG:
        sys.exit("error: ffmpeg not found on PATH")
    ap = argparse.ArgumentParser(description="Offline WAV downsampler for the Vita port.")
    ap.add_argument("src", nargs="+")
    ap.add_argument("-o", "--outdir", required=True)
    ap.add_argument("--rate", type=int, default=22050, help="target rate (match s_khz; default 22050)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    jobs = list(gather(args.src))
    if not jobs:
        sys.exit("no .wav files found")
    print(f"snd_pak: scanning {len(jobs)} wav -> {args.outdir} (target {args.rate}Hz, jobs={args.jobs})")

    counts = {"ok": 0, "skip": 0, "fail": 0}
    fails = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(process, j, args.outdir, args.rate, args.dry_run) for j in jobs]
        done = 0
        for f in concurrent.futures.as_completed(futs):
            rel, st, note = f.result()
            counts[st] = counts.get(st, 0) + 1
            if st == "fail":
                fails.append((rel, note))
            done += 1
            if done % 200 == 0 or done == len(jobs):
                print(f"  ...{done}/{len(jobs)}", file=sys.stderr)

    out_mb = 0.0
    if not args.dry_run:
        for _k, _r, rel in jobs:
            p = os.path.join(args.outdir, rel)
            if os.path.exists(p):
                out_mb += os.path.getsize(p)
        out_mb /= 1024 * 1024

    print("\n=== snd_pak summary ===")
    print(f"  resampled : {counts['ok']}")
    print(f"  left as-is: {counts['skip']} (already <= {args.rate}Hz)")
    print(f"  failed    : {counts['fail']}")
    if out_mb:
        print(f"  output    : {out_mb:.1f} MB")
    for rel, note in fails[:15]:
        print(f"    FAIL {rel}: {note}")
    sys.exit(1 if counts["fail"] else 0)


if __name__ == "__main__":
    main()
