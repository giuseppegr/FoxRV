"""Export Titus the Fox levels: tileset sheet, full map render and metadata JSON.

Level layout (decompressed):  size = rows*256 + 35828
  0               rows*256   tile map, 256 tiles wide
  rows*256        32768      tileset, 256 tiles x 128 bytes (4 planes x 16 rows x 2 bytes)
  rows*256+32768  3060       metadata tail, see TAIL_* offsets below

Field meanings marked "tentative" are inferred from cross-level comparison only.
"""
import argparse
import glob
import json
import os
import struct

from foxexe import FoxExe, to_rgb8
from gfx import EGA_PALETTE, planar_to_chunky, write_png
from sqz import load

FIXED_SIZE = 35828
MAP_WIDTH = 256
TILE = 16

# Offsets inside the 3060-byte metadata tail.
TAIL_FLAGS = 0x000        # 3 x 256: horizontal(wall), floor, ceiling flag per tile
TAIL_OBJECTS = 0x300      # 40 x 6
TAIL_UNKNOWN_A = 0x3F0    # u16, varies per level (tentative)
TAIL_START = 0x3F2        # u16 x, u16 y (pixels)
TAIL_ENEMIES = 0x3F6      # 50 x 26
TAIL_BONUSES = 0x90A      # 100 x 4
TAIL_UNKNOWN_B = 0xA9A    # u16, 0x00EC in most levels (tentative)
TAIL_GATES = 0xA9C        # 20 x 7
TAIL_ELEVATORS = 0xB28    # 10 x 20
TAIL_FINISH = 0xBF0       # u16 x, u16 y (pixels)

# Extra palette entries (beyond the 16 tile colours) used for overlay markers.
MARKERS = {"start": (0, 255, 0), "finish": (255, 0, 0), "enemy": (255, 0, 255),
           "object": (255, 255, 0), "elevator": (0, 255, 255), "gate": (255, 128, 0)}
MARKER_INDEX = {name: 16 + i for i, name in enumerate(MARKERS)}


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def split_level(data):
    rows, rem = divmod(len(data) - FIXED_SIZE, MAP_WIDTH)
    if rem or rows <= 0:
        raise ValueError(f"not a level: {len(data)} bytes")
    tiles_off = rows * MAP_WIDTH
    return rows, data[:tiles_off], data[tiles_off:tiles_off + 32768], data[tiles_off + 32768:]


def decode_tiles(tileset):
    return [planar_to_chunky(tileset[i * 128:(i + 1) * 128], TILE, TILE) for i in range(256)]


def parse_tail(tail):
    def unused(rec):
        return all(b == 0xFF for b in rec)

    meta = {
        "flags": {
            "horizontal": list(tail[TAIL_FLAGS:TAIL_FLAGS + 256]),
            "floor": list(tail[TAIL_FLAGS + 256:TAIL_FLAGS + 512]),
            "ceiling": list(tail[TAIL_FLAGS + 512:TAIL_FLAGS + 768]),
        },
        "unknown_a": u16(tail, TAIL_UNKNOWN_A),
        "start": {"x": u16(tail, TAIL_START), "y": u16(tail, TAIL_START + 2)},
        "unknown_b": u16(tail, TAIL_UNKNOWN_B),
        "finish": {"x": u16(tail, TAIL_FINISH), "y": u16(tail, TAIL_FINISH + 2)},
        "objects": [], "enemies": [], "bonuses": [], "gates": [], "elevators": [],
    }
    for i in range(40):
        rec = tail[TAIL_OBJECTS + i * 6:TAIL_OBJECTS + i * 6 + 6]
        if not unused(rec):
            word = u16(rec, 0)
            meta["objects"].append({"index": i, "sprite": word & 0x1FFF, "flags": word >> 13,
                                    "x": u16(rec, 2), "y": u16(rec, 4)})
    for i in range(50):
        rec = tail[TAIL_ENEMIES + i * 26:TAIL_ENEMIES + i * 26 + 26]
        if u16(rec, 0) != 0xFFFF:
            # fully decoded (behaviour parameters, animation) by export_entities.py
            meta["enemies"].append({"index": i, "x": u16(rec, 0), "y": u16(rec, 2),
                                    "sprite_key": u16(rec, 4) & 0x1FFF,
                                    "type": u16(rec, 6) & 0x1FFF, "raw": rec.hex(" ")})
    for i in range(100):
        rec = tail[TAIL_BONUSES + i * 4:TAIL_BONUSES + i * 4 + 4]
        if not unused(rec):
            meta["bonuses"].append({"index": i, "bonus_tile": rec[0], "replace_tile": rec[1],
                                    "tile_x": rec[2], "tile_y": rec[3]})
    for i in range(20):
        rec = tail[TAIL_GATES + i * 7:TAIL_GATES + i * 7 + 7]
        if not unused(rec):
            meta["gates"].append({"index": i, "entrance": [rec[0], rec[1]],
                                  "screen": [rec[2], rec[3]], "exit": [rec[4], rec[5]],
                                  "noscroll": rec[6]})
    for i in range(10):
        rec = tail[TAIL_ELEVATORS + i * 20:TAIL_ELEVATORS + i * 20 + 20]
        if u16(rec, 0) != 0xFFFF:
            meta["elevators"].append({"index": i, "x": u16(rec, 0), "y": u16(rec, 2),
                                      "sprite_word": u16(rec, 4), "range": u16(rec, 10),
                                      "init_x": u16(rec, 12), "init_y": u16(rec, 14),
                                      "raw": rec.hex(" ")})
    return meta


def tileset_sheet(tiles):
    img = bytearray(256 * 256)
    for t, px in enumerate(tiles):
        tx, ty = (t % 16) * TILE, (t // 16) * TILE
        for y in range(TILE):
            row = (ty + y) * 256 + tx
            img[row:row + TILE] = px[y * TILE:(y + 1) * TILE]
    return img


def render_map(rows, tilemap, tiles):
    width = MAP_WIDTH * TILE
    img = bytearray(width * rows * TILE)
    for my in range(rows):
        for mx in range(MAP_WIDTH):
            px = tiles[tilemap[my * MAP_WIDTH + mx]]
            for y in range(TILE):
                row = (my * TILE + y) * width + mx * TILE
                img[row:row + TILE] = px[y * TILE:(y + 1) * TILE]
    return img


def draw_box(img, width, height, x, y, size, colour):
    x0, y0 = max(0, x - size // 2), max(0, y - size)
    x1, y1 = min(width - 1, x + size // 2), min(height - 1, y)
    if x0 > x1 or y0 > y1:
        return
    for xx in range(x0, x1 + 1):
        img[y0 * width + xx] = img[y1 * width + xx] = colour
    for yy in range(y0, y1 + 1):
        img[yy * width + x0] = img[yy * width + x1] = colour


def draw_markers(img, rows, meta):
    width, height = MAP_WIDTH * TILE, rows * TILE
    for o in meta["objects"]:
        draw_box(img, width, height, o["x"], o["y"], 12, MARKER_INDEX["object"])
    for e in meta["enemies"]:
        draw_box(img, width, height, e["x"], e["y"], 20, MARKER_INDEX["enemy"])
    for e in meta["elevators"]:
        draw_box(img, width, height, e["x"], e["y"], 16, MARKER_INDEX["elevator"])
    for g in meta["gates"]:
        ex, ey = g["entrance"]
        draw_box(img, width, height, ex * TILE + 8, ey * TILE + 15, 14, MARKER_INDEX["gate"])
    for name in ("start", "finish"):
        p = meta[name]
        for size in (26, 28):
            draw_box(img, width, height, p["x"], p["y"], size, MARKER_INDEX[name])


def export_level(path, out_dir, palette, markers, info=None):
    name = os.path.splitext(os.path.basename(path))[0].lower()
    rows, tilemap, tileset, tail = split_level(load(path))
    tiles = decode_tiles(tileset)
    meta = parse_tail(tail)
    if info and info["rows"] != rows:
        raise ValueError(f"{name}: FOX.COM says {info['rows']} rows, file has {rows}")
    meta = {"name": name, "title": info and info["name"],
            "play_number": info and info["play_number"],
            "palette_vga6": info and [list(c) for c in info["palette"]],
            "rows": rows, "width_px": MAP_WIDTH * TILE, "height_px": rows * TILE, **meta}
    full_palette = palette + list(MARKERS.values())

    write_png(os.path.join(out_dir, f"{name}_tiles.png"), 256, 256, tileset_sheet(tiles), full_palette)
    img = render_map(rows, tilemap, tiles)
    if markers:
        draw_markers(img, rows, meta)
    write_png(os.path.join(out_dir, f"{name}_map.png"), MAP_WIDTH * TILE, rows * TILE, img, full_palette)
    with open(os.path.join(out_dir, f"{name}.json"), "w") as f:
        json.dump(meta, f, indent=1)
    with open(os.path.join(out_dir, f"{name}_tilemap.bin"), "wb") as f:
        f.write(tilemap)
    title = f" #{info['play_number']} {info['name']}" if info else ""
    print(f"{name}{title}: {rows} rows, {len(meta['objects'])} objects, {len(meta['enemies'])} enemies, "
          f"{len(meta['bonuses'])} bonuses, {len(meta['gates'])} gates, "
          f"{len(meta['elevators'])} elevators")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "levels"))
    ap.add_argument("--palette", help="JSON file with 16 [r,g,b] entries (default: the game's "
                                      "own per-level VGA palette read from FOX.COM)")
    ap.add_argument("--no-markers", action="store_true", help="don't overlay entity markers on maps")
    args = ap.parse_args()

    override = None
    if args.palette:
        with open(args.palette) as f:
            override = [tuple(c) for c in json.load(f)]
    try:
        slots = {lv["file"].lower(): lv for lv in FoxExe.from_game_dir(args.game).levels()}
    except (OSError, ValueError) as e:
        print(f"warning: can't read FOX.COM tables ({e}); using EGA palette")
        slots = {}

    os.makedirs(args.out, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(args.game, "level*.sqz"))):
        info = slots.get(os.path.basename(path).lower())
        if info is not None and info["play_number"] is None:
            continue    # e.g. LEVELA.SQZ: a 320x200 EGA picture, not a level
        if info is None and os.path.basename(path).lower() == "levela.sqz":
            continue
        palette = override or (to_rgb8(info["palette"]) if info else EGA_PALETTE)
        export_level(path, args.out, palette, not args.no_markers, info)


if __name__ == "__main__":
    main()
