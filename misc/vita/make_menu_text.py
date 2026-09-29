#!/usr/bin/env python3
"""Builds translated copies of the menu pictures that have words drawn on them.

Some menus are pictures (the difficulty board, for example), so the Text Language
table cannot reach their words. This reads each picture from YOUR OWN game paks,
paints the English words over with the wood/paper around them, writes the
translation from misc/vita/lang/<code>.txt in a similar font, and packs the result
as lang/<code>/<picture> in one .pk3. The game uses those copies when that Text
Language is chosen in the Vita settings. No game art is stored in this repository.

The translation is set with the letters of the picture itself (cut out of the English
words, with their wear and shading), so it looks like the original lettering. A letter
the picture lacks is built when possible (O from C, accents drawn at the letters' stem
width); otherwise that word falls back to a similar font.

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


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def letter_mask(pix, box):
    x0, y0, x1, y1 = box
    area = luma(pix)[y0:y1, x0:x1]
    return area > np.median(area) + 55  # the light letters on the dark wood


def split_letters(mask, count):
    """Column runs of ink, one per letter; touching letters are split where the ink is thinnest."""
    cols = mask.any(axis=0)
    runs, x = [], 0
    while x < len(cols):
        if cols[x]:
            start = x
            while x < len(cols) and cols[x]:
                x += 1
            runs.append((start, x))
        else:
            x += 1
    while 0 < len(runs) < count:
        i = max(range(len(runs)), key=lambda k: runs[k][1] - runs[k][0])
        start, end = runs[i]
        if end - start < 6:
            break
        cut = start + 2 + int(np.argmin(mask[:, start:end].sum(axis=0)[2:-2]))
        runs[i:i + 1] = [(start, cut), (cut, end)]
    return runs


# --- letters taken from the picture itself ---------------------------------------------
#
# Each English letter is cut out of the original picture and split into two layers by
# comparing it with the wood painted in under it: the light letter (alpha + colour) and
# its dark halo. Words are then set with those letters, at the original spacing, so they
# keep the picture's exact lettering, wear and shading.

def harvest(orig, wood, areas, masks):
    glyphs, gaps = {}, {}
    for box, english in areas:
        x0, y0, x1, y1 = box
        mask = masks[english]
        letters = english.replace(" ", "")
        runs = split_letters(mask, len(letters))
        if len(runs) != len(letters):
            continue
        ys = np.nonzero(mask.any(axis=1))[0]
        top, bot = ys.min(), ys.max()
        between = [runs[k + 1][0] - runs[k][1] for k in range(len(runs) - 1)]
        small = [g for g in between if g < (bot - top) * 0.5]
        gaps[english] = float(np.median(small)) if small else 2.0
        for (start, end), ch in zip(runs, letters):
            gx0, gx1, gy0, gy1 = x0 + start - 2, x0 + end + 2, y0 + top - 2, y0 + bot + 3
            o, w = orig[gy0:gy1, gx0:gx1], wood[gy0:gy1, gx0:gx1]
            lo, lw = luma(o), luma(w)
            light = lo > lw + 30
            top_l = np.percentile(lo[light], 95) if light.any() else lo.max()
            alpha = np.clip((lo - lw) / np.maximum(top_l - lw, 1), 0, 1) ** 0.8
            alpha[:, :2] = 0
            alpha[:, -2:] = 0
            halo = np.clip((lw - lo) / np.maximum(lw, 1), 0, 1)
            halo[:, 0] = 0
            halo[:, -1] = 0
            colour = np.clip((o[..., :3] - (1 - alpha[..., None]) * w[..., :3]) / np.maximum(alpha[..., None], 1e-3), 0, 255)
            glyphs.setdefault(ch, []).append({"a": alpha, "halo": halo, "col": colour, "cap": bot - top + 1})
    return glyphs, gaps


def glyph_for(glyphs, ch, cap):
    choices = glyphs.get(ch)
    if not choices and ch == "O" and "C" in glyphs:
        # No O in the picture: the left half of a C, mirrored, closes the ring.
        c = glyphs["C"][0]
        width = c["a"].shape[1]
        half = width // 2 + 1
        ring = lambda arr: np.concatenate([arr[:, :half], arr[:, :width - half][:, ::-1]], axis=1)
        choices = [{"a": ring(c["a"]), "halo": ring(c["halo"]), "col": ring(c["col"]), "cap": c["cap"]}]
    if not choices:
        return None
    best = min(choices, key=lambda g: abs(g["cap"] - cap))
    if best["cap"] == cap:
        return best
    f = cap / best["cap"]
    size = (max(1, round(best["a"].shape[1] * f)), max(1, round(best["a"].shape[0] * f)))
    scale = lambda arr: np.array(Image.fromarray(arr.astype(np.float32)).resize(size, Image.BILINEAR))
    return {"a": scale(best["a"]), "halo": scale(best["halo"]),
            "col": np.stack([scale(best["col"][..., k]) for k in range(3)], -1), "cap": cap}


ACCENTS = {"Á": "A/", "É": "E/", "Í": "I/", "Ó": "O/", "Ú": "U/", "À": "A\\", "Â": "A^", "Ê": "E^",
           "Ô": "O^", "Ã": "A~", "Õ": "O~", "Ç": "C,", "Ü": "U:"}


def draw_accent(out, kind, cx, top, cap, stem, colour, glyph_bottom):
    """An accent as thick as the letters' stems, antialiased, with the same dark halo."""
    ss = 4
    h = max(4, round(cap * 0.28))
    w = h * 0.8 if kind in "/\\" else h * 1.4
    cw, ch = int((w + stem + 6) * ss), int((h + 6) * ss)
    shape = Image.new("L", (cw, ch), 0)
    d = ImageDraw.Draw(shape)
    t, x0, y0 = stem * ss, 3 * ss, 3 * ss
    x1, y1 = x0 + w * ss, y0 + h * ss
    if kind == "/":
        d.polygon([(x0, y1), (x0 + t, y1), (x1 + t, y0), (x1, y0)], fill=255)
    elif kind == "\\":
        d.polygon([(x0, y0), (x0 + t, y0), (x1 + t, y1), (x1, y1)], fill=255)
    elif kind == "^":
        mid = (x0 + x1 + t) / 2
        d.line([(x0 + t / 2, y1), (mid, y0 + t / 2), (x1 + t / 2, y1)], fill=255, width=int(t))
    elif kind == "~":
        pts = [(x0 + (x1 + t - x0) * k / 20, (y0 + y1) / 2 - np.sin(k / 20 * 2 * np.pi) * (y1 - y0) / 3) for k in range(21)]
        d.line(pts, fill=255, width=int(t))
    elif kind == ":":
        r = t / 2 + ss
        for px in (x0 + r, x1 + t - r):
            d.ellipse([px - r, y1 - 2 * r, px + r, y1], fill=255)
    elif kind == ",":
        d.line([(x0 + t, y0), (x0 + t, (y0 + y1) / 2), (x1, (y0 + y1) / 2 + t), (x0, y1)], fill=255, width=int(t))
    mask = np.array(shape.resize((cw // ss, ch // ss), Image.LANCZOS)).astype(np.float32) / 255
    halo = np.array(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3))
                    .filter(ImageFilter.GaussianBlur(0.7))).astype(np.float32) / 255
    ax = int(round(cx - mask.shape[1] / 2))
    ay = glyph_bottom - 2 if kind == "," else top - mask.shape[0] + 3
    ay, ax = max(ay, 0), max(ax, 0)
    region = out[ay:ay + mask.shape[0], ax:ax + mask.shape[1], :3]
    m, hl = mask[:region.shape[0], :region.shape[1], None], halo[:region.shape[0], :region.shape[1], None]
    region *= (1 - 0.55 * hl)
    region[:] = region * (1 - m) + colour * m


def set_with_glyphs(out, glyphs, gaps, english, words, box, mask):
    """Set the translation with the picture's own letters. False if a letter is missing."""
    x0, y0, x1, y1 = box
    ys, xs = np.nonzero(mask.any(axis=1))[0], np.nonzero(mask.any(axis=0))[0]
    cap, top = ys.max() - ys.min() + 1, y0 + ys.min()
    parts = []
    for ch in words:
        if ch == " ":
            parts.append(None)
            continue
        base, accent = (ACCENTS[ch][0], ACCENTS[ch][1]) if ch in ACCENTS else (ch, None)
        g = glyph_for(glyphs, base, cap)
        if g is None:
            return False
        parts.append((g, accent))
    gap = round(gaps.get(english, np.median(list(gaps.values())) if gaps else 2.0))
    space = round(cap * 0.35)
    widths = [space if p is None else p[0]["a"].shape[1] - 4 for p in parts]
    total = sum(widths) + gap * (len(parts) - 1)
    if total > (x1 - x0) - 6:
        return False
    x = int(round(x0 + (xs.min() + xs.max()) / 2 - total / 2)) - 2
    for part, width in zip(parts, widths):
        if part is not None:
            g, accent = part
            gh, gw = g["a"].shape
            region = out[top - 2:top - 2 + gh, x:x + gw, :3]
            region *= (1 - g["halo"][..., None])
            region[:] = region * (1 - g["a"][..., None]) + g["col"] * g["a"][..., None]
            if accent:
                solid = g["a"] > 0.8
                colour = np.median(g["col"][solid], axis=0) if solid.any() else np.array([230.0, 220.0, 200.0])
                rows = (g["a"] > 0.5).sum(axis=1)
                stem = max(2.0, float(np.median(rows[rows > 0])) * 0.5) if (rows > 0).any() else 2.5
                draw_accent(out, accent, x + gw / 2 + 1, top - 2, cap, stem, colour, top - 2 + gh)
        x += width + gap
    return True


# --- fallback: a similar font --------------------------------------------------------------

def set_with_font(out, words, box, mask, orig):
    x0, y0, x1, y1 = box
    ys, xs = np.nonzero(mask)
    cap_h = ys.max() - ys.min() + 1
    baseline = y0 + ys.max()
    centre = x0 + (xs.min() + xs.max()) / 2
    colour = tuple(int(c) for c in np.percentile(orig[y0:y1, x0:x1][mask][:, :3], 90, axis=0))
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
    layer = Image.new("RGBA", (tb[2] - tb[0] + 8, cap[3] + 16), (0, 0, 0, 0))
    shadow = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((4 - tb[0], 4), words, font=font, fill=(20, 10, 5, 200))
    ImageDraw.Draw(layer).text((4 - tb[0], 4), words, font=font, fill=colour + (255,))
    layer = Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(1.2)), layer)
    room = (x1 - x0) - 12
    if layer.width - 8 > room:  # too long: narrow it to fit the board
        layer = layer.resize((room + 8, layer.height), Image.LANCZOS)
    base = Image.fromarray(out.astype(np.uint8))
    base.alpha_composite(layer, (max(int(round(centre - layer.width / 2)), 0), max(int(round(baseline - (4 + cap[3]) + 1)), 0)))
    out[:] = np.array(base).astype(np.float32)


def translate_picture(img, areas, table):
    orig = np.array(img.convert("RGBA")).astype(np.float32)
    wood = orig.copy()
    masks = {}
    for box, english in areas:
        masks[english] = letter_mask(orig, box)
        grown = Image.fromarray((masks[english] * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))
        erase(wood, box, np.array(grown) > 0)
    glyphs, gaps = harvest(orig, wood, areas, masks)

    out = orig.copy()
    done = 0
    for box, english in areas:
        words = table.get(english)
        if not words or words == english or not masks[english].any():
            continue
        x0, y0, x1, y1 = box
        out[y0:y1, x0:x1] = wood[y0:y1, x0:x1]
        if not set_with_glyphs(out, glyphs, gaps, english, words, box, masks[english]):
            set_with_font(out, words, box, masks[english], orig)
            print(f"    '{words}': letters missing from the picture, used the font")
        done += 1
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)), done


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
