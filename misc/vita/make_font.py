#!/usr/bin/env python3
"""Builds the "vita-14" game font used by the Vita settings menu.

The game's own fonts only have ASCII, so accented text (the Portuguese menu) drew
'?' instead of letters. This renders printable ASCII plus Latin-1 (160-255) from
DejaVu Sans into the game's RitualFont format:

    misc/vita/main/fonts/vita-14.RitualFont   glyph table
    misc/vita/main/gfx/fonts/vita-14.tga      glyph page, white with alpha

DejaVu Sans is free to use and modify (Bitstream Vera license, see
misc/vita/main/fonts/DejaVu-LICENSE.txt). Get DejaVuSans.ttf from
https://dejavu-fonts.github.io/ and run:

    python3 misc/vita/make_font.py /path/to/DejaVuSans.ttf
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "vita-14"
HEIGHT = 14      # line height in pixels, as verdana-14
POINT = 12       # DejaVu size that fits the 14 px line
PAGE_W = 256
CHARS = list(range(32, 127)) + list(range(160, 256))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    font = ImageFont.truetype(sys.argv[1], POINT)
    ascent, descent = font.getmetrics()
    top = (HEIGHT - (ascent + descent)) // 2

    # Lay the glyphs out in rows of HEIGHT + 1 pixels.
    cells = []
    x = y = 0
    for c in CHARS:
        w = max(1, math.ceil(font.getlength(chr(c))))
        if x + w > PAGE_W:
            x, y = 0, y + HEIGHT + 1
        cells.append((c, x, y, w))
        x += w + 1
    page_h = 1 << math.ceil(math.log2(y + HEIGHT))
    aspect = PAGE_W / page_h

    alpha = Image.new("L", (PAGE_W, page_h), 0)
    draw = ImageDraw.Draw(alpha)
    for c, cx, cy, _ in cells:
        draw.text((cx, cy + top), chr(c), font=font, fill=255)
    page = Image.merge("RGBA", (Image.new("L", alpha.size, 255),) * 3 + (alpha,))
    tga = os.path.join(HERE, "main", "gfx", "fonts", NAME + ".tga")
    os.makedirs(os.path.dirname(tga), exist_ok=True)
    page.save(tga)

    indirection = [-1] * 256
    locations = [(0.0, 0.0, 0.0, 0.0)] * 256
    for i, (c, cx, cy, w) in enumerate(cells):
        indirection[c] = i
        locations[i] = (cx, cy, w, HEIGHT)

    lines = ["RitFont", "height %f" % HEIGHT, "aspect %f" % aspect, "indirections {"]
    for i in range(0, 256, 16):
        lines.append(" ".join(str(v) for v in indirection[i:i + 16]))
    lines.append("}")
    lines.append("locations {")
    for loc in locations:
        lines.append("{ %f %f %f %f }" % loc)
    lines.append("}")
    ritual = os.path.join(HERE, "main", "fonts", NAME + ".RitualFont")
    os.makedirs(os.path.dirname(ritual), exist_ok=True)
    with open(ritual, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", ritual)
    print("wrote", tga, page.size)


if __name__ == "__main__":
    main()
