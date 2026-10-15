#include "platform/audio.h"

#include <SDL3/SDL.h>

#include "audio/adlib_driver.h"
#include "platform/audio_music.h"

#define MIX_CHUNK 1024              /* frames the callback mixes per step */

typedef struct {
    const Sample *sample;           /* NULL = free */
    int pos;
} Voice;

static Settings settings;           /* what audio_init got, for audio_set_device */
static int last_song = -1;          /* restarted after a device change */
static const MusicSource *source;

/* Owned by the callback; the request queue and volumes are guarded by the stream lock,
 * which SDL holds while the callback runs. */
static SDL_AudioStream *stream;
static Sample sfx_cache[ADLIB_SFX_COUNT];
static Voice voices[SFX_VOICES];
static int sfx_requests[8];
static int nsfx_requests;
static int music_volume = 100, sfx_volume = 100;

/* --- device callback ---------------------------------------------------------------------- */

static void start_requested_sfx(void)
{
    for (int r = 0; r < nsfx_requests; r++) {
        const Sample *smp = &sfx_cache[sfx_requests[r]];
        Voice *v = &voices[0];
        for (int i = 0; i < SFX_VOICES; i++) {
            if (voices[i].sample == smp || !voices[i].sample) {   /* restart, or a free voice */
                v = &voices[i];
                break;
            }
            if (voices[i].pos > v->pos)                           /* else steal the oldest */
                v = &voices[i];
        }
        *v = (Voice){smp, 0};
    }
    nsfx_requests = 0;
}

static void SDLCALL feed(void *user, SDL_AudioStream *s, int additional, int total)
{
    int16_t music[MIX_CHUNK], out[MIX_CHUNK];
    int32_t mix[MIX_CHUNK];
    /* A real speaker has one voice: an effect silences the music while it plays. */
    bool one_voice = source == &music_speaker;
    start_requested_sfx();

    int frames = additional / (int)sizeof(int16_t);
    while (frames > 0) {
        int n = SDL_min(frames, MIX_CHUNK);
        int got = source->take(music, n);
        int music_from = 0;
        for (int v = 0; one_voice && v < SFX_VOICES; v++)
            if (voices[v].sample)
                music_from = SDL_max(music_from, SDL_min(n, voices[v].sample->frames - voices[v].pos));
        for (int i = 0; i < n; i++)
            mix[i] = i < got && i >= music_from ? music[i] * music_volume / 100 : 0;

        for (int v = 0; v < SFX_VOICES; v++) {
            Voice *vc = &voices[v];
            if (!vc->sample)
                continue;
            int len = SDL_min(n, vc->sample->frames - vc->pos);
            for (int i = 0; i < len; i++)
                mix[i] += vc->sample->pcm[vc->pos + i] * sfx_volume / 100;
            vc->pos += len;
            if (vc->pos >= vc->sample->frames)
                vc->sample = NULL;
        }

        for (int i = 0; i < n; i++)
            out[i] = (int16_t)SDL_clamp(mix[i], -32768, 32767);
        SDL_PutAudioStreamData(s, out, n * (int)sizeof *out);
        frames -= n;
    }
}

/* --- public API ----------------------------------------------------------------------------- */

static const MusicSource *source_for(AudioDevice d)
{
#if FOXRV_WITH_OPL
    if (d == AUDIO_ADLIB)
        return &music_opl;
#endif
    return &music_speaker;
}

bool audio_init(const Settings *s)
{
    if (s != &settings)
        settings = *s;
    source = source_for(s->audio_device);
    Uint64 t0 = SDL_GetTicks();
    bool ok = source->build_sfx(s, sfx_cache, ADLIB_SFX_COUNT);
    size_t bytes = 0;
    for (int n = 0; n < ADLIB_SFX_COUNT; n++)
        bytes += (size_t)sfx_cache[n].frames * sizeof(int16_t);
    SDL_Log("audio: %d sound effects cached (%zu KB)", ADLIB_SFX_COUNT, bytes / 1024);
    if (!ok || !source->init(s)) {
        SDL_Log("audio: initialisation failed");
        audio_shutdown();
        return false;
    }

    SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, source->rate};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!stream) {
        SDL_Log("audio: no output device (%s); running silent", SDL_GetError());
        audio_shutdown();
        return false;
    }
    audio_set_volume(s->master_volume, s->music_volume, s->sfx_volume);
    SDL_ResumeAudioStreamDevice(stream);
    SDL_Log("audio: driver '%s', %s at %d Hz, tick %.4f Hz, music buffer %d ms, ready in %llu ms",
            SDL_GetCurrentAudioDriver(), source->name, source->rate, s->tick_hz, s->music_buffer_ms,
            (unsigned long long)(SDL_GetTicks() - t0));
    return true;
}

void audio_shutdown(void)
{
    SDL_DestroyAudioStream(stream);     /* stops the callback first */
    stream = NULL;
    if (source)
        source->shutdown();
    source = NULL;
    for (int n = 0; n < ADLIB_SFX_COUNT; n++) {
        SDL_free(sfx_cache[n].pcm);
        sfx_cache[n] = (Sample){0};
    }
    SDL_memset(voices, 0, sizeof voices);
    nsfx_requests = 0;
}

AudioDevice audio_device(void) { return settings.audio_device; }

bool audio_set_device(AudioDevice d)
{
    if (d == settings.audio_device && stream)
        return true;
    audio_shutdown();
    settings.audio_device = d;
    if (!audio_init(&settings))
        return false;
    if (last_song >= 0)
        audio_play_song(last_song);
    return true;
}

static void music_command(MusicCmd cmd, int song)
{
    if (!stream)
        return;
    SDL_LockAudioStream(stream);
    source->command(cmd, song);
    SDL_UnlockAudioStream(stream);
}

bool audio_music_finished(void)
{
    if (!stream)
        return false;
    SDL_LockAudioStream(stream);
    bool done = source->finished();
    SDL_UnlockAudioStream(stream);
    return done;
}

void audio_play_song(int song)
{
    if (song < 0 || song >= ADLIB_SONG_COUNT)
        return;
    last_song = song;
    music_command(MUSIC_PLAY, song);
}

void audio_stop_music(void)
{
    last_song = -1;
    music_command(MUSIC_STOP, 0);
}

void audio_pause(bool paused)
{
    if (!stream)
        return;
    if (paused)
        SDL_PauseAudioStreamDevice(stream);
    else
        SDL_ResumeAudioStreamDevice(stream);
}

void audio_play_sfx(int n)
{
    if (!stream || n < 0 || n >= ADLIB_SFX_COUNT)
        return;
    SDL_LockAudioStream(stream);
    if (nsfx_requests < (int)SDL_arraysize(sfx_requests))
        sfx_requests[nsfx_requests++] = n;
    SDL_UnlockAudioStream(stream);
}

void audio_set_volume(int master, int music, int sfx)
{
    settings.master_volume = master;
    settings.music_volume = music;
    settings.sfx_volume = sfx;
    if (!stream)
        return;
    SDL_SetAudioStreamGain(stream, SDL_clamp(master, 0, 100) / 100.0f);
    SDL_LockAudioStream(stream);
    music_volume = SDL_clamp(music, 0, 100);
    sfx_volume = SDL_clamp(sfx, 0, 100);
    SDL_UnlockAudioStream(stream);
}
