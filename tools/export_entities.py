"""Export decoded enemies, elevators, animations and sound events of Titus the Fox to JSON.

Sources (all verified against the game code unless marked tentative):
  enemies    level tail +0x3F6, 50 x 26 bytes; behaviour dispatched through DS:8EB5 (image 0x5B61)
  elevators  level tail +0xB28, 10 x 20 bytes; updated by image 0x4028
  animations word table DS:87D5..8EB5; stepped by image 0x66FF (enemy sprite = value + 101)
  sound      call sites of the play-song (0x12FF) and AdLib-sfx (0x13E6) routines
"""
import argparse
import glob
import json
import os
import struct

from export_levels import split_level
from foxexe import FoxExe
from sqz import load

ANIM_START, ANIM_END = 0x87D5, 0x8EB5
ENEMY_SPRITE_BASE = 101          # image 0x673D: add ax, 0x65
ELEVATOR_SPRITE_BASE = 30        # image 0x40B1: add bx, 0x1E
REMOVE_FRAME = 0x55AA
TAIL_ENEMIES, TAIL_ELEVATORS = 0x3F6, 0xB28

# Behaviour types: handler address (image) and the meaning of the type-specific fields.
# Field tuples: (name, record offset, kind, description); kinds: u8, u16, s16.
PATROL = [("center_x", 0x0F, "u16", "middle of the patrol path (px)"),
          ("range_x", 0x11, "u16", "turns back when this far from center_x (px)")]
HOME = [("home_x", 0x0F, "u16", "respawn point when it wanders off-screen (px)"),
        ("home_y", 0x11, "u16", "respawn point y (px)"),
        ("walk_speed", 0x13, "u8", "speed once activated, signed toward the player")]
TYPES = {
    0: ("patrol", 0x65BF, "Moves back and forth around center_x, ignoring walls and gravity.", PATROL),
    1: ("patrol", 0x65BF, "Same handler as type 0.", PATROL),
    2: ("shooter", 0x65F1, "Stays put and fires a projectile (the frame of its next animation "
        "sequence) when the player is within range and 24 px vertically.",
        [("first_delay", 0x0F, "u8", "countdown before the first shot (frames)"),
         ("reload", 0x10, "u8", "countdown between shots (frames)"),
         ("range_x", 0x11, "u16&3FFF", "fires when the player is this close horizontally (px)"),
         ("direction", 0x11, "bits14-15", "0 = faces/fires at player, 1 = left only, 2/3 = right only")]),
    3: ("patrol_leap", 0x64C9, "Patrols; when the player passes overhead (within leap_height, "
        "48 px horizontally, in front of it) it leaps up in an arc and falls back.",
        PATROL + [("leap_height", 0x13, "u8", "vertical reach (px)")]),
    4: ("patrol_leap", 0x64C9, "Same handler as type 3.", PATROL + [("leap_height", 0x13, "u8", "vertical reach (px)")]),
    5: ("patrol_swoop", 0x642E, "Patrols; when the player is within dive_depth vertically and 40 px "
        "horizontally it dives toward the player and returns.",
        PATROL + [("dive_depth", 0x13, "u8", "vertical reach (px)")]),
    6: ("patrol_swoop", 0x642E, "Same handler as type 5.", PATROL + [("dive_depth", 0x13, "u8", "vertical reach (px)")]),
    7: ("gravity_striker", 0x62E0, "Waits until the player is level or below and within detect_range; "
        "then walks (with gravity) toward the player and strikes when touching.",
        HOME + [("detect_range", 0x17, "u16", "activation distance (px)")]),
    8: ("gravity_walker", 0x620F, "Activates while off-screen, then walks with gravity, turning at walls.", HOME),
    9: ("walker_periodic", 0x60F2, "Activates within detect_range; walks and periodically performs "
        "its action animation (global timer).", HOME + [("detect_range", 0x17, "u16", "activation distance (px)")]),
    10: ("alert_walker", 0x5FCE, "Becomes alert within detect_range, starts walking when the player "
         "comes 50 px closer.", HOME + [("detect_range", 0x17, "u16", "alert distance (px)")]),
    11: ("walker_attacker", 0x5ECA, "Activates within detect_range; walks along platforms and attacks/"
          "throws when the player is within 64 px.", HOME + [("detect_range", 0x17, "u16", "activation distance (px)")]),
    12: ("bouncer", 0x5E60, "Immortal. Jumps straight up and falls back, pausing between jumps "
         "(e.g. lava fireballs).",
         [("jump_power", 0x0F, "u16", "initial upward speed"), ("pause", 0x13, "u8", "frames between jumps")]),
    13: ("leaper", 0x5DBB, "Faces the player; leaps at the player when within detect_range "
         "(and 40 px vertically), pauses after landing.",
         [("pause", 0x14, "u8", "frames to wait after landing"),
          ("detect_range", 0x17, "u16", "trigger distance (px)")]),
    14: ("gravity_walker_immortal", 0x6209, "Immortal version of type 8.", HOME),
    15: ("static_immortal", 0x61F9, "Does nothing and cannot be killed.", []),
    16: ("static", 0x61FE, "Does nothing but can be killed.", []),
    17: ("dropper", 0x5D4E, "Immortal. Drops/throws an object when the player is within range_x "
         "horizontally and range_y below it, at most every delay frames.",
         [("range_x", 0x0F, "u16", "horizontal trigger distance (px)"),
          ("delay", 0x11, "u16", "minimum frames between drops"),
          ("range_y", 0x15, "u16", "how far below it the player may be (px)")]),
    18: ("zone_guard", 0x5CB8, "Chases the player while the player is inside its zone, otherwise "
         "returns to the zone centre. Uses speed for x and speed_y for y.",
         [("zone_half_width", 0x0F, "u16", "px"), ("zone_half_height", 0x11, "u16", "px"),
          ("speed_y", 0x13, "u8", "vertical speed"),
          ("zone_center_x", 0x14, "u16", "px"), ("zone_center_y", 0x16, "u16", "px")]),
}
DIRECTIONS = {0: "up", 1: "right", 2: "down", 3: "left"}

SONG_EVENTS = {
    0: ("level start / respawn card", [0x384, 0x38EE]),
    1: ("life lost", [0x386C]),
    2: ("game over", [0x3913]),
    3: ("level music (see levels)", [0x308, 0x38FF]),
    4: ("level complete", [0x3B0D]),
    5: ("special bonus tile touched (wall flag 5, floor flag 12, ceiling flag 3)", [0x4FCC]),
    6: ("energy / extra-life bonus (bonus tiles FD-FF)", [0x4F33]),
    7: ("level code revealed (wall flag 4 / floor flag 11; flag 6 / 13 reveals level 14's code)",
        [0x4F9D, 0x4FB4]),
    8: ("unused: no call site", []),
    9: ("credits screen; also the hidden developer photo (LEVELA.SQZ, hold Ctrl+Alt+E)", [0x1B69, 0x1C80]),
    15: ("title picture (TITRE.SQZ) at start-up", [0x550D, 0x5529]),
}
SFX_EVENTS = {
    1: ("enemy hit by a thrown object", [0x5A59]),
    3: ("player throws an object", [0x4B46]),
    4: ("player hurt by an enemy", [0x5B72]),
    5: ("player hit by a flying object", [0x400F]),
    9: ("enemy defeated (replaced by its death sprite)", [0x4C92]),
    12: ("vertical bounce / landing on an object (tentative)", [0x5210]),
}


def read_field(rec, off, kind):
    if kind == "u8":
        return rec[off]
    if kind == "u16":
        return struct.unpack_from("<H", rec, off)[0]
    if kind == "s16":
        return struct.unpack_from("<h", rec, off)[0]
    if kind == "u16&3FFF":
        return struct.unpack_from("<H", rec, off)[0] & 0x3FFF
    if kind == "bits14-15":
        return struct.unpack_from("<H", rec, off)[0] >> 14
    raise ValueError(kind)


def parse_animations(fox):
    """Split the animation word table into sequences, each ending with a backward jump."""
    words = [(ANIM_START + 2 * i, v) for i, v in
             enumerate(struct.unpack(f"<{(ANIM_END - ANIM_START) // 2}H",
                                     fox.data(ANIM_START, ANIM_END - ANIM_START)))]
    seqs, cur = [], []
    for addr, v in words:
        cur.append((addr, v))
        if v & 0x8000 and v != REMOVE_FRAME:
            seqs.append(cur)
            cur = []
    if cur:
        seqs.append(cur)
    out, addr_index = [], {}
    for sid, seq in enumerate(seqs):
        frames = []
        for fi, (addr, v) in enumerate(seq):
            addr_index[addr] = (sid, fi)
            if v == REMOVE_FRAME:
                frames.append({"remove": True})
            elif v & 0x8000:
                pass
            else:
                key = v & ~0x2000
                frames.append({"key": key, "sprite": key + ENEMY_SPRITE_BASE, "event": bool(v & 0x2000)})
        entry = {"id": sid, "ds_offset": f"{seq[0][0]:04X}", "frames": frames}
        last_addr, last = seq[-1]
        if last & 0x8000 and last != REMOVE_FRAME:
            target = last_addr + (last - 0x10000)
            entry["jump_back_bytes"] = last - 0x10000
            entry["_target_addr"] = target
        out.append(entry)
    for entry in out:            # resolve jump targets to (sequence, frame)
        t = entry.pop("_target_addr", None)
        if t is not None and t in addr_index:
            sid, fi = addr_index[t]
            entry["then_goes_to"] = {"sequence": sid, "frame": fi}
    first = {}
    for addr, v in words:        # the game's lookup: first word equal to the sprite key
        if not v & 0x8000:
            first.setdefault(v, addr)
    return out, addr_index, first


def decode_enemy(i, rec, anim_lookup, addr_index):
    x, y, sprite_word, type_word, speed = struct.unpack_from("<HHHHh", rec, 0)
    if sprite_word == 0xFFFF:
        return None
    key = sprite_word & 0x1FFF
    etype = type_word & 0x1FFF
    name, handler, desc, fields = TYPES.get(etype, ("unknown", None, "", []))
    e = {"index": i, "x": x, "y": y, "type": etype, "type_name": name,
         "sprite_key": key, "first_sprite": key + ENEMY_SPRITE_BASE,
         "initial_flip": bool(sprite_word & 0x8000),
         "speed": speed, "facing": "left" if speed > 0 else "right" if speed < 0 else "none",
         "knockback": struct.unpack_from("<H", rec, 0x0C)[0],
         "harmless_on_contact": struct.unpack_from("<H", rec, 0x0C)[0] == 0 or etype in (15, 16, 17)}
    e["params"] = {n: read_field(rec, off, kind) for n, off, kind, _ in fields}
    addr = anim_lookup.get(key)
    if addr is not None:
        sid, fi = addr_index[addr]
        e["animation"] = {"sequence": sid, "frame": fi}
    e["raw"] = rec.hex(" ")
    return e


def decode_elevator(i, rec):
    sprite_word = struct.unpack_from("<H", rec, 4)[0]
    if sprite_word == 0xFFFF:
        return None
    x, y = struct.unpack_from("<HH", rec, 0)
    direction, speed = rec[6], rec[7]
    return {"index": i, "x": x, "y": y,
            "sprite": (sprite_word & 0x1FFF) + ELEVATOR_SPRITE_BASE, "sprite_flags": sprite_word >> 13,
            "direction": DIRECTIONS.get(direction, direction), "speed": speed,
            "enabled": speed < 8,                       # image 0x3157 disables speed >= 8
            "run_length": struct.unpack_from("<H", rec, 0x0A)[0],
            "travel_px": struct.unpack_from("<H", rec, 0x0A)[0] * speed,
            "start": {"x": struct.unpack_from("<H", rec, 0x0C)[0], "y": struct.unpack_from("<H", rec, 0x0E)[0],
                      "direction": DIRECTIONS.get(rec[0x10], rec[0x10])},
            "raw": rec.hex(" ")}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(here, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(here, "..", "export", "entities"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    fox = FoxExe.from_game_dir(args.game)
    anims, addr_index, anim_lookup = parse_animations(fox)
    levels = {lv["file"].lower(): lv for lv in fox.levels()}
    boss_hp = fox.data(0x50B4, 20)
    music = fox.data(0x508C, 20)

    enemies_out, elevators_out = [], []
    for path in sorted(glob.glob(os.path.join(args.game, "level*.sqz"))):
        info = levels.get(os.path.basename(path).lower())
        if not info or not info["play_number"]:
            continue
        _, _, _, tail = split_level(load(path))
        head = {"level": info["play_number"], "name": info["name"], "file": info["file"]}
        enemies = [e for i in range(50)
                   if (e := decode_enemy(i, tail[TAIL_ENEMIES + 26 * i:TAIL_ENEMIES + 26 * (i + 1)],
                                         anim_lookup, addr_index))]
        elevators = [e for i in range(10)
                     if (e := decode_elevator(i, tail[TAIL_ELEVATORS + 20 * i:TAIL_ELEVATORS + 20 * (i + 1)]))]
        enemies_out.append({**head, "boss_hit_points": boss_hp[info["slot"]], "enemies": enemies})
        elevators_out.append({**head, "elevators": elevators})
    enemies_out.sort(key=lambda l: l["level"])
    elevators_out.sort(key=lambda l: l["level"])

    type_doc = {str(t): {"name": n, "handler_image": f"{h:04X}", "behaviour": d,
                         "params": [{"name": fn, "offset": f"+{o:02X}", "type": k, "meaning": m}
                                    for fn, o, k, m in f]}
                for t, (n, h, d, f) in TYPES.items()}
    record_doc = {
        "+00 u16": "x (px)", "+02 u16": "y (px)",
        "+04 u16": "animation key (low 13 bits) = first sprite - 101; bit 15 initial flip, bit 13 runtime on-screen",
        "+06 u16": "behaviour type (low 13 bits); top bits are runtime state (0x4000 hit, 0x8000 dead, 0x2000 event frame)",
        "+08 s16": "horizontal speed; positive moves left (x -= speed)",
        "+0A u16": "animation pointer (runtime; file values are stale editor pointers)",
        "+0C u16": "knock-back given to the player on contact (0 = harmless)",
        "+0E u8": "state machine (runtime)",
        "+0F..+19": "type-specific parameters, see types",
    }
    with open(os.path.join(args.out, "enemies.json"), "w") as f:
        json.dump({"record_layout": record_doc, "types": type_doc,
                   "boss_hit_points_note": "per-level table DS:50B4; the per-sprite boss flag table "
                                           "DS:70C5 is all zero in this build",
                   "levels": enemies_out}, f, indent=1)
    with open(os.path.join(args.out, "elevators.json"), "w") as f:
        json.dump({"record_layout": {
            "+00 u16": "x (runtime, px)", "+02 u16": "y (runtime, px)",
            "+04 u16": "sprite - 30 (low 13 bits); FFFF = unused",
            "+06 u8": "direction 0 up / 1 right / 2 down / 3 left (flipped with xor 2 at the end of a run)",
            "+07 u8": "speed in px per frame; >= 8 disables the elevator at load",
            "+0A u16": "run length in frames before reversing",
            "+0C u16": "start x", "+0E u16": "start y", "+10 u8": "start direction",
            "+11 u16": "frame counter (runtime)"}, "levels": elevators_out}, f, indent=1)
    with open(os.path.join(args.out, "animations.json"), "w") as f:
        json.dump({"format": "word table DS:87D5-8EB5. value & 0x1FFF = sprite key (sprite = key + 101); "
                             "0x2000 = event frame (shoot/attack trigger); 0x55AA = remove the enemy; "
                             "negative = jump back that many bytes. One entry per game frame. An enemy "
                             "starts at the first word equal to its key; its behaviour steps to the "
                             "next/previous sequence for state changes (image 0x66DF / 0x66EF).",
                   "sequences": anims}, f, indent=1)

    used = {}
    for lv in fox.levels():
        if lv["play_number"]:
            used.setdefault(music[lv["slot"]], []).append(lv["play_number"])
    songs = []
    for n in range(16):
        ev, sites = SONG_EVENTS.get(n, ("level music", [0x308, 0x38FF]))
        songs.append({"song": n, "event": ev, "levels": sorted(used.get(n, [])),
                      "call_sites_image": [f"{a:04X}" for a in sites],
                      "kind": "level music" if n in used else "jingle"})
    sfx = [{"sfx": n, "event": SFX_EVENTS.get(n, ("no direct call site found", []))[0],
            "call_sites_image": [f"{a:04X}" for a in SFX_EVENTS.get(n, ("", []))[1]]} for n in range(14)]
    with open(os.path.join(args.out, "sound_events.json"), "w") as f:
        json.dump({"songs": songs, "sfx": sfx}, f, indent=1)

    total_e = sum(len(l["enemies"]) for l in enemies_out)
    total_v = sum(len(l["elevators"]) for l in elevators_out)
    print(f"{total_e} enemies, {total_v} elevators, {len(anims)} animation sequences -> {args.out}")


if __name__ == "__main__":
    main()
