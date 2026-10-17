#!/usr/bin/env python3
"""Draws Vellum's own sleep pictures: quiet Lithuanian nature scenes.

Run after changing a scene:
  python3 tools/make_nature_pictures.py
  python3 tools/embed_pictures.py

Each scene is drawn at twice the screen's size and shrunk to 480 x 800
grey PNGs in assets/sleep. Few, flat tones and soft gradients suit the
reader, which dithers them to black and white. The pictures are made from
code only (no photographs), so they are ours to share under the MIT
licence. `--preview DIR` also writes each picture dithered as the reader
shows it.
"""
import argparse
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFont

root = pathlib.Path(__file__).resolve().parent.parent
S = 2
W, H = 480 * S, 800 * S
FONT = root / "assets/fonts/literata/Literata[opsz,wght].ttf"


def gradient(img, top, bottom, y0=0, y1=H):
    d = ImageDraw.Draw(img)
    for y in range(y0, y1):
        t = (y - y0) / max(1, y1 - y0 - 1)
        d.line([(0, y), (W, y)], fill=round(top + (bottom - top) * t))


def disc(d, cx, cy, r, fill):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def spruce(d, cx, base, w, h, fill, tiers=True):
    """One spruce, its branches in small steps when `tiers`."""
    if not tiers:
        d.polygon([(cx - w / 2, base), (cx, base - h), (cx + w / 2, base)], fill=fill)
        return
    n = max(3, int(h / (9 * S)))
    left, right = [], []
    for i in range(n):
        f = i / n
        y = base - h * f
        hw = w / 2 * (1 - f)
        left += [(cx - hw, y), (cx - hw * 0.72, y - h / n * 0.55)]
        right += [(cx + hw, y), (cx + hw * 0.72, y - h / n * 0.55)]
    d.polygon(left + [(cx, base - h)] + list(reversed([(x, y) for x, y in right])), fill=fill)


def spruce_ridge(d, rng, base, tall, fill, step=(26, 46), tiers=True):
    """A row of spruces along the screen, filled down to the bottom."""
    d.rectangle([0, base, W, H], fill=fill)
    x = -40.0
    while x < W + 40:
        w = rng.uniform(*step) * S
        spruce(d, x + w / 2, base + 1, w, tall * rng.uniform(0.55, 1.0), fill, tiers)
        x += w * rng.uniform(0.55, 0.85)


def blobs(d, rng, cx, cy, rx, ry, r, n, fill):
    """A crown of leaves: many round blobs inside an ellipse."""
    for _ in range(n):
        t = rng.uniform(0, 2 * math.pi)
        k = math.sqrt(rng.uniform(0, 1))
        x = cx + math.cos(t) * rx * k
        y = cy + math.sin(t) * ry * k
        rr = r * rng.uniform(0.6, 1.0)
        d.ellipse([x - rr, y - rr * 0.85, x + rr, y + rr * 0.85], fill=fill)


def stroke(d, pts, r0, r1, fill):
    """A line growing thinner from r0 to r1, drawn as overlapping discs."""
    steps = []
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) / 2))
        steps += [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n)]
    steps.append(pts[-1])
    for i, (x, y) in enumerate(steps):
        r = r0 + (r1 - r0) * i / max(1, len(steps) - 1)
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def bezier(p0, p1, p2, n=40):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / n for i in range(n + 1))]


def scots_pine(d, rng, x, foot, top, size, fill):
    """A Scots pine: a tall bare trunk, its crown in a few flat clumps."""
    lean = rng.uniform(-0.04, 0.04) * (foot - top)
    stroke(d, bezier((x, foot), (x - lean, (foot + top) / 2), (x + lean, top)), 5 * size, 2.5 * size, fill)
    for k in range(4):
        side = -1 if k % 2 else 1
        cy = top + (k - 0.6) * 22 * size
        cx = x + lean + side * rng.uniform(8, 36) * size
        stroke(d, [(x + lean, cy + 16 * size), (cx, cy)], 2.2 * size, 1.5 * size, fill)
        blobs(d, rng, cx, cy, (40 - 6 * k) * size, 9 * size, 11 * size, 16, fill)


def hills(d, base, amp, waves, fill, phase=0.0, y_end=H):
    pts = [(0, y_end)]
    for x in range(0, W + 1, 4):
        y = base
        for k, (a, f) in enumerate(waves):
            y -= amp * a * math.sin(x / W * math.pi * f + phase + k * 1.7)
        pts.append((x, y))
    pts.append((W, y_end))
    d.polygon(pts, fill=fill)


def caption(img, text, y, fill, size=21):
    """The place's name in small, widely spaced capitals."""
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(str(FONT), size * S)
    try:
        font.set_variation_by_axes([12, 500])
    except OSError:
        pass
    text = text.upper()
    gap = 0.22 * size * S
    widths = [d.textlength(c, font=font) for c in text]
    total = sum(widths) + gap * (len(text) - 1)
    x = (W - total) / 2
    for c, w in zip(text, widths):
        d.text((x, y), c, font=font, fill=fill, anchor="ls")
        x += w + gap


# --- The scenes -----------------------------------------------------------


def pine_forest(rng):
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    disc(d, 290 * S, 380 * S, 58 * S, 200)
    layers = [(490, 80, 216), (570, 110, 150), (650, 140, 92)]
    for base, tall, fill in layers:
        spruce_ridge(d, rng, base * S, tall * S, fill)
    spruce_ridge(d, rng, 730 * S, 190 * S, 20, step=(42, 70))
    # Two tall pines close by, their crowns high on bare trunks.
    scots_pine(d, rng, 78 * S, H, 150 * S, 1.3 * S, 20)
    scots_pine(d, rng, 402 * S, H, 230 * S, 1.1 * S, 20)
    caption(img, "Pušynas", 772 * S, 255)
    return img


def dune(d, base, peak_x, peak_y, left_y, right_y, lit, shade, light_from_left=True):
    """A dune with a soft windward side and a steep slip face in shade."""
    prof = []
    for x in range(-20, W + 21, 4):
        if x <= peak_x:
            t = (x + 20) / (peak_x + 20)
            y = left_y + (peak_y - left_y) * (1 - (1 - t) ** 2)
        else:
            t = (x - peak_x) / (W + 20 - peak_x)
            y = peak_y + (right_y - peak_y) * (1 - (1 - t) ** 3)
        prof.append((x, y))
    d.polygon([(-20, base)] + prof + [(W + 20, base)], fill=lit)
    # The slip face: from the crest down to the right, under the profile.
    face = [(x, y) for x, y in prof if x >= peak_x]
    foot = [(peak_x + (x - peak_x) * 0.45, base) for x, _ in reversed(face)]
    edge = bezier((peak_x, peak_y), (peak_x + 70 * S, peak_y + (base - peak_y) * 0.35), (peak_x + 200 * S, base))
    d.polygon(face + [(W + 20, base)] + list(reversed(edge)), fill=shade)


def curonian_spit(rng):
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    disc(d, 370 * S, 230 * S, 24 * S, 196)
    d.rectangle([0, 420 * S, W, 470 * S], fill=160)  # the lagoon, far off
    for i in range(3):
        d.line([(0, (428 + 12 * i) * S), (W, (428 + 12 * i) * S)], fill=185, width=S)
    dune(d, 560 * S, 300 * S, 300 * S, 470 * S, 452 * S, 222, 160)
    dune(d, H, 120 * S, 540 * S, 590 * S, 610 * S, 248, 248)
    # Wind ripples on the near sand.
    for i in range(9):
        y0 = (632 + i * 18) * S
        pts = [(x, y0 + 4 * S * math.sin(x / (34 * S) + i * 0.9) - (x / W) * 20 * S) for x in range(0, W + 1, 6)]
        d.line(pts, fill=150, width=S)
    # Dune grass on the near crest.
    for _ in range(26):
        x = rng.uniform(20, 210) * S
        y = 540 * S + (abs(x - 120 * S) / (100 * S)) ** 2 * 40 * S
        for _ in range(6):
            a = math.radians(rng.uniform(-50, 25) - 90)
            ln = rng.uniform(12, 30) * S
            d.line([(x, y + 6 * S), (x + math.cos(a) * ln, y + math.sin(a) * ln)], fill=40, width=S + 1)
    for gx, gy, s in ((150, 170, 1.0), (196, 196, 0.7)):
        d.line([(gx * S - 14 * S * s, gy * S - 6 * S * s), (gx * S, gy * S), (gx * S + 14 * S * s, gy * S - 6 * S * s)],
               fill=20, width=2 * S)
    caption(img, "Kuršių nerija", 772 * S, 0)
    return img


def lake_night(rng):
    img = Image.new("L", (W, H), 255)
    gradient(img, 40, 112, 0, 470 * S)
    d = ImageDraw.Draw(img)
    disc(d, 240 * S, 190 * S, 44 * S, 250)
    for _ in range(40):
        x, y = rng.uniform(0, W), rng.uniform(0, 300 * S)
        if math.hypot(x - 240 * S, y - 190 * S) > 90 * S:
            disc(d, x, y, rng.choice((1, 1, 2)) * S, 230)
    spruce_ridge(d, rng, 470 * S, 58 * S, 12, step=(16, 30))
    # Still water, the moon's path broken into ripples.
    gradient(img, 104, 70, 472 * S, H)
    d = ImageDraw.Draw(img)
    y = 480 * S
    while y < 712 * S:
        f = (y - 470 * S) / (240 * S)
        for _ in range(rng.choice((1, 2, 2, 3))):
            hw = (6 + 34 * f) * S * rng.uniform(0.3, 1.0)
            cx = 240 * S + rng.uniform(-1, 1) * (8 + 40 * f) * S
            d.line([(cx - hw, y), (cx + hw, y)], fill=rng.choice((200, 225, 245)), width=int((1 + 2 * f) * S))
        y += (5 + 12 * f) * S * rng.uniform(0.7, 1.3)
    # Reeds in the corners.
    for side in (0, 1):
        for _ in range(26):
            bx = (rng.uniform(-10, 110) if side == 0 else rng.uniform(370, 490)) * S
            top = rng.uniform(520, 680) * S
            lean = rng.uniform(-30, 30) * S
            d.line([(bx, H), (bx + lean, top)], fill=8, width=3 * S)
            if rng.random() < 0.35:
                d.ellipse([bx + lean - 4 * S, top - 6 * S, bx + lean + 4 * S, top + 26 * S], fill=8)
    caption(img, "Ežeras", 772 * S, 255)
    return img


def standing_stork(d, x, y, s, fill):
    """A stork side on, facing right, its feet at (x, y)."""
    P = lambda dx, dy: (x + dx * s, y + dy * s)
    # Body: an egg tilted down to the tail, the long wing tips beyond it.
    body = []
    for k in range(48):
        t = 2 * math.pi * k / 48
        ex, ey = 48 * math.cos(t), 17 * math.sin(t)
        a = math.radians(-14)
        body.append(P(-14 + ex * math.cos(a) - ey * math.sin(a), -48 + ex * math.sin(a) + ey * math.cos(a)))
    d.polygon(body, fill=fill)
    d.polygon([P(-40, -44), P(-84, -24), P(-80, -20), P(-34, -30)], fill=fill)
    stroke(d, [P(24, -58), P(36, -84), P(34, -110), P(42, -132)], 10 * s, 6 * s, fill)  # neck
    disc(d, *P(44, -136), 9 * s, fill)
    d.polygon([P(48, -142), P(98, -112), P(96, -110), P(46, -130)], fill=fill)  # beak, pointing down
    d.line([P(0, -36), P(-4, 0)], fill=fill, width=int(3 * s))
    d.line([P(8, -36), P(12, 0)], fill=fill, width=int(3 * s))


def stork(rng):
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    disc(d, 110 * S, 190 * S, 40 * S, 200)
    # Meadow and the line of trees beyond it.
    spruce_ridge(d, rng, 640 * S, 34 * S, 128, step=(12, 26), tiers=False)
    d.rectangle([0, 640 * S, W, H], fill=200)
    for i in range(12):
        y = 652 * S + i * i * 1.2 * S
        d.line([(0, y), (W, y)], fill=184, width=S)
    # The pole and the nest on it.
    px, nest_y = 344 * S, 390 * S
    d.polygon([(px - 6 * S, H), (px - 4 * S, nest_y), (px + 4 * S, nest_y), (px + 6 * S, H)], fill=30)
    d.ellipse([px - 60 * S, nest_y + 10 * S, px + 60 * S, nest_y + 34 * S], fill=30)
    d.rectangle([px - 60 * S, nest_y - 6 * S, px + 60 * S, nest_y + 22 * S], fill=30)
    d.ellipse([px - 64 * S, nest_y - 16 * S, px + 64 * S, nest_y + 6 * S], fill=30)
    for _ in range(46):
        a = rng.uniform(-0.6, 0.6) + (math.pi if rng.random() < 0.5 else 0)
        x0 = px + rng.uniform(-40, 40) * S
        y0 = nest_y + rng.uniform(-12, 12) * S
        ln = rng.uniform(30, 52) * S
        d.line([(x0, y0), (x0 + ln * math.cos(a), y0 + ln * math.sin(a) * 0.5)], fill=30, width=2 * S)
    standing_stork(d, px - 6 * S, nest_y - 6 * S, 1.2 * S, 30)
    caption(img, "Gandras", 772 * S, 0)
    return img


def birches(rng):
    img = Image.new("L", (W, H), 255)
    gradient(img, 150, 196, 0, 640 * S)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 640 * S, W, H], fill=236)

    def trunk(x, w, foot, tone, marks):
        d.polygon([(x - w * 0.42, -10), (x + w * 0.42, -10), (x + w / 2, foot), (x - w / 2, foot)], fill=tone)
        y = 0.0
        while y < foot:
            y += rng.uniform(14, 46) * S
            if y > foot:
                break
            near_foot = y / foot
            ln = w * rng.uniform(0.25, 0.7 + 0.3 * near_foot)
            left = rng.random() < 0.5
            x0 = x - w / 2 + 1 if left else x + w / 2 - ln - 1
            th = rng.uniform(2, 4 + 6 * near_foot) * S
            d.polygon([(x0, y), (x0 + ln, y + th * 0.3), (x0 + ln * 0.85, y + th), (x0, y + th * 0.7)], fill=marks)
        d.rectangle([x - w / 2, foot - 30 * S * (w / (60 * S)), x + w / 2, foot], fill=marks)

    # Far trunks pale, near ones white with black bark marks.
    for x, w in ((40, 10), (150, 8), (230, 12), (330, 9), (450, 11)):
        trunk(x * S, w * S, 600 * S, 222, 150)
    for x, w in ((96, 22), (276, 18), (402, 26)):
        trunk(x * S, w * S, 650 * S, 250, 70)
    for x, w in ((20, 56), (190, 44), (470, 64)):
        trunk(x * S, w * S, 740 * S, 255, 20)
    for _ in range(60):
        x = rng.uniform(0, W)
        y = rng.uniform(650, 760) * S
        d.line([(x, y), (x + rng.uniform(-6, 6) * S, y - rng.uniform(8, 20) * S)], fill=150, width=S + 1)
    caption(img, "Beržynas", 772 * S, 0)
    return img


def meadow_oak(rng):
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    hills(d, 520 * S, 18 * S, [(1.0, 1.3), (0.5, 3.0)], 220, phase=1.0)
    hills(d, 570 * S, 30 * S, [(1.0, 1.0), (0.3, 2.4)], 178, phase=3.0)
    hills(d, 640 * S, 34 * S, [(1.0, 0.8), (0.25, 2.0)], 255, phase=0.2)
    # The old oak on the near hill: a thick trunk, a wide round crown.
    ox, oy = 290 * S, 618 * S
    d.polygon([(ox - 26 * S, oy + 6 * S), (ox - 14 * S, oy - 90 * S), (ox + 14 * S, oy - 90 * S), (ox + 28 * S, oy + 6 * S)],
              fill=20)
    for a, ln in ((-1.1, 110), (-0.5, 90), (0.2, 80), (0.8, 100), (1.2, 120)):
        stroke(d, [(ox, oy - 80 * S), (ox + math.sin(a) * ln * S, oy - 80 * S - math.cos(a) * ln * S * 0.6)],
               9 * S, 4 * S, 20)
    blobs(d, rng, ox, oy - 196 * S, 160 * S, 80 * S, 24 * S, 160, 20)
    blobs(d, rng, ox - 70 * S, oy - 150 * S, 80 * S, 34 * S, 28 * S, 20, 20)
    blobs(d, rng, ox + 80 * S, oy - 146 * S, 76 * S, 30 * S, 28 * S, 20, 20)
    # Haystacks on the far slope.
    for hx, hy, s in ((80, 566, 1.0), (112, 572, 0.8), (430, 598, 0.9)):
        d.chord([(hx - 16 * s) * S, (hy - 22 * s) * S, (hx + 16 * s) * S, (hy + 14 * s) * S], 180, 360, fill=20)
        d.line([(hx * S, (hy - 22 * s) * S), (hx * S, (hy - 30 * s) * S)], fill=20, width=2 * S)
    for gx, gy in ((390, 130), (412, 142), (370, 146)):
        d.line([(gx * S - 8 * S, gy * S - 4 * S), (gx * S, gy * S), (gx * S + 8 * S, gy * S - 4 * S)], fill=20, width=2 * S)
    caption(img, "Ąžuolas", 772 * S, 0)
    return img


def bog(rng):
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    disc(d, 340 * S, 400 * S, 34 * S, 196)
    spruce_ridge(d, rng, 472 * S, 22 * S, 150, step=(10, 20), tiers=False)
    d.rectangle([0, 470 * S, W, H], fill=200)
    # Pools catching the sky.
    for cx, cy, rx, ry in ((390, 506, 70, 8), (90, 540, 90, 12), (400, 600, 110, 20), (390, 700, 90, 24)):
        d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], fill=238)
    # Small crooked pines.
    for x, y, h in ((40, 500, 34), (170, 482, 16), (300, 488, 22), (450, 530, 46), (300, 560, 60)):
        scots_pine(d, rng, x * S, y * S, y * S - h * S, h / 70 * S, 30)
    # The boardwalk, bending away into the distance.
    path = bezier((110 * S, H + 20 * S), (120 * S, 560 * S), (232 * S, 474 * S), 400)
    i = 0
    while i < len(path) - 1:
        x, y = path[i]
        f = (y - 470 * S) / (H - 470 * S)
        hw = 70 * S * f + 2 * S
        th = max(S, 14 * S * f)
        d.polygon([(x - hw, y), (x + hw, y), (x + hw, y - th), (x - hw, y - th)], fill=20)
        gap = th + max(1.0, 4 * S * f)
        while i < len(path) - 1 and path[i][1] > y - gap:
            i += 1
    caption(img, "Pelkė", 772 * S, 0)
    return img


def winter(rng):
    img = Image.new("L", (W, H), 255)
    gradient(img, 206, 206, 0, 600 * S)
    d = ImageDraw.Draw(img)
    hills(d, 600 * S, 24 * S, [(1.0, 1.2), (0.3, 3.4)], 252, phase=0.5)
    # Firs with snow lying on each tier.
    for x, base, h in ((90, 640, 300), (230, 600, 190), (330, 620, 240), (430, 660, 360)):
        x, base, h = x * S, base * S, h * S
        w = h * 0.42
        tiers = 7
        for k in range(tiers):
            f0 = k / tiers
            y0 = base - h * f0 * 0.92
            hw = w * (1 - f0)
            top = y0 - h * 0.24
            d.polygon([(x - hw, y0), (x, top), (x + hw, y0)], fill=26)
            d.polygon([(x - hw * 0.8, y0 - h * 0.02), (x, top + h * 0.05), (x + hw * 0.5, y0 - h * 0.05),
                       (x - hw * 0.2, y0 - h * 0.01)], fill=250)
        d.rectangle([x - 6 * S, base - 4 * S, x + 6 * S, base + 16 * S], fill=26)
    d.rectangle([0, 660 * S, W, H], fill=252)
    hills(d, 664 * S, 10 * S, [(1.0, 2.0)], 252)
    for _ in range(140):
        x, y = rng.uniform(0, W), rng.uniform(0, 720 * S)
        disc(d, x, y, rng.choice((1.5, 2, 3)) * S, 255)
    caption(img, "Žiema", 772 * S, 0)
    return img


SCENES = [
    ("1-pusynas", pine_forest),
    ("2-kursiu-nerija", curonian_spit),
    ("3-ezeras", lake_night),
    ("4-gandras", stork),
    ("5-berzynas", birches),
    ("6-azuolas", meadow_oak),
    ("7-pelke", bog),
    ("8-ziema", winter),
]


# Atkinson dithering drops a quarter of each pixel's error, so on the reader
# greys above about 220 come out pure white and below about 36 pure black.
# Near-white and near-black stay solid; every grey between is spread over
# the range that still shows as dots.
TONES = [0 if v <= 40 else 255 if v >= 244 else round(46 + (v - 40) * (214 - 46) / (244 - 40)) for v in range(256)]


def atkinson(img):
    """Black and white as the reader draws it (ui::draw_picture)."""
    w, h = img.size
    px = [[img.getpixel((x, y)) for x in range(w)] for y in range(h)]
    out = Image.new("1", (w, h), 1)
    o = out.load()
    for y in range(h):
        row = px[y]
        for x in range(w):
            old = row[x]
            new = 255 if old >= 128 else 0
            o[x, y] = 1 if new else 0
            e = int((old - new) / 8)  # as C++ divides
            for dx, dy in ((1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and yy < h:
                    px[yy][xx] += e
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(root / "assets/sleep"))
    ap.add_argument("--preview", help="also write dithered previews here")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, scene in SCENES:
        img = scene(random.Random(name)).resize((W // S, H // S), Image.LANCZOS).point(TONES)
        img.save(out / f"{name}.png", optimize=True)
        if args.preview:
            pathlib.Path(args.preview).mkdir(parents=True, exist_ok=True)
            atkinson(img).convert("L").save(pathlib.Path(args.preview) / f"{name}.png", optimize=True)
        print(f"{name}.png {(out / f'{name}.png').stat().st_size} bytes")


if __name__ == "__main__":
    main()
