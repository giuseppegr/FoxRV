/* One emulated OPL chip (Nuked-OPL3) played by one copy of the game's AdLib driver.
 *
 * Renders mono audio at the chip's native rate, ticking the driver (music and sound effects)
 * every 1/tick_hz seconds of generated audio. Not thread-safe: each user owns its Synth.
 */
#ifndef FOXRV_SYNTH_H
#define FOXRV_SYNTH_H

#include <stddef.h>
#include <stdint.h>

#include "audio/adlib_driver.h"
#include "opl3.h"

#define SYNTH_RATE 49716

typedef struct {
    opl3_chip chip;
    AdlibDriver drv;
    double samples_per_tick;
    double until_tick;          /* samples left before the next driver tick */
} Synth;

/* Resets the chip and driver. The Synth must not move afterwards (the driver points at it). */
bool synth_init(Synth *s, const uint8_t *ds, size_t len, double tick_hz);

void synth_render(Synth *s, int16_t *out, int frames);

#endif
