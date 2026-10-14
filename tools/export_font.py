"""Export the Titus the Fox font (FONTS.SQZ) to PNG + JSON.

FONTS.SQZ: 41 glyphs x 48 bytes; each glyph is 8x12 pixels as 4 sequential planes of 12 bytes.
Glyph order: 0-9 ! ? . $ _ A-Z. Glyphs are drawn opaque (background is colour 1).
The PNGs use the palette the game sets before drawing text (DS:04C4, image 0x58A), so the
letters come out yellow on black as on screen.

Character mapping, from the game's text routine (image 0x756):
  ' '         -> solid colour-0 cell (no glyph)
  other char  -> glyph = ch - 0x30, minus 2 more if that is > 9
so '0'-'9' -> 0-9, 'A'-'Z' -> 15-40, and the game's strings use '<' '=' '>' '?' '@'
to reach glyphs 10-14 (! ? . $ _).
"""
import argparse
import json
import os

from foxexe import FoxExe, to_rgb8
from gfx import planar_to_chunky, write_png
from sqz import load

GLYPH_W, GLYPH_H, GLYPH_BYTES = 8, 12, 48
GLYPH_CHARS = "0123456789!?.$_ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def glyph_for(ch):
    """Glyph index the game draws for a character, or None for the blank space cell."""
    if ch == " ":
        return None
    g = ord(ch) - 0x30
    return g - 2 if g > 9 else g


def render_text(glyphs, text):
    width = GLYPH_W * len(text)
    img = bytearray(width * GLYPH_H)
    for i, ch in enumerate(text):
        g = glyph_for(ch)
        if g is None or not 0 <= g < len(glyphs):
            continue
        for y in range(GLYPH_H):
            row = y * width + i * GLYPH_W
            img[row:row + GLYPH_W] = glyphs[g][y * GLYPH_W:(y + 1) * GLYPH_W]
    return width, img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "font"))
    args = ap.parse_args()

    fox = FoxExe.from_game_dir(args.game)
    palette = to_rgb8(fox.text_palette())
    data = load(os.path.join(args.game, "fonts.sqz"))
    if len(data) != len(GLYPH_CHARS) * GLYPH_BYTES:
        raise ValueError(f"unexpected FONTS.SQZ size {len(data)}")
    glyphs = [planar_to_chunky(data[i * GLYPH_BYTES:(i + 1) * GLYPH_BYTES], GLYPH_W, GLYPH_H)
              for i in range(len(GLYPH_CHARS))]

    os.makedirs(os.path.join(args.out, "glyphs"), exist_ok=True)
    for i, px in enumerate(glyphs):
        write_png(os.path.join(args.out, "glyphs", f"glyph_{i:02d}.png"), GLYPH_W, GLYPH_H, px, palette)

    width, sheet = render_text(glyphs, "".join(chr(0x30 + (i if i < 10 else i + 2))
                                               for i in range(len(glyphs))))
    write_png(os.path.join(args.out, "font_sheet.png"), width, GLYPH_H, sheet, palette)

    # Sample: every playable level name, rendered exactly as the game would.
    names = [lv["name"] for lv in sorted((l for l in fox.levels() if l["play_number"]),
                                         key=lambda l: l["play_number"])]
    lines = [render_text(glyphs, n) for n in names]
    sw = max(w for w, _ in lines)
    sample = bytearray()
    for w, img in lines:
        for y in range(GLYPH_H):
            sample += img[y * w:(y + 1) * w] + bytes(sw - w)
    write_png(os.path.join(args.out, "sample_level_names.png"), sw, GLYPH_H * len(lines), sample, palette)

    ascii_map = {chr(c): glyph_for(chr(c)) for c in range(0x20, 0x5B)
                 if glyph_for(chr(c)) is None or 0 <= glyph_for(chr(c)) < len(glyphs)}
    with open(os.path.join(args.out, "font.json"), "w") as f:
        json.dump({"glyph_width": GLYPH_W, "glyph_height": GLYPH_H, "bytes_per_glyph": GLYPH_BYTES,
                   "format": "4 sequential bit-planes of 12 bytes, plane 0 = bit 0, drawn opaque",
                   "glyphs": [{"index": i, "char": c, "file": f"glyphs/glyph_{i:02d}.png"}
                              for i, c in enumerate(GLYPH_CHARS)],
                   "game_char_to_glyph": ascii_map}, f, indent=1)
    print(f"{len(glyphs)} glyphs -> {args.out}")


if __name__ == "__main__":
    main()
