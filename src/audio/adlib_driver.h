/* The game's AdLib music and sound-effect driver, ported from tools/titus_audio.py
 * (AdlibDriver), which was itself ported from the unpacked FOX.COM.
 *
 * Like the original, it runs on a private copy of FOX.COM's 64 KB data segment, so all song tables, instrument patches and quirks behave
 * exactly as in the game. Register writes go to a callback, normally an OPL emulator.
 *
 * Drive it at the PIT rate (72.82 Hz): call adlib_music_tick and adlib_sfx_tick once per tick.
 */
#ifndef FOXRV_ADLIB_DRIVER_H
#define FOXRV_ADLIB_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADLIB_SONG_COUNT 16
#define ADLIB_SFX_COUNT 14
#define ADLIB_DS_SIZE 0x10000

typedef void (*OplWriteFn)(void *user, uint8_t reg, uint8_t val);

typedef struct {
    uint8_t mem[ADLIB_DS_SIZE];
    OplWriteFn write;
    void *user;
    bool music_on;
} AdlibDriver;

/* Copies the data segment and runs the original init (image 0x1398) and reset. */
bool adlib_init(AdlibDriver *d, const uint8_t *ds, size_t len, OplWriteFn write, void *user);

void adlib_play(AdlibDriver *d, int song);
void adlib_stop(AdlibDriver *d);
void adlib_sfx_start(AdlibDriver *d, int n);

void adlib_music_tick(AdlibDriver *d);
void adlib_sfx_tick(AdlibDriver *d);

/* True while at least one music channel still has data (songs that don't loop end). */
bool adlib_music_active(const AdlibDriver *d);

/* True while a sound effect is playing. */
bool adlib_sfx_active(const AdlibDriver *d);

/* Switches the chip to rhythm mode. Songs do this themselves; sound effects (bass drum)
 * rendered on their own need it first. */
void adlib_enable_rhythm(AdlibDriver *d);

#endif
