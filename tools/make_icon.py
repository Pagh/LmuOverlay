"""Draws the LMU Overlay icon (dark rounded square, red gauge arc, white needle) as a multi-size .ico."""
import math
import sys
from PIL import Image, ImageDraw

OUT = sys.argv[1]
S = 1024  # draw big, downsample for clean edges


def draw(size):
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = S * 0.04
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.2, fill=(21, 24, 30, 255))
    cx, cy, r = S / 2, S * 0.60, S * 0.34
    w = int(S * (0.11 if size >= 32 else 0.15))
    box = [cx - r, cy - r, cx + r, cy + r]
    # Gauge: grey track, red arc for the "fast" part.
    d.arc(box, start=150, end=390, fill=(70, 78, 92, 255), width=w)
    d.arc(box, start=300, end=390, fill=(229, 50, 45, 255), width=w)
    # Needle pointing into the red.
    ang = math.radians(318)
    nx, ny = cx + math.cos(ang) * r * 0.95, cy + math.sin(ang) * r * 0.95
    d.line([cx, cy, nx, ny], fill=(242, 244, 247, 255), width=int(S * (0.06 if size >= 32 else 0.09)))
    hub = S * (0.075 if size >= 32 else 0.1)
    d.ellipse([cx - hub, cy - hub, cx + hub, cy + hub], fill=(242, 244, 247, 255))
    return img.resize((size, size), Image.LANCZOS)


sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
images = [draw(s) for s in sizes]
images[-1].save(OUT, format='ICO', sizes=[(s, s) for s in sizes], append_images=images[:-1])
images[-1].save(OUT.replace('.ico', '.png'))
print('ok')
