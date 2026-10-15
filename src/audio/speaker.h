/* A PC speaker played by one copy of the game's PC speaker driver.
 *
 * The speaker is a square wave at PIT_HZ / divisor, switched by the gate, through a gentle
 * one-pole low-pass that stands in for the speaker cone (as tools/export_sound.py's render_pc).
 * Tones above the Nyquist frequency (the driver's ultrasonic "rest") are silent.
 *
 * Renders mono audio at SPEAKER_RATE, ticking the driver every 1/tick_hz seconds of generated
 * audio. Not thread-safe: each user owns its Speaker.
 */
#ifndef FOXRV_SPEAKER_H
#define FOXRV_SPEAKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio/pcspk_driver.h"

#define SPEAKER_RATE 44100

typedef struct {
    uint32_t phase;
    int32_t lp;                 /* low-pass state */
} SpeakerOsc;

typedef struct {
    PcspkDriver drv;
    SpeakerOsc osc;
    double samples_per_tick;
    double until_tick;          /* samples left before the next driver tick */
} Speaker;

bool speaker_init(Speaker *s, const uint8_t *ds, size_t len, double tick_hz);

void speaker_render(Speaker *s, int16_t *out, int frames);

/* A sound effect: steps sweeping linearly from `from_hz` to `to_hz` (0 = silence), ended by
 * a step of 0 ms. The original had no PC speaker effects; these are new. */
typedef struct {
    uint16_t from_hz, to_hz, ms;
} ToneStep;

/* The tone steps for sound effect `n` (0..ADLIB_SFX_COUNT-1). */
const ToneStep *speaker_sfx(int n);

/* Frames speaker_tone writes for `steps`. */
int speaker_tone_frames(const ToneStep *steps);
/* Renders `steps` on a speaker of its own; returns the frames written. */
int speaker_tone(const ToneStep *steps, int16_t *out);

#endif
