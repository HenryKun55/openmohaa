#!/usr/bin/env python3
"""Builds the LiveArea "Settings" button images (misc/vita/sce_sys/livearea/contents).

The LiveArea only accepts 8-bit palette PNGs, so the result is quantized.
Usage: python3 misc/vita/make_livearea_buttons.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sce_sys", "livearea", "contents")
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
W, H = 200, 62


def button(label, path):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((1, 1, W - 2, H - 2), radius=10, fill=(40, 36, 20, 235), outline=(214, 180, 72, 255), width=2)
    size = 24
    while True:
        font = ImageFont.truetype(FONT, size)
        box = d.textbbox((0, 0), label, font=font)
        if box[2] - box[0] <= W - 24 or size <= 12:
            break
        size -= 1
    tw, th = box[2] - box[0], box[3] - box[1]
    d.text(((W - tw) / 2 - box[0], (H - th) / 2 - box[1]), label, font=font, fill=(240, 220, 150, 255))
    img.quantize(colors=255, method=Image.Quantize.FASTOCTREE).save(path, optimize=True)
    print(path)


button("SETTINGS", os.path.join(OUT, "settings_en.png"))
button("CONFIGURAÇÕES", os.path.join(OUT, "settings_pt.png"))
