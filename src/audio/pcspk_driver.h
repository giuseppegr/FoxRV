/* The game's PC speaker music driver, ported from tools/titus_audio.py (PCSpeakerDriver),
 * itself ported from the unpacked FOX.COM (image 0x12FF non-AdLib path and 0x18E5).
 *
 * One voice: the same stream encoding as the AdLib driver, but only notes, durations and the
 * F0-F4 / FF commands do anything. Each note programs PIT channel 2 with a divisor; this port
 * records the divisor and whether the speaker gate is open, and speaker.c turns that into sound.
 *
 * Like the AdLib driver it runs on a private copy of FOX.COM's data segment. Drive it at the PIT
 * rate (72.82 Hz). The original had no PC speaker sound effects.
 */
#ifndef FOXRV_PCSPK_DRIVER_H
#define FOXRV_PCSPK_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio/adlib_driver.h"

typedef struct {
    uint8_t mem[ADLIB_DS_SIZE];
    uint16_t divisor;           /* PIT channel 2 divisor; 0 = 65536, as on the PIT */
    bool gate;                  /* port 61h bits 0-1: the speaker sounds */
    unsigned writes;            /* divisor writes so far (--dump-pcspk) */
} PcspkDriver;

bool pcspk_init(PcspkDriver *d, const uint8_t *ds, size_t len);

/* Song 8 has no PC speaker version. */
bool pcspk_has_song(const PcspkDriver *d, int song);

void pcspk_play(PcspkDriver *d, int song);
void pcspk_stop(PcspkDriver *d);
void pcspk_tick(PcspkDriver *d);

/* True while the song still has data (songs that don't loop end). */
bool pcspk_active(const PcspkDriver *d);

#endif
