"""Export Titus's animation table, per-sprite placement/collision tables and start values.

Everything comes from the unpacked FOX.COM (DS = 0x68F); addresses are DS offsets, code
addresses are image offsets. Writes export/entities/player.json:

  words        the animation word area DS:6527.. as signed values: >= 0 is a sprite shown for
               one game frame (bit 15 of a positive word never occurs), < 0 jumps back that many
               bytes (so -2k jumps back k words)
  states       30 entries, one per player action state (DS:6EBF); index into `words` or null.
               States 16-29 are the same actions while carrying an object.
  sprite_hot   356 x [hot_x, hot_y], signed, as stored (DS:692F). The draw routine (image
               0x1E61) puts a sprite's left edge at x - hot_x (x - (width - hot_x) when
               mirrored) and its top edge at y + 1 - hot_y. (At load, image 0x272F rewrites
               hot_y as height - hot_y and the draw routine adds it; the result is the same.)
               Most sprites have hot_y = height, so their lowest row is the line y.
  sprite_box   356 x [width, height] used for collision (DS:6BF7)
  start_*      new-game values (immediates at image 0x2D7)
  object_flags 101 bytes, one per sprite < 0x65 (DS:6F19): 0x01 platform (can be stood on),
               0x02 bouncy, 0x04 heavy (thrown as a physics object, not a projectile),
               0x08 fragile (falls through floors at vy >= 160), 0x10 never hurts enemies
  object_fall_limit  101 bytes (DS:6F60): terminal fall speed of an object, in px per frame
  enemy_words  the enemy animation word area DS:87D5-8EB5 (signed; 0x55AA = remove the enemy;
               bit 0x2000 of a frame = event frame; negative = jump back that many bytes)
  enemy_class  one byte per enemy key (sprite - 0x65) (DS:6FDF): low nibble = carried pose
               (0xF = can't be picked up), high nibble = death frame (0xF = falls off instead)
  enemy_carry_keys  7 keys shown while an enemy of each low-nibble class is carried (DS:6FC5)
  enemy_death_keys  6 corpse keys by high-nibble class (DS:6FD3)
  enemy_boss   one byte per enemy key (DS:70C5): non-zero = boss. The table is empty in the
               file; at start-up image 0x26C9 fills it from the list at DS:7255 of
               (first key, count) word pairs ending with FFFF, and so does this tool
  enemy_contact  19 booleans by behaviour type: touching it can hurt (DS:8EDB is not `ret`)
  ending       the wedding scene that "Just Married" (file slot G) plays instead of a level
               (image 0x370E): where Titus and the bride start, their running animations (same
               word format as `words`, each sequence on its own), the kissing couple and the
               burst ring shown when they meet, and the path the little heart hops along until
               fire is pressed (x, y pairs, repeated; each pair is one frame)
"""
import argparse
import json
import os
import struct

from foxexe import FoxExe

ANIM_TABLE, STATE_COUNT = 0x6EBF, 30
SPRITE_HOT, SPRITE_BOX, SPRITE_COUNT = 0x692F, 0x6BF7, 356
OBJECT_FLAGS, OBJECT_FALL_LIMIT, OBJECT_SPRITES = 0x6F19, 0x6F60, 0x65
ENEMY_WORDS, ENEMY_WORDS_END = 0x87D5, 0x8EB5
ENEMY_CLASS, ENEMY_CARRY, ENEMY_DEATH, ENEMY_CONTACT = 0x6FDF, 0x6FC5, 0x6FD3, 0x8EDB
BOSS_LIST = 0x7255            # (first key, count) pairs, FFFF-terminated (image 0x26F1)
ENEMY_KEYS, ENEMY_TYPES = 0x163 - 0x65 + 1, 19
NO_CONTACT = 0x5C03 - 0x200       # CS offset of the bare `ret`
NEW_GAME = 0x2D7              # mov byte [041B],2 / mov byte [0419],10h / mov byte [041A],0
MAX_ENERGY, MAX_LIVES = 16, 9  # clamps at image 0x5BAA and 0x5BBB


def immediate_byte(fox, image, ds_addr):
    """Value of `mov byte [ds_addr], imm8` (C6 06 lo hi imm) at image offset `image`."""
    op = fox.image[image:image + 5]
    if op[:2] != b"\xC6\x06" or struct.unpack_from("<H", op, 2)[0] != ds_addr:
        raise ValueError(f"unexpected code at image {image:#x}: {op.hex(' ')}")
    return op[4]


def immediate_word(fox, image, ds_addr):
    """Value of `mov word [ds_addr], imm16` (C7 06 lo hi imm) at image offset `image`."""
    op = fox.image[image:image + 6]
    if op[:2] != b"\xC7\x06" or struct.unpack_from("<H", op, 2)[0] != ds_addr:
        raise ValueError(f"unexpected code at image {image:#x}: {op.hex(' ')}")
    return struct.unpack_from("<H", op, 4)[0]


def ending(fox, sword):
    """The scene at image 0x370E. Titus is the player record (DS:527C: x, y, sprite +4, anim
    pointer +C), the bride the object record at DS:52B2."""
    def sequence(ptr):
        out = []
        while True:
            out.append(sword(ptr))
            if out[-1] < 0:
                return out
            ptr += 2

    s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    si = fox.image[0x37C8:0x37CB]                 # mov si, path
    if si[0] != 0xBE:
        raise ValueError("unexpected code at image 0x37c8")
    path_ds = struct.unpack_from("<H", si, 1)[0]
    ds = fox.image[fox.ds:fox.ds + 0x10000]
    path = []
    while sword(path_ds + 2 * len(path)) != -2 * 18 or len(path) < 18:
        a = path_ds + 2 * len(path)
        path.append([ds[a], ds[a + 1]])
        if len(path) > 64:
            raise ValueError("heart path: no loop word")
    return {
        "titus": {"x": s16(immediate_word(fox, 0x3735, 0x527C)), "y": immediate_word(fox, 0x373B, 0x527E),
                  "sprite": immediate_word(fox, 0x372F, 0x5280), "anim": sequence(immediate_word(fox, 0x3741, 0x5288))},
        "bride": {"x": immediate_word(fox, 0x374D, 0x52B2), "y": immediate_word(fox, 0x3753, 0x52B4),
                  "sprite": immediate_word(fox, 0x3747, 0x52B6), "anim": sequence(immediate_word(fox, 0x3759, 0x52BE))},
        "run_speed": 3,                              # add/sub word [..], 3 at 0x3765 / 0x3771
        "meet_distance": 0x1C,                       # loop while bride x > titus x + 1Ch (0x3780)
        "kiss_sprite": immediate_word(fox, 0x378C, 0x52B6),     # bit 15 = mirrored
        "kiss_shift": 0x18,                          # the couple moves 18h px left (0x3792)
        "burst_anim": sequence(immediate_word(fox, 0x379A, 0x5288)),
        "burst_rise": 0x10, "burst_frames": 0x10,    # y -= 10h, then 10h frames of y += 1
        "heart_sprite": immediate_word(fox, 0x37C2, 0x5280),
        "heart_path": path,
    }


def boss_flags(word):
    """Image 0x26C9: keys first..first+count-1 of each pair in the list are bosses."""
    flags = [0] * ENEMY_KEYS
    a = BOSS_LIST
    while word(a) != 0xFFFF:
        first, count = word(a), word(a + 2)
        for key in range(first, first + count):
            if key < ENEMY_KEYS:
                flags[key] = 1
        a += 4
    return flags


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "entities"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    fox = FoxExe.from_game_dir(args.game)
    ds = fox.image[fox.ds:fox.ds + 0x10000]
    word = lambda a: struct.unpack_from("<H", ds, a)[0]
    sword = lambda a: struct.unpack_from("<h", ds, a)[0]
    sbyte = lambda a: struct.unpack_from("<b", ds, a)[0]

    pointers = [word(ANIM_TABLE + 2 * i) for i in range(STATE_COUNT)]
    used = [p for p in pointers if p]
    base = min(used)
    # Follow every sequence to its jump word to find the end of the area.
    end = base
    for p in used:
        while sword(p) >= 0:
            p += 2
        end = max(end, p + 2)
    words = [sword(a) for a in range(base, end, 2)]
    states = [(p - base) // 2 if p else None for p in pointers]

    out = {
        "format": __doc__,
        "words_ds": f"{base:04X}",
        "words": words,
        "states": states,
        "sprite_hot": [[sbyte(SPRITE_HOT + 2 * i), sbyte(SPRITE_HOT + 2 * i + 1)] for i in range(SPRITE_COUNT)],
        "sprite_box": [[ds[SPRITE_BOX + 2 * i], ds[SPRITE_BOX + 2 * i + 1]] for i in range(SPRITE_COUNT)],
        "object_flags": list(ds[OBJECT_FLAGS:OBJECT_FLAGS + OBJECT_SPRITES]),
        "object_fall_limit": list(ds[OBJECT_FALL_LIMIT:OBJECT_FALL_LIMIT + OBJECT_SPRITES]),
        "enemy_words_ds": f"{ENEMY_WORDS:04X}",
        "enemy_words": [sword(a) for a in range(ENEMY_WORDS, ENEMY_WORDS_END, 2)],
        "enemy_class": list(ds[ENEMY_CLASS:ENEMY_CLASS + ENEMY_KEYS]),
        "enemy_carry_keys": [word(ENEMY_CARRY + 2 * i) for i in range(7)],
        "enemy_death_keys": [word(ENEMY_DEATH + 2 * i) for i in range(6)],
        "enemy_boss": boss_flags(word),
        "enemy_contact": [word(ENEMY_CONTACT + 2 * i) != NO_CONTACT for i in range(ENEMY_TYPES)],
        "start_lives": immediate_byte(fox, NEW_GAME, 0x041B),
        "start_energy": immediate_byte(fox, NEW_GAME + 5, 0x0419),
        "max_energy": MAX_ENERGY,
        "max_lives": MAX_LIVES,
        "ending": ending(fox, sword),
    }
    path = os.path.join(args.out, "player.json")
    with open(path, "w") as f:
        json.dump(out, f, indent=1)
    print(f"player: {len(words)} animation words, {sum(s is not None for s in states)} states, "
          f"lives {out['start_lives']}, energy {out['start_energy']} -> {path}")


if __name__ == "__main__":
    main()
