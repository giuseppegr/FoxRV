/* Audio output, built so the device callback never runs the chip emulator:
 *
 *  - Sound effects are rendered completely at startup into a RAM cache (each on its own
 *    emulated chip) and mixed in the callback on demand, up to SFX_VOICES at once.
 *  - Music is rendered by a background thread into a ring buffer that it keeps
 *    `music_buffer_ms` ahead of playback. The callback only copies from that buffer, so a
 *    stall in rendering is heard only if it lasts longer than the whole buffer.
 *
 * Difference from the original: there, effects stole the music's bass-drum channel; here they
 * are mixed on top of it.
 *
 * All functions are called from the game thread.
 */
#ifndef FOXRV_AUDIO_H
#define FOXRV_AUDIO_H

#include <stdbool.h>

#include "core/config.h"

#define SFX_VOICES 4

/* The driver runs on FOX.COM's data segment (assets.h). Without an audio device it returns
 * false and the game runs silent. */
bool audio_init(const Settings *s);
void audio_shutdown(void);

void audio_play_song(int song);
void audio_stop_music(void);
/* True once a song that doesn't loop (a jingle) has been heard to the end. Never true
 * without an audio device, so callers waiting for it also need a time limit. */
bool audio_music_finished(void);
/* Holds all output (pause menu); music resumes where it stopped. */
void audio_pause(bool paused);
void audio_play_sfx(int n);

/* 0..100 each. */
void audio_set_volume(int master, int music, int sfx);

#endif
