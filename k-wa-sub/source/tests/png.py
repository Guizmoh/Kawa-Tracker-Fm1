#!/usr/bin/env python3
"""build/shots/*.raw (240 x 240 RGB565) -> build/shots/*.png and build/shots/sheet.png (all of them)"""
import sys
from pathlib import Path
from PIL import Image
d = Path("build/shots")
ims = []
for f in sorted(d.glob("*.raw")):
    b = f.read_bytes()
    im = Image.new("RGB", (240, 240))
    px = [((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
          for v in (b[i] | b[i + 1] << 8 for i in range(0, len(b), 2))]
    im.putdata(px)
    im.save(f.with_suffix(".png"))
    ims.append(im)
cols = int(sys.argv[1]) if len(sys.argv) > 1 else 4
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 250, rows * 250), (40, 40, 40))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % cols) * 250 + 5, (i // cols) * 250 + 5))
sheet.save(d / "sheet.png")
print(len(ims), "screens")
