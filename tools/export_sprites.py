"""Export Titus the Fox sprites from SPREXP.SQZ to PNG.

SPREXP.SQZ is a plain concatenation of sprites with no headers. Each sprite is 4 sequential
bit-planes of (width/8)*height bytes; colour 0 is transparent. The sizes come from the
(width, height) table at DS:6665 in the unpacked FOX.COM (see foxexe.py).
Colour 14 differs per level, so the palette of one chosen level is applied (default: level 1).
"""
import argparse
import json
import os

from foxexe import LEVEL_COLOUR_INDEX, FoxExe, to_rgb8
from gfx import planar_to_chunky, write_png
from sqz import load

SHEET_COLUMNS = 16
SHEET_BG = (96, 96, 96)


def split_sprites(data, sizes):
    sprites, offset = [], 0
    for w, h in sizes:
        n = w * h // 2
        sprites.append({"width": w, "height": h, "offset": offset,
                        "pixels": planar_to_chunky(data[offset:offset + n], w, h)})
        offset += n
    if offset != len(data):
        raise ValueError(f"size table covers {offset} bytes, SPREXP has {len(data)}")
    return sprites


def contact_sheet(sprites):
    """Fixed grid so a sprite's index = row * SHEET_COLUMNS + column; index 16 = background."""
    cell_w = max(s["width"] for s in sprites) + 4
    cell_h = max(s["height"] for s in sprites) + 4
    rows = -(-len(sprites) // SHEET_COLUMNS)
    width, height = SHEET_COLUMNS * cell_w, rows * cell_h
    img = bytearray([16]) * (width * height)
    for i, s in enumerate(sprites):
        x0 = (i % SHEET_COLUMNS) * cell_w + 2
        y0 = (i // SHEET_COLUMNS) * cell_h + 2
        for y in range(s["height"]):
            for x in range(s["width"]):
                c = s["pixels"][y * s["width"] + x]
                if c:
                    img[(y0 + y) * width + x0 + x] = c
    return width, height, img, cell_w, cell_h


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "sprites"))
    ap.add_argument("--level", type=int, default=1,
                    help="play number (1-15) whose palette is used for colour 14 (default 1)")
    args = ap.parse_args()

    fox = FoxExe.from_game_dir(args.game)
    level = next((lv for lv in fox.levels() if lv["play_number"] == args.level), None)
    if level is None:
        ap.error(f"no level with play number {args.level}")
    palette = to_rgb8(level["palette"])

    sprites = split_sprites(load(os.path.join(args.game, "sprexp.sqz")), fox.sprite_sizes())
    os.makedirs(args.out, exist_ok=True)
    for i, s in enumerate(sprites):
        write_png(os.path.join(args.out, f"sprite_{i:03d}.png"), s["width"], s["height"],
                  s["pixels"], palette, transparent=0)

    width, height, sheet, cell_w, cell_h = contact_sheet(sprites)
    write_png(os.path.join(args.out, "sheet.png"), width, height, sheet, palette + [SHEET_BG])

    uses_level_colour = [i for i, s in enumerate(sprites) if LEVEL_COLOUR_INDEX in s["pixels"]]
    index = {"palette_level": {"play_number": args.level, "name": level["name"]},
             "sheet": {"columns": SHEET_COLUMNS, "cell_width": cell_w, "cell_height": cell_h},
             "uses_level_colour_14": uses_level_colour,
             "sprites": [{"index": i, "file": f"sprite_{i:03d}.png", "width": s["width"],
                          "height": s["height"], "offset": s["offset"]}
                         for i, s in enumerate(sprites)]}
    with open(os.path.join(args.out, "sprites.json"), "w") as f:
        json.dump(index, f, indent=1)
    print(f"{len(sprites)} sprites -> {args.out} (palette of level {args.level}: {level['name']}); "
          f"{len(uses_level_colour)} use the per-level colour 14")


if __name__ == "__main__":
    main()
