"""Export Titus the Fox music and sound effects.

Uses the driver ports in titus_audio.py:
  music/song_NN.dro            exact AdLib register capture (DOSBox Raw OPL v2)
  music/song_NN_adlib.wav      rendered with opl2synth.c (an approximate OPL2 model)
  music/song_NN_pcspeaker.wav  the game's PC speaker version of the tune
  sfx/sfx_NN_adlib.wav         AdLib sound effects (bass-drum hits with per-effect patches)
  unused_mod/                  samples of SWHERE.MOD, which the game never loads
Requires numpy; WAV rendering of AdLib also needs gcc (to build opl2synth.c once).
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import tempfile
import wave

import numpy as np

from export_entities import SONG_EVENTS
from foxexe import FoxExe
from titus_audio import (PIT_HZ, SFX_COUNT, SONG_COUNT, TICK_HZ, run_adlib_sfx, run_adlib_song,
                         run_pc_song, write_dro)

OPL_RATE = 49716
PC_RATE = 44100
HERE = os.path.dirname(os.path.abspath(__file__))

# Where the game plays each song (from its call sites); level songs come from DS:508C.
SONG_USES = {n: event for n, (event, _) in SONG_EVENTS.items() if n != 3}


def build_synth():
    exe = os.path.join(HERE, "build", "opl2synth.exe" if os.name == "nt" else "opl2synth")
    src = os.path.join(HERE, "opl2synth.c")
    if os.path.exists(exe) and os.path.getmtime(exe) >= os.path.getmtime(src):
        return exe
    gcc = shutil.which("gcc") or shutil.which("cc")
    if not gcc:
        return None
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    subprocess.run([gcc, "-O2", "-o", exe, src, "-lm"], check=True)
    return exe


def write_wav(path, samples, rate, peak_level=0.89):
    peak = float(np.max(np.abs(samples))) if len(samples) else 0
    if peak > 0:
        samples = samples * (peak_level / peak)    # 0.89 = about -1 dBFS
    pcm = np.clip(samples * 32767, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def render_opl(synth, writes, ticks):
    with tempfile.TemporaryDirectory() as tmp:
        ev, out = os.path.join(tmp, "ev.bin"), os.path.join(tmp, "out.f32")
        total = int(ticks * OPL_RATE / TICK_HZ)
        with open(ev, "wb") as f:
            f.write(struct.pack("<I", total))
            for tick, reg, val in writes:
                f.write(struct.pack("<IBB", int(tick * OPL_RATE / TICK_HZ), reg, val))
        subprocess.run([synth, ev, out], check=True)
        return np.fromfile(out, dtype="<f4")


def render_pc(events, ticks):
    """Square wave following the PIT channel 2 divisor changes."""
    total = int(ticks * PC_RATE / TICK_HZ)
    freq = np.zeros(total)
    bounds = [(int(t * PC_RATE / TICK_HZ), d) for t, d in events] + [(total, None)]
    for (start, div), (end, _) in zip(bounds, bounds[1:]):
        if div is not None:
            f = PIT_HZ / (div or 0x10000)
            freq[start:end] = f if f < PC_RATE / 2 else 0
    phase = np.cumsum(freq / PC_RATE)
    sq = np.where(freq > 0, np.sign(np.sin(2 * np.pi * phase)), 0.0) * 0.5
    # gentle one-pole low-pass, standing in for the speaker cone
    out, acc, a = np.empty_like(sq), 0.0, 0.35
    for i, v in enumerate(sq):
        acc += a * (v - acc)
        out[i] = acc
    return out


def export_mod_samples(game, out_dir):
    path = os.path.join(game, "swhere.mod")
    if not os.path.exists(path):
        return []
    with open(path, "rb") as f:
        mod = f.read()
    os.makedirs(out_dir, exist_ok=True)
    patterns = max(mod[952:1080]) + 1
    pos = 1084 + patterns * 1024
    saved = []
    for i in range(31):
        hdr = mod[20 + 30 * i:50 + 30 * i]
        name = hdr[:22].split(b"\0")[0].decode("latin1").strip()
        length = struct.unpack(">H", hdr[22:24])[0] * 2
        if length:
            data = np.frombuffer(mod[pos:pos + length], dtype=np.int8).astype(np.float64) / 128
            safe = "".join(c if c.isalnum() else "_" for c in name) or f"sample{i + 1}"
            fname = f"sample_{i + 1:02d}_{safe}.wav"
            write_wav(os.path.join(out_dir, fname), data, 8287)    # ProTracker C-2 rate
            saved.append({"index": i + 1, "name": name, "bytes": length, "file": fname})
        pos += length
    shutil.copyfile(path, os.path.join(out_dir, "swhere.mod"))
    return saved


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=os.path.join(HERE, "..", "TTF"))
    ap.add_argument("--out", default=os.path.join(HERE, "..", "export", "sound"))
    ap.add_argument("--loops", type=int, default=1, help="times to play looping songs (default 1)")
    args = ap.parse_args()

    fox = FoxExe.from_game_dir(args.game)
    synth = build_synth()
    if not synth:
        print("warning: no C compiler found; AdLib WAVs skipped (DRO captures still written)")
    level_songs = {}
    order = fox.data(0x508C, 20)
    for lv in fox.levels():
        if lv["play_number"]:
            level_songs.setdefault(order[lv["slot"]], []).append(f"{lv['play_number']} {lv['name']}")

    music_dir, sfx_dir = os.path.join(args.out, "music"), os.path.join(args.out, "sfx")
    os.makedirs(music_dir, exist_ok=True)
    os.makedirs(sfx_dir, exist_ok=True)
    index = {"tick_hz": TICK_HZ, "songs": [], "sfx": []}

    for n in range(SONG_COUNT):
        writes, ticks, ended = run_adlib_song(fox, n, loops=args.loops)
        entry = {"song": n, "used_for": SONG_USES.get(n) or (
                     "level music: " + ", ".join(sorted(level_songs[n], key=lambda s: int(s.split()[0])))
                     if n in level_songs else "event jingle"),
                 "adlib_seconds": round(ticks / TICK_HZ, 1), "adlib_loops": not ended,
                 "dro": f"music/song_{n:02d}.dro"}
        write_dro(os.path.join(args.out, entry["dro"]), writes, ticks)
        if synth:
            entry["adlib_wav"] = f"music/song_{n:02d}_adlib.wav"
            write_wav(os.path.join(args.out, entry["adlib_wav"]), render_opl(synth, writes, ticks), OPL_RATE)
        pc = run_pc_song(fox, n, loops=args.loops)
        if pc:
            entry["pcspeaker_wav"] = f"music/song_{n:02d}_pcspeaker.wav"
            # square waves are dense: a lower peak keeps them about as loud as the FM renders
            write_wav(os.path.join(args.out, entry["pcspeaker_wav"]), render_pc(*pc), PC_RATE, 0.35)
        index["songs"].append(entry)
        print(f"song {n:2}: {entry['adlib_seconds']:6.1f}s {'(loops)' if entry['adlib_loops'] else '(ends) '} "
              f"{'+ PC speaker' if pc else '(no PC speaker version)'}  {entry['used_for']}")

    for n in range(SFX_COUNT):
        writes, ticks = run_adlib_sfx(fox, n)
        entry = {"sfx": n, "file": f"sfx/sfx_{n:02d}_adlib.wav" if synth else None}
        if synth:
            write_wav(os.path.join(args.out, entry["file"]), render_opl(synth, writes, ticks), OPL_RATE)
        index["sfx"].append(entry)
    print(f"{SFX_COUNT} AdLib sound effects")

    index["unused_mod_samples"] = export_mod_samples(args.game, os.path.join(args.out, "unused_mod"))
    with open(os.path.join(args.out, "sound.json"), "w") as f:
        json.dump(index, f, indent=1)


if __name__ == "__main__":
    main()
