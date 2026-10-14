#include "audio/synth.h"

#include <math.h>

#define CHUNK_FRAMES 512

/* Buffered writes are applied a couple of samples apart, like the real chip's write delay.
 * Immediate writes would put the driver's key-off/key-on pairs in the same sample, so notes
 * would never retrigger and the music would fade to silence. */
static void opl_write(void *user, uint8_t reg, uint8_t val)
{
    OPL3_WriteRegBuffered(user, reg, val);
}

bool synth_init(Synth *s, const uint8_t *ds, size_t len, double tick_hz)
{
    OPL3_Reset(&s->chip, SYNTH_RATE);
    s->samples_per_tick = SYNTH_RATE / tick_hz;
    s->until_tick = s->samples_per_tick;    /* first tick one period after start, as on the PIT */
    return adlib_init(&s->drv, ds, len, opl_write, &s->chip);
}

void synth_render(Synth *s, int16_t *out, int frames)
{
    int16_t stereo[CHUNK_FRAMES * 2];
    while (frames > 0) {
        if (s->until_tick <= 0) {
            adlib_music_tick(&s->drv);
            adlib_sfx_tick(&s->drv);
            s->until_tick += s->samples_per_tick;
        }
        int n = frames < CHUNK_FRAMES ? frames : CHUNK_FRAMES;
        int to_tick = (int)ceil(s->until_tick);
        if (to_tick < n)
            n = to_tick;
        OPL3_GenerateStream(&s->chip, stereo, (uint32_t)n);
        for (int i = 0; i < n; i++)
            out[i] = stereo[i * 2];     /* OPL2 mode: both outputs carry the same signal */
        out += n;
        s->until_tick -= n;
        frames -= n;
    }
}
