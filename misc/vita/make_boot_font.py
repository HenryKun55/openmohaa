#!/usr/bin/env python3
"""Builds misc/vita/main/fonts/vita-boot.font, the font of the screens the game draws
before its own renderer starts (the update check over the boot picture, code/sys/
vita_bootui.c): Oswald (SIL Open Font License, misc/vita/fonts/OFL.txt), Latin-1, as
8-bit alpha glyphs.

Usage (needs Pillow):
    python3 misc/vita/make_boot_font.py

Format, little endian:
    "VBF1", u16 line height, u16 ascent
    224 glyphs for the Latin-1 codes 32..255:
        u16 width, u16 height, i16 x offset, i16 y offset (from the line top),
        u16 advance, u32 offset of its rows in the bitmap data
    u32 bitmap size, then the bitmap data (width * height alpha bytes per glyph)
"""
import os
import struct

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "fonts", "Oswald.ttf")
OUT = os.path.join(HERE, "main", "fonts", "vita-boot.font")
SIZE = 26


def main():
    font = ImageFont.truetype(FONT, SIZE)
    font.set_variation_by_name("Regular")
    ascent, descent = font.getmetrics()
    height = ascent + descent
    glyphs, data = [], bytearray()
    for code in range(32, 256):
        ch = bytes([code]).decode("latin-1")
        advance = int(round(font.getlength(ch)))
        box = font.getbbox(ch)
        if code < 0xA0 and code >= 0x7F or box[2] <= box[0] or box[3] <= box[1]:
            glyphs.append((0, 0, 0, 0, advance, len(data)))
            continue
        w, h = box[2] - box[0], box[3] - box[1]
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-box[0], -box[1]), ch, font=font, fill=255)
        glyphs.append((w, h, box[0], box[1], advance, len(data)))
        data += img.tobytes()
    with open(OUT, "wb") as f:
        f.write(b"VBF1" + struct.pack("<HH", height, ascent))
        for g in glyphs:
            f.write(struct.pack("<HHhhHI", *g))
        f.write(struct.pack("<I", len(data)))
        f.write(data)
    print("wrote", OUT, os.path.getsize(OUT), "bytes, line", height)


if __name__ == "__main__":
    main()
