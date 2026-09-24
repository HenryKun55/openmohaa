#!/usr/bin/env python3
"""
Build the PS Vita LiveArea bubble art and the boot/loading screen from the
original Medal of Honor: Allied Assault game data (your own copy).

    python3 misc/vita/make_art.py /path/to/mohaa/main

Reads the menu art straight out of the game's Pak*.pk3 (textures/mohmenu/...)
and writes, under misc/vita/art_local/ (git-ignored: the art is EA's, so it is
never committed to the repository):

    sce_sys/icon0.png                        128x128  bubble icon
    sce_sys/livearea/contents/bg.png         840x500  LiveArea background
    sce_sys/livearea/contents/startup.png    280x158  launch gate image
    splash.png                               960x544  boot / loading screen

cmake/platforms/vita.cmake packs these into the VPK when they exist, otherwise
the generic OpenMoHAA art in misc/vita/sce_sys is used.

Requires Pillow (pip install pillow).
"""

import io
import os
import sys
import zipfile

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "art_local")

TITLE = "textures/mohmenu/mohaa_title.tga"  # official logo plate on black
PHOTO = "textures/mohmenu/briefing/higginsboat.tga"  # Omaha landing craft


def load_from_paks(main_dir, member):
    paks = sorted(f for f in os.listdir(main_dir) if f.lower().endswith(".pk3"))
    # Later paks override earlier ones, like the engine's search order.
    for pak in reversed(paks):
        with zipfile.ZipFile(os.path.join(main_dir, pak)) as z:
            for name in z.namelist():
                if name.lower() == member.lower():
                    return Image.open(io.BytesIO(z.read(name))).convert("RGB")
    sys.exit("error: %s not found in the pk3s under %s" % (member, main_dir))


def extract_logo(title):
    """Cut the logo plate out of its black background, with a soft alpha edge.

    Everything reachable from the image border through near-black pixels is
    background; the rest (including dark pixels inside the plate) is logo. The
    copyright line under the plate is dropped by keeping only the largest blob.
    """
    w, h = title.size
    gray = title.convert("L")
    dark = gray.point(lambda v: 255 if v < 18 else 0)

    # Flood the background from the borders.
    bg = Image.new("L", (w, h), 0)
    px, dpx = bg.load(), dark.load()
    stack = [(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)]
    stack += [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    while stack:
        x, y = stack.pop()
        if px[x, y] or not dpx[x, y]:
            continue
        px[x, y] = 255
        if x > 0: stack.append((x - 1, y))
        if x < w - 1: stack.append((x + 1, y))
        if y > 0: stack.append((x, y - 1))
        if y < h - 1: stack.append((x, y + 1))
    mask = ImageOps.invert(bg)

    # Keep the plate: the tallest connected run of mask rows (the copyright text
    # sits below it, separated by background rows).
    rows = [any(mask.getpixel((x, y)) for x in range(0, w, 2)) for y in range(h)]
    runs, start = [], None
    for y, r in enumerate(rows + [False]):
        if r and start is None:
            start = y
        elif not r and start is not None:
            runs.append((start, y))
            start = None
    top, bottom = max(runs, key=lambda r: r[1] - r[0])
    band = mask.crop((0, top, w, bottom))
    cols = [x for x in range(w) if any(band.getpixel((x, y)) for y in range(0, bottom - top, 2))]
    box = (min(cols), top, max(cols) + 1, bottom)

    logo = title.crop(box).convert("RGBA")
    alpha = mask.crop(box).filter(ImageFilter.GaussianBlur(0.8))
    logo.putalpha(alpha)
    return logo


def sepia(img, strength=1.0):
    g = ImageOps.grayscale(img)
    tinted = ImageOps.colorize(g, black=(18, 14, 10), mid=(128, 104, 74), white=(238, 222, 190))
    return Image.blend(img.convert("RGB"), tinted, strength)


def cover(img, size, focus_y=0.5):
    """Scale to cover `size`, cropping around a vertical focus point."""
    tw, th = size
    scale = max(tw / img.width, th / img.height)
    im = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)
    x = (im.width - tw) // 2
    y = int(max(0, min(im.height - th, focus_y * im.height - th / 2)))
    return im.crop((x, y, x + tw, y + th))


def vignette(img, strength=0.75, top_shade=0.0):
    w, h = img.size
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(m)
    d.ellipse((-w * 0.25, -h * 0.35, w * 1.25, h * 1.35), fill=255)
    m = m.filter(ImageFilter.GaussianBlur(min(w, h) * 0.18))
    dark = Image.new("RGB", (w, h), (8, 6, 4))
    out = Image.composite(img, Image.blend(img, dark, strength), m)
    if top_shade:
        grad = Image.linear_gradient("L").resize((w, h))  # 0 at top -> 255 at bottom
        grad = grad.point(lambda v: int(255 * min(1.0, v / 255 / 0.6)))
        out = Image.composite(out, Image.blend(out, dark, top_shade), grad)
    return out


def backdrop(photo, size, focus_y, darken, top_shade=0.0):
    im = cover(photo, size, focus_y)
    im = sepia(im, 0.9)
    im = ImageEnhance.Contrast(im).enhance(1.15)
    im = ImageEnhance.Brightness(im).enhance(darken)
    return vignette(im, 0.8, top_shade)


def place_logo(canvas, logo, width, center, shadow=True):
    lw = width
    lh = round(logo.height * lw / logo.width)
    lg = logo.resize((lw, lh), Image.LANCZOS)
    x = round(center[0] - lw / 2)
    y = round(center[1] - lh / 2)
    if shadow:
        sh = Image.new("RGBA", (lw + 40, lh + 40), (0, 0, 0, 0))
        a = lg.split()[3].point(lambda v: int(v * 0.85))
        sh.paste((0, 0, 0, 255), (20, 20), a)
        sh = sh.filter(ImageFilter.GaussianBlur(max(3, lw // 60)))
        canvas.alpha_composite(sh, (x - 20 + max(2, lw // 120), y - 20 + max(3, lw // 80)))
    canvas.alpha_composite(lg, (x, y))
    return (x, y, lw, lh)


def serif_font(size):
    for path in (
        "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
        "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
    ):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def spaced_text(canvas, text, font, center_x, y, fill, spacing):
    d = ImageDraw.Draw(canvas)
    widths = [d.textlength(ch, font=font) for ch in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x = center_x - total / 2
    for ch, cw in zip(text, widths):
        d.text((x + 1, y + 1), ch, font=font, fill=(0, 0, 0, 200))
        d.text((x, y), ch, font=font, fill=fill)
        x += cw + spacing


def save(img, rel):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # The Vita wants 8-bit indexed PNGs for icon0 / LiveArea images (<= 256 colors
    # keeps the files small and is what the system expects for the bubble).
    img.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG).save(path, optimize=True)
    print("wrote", path, img.size)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main_dir = sys.argv[1]
    logo = extract_logo(load_from_paks(main_dir, TITLE))
    photo = load_from_paks(main_dir, PHOTO)

    # LiveArea background: the landing craft, logo in the upper-middle.
    bg = backdrop(photo, (840, 500), focus_y=0.58, darken=0.5, top_shade=0.6).convert("RGBA")
    place_logo(bg, logo, 430, (420, 190))
    save(bg, "sce_sys/livearea/contents/bg.png")

    # Launch gate image (shown as the bubble opens).
    st = backdrop(photo, (280, 158), focus_y=0.55, darken=0.45, top_shade=0.5).convert("RGBA")
    place_logo(st, logo, 210, (140, 79))
    save(st, "sce_sys/livearea/contents/startup.png")

    # Bubble icon: soldiers in the boat, logo across the lower half.
    ic = backdrop(photo, (128, 128), focus_y=0.62, darken=0.55, top_shade=0.4).convert("RGBA")
    place_logo(ic, logo, 124, (64, 66), shadow=True)
    save(ic, "sce_sys/icon0.png")

    # Boot / loading screen (full resolution, RGB PNG, not palettized).
    sp = backdrop(photo, (960, 544), focus_y=0.58, darken=0.45, top_shade=0.6).convert("RGBA")
    x, y, lw, lh = place_logo(sp, logo, 500, (480, 230))
    spaced_text(sp, "LOADING", serif_font(22), 480, y + lh + 46, (226, 214, 190, 255), 7)
    path = os.path.join(OUT, "splash.png")
    os.makedirs(OUT, exist_ok=True)
    sp.convert("RGB").save(path, optimize=True)
    print("wrote", path, sp.size)


if __name__ == "__main__":
    main()
