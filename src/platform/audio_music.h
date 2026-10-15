/* The sound devices behind audio.c's mixer: each renders the music and the sound-effect cache
 * at its own output rate. audio.c calls `command`, `take` and `finished` with the audio stream
 * locked (SDL holds that lock while the callback runs), so they never run at the same time. */
#ifndef FOXRV_AUDIO_MUSIC_H
#define FOXRV_AUDIO_MUSIC_H

#include <stdbool.h>
#include <stdint.h>

#include "core/config.h"

typedef struct {
    int16_t *pcm;
    int frames;
} Sample;

typedef enum { MUSIC_NONE, MUSIC_PLAY, MUSIC_STOP } MusicCmd;

typedef struct {
    const char *name;
    int rate;                                           /* output frames per second */
    bool (*init)(const Settings *s);
    void (*shutdown)(void);                             /* after the stream is gone */
    bool (*build_sfx)(const Settings *s, Sample *cache, int count);
    void (*command)(MusicCmd cmd, int song);
    int (*take)(int16_t *out, int frames);              /* returns the frames there were */
    bool (*finished)(void);
    bool (*played)(void);                               /* ended, or heard all through once */
} MusicSource;

#if FOXRV_WITH_OPL
extern const MusicSource music_opl;
#endif
extern const MusicSource music_speaker;

#endif
