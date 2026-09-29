#!/usr/bin/env python3
"""Builds translated copies of the menu pictures that have words drawn on them.

Some menus are pictures (the difficulty board, for example), so the Text Language
table cannot reach their words. This reads each picture from YOUR OWN game paks,
paints the English words over with the wood/paper around them, writes the
translation from misc/vita/lang/<code>.txt in a similar font, and packs the result
as lang/<code>/<picture> in one .pk3. The game uses those copies when that Text
Language is chosen in the Vita settings. No game art is stored in this repository.

Usage (needs Pillow and numpy):
    python3 misc/vita/make_menu_text.py /path/to/main -o Pak9_lang.pk3

then copy Pak9_lang.pk3 to ux0:data/openmohaa/main/ on the Vita.

The font is Oswald (SIL Open Font License, misc/vita/fonts/OFL.txt).
"""
import argparse
import glob
import io
import os
import re
import sys
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "fonts", "Oswald.ttf")

# Picture -> text areas: (x0, y0, x1, y1) in the picture, and the English words there.
# The translation is looked up in misc/vita/lang/<code>.txt with those words as key.
PICTURES = {
    "textures/mohmenu/difficulty.tga": [
        ((20, 14, 236, 58), "SELECT DIFFICULTY"),
        ((20, 72, 236, 112), "EASY"),
        ((20, 118, 236, 160), "MEDIUM"),
        ((20, 166, 236, 208), "HARD"),
        ((82, 220, 176, 248), "CANCEL"),
    ],
}


def load_translations(path):
    text = open(path, encoding="utf-8").read()
    return dict(re.findall(r'^\{ "([^"]*)" "([^"]*)" \}', text, re.M))


def find_in_paks(main_dir, member):
    """The last pak that has the file wins, like the game."""
    data = None
    for pak in sorted(glob.glob(os.path.join(main_dir, "*.pk3")), key=str.lower):
        with zipfile.ZipFile(pak) as z:
            for name in z.namelist():
                if name.lower() == member.lower():
                    data = z.read(name)
    return data


def erase(pix, box, mask):
    """Paint the masked pixels with the colours to their left and right on the same
    row: the wood grain runs sideways, so this keeps it."""
    x0, y0, x1, y1 = box
    sub = pix[y0:y1, x0:x1]
    for y in range(sub.shape[0]):
        row = mask[y]
        x, n = 0, row.shape[0]
        while x < n:
            if not row[x]:
                x += 1
                continue
            start = x
            while x < n and row[x]:
                x += 1
            left = sub[y, start - 1] if start > 0 else (sub[y, x] if x < n else sub[y, start])
            right = sub[y, x] if x < n else left
            for k in range(start, x):
                t = (k - start + 1) / (x - start + 1)
                sub[y, k] = left * (1 - t) + right * t


def translate_picture(img, areas, table):
    pix = np.array(img.convert("RGBA")).astype(np.float32)
    h, w = pix.shape[:2]
    luma = pix[..., 0] * 0.299 + pix[..., 1] * 0.587 + pix[..., 2] * 0.114
    done = 0
    for box, english in areas:
        words = table.get(english)
        if not words or words == english:
            continue
        x0, y0, x1, y1 = box
        area = luma[y0:y1, x0:x1]
        mask = area > np.median(area) + 55  # the light letters on the dark wood
        if not mask.any():
            continue
        ys, xs = np.nonzero(mask)
        cap_h = ys.max() - ys.min() + 1
        baseline = y0 + ys.max()
        centre = x0 + (xs.min() + xs.max()) / 2
        letters = pix[y0:y1, x0:x1][mask][:, :3]
        colour = tuple(int(c) for c in np.percentile(letters, 90, axis=0))

        grown = Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))
        erase(pix, box, np.array(grown) > 0)

        # Same cap height, baseline and centre as the English words.
        probe = ImageDraw.Draw(Image.new("L", (1, 1)))
        size = int(cap_h)
        while True:
            font = ImageFont.truetype(FONT, size)
            font.set_variation_by_name("Regular")
            cap = probe.textbbox((0, 0), "H", font=font)
            if cap[3] - cap[1] >= cap_h or size > 200:
                break
            size += 1
        tb = probe.textbbox((0, 0), words, font=font)
        tw, th = tb[2] - tb[0], cap[3]
        layer = Image.new("RGBA", (tw + 8, th + 16), (0, 0, 0, 0))
        shadow = Image.new("RGBA", layer.size, (0, 0, 0, 0))
        ImageDraw.Draw(shadow).text((4 - tb[0], 4), words, font=font, fill=(20, 10, 5, 200))
        ImageDraw.Draw(layer).text((4 - tb[0], 4), words, font=font, fill=colour + (255,))
        layer = Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(1.2)), layer)
        room = (x1 - x0) - 12
        if layer.width - 8 > room:  # too long: narrow it to fit the board
            layer = layer.resize((room + 8, layer.height), Image.LANCZOS)
        px = int(round(centre - layer.width / 2))
        py = int(round(baseline - (4 + cap[3]) + 1))
        base = Image.fromarray(pix.astype(np.uint8))
        base.alpha_composite(layer, (max(px, 0), max(py, 0)))
        pix = np.array(base).astype(np.float32)
        done += 1
    return Image.fromarray(pix.astype(np.uint8)), done


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("main_dir", help="the game's main folder (with the Pak*.pk3 files)")
    parser.add_argument("-o", "--output", default="Pak9_lang.pk3")
    parser.add_argument("--lang", action="append", help="language code (default: every misc/vita/lang/*.txt)")
    args = parser.parse_args()

    codes = args.lang or sorted(os.path.splitext(os.path.basename(p))[0]
                                for p in glob.glob(os.path.join(HERE, "lang", "*.txt")))
    written = 0
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as out:
        for picture, areas in PICTURES.items():
            data = find_in_paks(args.main_dir, picture)
            if data is None:
                print(f"  not found in your paks: {picture}")
                continue
            original = Image.open(io.BytesIO(data))
            for code in codes:
                table = load_translations(os.path.join(HERE, "lang", code + ".txt"))
                image, done = translate_picture(original, areas, table)
                if not done:
                    continue
                buf = io.BytesIO()
                image.save(buf, format="TGA")
                out.writestr(f"lang/{code}/{picture}", buf.getvalue())
                written += 1
                print(f"  {code}: {picture} ({done} texts)")
    print(f"wrote {args.output} ({written} pictures)")
    if not written:
        os.remove(args.output)
        sys.exit(1)


if __name__ == "__main__":
    main()
