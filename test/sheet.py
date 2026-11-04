#!/usr/bin/env python3
"""Contact sheet of film frames: sheet.py OUT.png frame1.png frame2.png ... (needs Pillow)."""
import sys
from PIL import Image

out, files = sys.argv[1], sys.argv[2:]
cols = 3
w, h = 264, 160            # half size of the 528x320 frames
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * w + (cols - 1) * 4, rows * h + (rows - 1) * 4), (255, 255, 255))
for i, f in enumerate(files):
    im = Image.open(f).resize((w, h))
    sheet.paste(im, ((i % cols) * (w + 4), (i // cols) * (h + 4)))
sheet.save(out)
print("wrote", out)
