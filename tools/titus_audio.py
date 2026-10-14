"""Titus the Fox music/sound drivers, ported from the unpacked FOX.COM.

Both drivers run on a private copy of the game's data segment (DS = 0x68F), so all tables,
pointers and quirks behave as in the original. Addresses below are DS offsets; code addresses
are offsets in the EXE load image.

AdLib driver (image 0x12FF play, 0x14AB tick, 0x153B sfx tick):
  song n: channel stream pointers = n-th FFFF-terminated word list at DS:0515
          instrument pointers      = n-th FFFF-terminated word list at DS:0675 (last word = tempo)
  per channel, a 17-byte state record at DS:09B3 + 17*i:
     +0 duration shift  +1 volume  +2/+3 params  +4 tie  +5 OPL channel  +6 instrument
     +7 ticks left  +8 note  +9 octave  +A return ptr  +C loop count  +D stream ptr  +F prev note
     +10 tie latch
  stream bytes: 0x00-0x7F note (octave<<4 | note, note 12 = rest),
                0x80-0xEF command (group<<4 | value) via DS:091A,
                0xF0 call, 0xF1 set loop count, 0xF2 loop, 0xF3 return, 0xF4 jump, 0xFF end.
  OPL runs in rhythm mode (BD shadow at DS:0919 = 0x20); instruments >= 15 are drums.
PC speaker driver (image 0x18E5): one voice, same encoding, song pointers at DS:0982,
  tempo at DS:09A2, PIT divisors at DS:0B20 (>> octave).
Both are ticked by IRQ0 reprogrammed to divisor 0x4000 (72.8 Hz); a tick is skipped every
`tempo` interrupts.
"""
import struct

from foxexe import FoxExe

PIT_HZ = 1193182
TICK_HZ = PIT_HZ / 0x4000          # 72.82 Hz
SONG_COUNT = 16
SFX_COUNT = 14

# Data-segment addresses used by the drivers.
SONG_LISTS, INSTR_LISTS = 0x0515, 0x0675
CMD_TEMPO, CMD_TEMPO_RELOAD = 0x0980, 0x0981
FNUMS, OP_OFFSETS, CH_SLOTS = 0x0966, 0x0948, 0x095B
CUR_INSTR_LIST, BD_SHADOW, ACTIVE = 0x0917, 0x0919, 0x09B2
CHANNELS, CH_SIZE = 0x09B3, 0x11
SFX_PATCHES, SFX_TICKS, SFX_ACTIVE, SFX_RESTORE = 0x0156, 0x0172, 0x0174, 0x0B9A
PC_SONGS, PC_TEMPOS, PC_DIVISORS = 0x0982, 0x09A2, 0x0B20
PC_PTR, PC_LEFT, PC_RET, PC_LOOP, PC_OCT, PC_NOTE, PC_PARAMS = 0x0B07, 0x0B09, 0x0B0A, 0x0B0C, 0x0B0D, 0x0B0E, 0x0B0F


class Memory:
    def __init__(self, fox):
        seg = fox.image[fox.ds:fox.ds + 0x10000]
        self.m = bytearray(seg) + bytearray(0x10000 - len(seg))

    def b(self, a):
        return self.m[a & 0xFFFF]

    def w(self, a):
        return self.m[a & 0xFFFF] | self.m[(a + 1) & 0xFFFF] << 8

    def sb(self, a, v):
        self.m[a & 0xFFFF] = v & 0xFF

    def sw(self, a, v):
        self.m[a & 0xFFFF] = v & 0xFF
        self.m[(a + 1) & 0xFFFF] = (v >> 8) & 0xFF

    def nth_list(self, base, n):
        """Start of the n-th FFFF-terminated word list starting at base."""
        p = base
        for _ in range(n):
            while self.w(p) != 0xFFFF:
                p += 2
            p += 2
        return p


class AdlibDriver:
    """Port of the AdLib music + sfx driver. Collects (tick, reg, value) OPL writes."""

    def __init__(self, fox):
        self.mem = Memory(fox)
        self.writes = []
        self.tick = 0
        self.loops = {}            # channel record -> backward F4 jumps taken
        self.opl(0x01, 0x20)       # init (image 0x1398): enable waveform select
        self.opl(0x08, 0x00)
        self.opl(0xBD, 0x00)
        self.reset()

    def opl(self, reg, val):
        self.writes.append((self.tick, reg & 0xFF, val & 0xFF))

    def reset(self):               # image 0x184F
        for r in range(0xB0, 0xB9):
            self.opl(r, 0)
        for r in range(0xA0, 0xB9):    # the original loop runs A0..B8 here too
            self.opl(r, 0)
        self.opl(0x08, 0)
        self.opl(0xBD, 0)
        for op in (0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21):
            self.opl(0x40 + op, 0x3F)

    # --- music ---------------------------------------------------------------
    def play(self, song):          # image 0x1357 / 0x1469
        m = self.mem
        self.reset()
        chans = m.nth_list(SONG_LISTS, song)
        instr = m.nth_list(INSTR_LISTS, song)
        p = instr
        while m.w(p) != 0xFFFF:
            p += 2
        tempo = m.w(p - 2) & 0xFF
        m.sb(CMD_TEMPO, tempo)
        m.sb(CMD_TEMPO_RELOAD, tempo)
        m.sw(CUR_INSTR_LIST, instr)
        di, dl = CHANNELS, 0
        m.sb(ACTIVE, 0xFF)
        while True:
            for k in range(CH_SIZE):
                m.sb(di + k, 0)
            ptr = m.w(chans)
            chans += 2
            m.sb(ACTIVE, m.b(ACTIVE) + 1)
            m.sw(di + 0xD, ptr)
            m.sb(di + 5, dl)
            dl += 1
            di += CH_SIZE
            if ptr == 0xFFFF:
                break

    def instr_ptr(self, di):
        bx = self.mem.w(self.mem.w(CUR_INSTR_LIST) + self.mem.b(di + 6) * 2)
        return bx + 1 if self.mem.b(di + 6) >= 0x0F else bx

    def load_op(self, bx, dl):     # image 0x17DE
        m = self.mem
        for i, base in enumerate((0x60, 0x80, 0x40, 0x20, 0xE0)):
            self.opl(base + dl, m.b(bx + i))

    def set_volume(self, di, al):  # image 0x167D
        m = self.mem
        m.sb(di + 1, al)
        cx = (al + 1) & 0xFF
        bx = self.instr_ptr(di)
        ah = (-(((m.b(bx + 2) & 0x3F) - 0x3F) & 0xFF)) & 0xFF
        ax = ((ah << 8) >> 4) * cx & 0xFFFF
        al = (0x3F - (ax >> 8)) & 0xFF
        slot = m.b(CH_SLOTS + m.b(di + 5))
        if slot <= 0x0D:
            slot += 3
        self.opl(0x40 + m.b(OP_OFFSETS + slot), al)

    def set_rhythm_instr(self, di):    # image 0x16E9
        m = self.mem
        bl = (m.b(di + 9) + 0x0F) & 0xFF
        m.sb(di + 6, bl)
        bx = m.w(m.w(CUR_INSTR_LIST) + bl * 2)
        cl = m.b(bx)
        m.sb(di + 5, cl)
        m.sb(BD_SHADOW, ((0x10 >> ((cl - 6) & 0xFF)) if cl >= 6 else 0) | m.b(BD_SHADOW))
        self.load_instr(di)

    def set_instr(self, di, al):   # image 0x16E5
        m = self.mem
        if al == 1:
            return self.set_rhythm_instr(di)
        m.sb(di + 6, al - 1 if al > 1 else al)
        cl = m.b(di + 5) - 6
        if cl >= 0:
            ah = ~(0x10 >> cl) & m.b(BD_SHADOW) & 0xFF
            m.sb(BD_SHADOW, ah)
            self.opl(0xBD, ah)
        self.load_instr(di)

    def load_instr(self, di):      # image 0x1734
        m = self.mem
        ch = m.b(di + 5)
        slot = m.b(CH_SLOTS + ch)
        if ch <= 6:
            slot += 3
        dl = m.b(OP_OFFSETS + slot)
        bx = self.instr_ptr(di)
        self.load_op(bx, dl)
        dl -= 3
        bx += 5
        if ch < 7:
            self.load_op(bx, dl)
            self.opl(0xC0 + ch, m.b(bx + 5))

    def note_on(self, di, ax, cl):     # image 0x15C2
        m = self.mem
        if ax:
            if m.b(di + 6) >= 0x0F:            # drum
                cl = (cl + 0x0F) & 0xFF
                if m.b(di + 6) != cl:
                    self.set_rhythm_instr(di)
                    self.set_volume(di, m.b(di + 1))
                ch = m.b(di + 5)
                bit = 0x10 >> (ch - 6) if ch >= 6 else 0
                self.opl(0xBD, ~bit & m.b(BD_SHADOW))
                if ch == 6:
                    self.opl(0xA6, 0x57)
                    self.opl(0xB6, 0x00)
                    self.opl(0xB6, 0x05)
                self.opl(0xBD, m.b(BD_SHADOW))
                return
            ch = m.b(di + 5)
            cl = (cl + 2) & 0xFF
            self.opl(0xA0 + ch, ax & 0xFF)
            if m.b(di + 0x10) != 1:
                self.opl(0xB0 + ch, 0)
            self.opl(0xB0 + ch, (ax >> 8) | ((cl << 2) & 0xFF) | 0x20)
        m.sb(di + 0x10, m.b(di + 4))

    def music_tick(self):          # image 0x14AB
        m = self.mem
        m.sb(CMD_TEMPO, m.b(CMD_TEMPO) - 1)
        if m.b(CMD_TEMPO) == 0:
            m.sb(CMD_TEMPO, m.b(CMD_TEMPO_RELOAD))
            return
        di = CHANNELS
        while True:
            si = m.w(di + 0xD)
            if si == 0xFFFF:
                return
            if si != 0xFFFE:
                left = m.b(di + 7)
                m.sb(di + 7, left - 1)
                if left <= 1:
                    si = self.run_channel(di, si)
                m.sw(di + 0xD, si)
            di += CH_SIZE

    def run_channel(self, di, si):
        m = self.mem
        while True:
            al = m.b(si)
            si += 1
            if al & 0x80:
                grp, val = (al & 0x70) >> 4, al & 0x0F
                if grp != 7:
                    if grp == 1:
                        self.set_volume(di, val)
                    elif grp == 6:
                        self.set_instr(di, val)
                    else:
                        m.sb(di + grp, val)     # 0:duration 2,3:params 4:tie 5:channel
                    continue
                if val == 0x0:                  # call
                    m.sw(di + 0xA, si + 2)
                    si = m.w(si)
                elif val == 0x1:                # set loop count
                    m.sb(di + 0xC, m.b(si))
                    si += 1
                elif val == 0x2:                # loop
                    m.sb(di + 0xC, m.b(di + 0xC) - 1)
                    si = si + 2 if m.b(di + 0xC) == 0 else m.w(si)
                elif val == 0x3:                # return
                    si = m.w(di + 0xA)
                elif val == 0x4:                # jump
                    target = m.w(si)
                    if target < si:
                        self.loops[di] = self.loops.get(di, 0) + 1
                    si = target
                elif val == 0xF:                # end
                    m.sb(ACTIVE, m.b(ACTIVE) - 1)
                    return 0xFFFE
                else:
                    raise ValueError(f"unknown command {al:#x} at DS:{si - 1:04x}")
                continue
            old = m.b(di + 8)
            m.sb(di + 8, al & 0x0F)
            m.sb(di + 0xF, old)
            m.sb(di + 9, al >> 4)
            self.note_on(di, m.w(FNUMS + m.b(di + 8) * 2), m.b(di + 9))
            if m.b(di) == 7:
                m.sb(di + 7, 0x40 >> m.b(di + 3))
            else:
                m.sb(di + 7, 0x60 >> m.b(di))
            return si

    def live_channels(self):
        m, di, live = self.mem, CHANNELS, []
        while m.w(di + 0xD) != 0xFFFF:
            if m.w(di + 0xD) != 0xFFFE:
                live.append(di)
            di += CH_SIZE
        return live

    # --- sound effects -------------------------------------------------------
    def play_sfx(self, n):         # image 0x13E6
        m = self.mem
        m.sw(SFX_TICKS, 0x0F)
        bx = m.w(SFX_PATCHES + n * 2)
        m.sb(SFX_ACTIVE, 1)
        self.load_op(bx, 0x13)
        self.load_op(bx + 5, 0x10)
        self.opl(0xC6, m.b(bx + 10))

    def sfx_tick(self):            # image 0x153B
        m = self.mem
        if not m.b(SFX_ACTIVE):
            return
        self.opl(0xBD, 0xEF & m.b(BD_SHADOW))
        self.opl(0xA6, 0x57)
        self.opl(0xB6, 0x01)
        self.opl(0xB6, 0x05)
        self.opl(0xBD, 0x10 | m.b(BD_SHADOW))
        m.sw(SFX_TICKS, m.w(SFX_TICKS) - 1)
        if m.w(SFX_TICKS) == 0:    # image 0x142B: restore the bass drum patch
            self.opl(0xB6, 0x20)
            self.load_op(SFX_RESTORE, 0x13)
            self.load_op(SFX_RESTORE + 5, 0x10)
            self.opl(0xC6, m.b(SFX_RESTORE + 10))
            m.sb(SFX_ACTIVE, 0)


def run_adlib_song(fox, song, loops=1, max_seconds=600):
    """Return (writes, ticks, ended) for one song; stops after `loops` full loops."""
    drv = AdlibDriver(fox)
    drv.play(song)
    limit = int(max_seconds * TICK_HZ)
    while drv.tick < limit:
        drv.tick += 1
        drv.music_tick()
        live = drv.live_channels()
        if not live:
            return drv.writes, drv.tick + int(TICK_HZ * 2), True
        if all(drv.loops.get(di, 0) >= loops for di in live):
            return drv.writes, drv.tick, False
    return drv.writes, drv.tick, False


def run_adlib_sfx(fox, n, tail_seconds=0.6):
    drv = AdlibDriver(fox)
    drv.mem.sb(BD_SHADOW, 0x20)
    drv.opl(0xBD, 0x20)
    drv.play_sfx(n)
    while drv.mem.b(SFX_ACTIVE):
        drv.tick += 1
        drv.sfx_tick()
    return drv.writes, drv.tick + int(TICK_HZ * tail_seconds)


class PCSpeakerDriver:
    """Port of the PC speaker music driver (image 0x12FF non-AdLib path and 0x18E5)."""

    def __init__(self, fox):
        self.mem = Memory(fox)
        self.events = []           # (tick, pit divisor or None for speaker off)
        self.tick = 0
        self.loops = 0

    def play(self, song):
        m = self.mem
        m.sb(CMD_TEMPO, m.b(PC_TEMPOS + song))
        m.sb(CMD_TEMPO_RELOAD, m.b(PC_TEMPOS + song))
        m.sw(PC_PTR, m.w(PC_SONGS + song * 2))
        m.sb(PC_LEFT, 0)

    def tick_once(self):
        m = self.mem
        si = m.w(PC_PTR)
        if si >= 0xFFFE:
            return
        m.sb(CMD_TEMPO, m.b(CMD_TEMPO) - 1)
        if m.b(CMD_TEMPO) == 0:
            m.sb(CMD_TEMPO, m.b(CMD_TEMPO_RELOAD))
            return
        if m.b(PC_LEFT) == 2 and m.b(PC_PARAMS + 4) != 1:
            self.events.append((self.tick, 0x3A98))       # articulation gap
        left = m.b(PC_LEFT)
        m.sb(PC_LEFT, left - 1)
        if left > 1:
            return
        while True:
            al = m.b(si)
            si += 1
            if al & 0x80:
                grp, val = (al & 0x70) >> 4, al & 0x0F
                if grp != 7:
                    m.sb(PC_PARAMS + grp, val)
                    continue
                if val == 0xF:
                    self.events.append((self.tick, None))
                    m.sw(PC_PTR, 0xFFFE)
                    return
                if val == 0x0:
                    m.sw(PC_RET, si + 2)
                    si = m.w(si)
                elif val == 0x1:
                    m.sb(PC_LOOP, m.b(si))
                    si += 1
                elif val == 0x2:
                    m.sb(PC_LOOP, m.b(PC_LOOP) - 1)
                    si = si + 2 if m.b(PC_LOOP) == 0 else m.w(si)
                elif val == 0x3:
                    si = m.w(PC_RET)
                elif val == 0x4:
                    target = m.w(si)
                    if target < si:
                        self.loops += 1
                    si = target
                else:
                    raise ValueError(f"unknown PC command {al:#x}")
                continue
            m.sb(PC_NOTE, al & 0x0F)
            m.sb(PC_OCT, al >> 4)
            div = m.w(PC_DIVISORS + m.b(PC_NOTE) * 2) >> m.b(PC_OCT)
            self.events.append((self.tick, div))
            if m.b(PC_PARAMS) == 7:
                m.sb(PC_LEFT, 0x40 >> m.b(PC_PARAMS + 3))
            else:
                m.sb(PC_LEFT, 0x60 >> m.b(PC_PARAMS))
            m.sw(PC_PTR, si)
            return


def run_pc_song(fox, song, loops=1, max_seconds=600):
    """Return (events, ticks) or None if the song has no PC speaker version."""
    drv = PCSpeakerDriver(fox)
    if drv.mem.w(PC_SONGS + song * 2) == 0:
        return None
    drv.play(song)
    limit = int(max_seconds * TICK_HZ)
    while drv.tick < limit:
        drv.tick += 1
        drv.tick_once()
        if drv.mem.w(PC_PTR) >= 0xFFFE:
            return drv.events, drv.tick + int(TICK_HZ)
        if drv.loops >= loops:
            return drv.events, drv.tick
    return drv.events, drv.tick


def write_dro(path, writes, ticks):
    """DOSBox Raw OPL v2.0 capture (OPL2), playable in AdPlug-based players."""
    regs = sorted({r for _, r, _ in writes})
    if len(regs) > 127:
        raise ValueError("too many distinct registers for a DRO codemap")
    code = {r: i for i, r in enumerate(regs)}
    short_delay, long_delay = 0x7E, 0x7F
    data = bytearray()
    last_ms = 0
    for tick, reg, val in writes:
        ms = int(tick * 1000 / TICK_HZ)
        gap, last_ms = ms - last_ms, ms
        while gap > 0:
            if gap > 256:
                n = min(gap // 256, 256)
                data += bytes((long_delay, n - 1))
                gap -= n * 256
            else:
                data += bytes((short_delay, gap - 1))
                gap = 0
        data += bytes((code[reg], val))
    total_ms = int(ticks * 1000 / TICK_HZ)
    if total_ms > last_ms:
        gap = total_ms - last_ms
        while gap > 0:
            n = min(gap, 256)
            data += bytes((short_delay, n - 1))
            gap -= n
    header = b"DBRAWOPL" + struct.pack("<HHIIBBBBBB", 2, 0, len(data) // 2, total_ms,
                                       0, 0, 0, short_delay, long_delay, len(regs))
    with open(path, "wb") as f:
        f.write(header + bytes(regs) + data)


__all__ = ["FoxExe", "TICK_HZ", "PIT_HZ", "SONG_COUNT", "SFX_COUNT", "run_adlib_song",
           "run_adlib_sfx", "run_pc_song", "write_dro"]
