#include "audio/speaker.h"

#include <math.h>
#include <string.h>

#define PIT_HZ 1193182u
#define LEVEL 5000              /* square-wave amplitude before the low-pass */
#define TONE_TAIL 64            /* frames after an effect for the low-pass to settle */
#define TONE_UPDATE 44          /* frames between pitch changes of a sweep (1 ms) */

/* Phase step per output frame for a PIT divisor (0 = 65536), or 0 when it can't be heard. */
static uint32_t divisor_step(unsigned divisor)
{
    uint64_t d = divisor ? divisor : 0x10000;
    if (2ull * PIT_HZ >= d * SPEAKER_RATE)
        return 0;
    return (uint32_t)(((uint64_t)PIT_HZ << 32) / (d * SPEAKER_RATE));
}

/* One frame of the square wave (0 = silent), through the one-pole low-pass (a = 23/64). */
static int16_t osc_sample(SpeakerOsc *o, uint32_t step)
{
    int32_t x = !step ? 0 : (o->phase & 0x80000000u) ? -LEVEL : LEVEL;
    o->phase += step;
    o->lp += (x - o->lp) * 23 / 64;
    return (int16_t)o->lp;
}

bool speaker_init(Speaker *s, const uint8_t *ds, size_t len, double tick_hz)
{
    memset(s, 0, sizeof *s);
    s->samples_per_tick = SPEAKER_RATE / tick_hz;
    s->until_tick = s->samples_per_tick;    /* first tick one period after start, as on the PIT */
    return pcspk_init(&s->drv, ds, len);
}

void speaker_render(Speaker *s, int16_t *out, int frames)
{
    while (frames > 0) {
        if (s->until_tick <= 0) {
            pcspk_tick(&s->drv);
            s->until_tick += s->samples_per_tick;
        }
        int n = (int)ceil(s->until_tick);
        if (n > frames)
            n = frames;
        uint32_t step = s->drv.gate ? divisor_step(s->drv.divisor) : 0;
        for (int i = 0; i < n; i++)
            out[i] = osc_sample(&s->osc, step);
        out += n;
        s->until_tick -= n;
        frames -= n;
    }
}

/* --- sound effects ------------------------------------------------------------------------ */

static int step_frames(const ToneStep *t) { return (int)((long)t->ms * SPEAKER_RATE / 1000); }

int speaker_tone_frames(const ToneStep *steps)
{
    int frames = TONE_TAIL;
    for (; steps->ms; steps++)
        frames += step_frames(steps);
    return frames;
}

int speaker_tone(const ToneStep *steps, int16_t *out)
{
    SpeakerOsc osc = {0};
    int frames = 0;
    for (; steps->ms; steps++) {
        int n = step_frames(steps);
        uint32_t step = 0;
        for (int i = 0; i < n; i++) {
            if (i % TONE_UPDATE == 0) {     /* the pitch moves in PIT divisor steps, like a real speaker */
                long hz = steps->from_hz + ((long)steps->to_hz - steps->from_hz) * i / n;
                step = hz > 0 ? divisor_step((unsigned)(PIT_HZ / (unsigned long)hz)) : 0;
            }
            out[frames++] = osc_sample(&osc, step);
        }
    }
    for (int i = 0; i < TONE_TAIL; i++)
        out[frames++] = osc_sample(&osc, 0);
    return frames;
}
