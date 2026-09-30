#!/usr/bin/env python3
"""Builds a language pack with translated copies of the menu pictures that have words drawn
on them.

Some menus are pictures (the signs of the main menu, the buttons, the stamps of the load/save
folder, the difficulty board...), so the Text Language table cannot reach their words. This
reads each picture from YOUR OWN game paks, paints the English words over with the wood/paper
around them, writes the translation from misc/vita/lang/<code>.txt and packs the result as
lang/<code>/<picture> in lang_<code>.pk3. The game uses those copies when that Text Language
is chosen in the Vita settings. No game art is stored in this repository.

The translation is set with the letters of the pictures themselves (cut out of the English
words, with their wear and shading), so it looks like the original lettering. Pictures drawn
with the same lettering lend each other their letters. A letter none of them has is built when
possible (O from C, V from W, accents drawn at the letters' stem width); otherwise that letter
is drawn with a similar font, as heavy and wide as the pictures' own letters. A translation longer than the English is narrowed, then made smaller, to
fit the sign.

Usage (needs Pillow and numpy):
    python3 misc/vita/make_menu_text.py /path/to/main            # every language
    python3 misc/vita/make_menu_text.py /path/to/main --lang pt   # lang_pt.pk3 only

then copy lang_<code>.pk3 to ux0:data/openmohaa/main/ on the Vita. --preview DIR also writes
each picture next to its original, to check them.

The font is Oswald (SIL Open Font License, misc/vita/fonts/OFL.txt).
"""
import argparse
import glob
import io
import os
import re
import sys
import unicodedata
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "fonts", "Oswald.ttf")
MENU = "textures/mohmenu/"

# Picture -> (lettering family, options, texts).
#
# Pictures drawn with the same lettering are one family; a text with another lettering names
# its own family after its areas.
#
# A text is (English, areas): the area (x0, y0, x1, y1) around each line of it in the
# picture. "A|B" is a text on two lines; its translation splits its lines with | too. The
# translation is looked up in misc/vita/lang/<code>.txt with the English (| as a space).
#
# Options: dark (dark letters on a light ground), alpha (white letters drawn by the alpha
# channel), threshold (how much lighter/darker than the ground a letter pixel is), skew
# (slant of an italic lettering: the picture is stood upright to cut its letters), unusable
# (letters not to take from the picture, when they come out with a piece of their neighbour),
# room (x0, x1: where the translation may go, when the sign is narrower than the English area).
PICTURES = {
    # the wooden boards
    "difficulty.tga": ("board", {}, [
        ("SELECT DIFFICULTY", [(20, 14, 236, 58)]),
        ("EASY", [(20, 72, 236, 112)]),
        ("MEDIUM", [(20, 118, 236, 160)]),
        ("HARD", [(20, 166, 236, 208)]),
        ("CANCEL", [(82, 220, 176, 248)]),
    ]),
    "cancel.tga": ("board bold", {}, [("CANCEL", [(40, 4, 216, 29)])]),
    "confirm_delete.tga": ("board bold", {}, [
        ("ARE YOU SURE YOU WANT TO DELETE?", [(5, 8, 251, 27)]),
        ("YES", [(52, 34, 100, 53)]),
        ("NO", [(156, 34, 204, 53)]),
    ]),
    # the green signs hanging in the main menu room
    "newgame_sign.tga": ("hanging", {}, [("NEW GAME", [(16, 20, 246, 52)])]),
    "options_sign.tga": ("hanging", {}, [("OPTIONS", [(16, 20, 246, 52)])]),
    "briefingroom_sign.tga": ("hanging", {}, [("BRIEFING ROOM", [(14, 18, 250, 54)])]),
    "credits_sign.tga": ("hanging", {}, [("CREDITS", [(16, 20, 246, 52)])]),
    "multiplayer_sign.tga": ("hanging", {}, [("MULTIPLAYER", [(16, 18, 250, 54)])]),
    "maplist_sign.tga": ("hanging", {}, [("MAPLIST", [(16, 20, 246, 52)])]),
    "continue_sign.tga": ("hanging", {"room": (18, 234)}, [("CONTINUE GAME", [(14, 18, 250, 54)])]),
    "loaddemo_sign.tga": ("hanging", {}, [("LOAD DEMO", [(16, 20, 246, 52)])]),
    "warrecords_sign.tga": ("hanging", {}, [("LOAD/SAVE &|MEDAL CASE", [(16, 10, 252, 62)])]),
    "maproom_sign.tga": ("hanging", {}, [("MAP ROOM", [(14, 20, 124, 48)])]),
    "startgame_sign.tga": ("hanging", {}, [("START GAME", [(12, 20, 126, 48)])]),
    "joingame_sign.tga": ("hanging", {}, [("JOIN GAME", [(14, 20, 124, 48)])]),
    "multiplayeroptions_sign.tga": ("hanging", {}, [("MULTIPLAYER|OPTIONS", [(3, 14, 127, 56)])]),
    # the blue signs of the options and records rooms
    "advanced_sign.tga": ("blue", {}, [("ADVANCED", [(4, 7, 126, 31)])]),
    "audio_sign.tga": ("blue", {}, [("AUDIO", [(4, 7, 126, 31)])]),
    "controls_sign.tga": ("blue", {}, [("CONTROLS", [(4, 7, 126, 31)])]),
    "video_sign.tga": ("blue", {}, [("VIDEO", [(4, 7, 126, 31)])]),
    "loadsave_sign.tga": ("blue", {}, [("LOAD/SAVE|GAME", [(4, 14, 127, 58)])]),
    "personalrecords_sign.tga": ("blue", {}, [("PERSONAL|RECORDS", [(4, 16, 126, 58)])]),
    # the blackboards of the briefing room
    "trainingcourse_sign.tga": ("blackboard", {"mono": True}, [("TRAINING|COURSE", [(20, 18, 236, 114)])]),
    "mission1_sign.tga": ("blackboard", {"mono": True}, [("MISSION 1:", [(14, 24, 242, 68)]), ("Lighting The Torch", [(8, 70, 248, 112)])]),
    "mission2_sign.tga": ("blackboard", {"mono": True}, [("MISSION 2:", [(14, 21, 242, 66)]), ("Scuttling The U-529", [(8, 70, 248, 112)])]),
    "mission3_sign.tga": ("blackboard", {"mono": True}, [("MISSION 3:", [(14, 21, 242, 66)]), ("Operation Overlord", [(8, 70, 248, 112)])]),
    "mission4_sign.tga": ("blackboard", {"mono": True}, [("MISSION 4:", [(14, 21, 242, 66)]), ("Behind Enemy Lines", [(8, 70, 248, 112)])]),
    "mission5_sign.tga": ("blackboard", {"mono": True}, [("MISSION 5:", [(14, 21, 242, 66)]), ("The Day Of The Tiger", [(10, 70, 247, 112)])]),
    "mission6_sign.tga": ("blackboard", {"mono": True}, [("MISSION 6:", [(14, 21, 242, 66)]), ("Return To Schmerzen", [(8, 70, 248, 112)])]),
    # the green buttons, and their lit copies shown under the cursor
    "apply.tga": ("button", {}, [("APPLY", [(3, 5, 125, 30)])]),
    "back.tga": ("button", {}, [("BACK", [(3, 5, 125, 30)])]),
    "backtogame.tga": ("button", {}, [("BACK TO GAME", [(3, 5, 125, 30)])]),
    "continue.tga": ("button", {}, [("CONTINUE", [(3, 5, 125, 30)])]),
    "default.tga": ("button", {}, [("DEFAULT", [(3, 5, 125, 30)])]),
    "quit.tga": ("button", {}, [("QUIT", [(3, 5, 125, 30)])]),
    "refresh.tga": ("button", {}, [("REFRESH", [(3, 5, 125, 30)])]),
    "join_game.tga": ("button", {}, [("JOIN GAME", [(3, 5, 125, 30)])]),
    "apply_h.tga": ("button lit", {}, [("APPLY", [(3, 5, 125, 30)])]),
    "back_h.tga": ("button lit", {}, [("BACK", [(3, 5, 125, 30)])]),
    "backtogame_h.tga": ("button lit", {}, [("BACK TO GAME", [(3, 5, 125, 30)])]),
    "continue_h.tga": ("button lit", {}, [("CONTINUE", [(3, 5, 125, 30)])]),
    "default_h.tga": ("button lit", {}, [("DEFAULT", [(3, 5, 125, 30)])]),
    "quit_h.tga": ("button lit", {}, [("QUIT", [(3, 5, 125, 30)])]),
    "refresh_h.tga": ("button lit", {}, [("REFRESH", [(3, 5, 125, 30)])]),
    "join_game_h.tga": ("button lit", {}, [("JOIN GAME", [(3, 5, 125, 30)])]),
    # the load/save folder: red stamps on paper
    "voodoo/loadsave_b.tga": ("stamp", {"dark": True}, [
        ("TOP SECRET", [(18, 10, 180, 38)], "typed"),
        ("LOAD", [(76, 70, 181, 101)]),
        ("SAVE", [(76, 124, 181, 152)]),
        ("DELETE", [(61, 176, 198, 203)]),
    ]),
    # the whole folder, drawn over the pieces above (ui/loadsave.urc)
    "loadsave_a.tga": ("stamp", {"dark": True}, [
        ("TOP SECRET", [(274, 10, 436, 38)], "typed"),
        ("LOAD", [(332, 70, 437, 101)]),
        ("SAVE", [(332, 124, 437, 152)]),
        ("DELETE", [(317, 176, 454, 203)]),
    ]),
    "load_h.tga": ("stamp", {"dark": True}, [("LOAD", [(12, 8, 114, 50)])]),
    "save_h.tga": ("stamp", {"dark": True}, [("SAVE", [(11, 13, 117, 54)])]),
    "delete_h.tga": ("stamp", {"dark": True}, [("DELETE", [(20, 19, 240, 54)])]),
    # messages
    "paused.tga": ("metal", {"threshold": 40}, [("PAUSED", [(2, 2, 126, 30)])]),
    "loadingwidget.tga": ("metal", {"threshold": 40}, [("LOADING", [(8, 6, 250, 58)])]),
    "clickfire.tga": ("italic", {"skew": 0.2, "unusable": "O"}, [("CLICK FIRE TO SKIP BRIEFING", [(22, 17, 490, 48)])]),
    "gamesaved.tga": ("white", {"alpha": True}, [("GAME SAVED", [(0, 0, 128, 16)])]),
}


def load_translations(path):
    text = open(path, encoding="utf-8").read()
    return dict(re.findall(r'^\{ "([^"]*)" "([^"]*)" \}', text, re.M))


def read_paks(main_dir):
    """Every picture of the menus, the last pak that has it winning, like the game."""
    files = {}
    for pak in sorted(glob.glob(os.path.join(main_dir, "*.pk3")), key=str.lower):
        if os.path.basename(pak).lower().startswith("lang_"):
            continue
        with zipfile.ZipFile(pak) as z:
            for name in z.namelist():
                if name.lower().startswith(MENU):
                    files[name.lower()] = (pak, name)
    return files


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


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


def components(mask):
    """Connected pieces of the mask, as lists of (y, x)."""
    seen = np.zeros_like(mask)
    h, w = mask.shape
    pieces = []
    for sy, sx in zip(*np.nonzero(mask)):
        if seen[sy, sx]:
            continue
        stack, piece = [(sy, sx)], []
        seen[sy, sx] = True
        while stack:
            y, x = stack.pop()
            piece.append((y, x))
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        pieces.append(piece)
    return pieces


def letter_mask(ink, box, threshold):
    """The letter pixels of an area. Long pieces through its edge are frames and borders."""
    x0, y0, x1, y1 = box
    area = ink[y0:y1, x0:x1]
    mask = area > np.median(area) + threshold
    h, w = mask.shape
    for piece in components(mask):
        ys, xs = zip(*piece)
        edge = min(ys) == 0 or min(xs) == 0 or max(ys) == h - 1 or max(xs) == w - 1
        bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if len(piece) < 3 or (edge and (bw > 0.5 * w or (bh > 0.8 * h and bw <= 3) or len(piece) < 12)):
            for y, x in piece:
                mask[y, x] = False
    return mask


def expected_width(ch):
    """Roughly how wide a letter is, next to the height of the capitals."""
    if ch in "Iil1|!.,:;'j":
        return 0.3
    if ch in "ftr/-J":
        return 0.5
    if ch in "MW&":
        return 1.2
    if ch in "mw":
        return 1.0
    return 0.8 if ch.isupper() or ch.isdigit() else 0.6


def misfit(runs, expected):
    """How far the widths of the cut letters are from what these letters usually measure."""
    widths = [b - a for a, b in runs]
    scale = sum(widths) / sum(expected)
    return sum(((w - scale * e) / (scale * e)) ** 2 for w, e in zip(widths, expected))


def share_out(mask, runs, expected):
    """Fewer runs than letters: some letters touch. Each run gets the consecutive letters whose
    widths best match its own, then is cut where the ink is thinnest near where they meet."""
    widths = [b - a for a, b in runs]
    scale = sum(widths) / sum(expected)
    n, m = len(runs), len(expected)
    # best[i][j]: cost of giving the first i runs the first j letters
    best = [[np.inf] * (m + 1) for _ in range(n + 1)]
    back = [[0] * (m + 1) for _ in range(n + 1)]
    best[0][0] = 0.0
    for i in range(1, n + 1):
        for j in range(i, m - (n - i) + 1):
            for k in range(1, j - i + 2):
                if best[i - 1][j - k] == np.inf:
                    continue
                want = scale * sum(expected[j - k:j])
                cost = best[i - 1][j - k] + ((widths[i - 1] - want) / max(want, 1)) ** 2
                if cost < best[i][j]:
                    best[i][j], back[i][j] = cost, k
    groups, j = [], m
    for i in range(n, 0, -1):
        groups.append(back[i][j])
        j -= back[i][j]
    groups.reverse()
    out, j = [], 0
    for (start, end), k in zip(runs, groups):
        share = expected[j:j + k]
        j += k
        cuts, left = [], start
        for q in range(k - 1):
            aim = start + (end - start) * sum(share[:q + 1]) / sum(share)
            reach = max(1, int((end - start) * share[q] / sum(share) * 0.35))
            lo, hi = max(left + 1, int(aim) - reach), min(end - 1, int(aim) + reach + 1)
            cut = lo + int(np.argmin(mask[:, lo:hi].sum(axis=0))) if hi > lo else int(aim)
            cuts.append(cut)
            left = cut
        edges = [start] + cuts + [end]
        out += [(edges[q], edges[q + 1]) for q in range(k)]
    return out


def split_letters(mask, letters, mono=False):
    """Column runs of ink, one per letter; touching letters are split where the ink is thinnest."""
    count = len(letters)
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
    if not runs:
        return runs
    # specks are not letters; a letter broken by worn ink is glued back to its neighbour
    ink = [mask[:, a:b].sum() for a, b in runs]
    runs = [r for r, n in zip(runs, ink) if n >= 0.12 * np.median(ink)]
    while len(runs) > count:
        gaps = [runs[k + 1][0] - runs[k][1] for k in range(len(runs) - 1)]
        k = int(np.argmin(gaps))
        if gaps[k] > 2:
            break
        runs[k:k + 2] = [(runs[k][0], runs[k + 1][1])]
    if 0 < len(runs) < count:
        # first the widest runs are halved; when that cuts a letter wrong (italics, where
        # several touch), the letters are shared out by their usual widths
        halved = list(runs)
        while 0 < len(halved) < count:
            i = max(range(len(halved)), key=lambda k: halved[k][1] - halved[k][0])
            start, end = halved[i]
            if end - start < 6:
                break
            # the join is near the middle of the run (a serif, a touching stroke)
            lo, hi = start + max(2, (end - start) * 3 // 10), end - max(2, (end - start) * 3 // 10)
            cut = lo + int(np.argmin(mask[:, lo:hi].sum(axis=0)))
            halved[i:i + 1] = [(start, cut), (cut, end)]
        # a typewriter lettering has every letter as wide as the others
        expected = [1.0 if mono else expected_width(ch) for ch in letters]
        shared = share_out(mask, runs, expected)
        good = lambda r: len(r) == count and letters_fit(mask, r, letters)
        if good(halved) and (not good(shared) or misfit(halved, expected) <= misfit(shared, expected)):
            return halved
        runs = shared
    return runs


def split_lines(mask, count):
    """The masks of the lines of a text on a slanted sign: the pieces of ink are divided where
    the gap between lines is widest, trying the slants a sign may have."""
    pieces = components(mask)
    big = [p for p in pieces if len(p) >= 10]    # specks are sorted afterwards
    if count < 2 or len(big) < 2 * count:
        return [mask] * count
    centres = [(np.mean([p[1] for p in piece]), np.mean([p[0] for p in piece])) for piece in pieces]
    big_centres = [(np.mean([p[1] for p in piece]), np.mean([p[0] for p in piece])) for piece in big]
    best = None
    for slope in np.linspace(-0.2, 0.2, 41):
        order = np.sort([cy - slope * cx for cx, cy in big_centres])
        gaps = np.diff(order)
        gaps[:1] = gaps[-1:] = 0    # a line has more than one piece
        cuts = np.sort(np.argsort(gaps)[-(count - 1):])
        score = gaps[cuts].min()
        if best is None or score > best[0]:
            best = (score, slope, order[cuts] + gaps[cuts] / 2)
    _, slope, limits = best
    masks = [np.zeros_like(mask) for _ in range(count)]
    for piece, (cx, cy) in zip(pieces, centres):
        k = int(np.searchsorted(limits, cy - slope * cx))
        for y, x in piece:
            masks[k][y, x] = True
    return masks


NARROW = "Il1ijtfr:;.,'!|/-"


def letters_fit(mask, runs, letters):
    """Whether each cut is as wide as its letter can be: a wrong cut gives a sliver or two
    letters in one."""
    tall = [np.ptp(np.nonzero(mask[:, a:b].any(axis=1))[0]) + 1 for a, b in runs]
    height = max(tall)
    for (a, b), ch in zip(runs, letters):
        w = b - a
        if w > 1.5 * height or (ch not in NARROW and w < 0.25 * height):
            return False
    return True


def fit_line(xs, ys):
    """y = a*x + b through the points, ignoring the ones far from the others (descenders,
    dashes)."""
    xs, ys = np.array(xs, float), np.array(ys, float)
    if len(xs) < 2:
        return 0.0, float(ys[0])
    keep = np.abs(ys - np.median(ys)) <= 2.5
    if keep.sum() < 2:
        keep[:] = True
    a, b = np.polyfit(xs[keep], ys[keep], 1)
    if abs(a) > 0.15:
        a, b = 0.0, float(np.median(ys[keep]))
    return float(a), float(b)


class Line:
    """One line of English in a picture: its letters and where they sit."""

    def __init__(self, box, english, mask, mono=False):
        self.box, self.english, self.mask = box, english, mask
        y0 = box[1]
        letters = english.replace(" ", "")
        x0 = box[0]
        runs = split_letters(mask, letters, mono) if mask.any() else []
        self.ok = len(runs) == len(letters) and letters_fit(mask, runs, letters)
        if not mask.any():
            return
        ys, xs = np.nonzero(mask)
        self.centre = x0 + (xs.min() + xs.max()) / 2
        if not self.ok:
            # still usable to place a translation: flat, as tall as the ink
            self.slope, self.base0 = 0.0, float(y0 + ys.max())
            self.asc, self.desc = float(ys.max() - ys.min()), 0.0
            self.gap, self.space = 2.0, (ys.max() - ys.min()) * 0.35
            self.runs, self.letters = [], ""
            return
        tops, bots, mids = [], [], []
        for start, end in runs:
            col = np.nonzero(mask[:, start:end].any(axis=1))[0]
            tops.append(y0 + col.min())
            bots.append(y0 + col.max())
            mids.append(x0 + (start + end - 1) / 2)
        self.slope, self.base0 = fit_line(mids, bots)
        self.asc = max(self.baseline(m) - t for m, t in zip(mids, tops))
        self.desc = max(0.0, max(b - self.baseline(m) for m, b in zip(mids, bots)))
        between = [runs[k + 1][0] - runs[k][1] for k in range(len(runs) - 1)]
        # gaps between the letters of a word, and between words
        inner, spaces, k = [], [], 0
        for word in english.split():
            for _ in range(len(word) - 1):
                inner.append(between[k])
                k += 1
            if k < len(between):
                spaces.append(between[k])
                k += 1
        self.gap = float(np.median(inner)) if inner else 2.0
        self.space = float(np.median(spaces)) - self.gap if spaces else self.asc * 0.35
        self.runs, self.letters = runs, letters

    def baseline(self, x):
        return self.slope * x + self.base0


# --- letters taken from the pictures ------------------------------------------------------
#
# Each English letter is cut out of its picture and split into two layers by comparing it
# with the ground painted in under it: the letter (alpha + colour) and its dark halo.

def owners(mask, runs):
    """Which letter each ink pixel belongs to: a piece of ink within one letter's columns is
    that letter's (even where it leans over the next one); a piece spanning several letters
    (touching serifs) is shared out by columns."""
    owner = np.full(mask.shape, -1, int)
    column_run = np.full(mask.shape[1], -1, int)
    for k, (start, end) in enumerate(runs):
        column_run[start:end] = k
    for piece in components(mask):
        ys, xs = np.array(piece).T
        counts = np.bincount(column_run[xs] + 1)
        k = int(counts[1:].argmax()) if len(counts) > 1 else -1
        if k >= 0 and counts[k + 1] >= 0.75 * len(piece):
            owner[ys, xs] = k    # one letter's, even what leans over its neighbours
        else:
            # touching letters: each pixel goes to the letter whose middle it is nearest to
            # along the ink, so a stroke reaching over the neighbour stays with its letter
            label = {}
            queue = []
            for y, x in zip(ys, xs):
                run = column_run[x]
                if run >= 0:
                    start, end = runs[run]
                    quarter = (end - start) / 4
                    if start + quarter <= x < end - quarter:
                        label[(y, x)] = run
                        queue.append((y, x))
            inside = set(zip(ys.tolist(), xs.tolist()))
            while queue:
                nxt = []
                for y, x in queue:
                    for q in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                        if q in inside and q not in label:
                            label[q] = label[(y, x)]
                            nxt.append(q)
                queue = nxt
            for y, x in zip(ys, xs):
                owner[y, x] = label.get((y, x), column_run[x])
    return owner


def harvest(pic, line, orig, ground, ink_o, ink_g, halo_on):
    glyphs = []
    x0, y0 = line.box[0], line.box[1]
    h, w = orig.shape[:2]
    owner = np.full((h, w), -1, int)
    bh, bw = line.mask.shape
    owner[y0:y0 + bh, x0:x0 + bw] = owners(line.mask, line.runs)
    for k, ((start, end), ch) in enumerate(zip(line.runs, line.letters)):
        mid = x0 + (start + end - 1) / 2
        base = line.baseline(mid)
        gy0 = int(round(base - line.asc)) - 2
        gy1 = int(round(base + line.desc)) + 3
        # wide enough for what leans past the letter's columns
        lean = np.nonzero((owner == k).any(axis=0))[0]
        if not len(lean):
            continue
        gx0, gx1 = min(x0 + start, lean.min()) - 2, max(x0 + end, lean.max() + 1) + 2
        if gy0 < 0 or gx0 < 0 or gy1 > h or gx1 > w:
            continue
        own = (owner[gy0:gy1, gx0:gx1] == k).astype(np.uint8) * 255
        near = np.array(Image.fromarray(own).filter(ImageFilter.MaxFilter(5))).astype(np.float32) / 255
        others = ((owner[gy0:gy1, gx0:gx1] >= 0) & (owner[gy0:gy1, gx0:gx1] != k))
        # the soft edge of a neighbour's stroke is the neighbour's, not this letter's
        grow = lambda m, n: np.array(Image.fromarray(m.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(n))) > 0
        others = others | (grow(others, 5) & ~grow(own > 0, 3))
        o, g = orig[gy0:gy1, gx0:gx1], ground[gy0:gy1, gx0:gx1]
        lo, lg = ink_o[gy0:gy1, gx0:gx1], ink_g[gy0:gy1, gx0:gx1]
        on = lo > lg + 30
        top = np.percentile(lo[on], 95) if on.any() else lo.max()
        alpha = np.clip((lo - lg) / np.maximum(top - lg, 1), 0, 1) ** 0.8
        alpha *= near
        alpha[others] = 0
        alpha[:, :2] = 0
        alpha[:, -2:] = 0
        if halo_on:
            lum_o, lum_g = luma(o), luma(g)
            halo = np.clip((lum_g - lum_o) / np.maximum(lum_g, 1), 0, 1)
            halo *= np.array(Image.fromarray(own).filter(ImageFilter.MaxFilter(7))).astype(np.float32) / 255
            halo[others] = 0
            halo[:, 0] = 0
            halo[:, -1] = 0
        else:
            halo = np.zeros_like(alpha)
        colour = np.clip((o[..., :3] - (1 - alpha[..., None]) * g[..., :3]) / np.maximum(alpha[..., None], 1e-3), 0, 255)
        glyphs.append((ch, {"a": alpha, "halo": halo, "col": colour, "asc": line.asc,
                            "base": base - gy0, "pic": pic, "lead": x0 + start - gx0, "ink": end - start}))
    return glyphs


def ring_from_c(c):
    """No O anywhere: the left half of a C, mirrored, closes the ring."""
    width = c["a"].shape[1]
    half = width // 2 + 1
    ring = lambda arr: np.concatenate([arr[:, :half], arr[:, :width - half][:, ::-1]], axis=1)
    return dict(c, a=ring(c["a"]), halo=ring(c["halo"]), col=ring(c["col"]))


def v_from_w(w):
    """No V anywhere: a W is two Vs side by side."""
    width = w["a"].shape[1]
    cut = int(round(width * 0.56))
    part = lambda arr: np.concatenate([arr[:, :cut], np.zeros_like(arr[:, :2])], axis=1)
    a = part(w["a"])
    return dict(w, a=a, halo=part(w["halo"]), col=part(w["col"]), lead=2, ink=a.shape[1] - 4)


# Families with the same lettering in another colour: they lend each other the letters one lacks.
SIBLINGS = {"button": "button lit", "button lit": "button"}


def recoloured(g, colour):
    solid = g["a"] > 0.8
    if not solid.any():
        return g
    return dict(g, col=np.clip(g["col"] * (np.array(colour, np.float32) / np.maximum(np.median(g["col"][solid], axis=0), 1)), 0, 255))


def pick(pool, ch, pic, asc):
    choices = pool.get(ch)
    if not choices and ch == "O" and pool.get("C"):
        choices = [ring_from_c(g) for g in pool["C"]]
    if not choices and ch == "V" and pool.get("W"):
        choices = [v_from_w(g) for g in pool["W"]]
    if not choices:
        return None
    if len(choices) > 2:
        # a copy much wider or narrower than the others was cut wrong
        usual = np.median([g["ink"] / g["asc"] for g in choices])
        mass = lambda g: g["a"].sum() / g["asc"] ** 2    # a letter cut short has less ink
        usual_mass = np.median([mass(g) for g in choices])
        choices = [g for g in choices if abs(g["ink"] / g["asc"] - usual) <= 0.2 * usual
                   and abs(mass(g) - usual_mass) <= 0.2 * usual_mass] or choices
    own = [g for g in choices if g["pic"] == pic] or choices
    return min(own, key=lambda g: (abs(g["asc"] - asc), -g["asc"]))


WEIGHTS = ("ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold")
_fonts = {}


def font_at(asc, weight):
    """The font, at the size whose capitals are asc pixels tall."""
    key = (int(asc * 4), weight)
    if key not in _fonts:
        probe = ImageDraw.Draw(Image.new("L", (1, 1)))
        size = max(4, int(asc))
        while True:
            font = ImageFont.truetype(FONT, size)
            font.set_variation_by_name(weight)
            cap = probe.textbbox((0, 0), "H", font=font)
            if cap[3] - cap[1] >= asc or size > 300:
                break
            size += 1
        _fonts[key] = (font, cap)
    return _fonts[key]


def stem_of(a):
    """Thickness of the upright strokes: the commonest length of a run of ink along a row."""
    runs = []
    for row in a > 0.5:
        edges = np.flatnonzero(np.diff(np.concatenate([[0], row.astype(np.int8), [0]])))
        runs += list(edges[1::2] - edges[0::2])
    return float(np.median(runs)) if runs else 1.0


def lettering(pool):
    """How heavy and how wide the pictures' letters are, next to their height."""
    stems, widths = [], []
    for ch, glyphs in pool.items():
        g = glyphs[0]
        if ch.isupper():
            stems.append(stem_of(g["a"]) / g["asc"])
            if ch not in "IJL":
                widths.append((ch, g["ink"] / g["asc"]))
    return (float(np.median(stems)) if stems else None), widths


def font_letter(ch, asc, colour, halo_on, style):
    """A letter drawn with the font, as tall, heavy and wide as the pictures' own letters."""
    stem, widths = style
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    weight = "Regular"
    if stem:
        def font_stem(wt):    # measured like the pictures' letters
            font, cap = font_at(100, wt)
            img = Image.new("L", (400, 200), 0)
            ImageDraw.Draw(img).text((10, 10), "HIEN", font=font, fill=255)
            return stem_of(np.array(img).astype(np.float32) / 255) / 100
        weight = min(WEIGHTS, key=lambda wt: abs(font_stem(wt) - stem))
    ss = 4    # drawn 4x bigger and shrunk: small letters come out sharp
    font, cap = font_at(asc * ss, weight)
    stretch = 1.0
    if widths:
        big, _ = font_at(100, weight)
        ratios = []
        for other, ratio in widths:
            bb = probe.textbbox((0, 0), other, font=big)
            ratios.append(ratio / max((bb[2] - bb[0]) / 100, 0.05))
        stretch = float(np.clip(np.median(ratios), 0.7, 1.8))
    bb = probe.textbbox((0, 0), ch, font=font)
    pad = 4
    img = Image.new("L", (bb[2] - bb[0] + 2 * pad * ss, int(cap[3] - cap[1] + asc * ss) + 2 * pad * ss), 0)
    ImageDraw.Draw(img).text((pad * ss - bb[0], pad * ss - cap[1]), ch, font=font, fill=255)
    img = img.resize((max(1, round(img.width * stretch / ss)), max(1, round(img.height / ss))), Image.LANCZOS)
    a = np.array(img).astype(np.float32) / 255
    cols = np.nonzero(a.max(axis=0) > 0.05)[0]
    a = a[:, max(cols.min() - 2, 0):cols.max() + 3]
    a[:, :2] = 0
    a[:, -2:] = 0
    halo = np.zeros_like(a)
    if halo_on:
        halo = np.array(Image.fromarray((a * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3))
                        .filter(ImageFilter.GaussianBlur(1.0))).astype(np.float32) / 255 * 0.6
    col = np.broadcast_to(np.array(colour, np.float32), a.shape + (3,)).copy()
    return {"a": a, "halo": halo, "col": col, "asc": float(asc), "base": pad + asc, "pic": None,
            "lead": 2, "ink": a.shape[1] - 4}


# Accented letters: the base letter and its accent, for every Latin-1 letter the game's fonts
# can show (/ acute, \\ grave, ^ circumflex, ~ tilde, : diaeresis, , cedilla).
MARKS = {"\u0300": "\\", "\u0301": "/", "\u0302": "^", "\u0303": "~", "\u0308": ":", "\u0327": ","}
ACCENTS = {}
for _code in range(0xC0, 0x100):
    _parts = unicodedata.normalize("NFD", chr(_code))
    if len(_parts) == 2 and _parts[1] in MARKS:
        ACCENTS[chr(_code)] = _parts[0] + MARKS[_parts[1]]
# The Spanish opening marks are the closing ones turned upside down.
TURNED = {"¿": "?", "¡": "!"}


def draw_accent(out, kind, cx, top, cap, stem, colour, glyph_bottom, halo_strength):
    """An accent as thick as the letters' stems, antialiased, with the same dark halo."""
    ss = 4
    h = max(4, round(cap * 0.28))
    w = h * 0.8 if kind in "/\\" else h * 2.0 if kind == ":" else h * 1.4
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
        r = t * 0.6    # two dots, each a little wider than a stroke
        for px in (x0 + r, x1 + t - r):
            d.ellipse([px - r, y1 - 2 * r, px + r, y1], fill=255)
    elif kind == ",":
        # a short stem under the letter, curling round to the left
        mid = (x0 + x1) / 2
        hook = [(mid, y0), (mid, y0 + (y1 - y0) * 0.3)]
        hook += [(mid + (x1 - x0) * 0.45 * np.sin(np.pi * q / 8), y0 + (y1 - y0) * (0.3 + 0.35 * (1 - np.cos(np.pi * q / 8))))
                 for q in range(1, 9)]
        hook += [(mid - (x1 - x0) * 0.4, y1 - t / 2)]
        d.line(hook, fill=255, width=max(ss, int(t * 0.7)), joint="curve")
    mask = np.array(shape.resize((cw // ss, ch // ss), Image.LANCZOS)).astype(np.float32) / 255
    halo = np.array(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3))
                    .filter(ImageFilter.GaussianBlur(0.7))).astype(np.float32) / 255
    ax = int(round(cx - mask.shape[1] / 2))
    ay = glyph_bottom - 2 if kind == "," else top - mask.shape[0] + 3
    paste(out, ax, ay, mask, halo * halo_strength, np.broadcast_to(np.array(colour, np.float32), mask.shape + (3,)))


def turned(g):
    """A letter turned half a circle, still standing on the line."""
    h, w = g["a"].shape
    return dict(g, a=g["a"][::-1, ::-1].copy(), halo=g["halo"][::-1, ::-1].copy(), col=g["col"][::-1, ::-1].copy(),
                base=h - 1 - g["base"] + g["asc"], lead=w - g["lead"] - g["ink"])


def paste(out, x, y, a, halo, col):
    """Lay a letter (alpha, halo, colour) on the picture at x, y, clipped to it."""
    h, w = out.shape[:2]
    gh, gw = a.shape
    sx0, sy0 = max(0, -x), max(0, -y)
    sx1, sy1 = min(gw, w - x), min(gh, h - y)
    if sx1 <= sx0 or sy1 <= sy0:
        return
    region = out[y + sy0:y + sy1, x + sx0:x + sx1, :3]
    a, halo, col = a[sy0:sy1, sx0:sx1, None], halo[sy0:sy1, sx0:sx1, None], col[sy0:sy1, sx0:sx1]
    region *= (1 - halo)
    region[:] = region * (1 - a) + col * a


def scaled(g, fx, fy):
    if abs(fx - 1) < 0.01 and abs(fy - 1) < 0.01:
        return g
    size = (max(1, round(g["a"].shape[1] * fx)), max(1, round(g["a"].shape[0] * fy)))
    rs = lambda arr: np.clip(np.array(Image.fromarray(arr.astype(np.float32)).resize(size, Image.BICUBIC)), 0, None)
    return dict(g, a=rs(g["a"]), halo=rs(g["halo"]), col=np.stack([rs(g["col"][..., k]) for k in range(3)], -1),
                base=g["base"] * fy, lead=g["lead"] * fx, ink=g["ink"] * fx)


def set_line(out, line, words, pool, pic, opts, colour):
    """Set a translated line where the English one was, with the pictures' own letters."""
    halo_on = not (opts.get("dark") or opts.get("alpha"))
    parts, fonted = [], []
    style = None
    for ch in words:
        if ch == " ":
            parts.append(None)
            continue
        base, accent = (ACCENTS[ch][0], ACCENTS[ch][1]) if ch in ACCENTS else (TURNED.get(ch, ch), None)
        g = pick(pool, base, pic, line.asc)
        if g is None:
            style = style or lettering(pool)
            g = font_letter(base, line.asc, colour, halo_on, style)
            fonted.append(base)
        elif g["pic"] != pic:
            g = recoloured(g, colour)     # another sign's letter, in this sign's colour
        if ch in TURNED:
            g = turned(g)
        parts.append((scaled(g, line.asc / g["asc"], line.asc / g["asc"]), accent))
    widths = [line.space if p is None else p[0]["ink"] for p in parts]
    total = sum(widths) + line.gap * (len(parts) - 1)
    left, right = opts.get("room", (line.box[0], line.box[2]))
    room = (right - left) - 4
    squeeze, size = 1.0, 1.0
    if total > room:
        squeeze = room / total
        if squeeze < 0.72:     # narrowing alone would crush the letters: make them smaller too
            size = max(0.5, room / (total * 0.8))
            squeeze = min(1.0, room / (total * size))
    fx, fy = squeeze * size, size
    # the line keeps its middle when the letters get smaller
    middle = lambda x: line.baseline(x) - line.asc / 2
    x = line.centre - total * fx / 2
    x = min(max(x, left + 2), right - 2 - total * fx)     # stay on the sign
    placed = []
    for part, width in zip(parts, widths):
        if part is not None:
            g, accent = part
            g = scaled(g, fx, fy)
            mid = x + width * fx / 2
            placed.append((g, accent, int(round(x - g["lead"])), int(round(middle(mid) + line.asc * size / 2 - g["base"])), mid))
        x += (width + line.gap) * fx
    # the dark halos first, all of them, so none darkens the letter next to it
    for g, _, gx, gy, _ in placed:
        paste(out, gx, gy, np.zeros_like(g["a"]), g["halo"], g["col"])
    for g, _, gx, gy, _ in placed:
        paste(out, gx, gy, g["a"], np.zeros_like(g["halo"]), g["col"])
    for g, accent, gx, gy, mid in placed:
        if accent:
            solid = g["a"] > 0.8
            col = np.median(g["col"][solid], axis=0) if solid.any() else np.array(colour, np.float32)
            rows = (g["a"] > 0.5).sum(axis=1)
            stem = max(1.5, float(np.median(rows[rows > 0])) * 0.5) if (rows > 0).any() else 2.0
            inked = np.nonzero((g["a"] > 0.5).any(axis=1))[0]
            top = gy + (inked.min() if len(inked) else 0)
            bottom = gy + (inked.max() if len(inked) else g["a"].shape[0])
            shift = line.asc * size * 0.08 if accent == "/" else 0.0    # an acute leans right
            draw_accent(out, accent, mid + 1 + shift, top - 2, line.asc * size, stem, col,
                        bottom, 0.55 if halo_on else 0.0)
    return fonted


def slant_rows(pix, skew, bottom):
    """Each row moved sideways by whole pixels, as an italic slant does it: undone exactly by
    moving back (see upright_rows)."""
    shift = np.round(skew * (bottom - np.arange(pix.shape[0]))).astype(int)
    return shift, int(max(0, shift.max())), int(max(0, -shift.min()))


class Picture:
    def __init__(self, name, img, family, opts, texts):
        self.name, self.family, self.opts = name, family, opts
        self.mode = img.mode
        rgba = np.array(img.convert("RGBA")).astype(np.float32)
        self.rgba = rgba
        self.skew = opts.get("skew", 0.0)
        if self.skew:
            # italics: the picture is stood upright (each row moved left by the slant), so its
            # letters no longer lean over each other; the result is slanted back at the end
            bottom = max(box[3] for _, boxes, *_ in texts for box in boxes)
            self.shift, left, right = slant_rows(rgba, self.skew, bottom)
            self.pad = left
            upright = np.zeros((rgba.shape[0], rgba.shape[1] + left + right, 4), np.float32)
            for y, sh in enumerate(self.shift):
                upright[y, left - sh:left - sh + rgba.shape[1]] = rgba[y]
            rgba = upright
            texts = [(e, [(b[0] + left, b[1], b[2] + left, b[3]) for b in boxes], *own) for e, boxes, *own in texts]
        if opts.get("alpha"):
            # white letters drawn by the alpha channel: work on the alpha as a grey picture
            grey = rgba[..., 3:4]
            rgba = np.concatenate([grey, grey, grey, np.full_like(grey, 255)], axis=-1)
        self.orig = rgba
        self.ink = (255 - luma(rgba)) if opts.get("dark") else luma(rgba)
        self.ground = rgba.copy()
        self.lines = []
        self.texts = []
        for english, boxes, *own in texts:
            words = english.split("|")
            if len(boxes) == 1 and len(words) > 1:
                # several lines in one area: divided by the gap between them
                masks = split_lines(letter_mask(self.ink, boxes[0], opts.get("threshold", 55)), len(words))
                boxes = boxes * len(words)
            else:
                masks = [letter_mask(self.ink, box, opts.get("threshold", 55)) for box in boxes]
            self.texts.append((english, boxes))
            for box, line_english, mask in zip(boxes, words, masks):
                grown = Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))
                erase(self.ground, box, np.array(grown) > 0)
                line = Line(box, line_english, mask, opts.get("mono", False))
                line.family = own[0] if own else family
                self.lines.append(line)
        self.ink_ground = (255 - luma(self.ground)) if opts.get("dark") else luma(self.ground)

    def harvest(self):
        halo_on = not (self.opts.get("dark") or self.opts.get("alpha"))
        glyphs = []
        for line in self.lines:
            if line.ok:
                glyphs += [(line.family, ch, g) for ch, g in
                           harvest(self.name, line, self.orig, self.ground, self.ink, self.ink_ground, halo_on)
                           if ch not in self.opts.get("unusable", "")]
        return glyphs

    def translate(self, table, pools):
        out = self.orig.copy()
        done, notes = 0, []
        k = 0
        for english, boxes in self.texts:
            lines = self.lines[k:k + len(boxes)]
            k += len(boxes)
            key = english.replace("|", " ")
            words = table.get(key)
            if not words or words == key:
                continue
            parts = words.split("|")
            if len(parts) != len(lines):
                notes.append(f"'{key}': the translation needs {len(lines)} lines split by |")
                continue
            if not all(line.mask.any() for line in lines):
                notes.append(f"'{key}': no letters found in the picture")
                continue
            if not all(line.ok for line in lines):
                notes.append(f"'{key}': could not cut its letters apart")
            for line in lines:
                x0, y0, x1, y1 = line.box
                out[y0:y1, x0:x1] = self.ground[y0:y1, x0:x1]
            for line, part in zip(lines, parts):
                x0, y0, x1, y1 = line.box
                solid = self.ink[y0:y1, x0:x1][line.mask]
                pix = self.orig[y0:y1, x0:x1][line.mask][:, :3]
                colour = np.median(pix[solid >= np.percentile(solid, 70)], axis=0)
                pool = dict(pools.get(line.family, {}))
                for ch, glyphs in pools.get(SIBLINGS.get(line.family), {}).items():
                    if ch not in pool:
                        pool[ch] = [recoloured(g, colour) for g in glyphs]
                fonted = set_line(out, line, part, pool, self.name, self.opts, colour)
                if fonted:
                    notes.append(f"'{part}': drew {''.join(sorted(set(fonted)))} with the font")
            done += 1
        if self.skew:
            slanted = np.zeros_like(self.rgba)
            for y, sh in enumerate(self.shift):
                slanted[y] = out[y, self.pad - sh:self.pad - sh + self.rgba.shape[1]]
            out = slanted
        if self.opts.get("alpha"):
            result = self.rgba.copy()
            result[..., 3] = np.clip(luma(out), 0, 255)
            out = result
        img = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")
        return (img if self.mode == "RGBA" else img.convert("RGB")), done, notes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("main_dir", help="the game's main folder (with the Pak*.pk3 files)")
    parser.add_argument("--lang", action="append", help="language code (default: every misc/vita/lang/*.txt)")
    parser.add_argument("-o", "--output-dir", default=".", help="where to write lang_<code>.pk3")
    parser.add_argument("--preview", help="also write each translated picture next to its original here")
    args = parser.parse_args()

    codes = args.lang or sorted(os.path.splitext(os.path.basename(p))[0]
                                for p in glob.glob(os.path.join(HERE, "lang", "*.txt")))
    files = read_paks(args.main_dir)
    pictures = []
    for name, (family, opts, texts) in PICTURES.items():
        found = files.get((MENU + name).lower())
        if not found:
            print(f"  not found in your paks: {MENU + name}")
            continue
        with zipfile.ZipFile(found[0]) as z:
            img = Image.open(io.BytesIO(z.read(found[1])))
            img.load()
        pictures.append(Picture(MENU + name, img, family, opts, texts))

    # the letters of each family; a picture uses its own ones first when it has them
    pools = {}
    for pic in pictures:
        for family, ch, g in pic.harvest():
            pools.setdefault(family, {}).setdefault(ch, []).append(g)

    failed = False
    for code in codes:
        table = load_translations(os.path.join(HERE, "lang", code + ".txt"))
        path = os.path.join(args.output_dir, f"lang_{code}.pk3")
        written = 0
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as out:
            for pic in pictures:
                image, done, notes = pic.translate(table, pools)
                for note in notes:
                    print(f"  {code}: {pic.name}: {note}")
                if not done:
                    continue
                buf = io.BytesIO()
                image.save(buf, format="TGA")
                out.writestr(f"lang/{code}/{pic.name}", buf.getvalue())
                written += 1
                if args.preview:
                    os.makedirs(args.preview, exist_ok=True)
                    orig = Image.fromarray(np.clip(pic.rgba, 0, 255).astype(np.uint8), "RGBA")
                    w, h = orig.size
                    both = Image.new("RGBA", (w * 2 + 4, h), (255, 0, 255, 255))
                    both.alpha_composite(orig, (0, 0))
                    both.alpha_composite(image.convert("RGBA"), (w + 4, 0))
                    both.save(os.path.join(args.preview, f"{code}_{pic.name[len(MENU):].replace('/', '_')}.png"))
        print(f"wrote {path} ({written} pictures)")
        if not written:
            os.remove(path)
            failed = True
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
