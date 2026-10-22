#!/usr/bin/env python3
"""Turn a host harness .raw dump (128x64 8-bit frames) into an upscaled GIF
tinted like the Flipper's backlit LCD."""
import sys
from PIL import Image

W, H, SCALE = 128, 64, 4
raw_path, gif_path, fps = sys.argv[1], sys.argv[2], float(sys.argv[3])
data = open(raw_path, "rb").read()
n = len(data) // (W * H)
ink, lit = (20, 12, 4), (255, 140, 40)
lut = [tuple(ink[c] + (lit[c] - ink[c]) * v // 255 for c in range(3)) for v in range(256)]
pal = [c for rgb in lut for c in rgb]
frames = []
for i in range(n):
    img = Image.frombytes("P", (W, H), data[i * W * H:(i + 1) * W * H])
    img.putpalette(pal)
    frames.append(img.resize((W * SCALE, H * SCALE), Image.NEAREST))
frames[0].save(gif_path, save_all=True, append_images=frames[1:],
               duration=int(1000 / fps), loop=0, optimize=False)
print(f"{gif_path}: {n} frames")
