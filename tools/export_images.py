"""Export Titus the Fox full-screen pictures (VGA and EGA) to PNG.

  66304 bytes -> VGA: 768-byte 6-bit palette + 320x200 8bpp (+1536 trailing bytes)
  32000 bytes -> EGA: 320x200, 4 sequential bit-planes, standard EGA palette
"""
import argparse
import glob
import os

from gfx import EGA_PALETTE, planar_to_chunky, vga_palette, write_png
from sqz import load

W, H = 320, 200
VGA_SIZE = 768 + W * H + 1536
EGA_SIZE = W * H // 2
NOT_PICTURES = {"fonts", "sprexp"}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "images"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    for path in sorted(glob.glob(os.path.join(args.game, "*.sqz"))):
        name = os.path.splitext(os.path.basename(path))[0].lower()
        if name in NOT_PICTURES:
            continue
        data = load(path)
        out = os.path.join(args.out, f"{name}.png")
        if len(data) == VGA_SIZE:
            write_png(out, W, H, data[768:768 + W * H], vga_palette(data[:768]))
            print(f"{name}: VGA 320x200 -> {out}")
        elif len(data) == EGA_SIZE:
            write_png(out, W, H, planar_to_chunky(data, W, H), EGA_PALETTE)
            print(f"{name}: EGA 320x200 -> {out}")


if __name__ == "__main__":
    main()
