#!/usr/bin/env python3
"""Contact sheet from a harness .raw dump at given times (seconds).

    python3 sheet.py out/s09.raw 25 sheet.png 0.2 0.8 1.2 1.8
"""
import sys
from PIL import Image

W, H = 128, 64
raw, fps, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
times = [float(t) for t in sys.argv[4:]]
data = open(raw, "rb").read()
n = len(data) // (W * H)
ink, lit = (20, 12, 4), (255, 140, 40)
pal = [c for v in range(256) for c in (ink[i] + (lit[i] - ink[i]) * v // 255 for i in range(3))]
tiles = []
for t in times:
    k = min(int(t * fps), n - 1)
    im = Image.frombytes("P", (W, H), data[k * W * H:(k + 1) * W * H])
    im.putpalette(pal)
    tiles.append(im.convert("RGB").resize((W * 2, H * 2), Image.NEAREST))
cols = 2
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (W * 2 + 4), rows * (H * 2 + 4)))
for i, im in enumerate(tiles):
    sheet.paste(im, ((i % cols) * (W * 2 + 4), (i // cols) * (H * 2 + 4)))
sheet.save(out)
