"""Export the game's data segment for the C engine's AdLib driver (no numpy needed).

The engine's music/sfx driver is a port of titus_audio.AdlibDriver and, like it, runs on a
private copy of the 64 KB data segment (DS = 0x68F), so every table and pointer is used
exactly as in the original. This writes that copy, zero-padded to 65536 bytes.
"""
import argparse
import os

from foxexe import FoxExe

SEGMENT_SIZE = 0x10000


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "sound"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    fox = FoxExe.from_game_dir(args.game)
    seg = bytes(fox.image[fox.ds:fox.ds + SEGMENT_SIZE])
    seg += bytes(SEGMENT_SIZE - len(seg))
    out = os.path.join(args.out, "adlib_ds.bin")
    with open(out, "wb") as f:
        f.write(seg)
    print(f"data segment: {len(seg)} bytes -> {out}")


if __name__ == "__main__":
    main()
