#!/usr/bin/env python3
"""Makes a .vpk installable by the in-game updater of v0.3.

v0.3's updater.bin unpacks the .vpk reading 64 KB at a time and inflating into a 64 KB
buffer; when the output fills up exactly at the end of a read, zlib answers Z_BUF_ERROR
("needs more input") and that updater took it for a damaged package: the v0.4 update
failed on splash.png, which deflate barely compresses. Fixed in later updaters, but a
player on v0.3 updates with v0.3's, whatever version comes next.

This runs the same steps as that updater on every compressed entry of the .vpk, and
stores the ones it would refuse without compression (that updater copies stored entries
as they are). Run by cmake/platforms/vita.cmake right after vita-pack-vpk.

    python3 misc/vita/vpk_compat.py build-vita/OpenMoHAA.vpk
"""
import os
import struct
import sys
import zipfile
import zlib

CHUNK = 64 * 1024


def refused_by_v03(path):
    """Names of the compressed entries v0.3's updater would refuse."""
    refused = []
    with zipfile.ZipFile(path) as z, open(path, "rb") as f:
        for info in z.infolist():
            if info.compress_type != zipfile.ZIP_DEFLATED:
                continue
            f.seek(info.header_offset)
            name_len, extra_len = struct.unpack("<HH", f.read(30)[26:30])
            f.seek(info.header_offset + 30 + name_len + extra_len)
            left, written, failed = info.compress_size, 0, False
            inflater = zlib.decompressobj(-15)
            while left > 0 and not failed and not inflater.eof:
                data = f.read(min(CHUNK, left))
                left -= len(data)
                while True:  # updater.c: inflate while the 64 KB output fills up
                    out = inflater.decompress(data, CHUNK)
                    data = inflater.unconsumed_tail
                    written += len(out)
                    if inflater.eof or len(out) < CHUNK:
                        break
                    if not data and not inflater.copy().decompress(b"", 1):
                        failed = True  # zlib would answer Z_BUF_ERROR here
                        break
            if failed or written != info.file_size:
                refused.append(info.filename)
    return refused


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    refused = refused_by_v03(path)
    if not refused:
        return
    tmp = path + ".tmp"
    with zipfile.ZipFile(path) as src, zipfile.ZipFile(tmp, "w") as dst:
        for info in src.infolist():
            out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            out.external_attr = info.external_attr
            out.compress_type = zipfile.ZIP_STORED if info.filename in refused else info.compress_type
            dst.writestr(out, src.read(info.filename))
    os.replace(tmp, path)
    left = refused_by_v03(path)
    print(f"vpk_compat: stored without compression for v0.3's updater: {', '.join(refused)}")
    if left:
        sys.exit(f"vpk_compat: still refused: {', '.join(left)}")


if __name__ == "__main__":
    main()
