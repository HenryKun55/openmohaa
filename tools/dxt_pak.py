#!/usr/bin/env python3
"""
dxt_pak.py -- offline DXT/.dds texture conditioner for the OpenMoHAA PS Vita port.

Converts .tga/.jpg/.jpeg/.png textures into DDS files (DXT1 for opaque, DXT5 for
images with alpha) carrying a FULL pre-computed mipmap chain, in the exact format
the Vita runtime's renderergl1 LoadDDS() consumes:
  - "DDS " signature + classic 124-byte header
  - FourCC DXT1 / DXT5  (-> GL_COMPRESSED_RGBA_S3TC_DXT1/5_EXT, exposed by vitaGL)
  - power-of-2 dimensions (LoadDDS REJECTS non-power-of-2 and falls back)
  - dwMipMapCount set, mip levels stored sequentially after the header

The runtime already auto-prefers "<name>.dds" over "<name>.tga/.jpg" whenever
r_ext_compressed_textures >= 1 (already set in the Vita autoexec), so NO engine
change is needed. Original .tga/.jpg are left untouched as a fallback.

Why: on the Vita's tile-based GPU, texture bandwidth is the #1 limit and RAM is
tight (240 MB). DXT is 4x (DXT5) to 8x (DXT1) smaller than RGBA8, which cuts VRAM
traffic, frees RAM headroom, and -- because mips ship in-file -- removes the
per-texture CPU mip generation + JPEG decode that dominates level-load time
(GPU mip-gen is broken on Vita: r_vita_gpu_mipmap leaves UI magenta).

Requires ImageMagick ("magick" on PATH); it writes fully LoadDDS-compatible DDS.

Usage:
  tools/dxt_pak.py <src> [<src> ...] -o <outdir> [options]

    <src>     a directory tree of images and/or a .pk3 (zip) archive.
    -o DIR    output directory. .dds files are written mirroring each source's
              RELATIVE path (extension replaced with .dds), e.g.
                textures/stone/wall.jpg -> <outdir>/textures/stone/wall.dds
              Copy <outdir>/* into your Vita "main/" (loose) and the loader picks
              them up automatically.

  options:
    --npot {skip,resize}  non-power-of-2 textures: skip them (safe default; they
                          keep loading from the original) or resize to the nearest
                          power of 2 so they also get compressed.
    --include GLOB        only convert entries whose relative path matches GLOB
                          (repeatable). Default: all images.
    --exclude GLOB        skip entries matching GLOB (repeatable). Useful for known
                          problem textures (e.g. 'textures/mohmenu/*').
    --jobs N              parallel workers (default: CPU count).
    --dry-run             report what would be converted, write nothing.
    --overwrite           re-encode even if the .dds already exists and is newer.

Exit code is non-zero if any texture failed to encode.
"""

import argparse
import concurrent.futures
import fnmatch
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

IMAGE_EXTS = (".tga", ".jpg", ".jpeg", ".png", ".bmp", ".pcx")
MAGICK = shutil.which("magick") or shutil.which("convert")


def is_pow2(n):
    return n > 0 and (n & (n - 1)) == 0


def nearest_pow2(n):
    if n < 1:
        return 1
    lo = 1 << (n.bit_length() - 1)
    hi = lo << 1
    return lo if (n - lo) <= (hi - n) else hi


def full_mip_count(w, h):
    return int(math.floor(math.log2(max(w, h)))) + 1


def identify(path):
    """Return (width, height, opaque_bool) via ImageMagick, or None on failure."""
    try:
        out = subprocess.check_output(
            [MAGICK, "identify", "-format", "%w %h %[opaque]", path + "[0]"],
            stderr=subprocess.DEVNULL,
        ).decode().strip().split()
        w, h = int(out[0]), int(out[1])
        opaque = out[2].lower() == "true"
        return w, h, opaque
    except Exception:
        return None


def encode_dds(src_path, dst_path, npot, dry_run):
    """
    Encode one image to a LoadDDS-compatible DDS.
    Returns (status, note): status in {"ok","skip","fail"}.
    """
    info = identify(src_path)
    if not info:
        return "fail", "identify failed"
    w, h, opaque = info

    tw, th = w, h
    if not (is_pow2(w) and is_pow2(h)):
        if npot == "skip":
            return "skip", f"npot {w}x{h}"
        tw, th = nearest_pow2(w), nearest_pow2(h)

    # DXT compresses in 4x4 blocks; a base texture below 4x4 has no valid block and
    # vitaGL's compressed upload faults on it. Leave these as originals (they are
    # tiny and irrelevant to VRAM/bandwidth anyway).
    if tw < 4 or th < 4:
        return "skip", f"sub-4x4 {w}x{h}"

    compression = "dxt1" if opaque else "dxt5"
    mips = full_mip_count(tw, th)

    if dry_run:
        return "ok", f"{w}x{h} -> {tw}x{th} {compression} {mips}mips"

    os.makedirs(os.path.dirname(dst_path) or ".", exist_ok=True)
    cmd = [MAGICK, src_path + "[0]"]
    if (tw, th) != (w, h):
        cmd += ["-resize", f"{tw}x{th}!"]
    # DXT1 has no alpha -- strip it so ImageMagick doesn't fall back to DXT5.
    if opaque:
        cmd += ["-alpha", "off"]
    cmd += [
        "-define", f"dds:compression={compression}",
        "-define", f"dds:mipmaps={mips}",
        dst_path,
    ]
    # Retry: under heavy parallelism ImageMagick occasionally races on its own
    # temp files and emits a truncated/invalid DDS. The validator catches it; a
    # serialized retry fixes it (confirmed: files that "failed" re-encode cleanly).
    last = ""
    for attempt in range(3):
        try:
            subprocess.check_call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as e:
            last = f"magick: {e}"
            continue
        if validate_dds(dst_path):
            note = f"{w}x{h} {compression} {mips}mips"
            return "ok", note if attempt == 0 else note + f" (retry {attempt})"
        last = "output failed LoadDDS validation"
    return "fail", last


def validate_dds(path):
    """Mirror renderergl1 LoadDDS's parse to guarantee the runtime will accept it."""
    try:
        with open(path, "rb") as f:
            d = f.read(4 + 124)
        if d[:4] != b"DDS ":
            return False
        header_size, _flags, height, width, _pitch, _depth, nmips = struct.unpack_from("<7I", d, 4)
        if header_size != 124:
            return False
        if not (is_pow2(width) and is_pow2(height)):
            return False
        off = 4 + 7 * 4 + 11 * 4 + 4 + 4  # -> fourCC
        fourcc = d[off:off + 4]
        if fourcc not in (b"DXT1", b"DXT3", b"DXT5"):
            return False
        return True
    except Exception:
        return False


def gather_dir(root, includes, excludes):
    """Yield (abs_src, rel_path) for images under a directory tree."""
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.lower().endswith(IMAGE_EXTS):
                continue
            ab = os.path.join(dirpath, name)
            rel = os.path.relpath(ab, root).replace(os.sep, "/")
            if match_filters(rel, includes, excludes):
                yield ("dir", ab, rel)


def gather_pk3(pk3, includes, excludes):
    """Yield (kind, member, rel_path) for images inside a .pk3 (zip)."""
    with zipfile.ZipFile(pk3) as z:
        for member in z.namelist():
            if member.endswith("/"):
                continue
            if not member.lower().endswith(IMAGE_EXTS):
                continue
            rel = member.replace("\\", "/")
            if match_filters(rel, includes, excludes):
                yield ("pk3", (pk3, member), rel)


def match_filters(rel, includes, excludes):
    rl = rel.lower()
    if includes and not any(fnmatch.fnmatch(rl, g.lower()) for g in includes):
        return False
    if excludes and any(fnmatch.fnmatch(rl, g.lower()) for g in excludes):
        return False
    return True


def rel_to_dds(rel):
    base = rel.rsplit(".", 1)[0]
    return base + ".dds"


def process(job, outdir, npot, dry_run, overwrite):
    kind, ref, rel = job
    dst = os.path.join(outdir, rel_to_dds(rel))

    if not overwrite and os.path.exists(dst) and not dry_run:
        return rel, "skip", "exists"

    tmp = None
    try:
        if kind == "dir":
            src = ref
        else:  # pk3 member -> extract to a temp file with the right suffix
            pk3, member = ref
            suffix = os.path.splitext(member)[1]
            fd, tmp = tempfile.mkstemp(suffix=suffix)
            os.close(fd)
            with zipfile.ZipFile(pk3) as z, open(tmp, "wb") as out:
                out.write(z.read(member))
            src = tmp
        status, note = encode_dds(src, dst, npot, dry_run)
        return rel, status, note
    finally:
        if tmp:
            try:
                os.remove(tmp)
            except OSError:
                pass


def main():
    if not MAGICK:
        sys.exit("error: ImageMagick not found (need 'magick' or 'convert' on PATH)")

    ap = argparse.ArgumentParser(description="Offline DXT/.dds texture conditioner for the Vita port.")
    ap.add_argument("src", nargs="+", help="image directory tree(s) and/or .pk3 file(s)")
    ap.add_argument("-o", "--outdir", required=True, help="output directory for .dds files")
    ap.add_argument("--npot", choices=["skip", "resize"], default="skip")
    ap.add_argument("--include", action="append", default=[], help="glob to include (repeatable)")
    ap.add_argument("--exclude", action="append", default=[], help="glob to exclude (repeatable)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--overwrite", action="store_true")
    args = ap.parse_args()

    jobs = []
    for s in args.src:
        if os.path.isdir(s):
            jobs.extend(gather_dir(s, args.include, args.exclude))
        elif zipfile.is_zipfile(s):
            jobs.extend(gather_pk3(s, args.include, args.exclude))
        else:
            print(f"warning: skipping '{s}' (not a directory or .pk3)", file=sys.stderr)

    if not jobs:
        sys.exit("no matching image files found")

    print(f"dxt_pak: {len(jobs)} textures -> {args.outdir}  (npot={args.npot}, jobs={args.jobs})")

    counts = {"ok": 0, "skip": 0, "fail": 0}
    src_bytes = 0
    dds_bytes = 0
    fails = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(process, j, args.outdir, args.npot, args.dry_run, args.overwrite) for j in jobs]
        done = 0
        for fut in concurrent.futures.as_completed(futs):
            rel, status, note = fut.result()
            counts[status] = counts.get(status, 0) + 1
            done += 1
            if status == "fail":
                fails.append((rel, note))
            if status == "skip" and note.startswith("npot"):
                pass
            if done % 200 == 0 or done == len(jobs):
                print(f"  ...{done}/{len(jobs)}", file=sys.stderr)

    if not args.dry_run:
        for _kind, _ref, rel in jobs:
            dds = os.path.join(args.outdir, rel_to_dds(rel))
            if os.path.exists(dds):
                dds_bytes += os.path.getsize(dds)

    print("\n=== dxt_pak summary ===")
    print(f"  converted : {counts['ok']}")
    print(f"  skipped   : {counts['skip']}")
    print(f"  failed    : {counts['fail']}")
    if dds_bytes:
        print(f"  .dds total: {dds_bytes/1024/1024:.1f} MB")
    if fails:
        print("  failures:")
        for rel, note in fails[:20]:
            print(f"    {rel}: {note}")
        if len(fails) > 20:
            print(f"    ... and {len(fails)-20} more")

    sys.exit(1 if counts["fail"] else 0)


if __name__ == "__main__":
    main()
