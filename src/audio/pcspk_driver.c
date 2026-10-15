/* Comments name the original routines by their address in the EXE load image. DS offsets and
 * byte/word arithmetic follow tools/titus_audio.py, so both produce identical divisors. */
#include "audio/pcspk_driver.h"

#include <string.h>

/* Data-segment addresses used by the driver. */
enum {
    CMD_TEMPO = 0x0980,         /* shared with the AdLib driver */
    CMD_TEMPO_RELOAD = 0x0981,
    SONGS = 0x0982,             /* stream pointer per song, 0 = no PC speaker version */
    TEMPOS = 0x09A2,            /* tempo byte per song */
    ACTIVE = 0x09B2,
    PTR = 0x0B07,
    LEFT = 0x0B09,              /* ticks left of the current note */
    RET = 0x0B0A,
    LOOP = 0x0B0C,
    OCTAVE = 0x0B0D,
    NOTE = 0x0B0E,
    PARAMS = 0x0B0F,            /* command groups 8x..Ex store their value at PARAMS + group */
    DIVISORS = 0x0B20,          /* octave-0 PIT divisor per note, shifted right by the octave */
};

#define PARAM_DURATION 0
#define PARAM_PARAM3 3
#define PARAM_TIE 4
#define PTR_DONE 0xFFFE
#define GAP_DIVISOR 0x3A98      /* 79.5 Hz buzz in the last ticks of an untied note */

static uint8_t rb(const PcspkDriver *d, unsigned a) { return d->mem[a & 0xFFFF]; }
static unsigned rw(const PcspkDriver *d, unsigned a) { return rb(d, a) | (unsigned)rb(d, a + 1) << 8; }
static void wb(PcspkDriver *d, unsigned a, unsigned v) { d->mem[a & 0xFFFF] = (uint8_t)v; }
static void ww(PcspkDriver *d, unsigned a, unsigned v)
{
    wb(d, a, v & 0xFF);
    wb(d, a + 1, (v >> 8) & 0xFF);
}

/* x >> n that yields 0 for large n, like the 8-bit original (and unlike C's undefined shift). */
static unsigned shr(unsigned x, unsigned n) { return n < 16 ? x >> n : 0; }

bool pcspk_init(PcspkDriver *d, const uint8_t *ds, size_t len)
{
    memset(d, 0, sizeof *d);
    if (!ds || len == 0)
        return false;
    memcpy(d->mem, ds, len < ADLIB_DS_SIZE ? len : ADLIB_DS_SIZE);
    ww(d, PTR, PTR_DONE);
    return true;
}

bool pcspk_has_song(const PcspkDriver *d, int song)
{
    return song >= 0 && song < ADLIB_SONG_COUNT && rw(d, SONGS + (unsigned)song * 2) != 0;
}

void pcspk_play(PcspkDriver *d, int song)  /* 0x12FF, speaker path */
{
    if (!pcspk_has_song(d, song)) {
        pcspk_stop(d);
        return;
    }
    wb(d, CMD_TEMPO, rb(d, TEMPOS + (unsigned)song));
    wb(d, CMD_TEMPO_RELOAD, rb(d, TEMPOS + (unsigned)song));
    ww(d, PTR, rw(d, SONGS + (unsigned)song * 2));
    wb(d, LEFT, 0);
    wb(d, ACTIVE, 1);
    d->gate = true;             /* the previous divisor sounds until the first note */
}

void pcspk_stop(PcspkDriver *d)
{
    ww(d, PTR, PTR_DONE);
    wb(d, ACTIVE, 0);
    d->gate = false;
}

bool pcspk_active(const PcspkDriver *d)
{
    return rw(d, PTR) < PTR_DONE;
}

static void end_song(PcspkDriver *d)
{
    d->gate = false;            /* 61h &= FC */
    ww(d, PTR, PTR_DONE);
    wb(d, ACTIVE, rb(d, ACTIVE) - 1);
}

void pcspk_tick(PcspkDriver *d)            /* 0x18E5 */
{
    unsigned si = rw(d, PTR);
    if (si >= PTR_DONE)
        return;
    wb(d, CMD_TEMPO, rb(d, CMD_TEMPO) - 1);
    if (rb(d, CMD_TEMPO) == 0) {
        wb(d, CMD_TEMPO, rb(d, CMD_TEMPO_RELOAD));
        return;
    }
    unsigned left = rb(d, LEFT);
    if (left == 2 && rb(d, PARAMS + PARAM_TIE) != 1) {
        d->divisor = GAP_DIVISOR;
        d->writes++;
    }
    wb(d, LEFT, left - 1);
    if (left > 1)
        return;
    for (;;) {
        unsigned al = rb(d, si);
        si = (si + 1) & 0xFFFF;
        if (al & 0x80) {
            unsigned group = (al & 0x70) >> 4, val = al & 0x0F;
            if (group != 7) {
                wb(d, PARAMS + group, val);
                continue;
            }
            switch (val) {
            case 0x0:               /* call */
                ww(d, RET, si + 2);
                si = rw(d, si);
                break;
            case 0x1:               /* set loop count */
                wb(d, LOOP, rb(d, si));
                si = (si + 1) & 0xFFFF;
                break;
            case 0x2:               /* loop */
                wb(d, LOOP, rb(d, LOOP) - 1);
                si = rb(d, LOOP) == 0 ? (si + 2) & 0xFFFF : rw(d, si);
                break;
            case 0x3:               /* return */
                si = rw(d, RET);
                break;
            case 0x4:               /* jump */
                si = rw(d, si);
                break;
            default:                /* 0xFF: end; nothing else is used */
                end_song(d);
                return;
            }
            continue;
        }
        wb(d, NOTE, al & 0x0F);
        wb(d, OCTAVE, al >> 4);
        d->divisor = (uint16_t)shr(rw(d, DIVISORS + rb(d, NOTE) * 2u), rb(d, OCTAVE));
        d->writes++;
        if (rb(d, PARAMS + PARAM_DURATION) == 7)
            wb(d, LEFT, shr(0x40, rb(d, PARAMS + PARAM_PARAM3)));
        else
            wb(d, LEFT, shr(0x60, rb(d, PARAMS + PARAM_DURATION)));
        ww(d, PTR, si);
        return;
    }
}
