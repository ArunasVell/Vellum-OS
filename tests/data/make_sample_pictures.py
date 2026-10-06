#!/usr/bin/env python3
"""Writes the small pictures in tests/data/sleep/ that the host tests use as
the reader's own sleep screen pictures. Needs Pillow.

dawn.jpg   360x600 portrait test card: white top, grey band, black bottom
           (smaller than the screen: kept at its own size).
field.png  900x500 landscape: black thirds left and right, white middle
           (cut to the screen's shape, only the white middle stays).
night.bmp  120x200, 24 bits, dark grey all over.
"""
import pathlib

from PIL import Image, ImageDraw

out = pathlib.Path(__file__).resolve().parent / "sleep"
out.mkdir(exist_ok=True)

w, h = 360, 600
img = Image.new("L", (w, h), 255)
d = ImageDraw.Draw(img)
d.rectangle([0, h // 2, w, h], fill=0)
d.rectangle([0, h * 5 // 12, w, h // 2], fill=128)
img.convert("RGB").save(out / "dawn.jpg", "JPEG", quality=85)

img = Image.new("RGB", (900, 500), (255, 255, 255))
d = ImageDraw.Draw(img)
d.rectangle([0, 0, 299, 500], fill=(0, 0, 0))
d.rectangle([600, 0, 900, 500], fill=(0, 0, 0))
img.save(out / "field.png", "PNG")

Image.new("RGB", (120, 200), (40, 40, 40)).save(out / "night.bmp", "BMP")
print("wrote", out)
