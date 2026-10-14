"""Unpack FOX.COM (ZIVPack) and read the game tables it contains.

ZIVPack layout: a small stub followed by an "MZ" marker; the word-aligned data after
the marker is an ordinary SQZ (LZW) stream that decodes to the real MZ executable.

All tables below live in the game's data segment (DS = 0x68F, relative to the load image)
and are indexed by level *file* index: '0'-'9' -> 0-9, 'A'-'J' -> 10-19.
"""
import os
import struct
import sys

from sqz import unsqz

DS_SEGMENT = 0x68F
LEVEL_SLOTS = 20

OFF_DAC = 0x5057           # 16 x RGB (6-bit) VGA palette, set with INT 10h AX=1012h
OFF_TEXT_DAC = 0x04C4      # 16 x RGB (6-bit) palette of the text screens (image 0x58A)
OFF_HEIGHTS = 0x50A0       # 20 x map height in rows
OFF_LEVEL_COLOUR = 0x50C8  # 20 x RGB, copied into DAC colour 14 on level load
OFF_NAMES = 0x5104         # 20 x NUL-terminated level names
OFF_ORDER = 0x523C         # 20 x play number (1-based), 0xFF = unused slot
OFF_SPRITE_SIZES = 0x6665  # (width px, height) byte pairs for SPREXP.SQZ, ended by (0, 0)
LEVEL_COLOUR_INDEX = 14


def unpack_fox_com(raw):
    marker = raw.find(b"MZ", 0x2ED)        # the stub scans for "MZ" from CS:03ED
    if marker < 0:
        raise ValueError("ZIVPack MZ marker not found")
    start = ((marker + 0x100 + 2) & ~1) - 0x100   # COM loads at 0x100; stub word-aligns
    exe = unsqz(raw[start:])
    last_page, pages = struct.unpack_from("<HH", exe, 2)
    if exe[:2] != b"MZ" or (pages - 1) * 512 + last_page != len(exe):
        raise ValueError("unpacked data is not a consistent MZ executable")
    return exe


def file_char(index):
    return "0123456789ABCDEFGHIJ"[index]


class FoxExe:
    def __init__(self, exe):
        self.exe = exe
        header_paras = struct.unpack_from("<H", exe, 8)[0]
        self.image = exe[header_paras * 16:]
        self.ds = DS_SEGMENT * 16

    @classmethod
    def from_game_dir(cls, game_dir):
        with open(os.path.join(game_dir, "fox.com"), "rb") as f:
            return cls(unpack_fox_com(f.read()))

    def data(self, offset, length):
        return self.image[self.ds + offset:self.ds + offset + length]

    def base_palette(self):
        """The 16-colour VGA palette as 6-bit RGB triples (colour 14 is per-level)."""
        raw = self.data(OFF_DAC, 48)
        return [tuple(raw[i:i + 3]) for i in range(0, 48, 3)]

    def text_palette(self):
        """The palette loaded before text screens (status card, codes): colour 1, the glyph
        cells, is black and colours 5-15 run from light yellow to dark orange."""
        raw = self.data(OFF_TEXT_DAC, 48)
        return [tuple(raw[i:i + 3]) for i in range(0, 48, 3)]

    def level_palette(self, index):
        pal = self.base_palette()
        pal[LEVEL_COLOUR_INDEX] = tuple(self.data(OFF_LEVEL_COLOUR + index * 3, 3))
        return pal

    def sprite_sizes(self):
        """(width, height) in pixels for each sprite in SPREXP.SQZ, in file order."""
        sizes, pos = [], self.ds + OFF_SPRITE_SIZES
        while self.image[pos] or self.image[pos + 1]:
            sizes.append((self.image[pos], self.image[pos + 1]))
            pos += 2
        return sizes

    def level_names(self):
        names, pos = [], self.ds + OFF_NAMES
        for _ in range(LEVEL_SLOTS):
            end = self.image.index(b"\0", pos)
            names.append(self.image[pos:end].decode("ascii"))
            pos = end + 1
        return names

    def levels(self):
        """Per file slot: file name, play number, name, height and palette (6-bit RGB)."""
        names = self.level_names()
        heights = self.data(OFF_HEIGHTS, LEVEL_SLOTS)
        order = self.data(OFF_ORDER, LEVEL_SLOTS)
        return [{"slot": i, "file": f"LEVEL{file_char(i)}.SQZ",
                 "play_number": None if order[i] == 0xFF else order[i],
                 "name": names[i], "rows": heights[i], "palette": self.level_palette(i)}
                for i in range(LEVEL_SLOTS)]


def to_rgb8(palette):
    return [tuple(c * 255 // 63 for c in rgb) for rgb in palette]


if __name__ == "__main__":
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "TTF")
    fox = FoxExe.from_game_dir(game)
    print(f"unpacked FOX.COM: {len(fox.exe)} bytes")
    for lv in sorted((l for l in fox.levels() if l["play_number"]), key=lambda l: l["play_number"]):
        print(f"{lv['play_number']:2}  {lv['file']:11} rows={lv['rows']:3}  "
              f"colour14={lv['palette'][LEVEL_COLOUR_INDEX]}  {lv['name']}")
