/* Comments name the original routines by their address in the EXE load image. DS offsets and
 * byte/word arithmetic follow tools/titus_audio.py, so both produce identical register writes. */
#include "audio/adlib_driver.h"

#include <string.h>

/* Data-segment addresses used by the driver. */
enum {
    SONG_LISTS = 0x0515,
    INSTR_LISTS = 0x0675,
    CMD_TEMPO = 0x0980,
    CMD_TEMPO_RELOAD = 0x0981,
    FNUMS = 0x0966,
    OP_OFFSETS = 0x0948,
    CH_SLOTS = 0x095B,
    CUR_INSTR_LIST = 0x0917,
    BD_SHADOW = 0x0919,
    ACTIVE = 0x09B2,
    CHANNELS = 0x09B3,
    CH_SIZE = 0x11,
    SFX_PATCHES = 0x0156,
    SFX_TICKS = 0x0172,
    SFX_ACTIVE = 0x0174,
    SFX_RESTORE = 0x0B9A,
};

/* Per-channel record fields (offsets from the record start). */
enum {
    CH_DURATION = 0x0,
    CH_VOLUME = 0x1,
    CH_PARAM3 = 0x3,
    CH_TIE = 0x4,
    CH_OPL = 0x5,
    CH_INSTR = 0x6,
    CH_LEFT = 0x7,
    CH_NOTE = 0x8,
    CH_OCTAVE = 0x9,
    CH_RET = 0xA,
    CH_LOOP = 0xC,
    CH_PTR = 0xD,
    CH_PREV_NOTE = 0xF,
    CH_TIE_LATCH = 0x10,
};

#define PTR_END 0xFFFF      /* end of the channel list */
#define PTR_DONE 0xFFFE     /* channel finished */

static uint8_t rb(const AdlibDriver *d, unsigned a) { return d->mem[a & 0xFFFF]; }
static unsigned rw(const AdlibDriver *d, unsigned a) { return rb(d, a) | (unsigned)rb(d, a + 1) << 8; }
static void wb(AdlibDriver *d, unsigned a, unsigned v) { d->mem[a & 0xFFFF] = (uint8_t)v; }
static void ww(AdlibDriver *d, unsigned a, unsigned v)
{
    wb(d, a, v & 0xFF);
    wb(d, a + 1, (v >> 8) & 0xFF);
}

static void opl(AdlibDriver *d, unsigned reg, unsigned val)
{
    d->write(d->user, (uint8_t)reg, (uint8_t)val);
}

/* x >> n that yields 0 for large n, like the 8-bit original (and unlike C's undefined shift). */
static unsigned shr(unsigned x, unsigned n) { return n < 8 ? x >> n : 0; }

/* The rhythm-mode bit (0xBD) of OPL channel `ch`: 0x10 for channel 6 (bass drum) down to 0x01. */
static unsigned rhythm_bit(int ch) { return ch >= 6 ? shr(0x10, (unsigned)(ch - 6)) : 0; }

static unsigned nth_list(const AdlibDriver *d, unsigned p, int n)
{
    for (int i = 0; i < n; i++) {
        while (rw(d, p) != 0xFFFF)
            p += 2;
        p += 2;
    }
    return p;
}

static void reset(AdlibDriver *d)          /* 0x184F */
{
    static const uint8_t ops[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21};
    for (unsigned r = 0xB0; r < 0xB9; r++)
        opl(d, r, 0);
    for (unsigned r = 0xA0; r < 0xB9; r++)  /* the original loop runs A0..B8 here too */
        opl(d, r, 0);
    opl(d, 0x08, 0);
    opl(d, 0xBD, 0);
    for (size_t i = 0; i < sizeof ops; i++)
        opl(d, 0x40 + ops[i], 0x3F);
}

bool adlib_init(AdlibDriver *d, const uint8_t *ds, size_t len, OplWriteFn write, void *user)
{
    memset(d, 0, sizeof *d);
    if (!ds || len == 0)
        return false;
    memcpy(d->mem, ds, len < ADLIB_DS_SIZE ? len : ADLIB_DS_SIZE);
    d->write = write;
    d->user = user;
    opl(d, 0x01, 0x20);         /* 0x1398: enable waveform select */
    opl(d, 0x08, 0x00);
    opl(d, 0xBD, 0x00);
    reset(d);
    return true;
}

/* --- music -------------------------------------------------------------------------------- */

void adlib_play(AdlibDriver *d, int song)  /* 0x1357 / 0x1469 */
{
    if (song < 0 || song >= ADLIB_SONG_COUNT)
        return;
    reset(d);
    memset(d->loops, 0, sizeof d->loops);
    unsigned chans = nth_list(d, SONG_LISTS, song);
    unsigned instr = nth_list(d, INSTR_LISTS, song);
    unsigned p = instr;
    while (rw(d, p) != 0xFFFF)
        p += 2;
    unsigned tempo = rw(d, p - 2) & 0xFF;
    wb(d, CMD_TEMPO, tempo);
    wb(d, CMD_TEMPO_RELOAD, tempo);
    ww(d, CUR_INSTR_LIST, instr);
    unsigned di = CHANNELS, dl = 0;
    wb(d, ACTIVE, 0xFF);
    for (;;) {
        for (unsigned k = 0; k < CH_SIZE; k++)
            wb(d, di + k, 0);
        unsigned ptr = rw(d, chans);
        chans += 2;
        wb(d, ACTIVE, rb(d, ACTIVE) + 1);
        ww(d, di + CH_PTR, ptr);
        wb(d, di + CH_OPL, dl++);
        di += CH_SIZE;
        if (ptr == 0xFFFF)
            break;
    }
    d->music_on = true;
}

void adlib_stop(AdlibDriver *d)
{
    d->music_on = false;
    reset(d);
}

static unsigned instr_ptr(const AdlibDriver *d, unsigned di)
{
    unsigned bx = rw(d, rw(d, CUR_INSTR_LIST) + rb(d, di + CH_INSTR) * 2u);
    return rb(d, di + CH_INSTR) >= 0x0F ? bx + 1 : bx;
}

static void load_op(AdlibDriver *d, unsigned bx, unsigned dl)     /* 0x17DE */
{
    static const uint8_t bases[] = {0x60, 0x80, 0x40, 0x20, 0xE0};
    for (unsigned i = 0; i < sizeof bases; i++)
        opl(d, bases[i] + dl, rb(d, bx + i));
}

static void set_volume(AdlibDriver *d, unsigned di, unsigned al)  /* 0x167D */
{
    wb(d, di + CH_VOLUME, al);
    unsigned cx = (al + 1) & 0xFF;
    unsigned bx = instr_ptr(d, di);
    unsigned ah = (0u - (((rb(d, bx + 2) & 0x3Fu) - 0x3Fu) & 0xFFu)) & 0xFF;
    unsigned ax = (((ah << 8) >> 4) * cx) & 0xFFFF;
    unsigned vol = (0x3Fu - (ax >> 8)) & 0xFF;
    unsigned slot = rb(d, CH_SLOTS + rb(d, di + CH_OPL));
    if (slot <= 0x0D)
        slot += 3;
    opl(d, 0x40 + rb(d, OP_OFFSETS + slot), vol);
}

static void load_instr(AdlibDriver *d, unsigned di)    /* 0x1734 */
{
    unsigned ch = rb(d, di + CH_OPL);
    unsigned slot = rb(d, CH_SLOTS + ch);
    if (ch <= 6)
        slot += 3;
    unsigned dl = rb(d, OP_OFFSETS + slot);
    unsigned bx = instr_ptr(d, di);
    load_op(d, bx, dl);
    dl -= 3;
    bx += 5;
    if (ch < 7) {
        load_op(d, bx, dl);
        opl(d, 0xC0 + ch, rb(d, bx + 5));
    }
}

static void set_rhythm_instr(AdlibDriver *d, unsigned di)  /* 0x16E9 */
{
    unsigned bl = (rb(d, di + CH_OCTAVE) + 0x0F) & 0xFF;
    wb(d, di + CH_INSTR, bl);
    unsigned bx = rw(d, rw(d, CUR_INSTR_LIST) + bl * 2);
    unsigned cl = rb(d, bx);
    wb(d, di + CH_OPL, cl);
    wb(d, BD_SHADOW, rhythm_bit((int)cl) | rb(d, BD_SHADOW));
    load_instr(d, di);
}

static void set_instr(AdlibDriver *d, unsigned di, unsigned al)   /* 0x16E5 */
{
    if (al == 1) {
        set_rhythm_instr(d, di);
        return;
    }
    wb(d, di + CH_INSTR, al > 1 ? al - 1 : al);
    int cl = (int)rb(d, di + CH_OPL) - 6;
    if (cl >= 0) {
        unsigned ah = ~shr(0x10, (unsigned)cl) & rb(d, BD_SHADOW) & 0xFF;
        wb(d, BD_SHADOW, ah);
        opl(d, 0xBD, ah);
    }
    load_instr(d, di);
}

static void note_on(AdlibDriver *d, unsigned di, unsigned ax, unsigned cl)    /* 0x15C2 */
{
    if (ax) {
        if (rb(d, di + CH_INSTR) >= 0x0F) {    /* drum */
            cl = (cl + 0x0F) & 0xFF;
            if (rb(d, di + CH_INSTR) != cl) {
                set_rhythm_instr(d, di);
                set_volume(d, di, rb(d, di + CH_VOLUME));
            }
            unsigned ch = rb(d, di + CH_OPL);
            opl(d, 0xBD, ~rhythm_bit((int)ch) & rb(d, BD_SHADOW));
            if (ch == 6) {
                opl(d, 0xA6, 0x57);
                opl(d, 0xB6, 0x00);
                opl(d, 0xB6, 0x05);
            }
            opl(d, 0xBD, rb(d, BD_SHADOW));
            return;
        }
        unsigned ch = rb(d, di + CH_OPL);
        cl = (cl + 2) & 0xFF;
        opl(d, 0xA0 + ch, ax & 0xFF);
        if (rb(d, di + CH_TIE_LATCH) != 1)
            opl(d, 0xB0 + ch, 0);
        opl(d, 0xB0 + ch, (ax >> 8) | ((cl << 2) & 0xFF) | 0x20);
    }
    wb(d, di + CH_TIE_LATCH, rb(d, di + CH_TIE));
}

/* Interprets one channel's stream up to its next note. Returns the new stream pointer. */
static unsigned run_channel(AdlibDriver *d, unsigned di, unsigned si)
{
    for (;;) {
        unsigned al = rb(d, si++);
        if (al & 0x80) {
            unsigned grp = (al & 0x70) >> 4, val = al & 0x0F;
            if (grp != 7) {
                if (grp == 1)
                    set_volume(d, di, val);
                else if (grp == 6)
                    set_instr(d, di, val);
                else
                    wb(d, di + grp, val);   /* 0: duration, 2/3: params, 4: tie, 5: channel */
                continue;
            }
            switch (val) {
            case 0x0:                       /* call */
                ww(d, di + CH_RET, si + 2);
                si = rw(d, si);
                break;
            case 0x1:                       /* set loop count */
                wb(d, di + CH_LOOP, rb(d, si));
                si++;
                break;
            case 0x2:                       /* loop */
                wb(d, di + CH_LOOP, rb(d, di + CH_LOOP) - 1);
                si = rb(d, di + CH_LOOP) == 0 ? si + 2 : rw(d, si);
                break;
            case 0x3:                       /* return */
                si = rw(d, di + CH_RET);
                break;
            case 0x4: {                     /* jump; back = the song repeats */
                unsigned target = rw(d, si), ch = (di - CHANNELS) / CH_SIZE;
                if (target < si && ch < ADLIB_MAX_CHANNELS && d->loops[ch] < 255)
                    d->loops[ch]++;
                si = target;
                break;
            }
            case 0xF:                       /* end */
                wb(d, ACTIVE, rb(d, ACTIVE) - 1);
                return PTR_DONE;
            default:                        /* unknown command: stop the channel */
                return PTR_DONE;
            }
            si &= 0xFFFF;
            continue;
        }
        unsigned old = rb(d, di + CH_NOTE);
        wb(d, di + CH_NOTE, al & 0x0F);
        wb(d, di + CH_PREV_NOTE, old);
        wb(d, di + CH_OCTAVE, al >> 4);
        note_on(d, di, rw(d, FNUMS + rb(d, di + CH_NOTE) * 2u), rb(d, di + CH_OCTAVE));
        if (rb(d, di + CH_DURATION) == 7)
            wb(d, di + CH_LEFT, shr(0x40, rb(d, di + CH_PARAM3)));
        else
            wb(d, di + CH_LEFT, shr(0x60, rb(d, di + CH_DURATION)));
        return si & 0xFFFF;
    }
}

void adlib_music_tick(AdlibDriver *d)     /* 0x14AB */
{
    if (!d->music_on)
        return;
    wb(d, CMD_TEMPO, rb(d, CMD_TEMPO) - 1);
    if (rb(d, CMD_TEMPO) == 0) {
        wb(d, CMD_TEMPO, rb(d, CMD_TEMPO_RELOAD));
        return;
    }
    for (unsigned di = CHANNELS;; di += CH_SIZE) {
        unsigned si = rw(d, di + CH_PTR);
        if (si == PTR_END)
            return;
        if (si != PTR_DONE) {
            unsigned left = rb(d, di + CH_LEFT);
            wb(d, di + CH_LEFT, left - 1);
            if (left <= 1)
                si = run_channel(d, di, si);
            ww(d, di + CH_PTR, si);
        }
    }
}

bool adlib_music_played(const AdlibDriver *d)
{
    if (!d->music_on)
        return true;
    unsigned ch = 0;
    for (unsigned di = CHANNELS; rw(d, di + CH_PTR) != PTR_END; di += CH_SIZE, ch++)
        if (rw(d, di + CH_PTR) != PTR_DONE && ch < ADLIB_MAX_CHANNELS && !d->loops[ch])
            return false;
    return true;
}

bool adlib_music_active(const AdlibDriver *d)
{
    if (!d->music_on)
        return false;
    for (unsigned di = CHANNELS; rw(d, di + CH_PTR) != PTR_END; di += CH_SIZE)
        if (rw(d, di + CH_PTR) != PTR_DONE)
            return true;
    return false;
}

/* --- sound effects ------------------------------------------------------------------------ */

bool adlib_sfx_active(const AdlibDriver *d)
{
    return rb(d, SFX_ACTIVE) != 0;
}

void adlib_enable_rhythm(AdlibDriver *d)
{
    wb(d, BD_SHADOW, rb(d, BD_SHADOW) | 0x20);
    opl(d, 0xBD, rb(d, BD_SHADOW));
}

void adlib_sfx_start(AdlibDriver *d, int n)   /* 0x13E6 */
{
    if (n < 0 || n >= ADLIB_SFX_COUNT)
        return;
    ww(d, SFX_TICKS, 0x0F);
    unsigned bx = rw(d, SFX_PATCHES + (unsigned)n * 2);
    wb(d, SFX_ACTIVE, 1);
    load_op(d, bx, 0x13);
    load_op(d, bx + 5, 0x10);
    opl(d, 0xC6, rb(d, bx + 10));
}

void adlib_sfx_tick(AdlibDriver *d)       /* 0x153B */
{
    if (!rb(d, SFX_ACTIVE))
        return;
    opl(d, 0xBD, 0xEF & rb(d, BD_SHADOW));
    opl(d, 0xA6, 0x57);
    opl(d, 0xB6, 0x01);
    opl(d, 0xB6, 0x05);
    opl(d, 0xBD, 0x10 | rb(d, BD_SHADOW));
    ww(d, SFX_TICKS, rw(d, SFX_TICKS) - 1);
    if (rw(d, SFX_TICKS) == 0) {            /* 0x142B: restore the bass drum patch */
        opl(d, 0xB6, 0x20);
        load_op(d, SFX_RESTORE, 0x13);
        load_op(d, SFX_RESTORE + 5, 0x10);
        opl(d, 0xC6, rb(d, SFX_RESTORE + 10));
        wb(d, SFX_ACTIVE, 0);
    }
}
