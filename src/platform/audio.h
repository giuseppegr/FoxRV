/* Audio output on one of the original's sound devices ([audio] device):
 *
 *  - AdLib (audio_opl.c): the AdLib driver on an emulated OPL chip, rendered ahead by a
 *    background thread so the device callback never runs the emulator.
 *  - PC speaker (audio_speaker.c): the PC speaker driver on an emulated speaker, rendered in
 *    the callback. A real speaker has one voice, so an effect silences the music meanwhile.
 *
 * Sound effects are rendered completely at startup into a RAM cache and mixed in the callback
 * on demand, up to SFX_VOICES at once.
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

/* The device in use (or that failed to open). Switching restarts the last song from its
 * start; false if the new device couldn't open (the game then runs silent). */
AudioDevice audio_device(void);
bool audio_set_device(AudioDevice d);

/* 0..100 each. */
void audio_set_volume(int master, int music, int sfx);

#endif
