#include "platform/audio.h"

#include <SDL3/SDL.h>

#include "audio/adlib_driver.h"
#include "audio/synth.h"
#include "core/assets.h"

#define RENDER_CHUNK 1024           /* frames the music thread renders per step */
#define MIX_CHUNK 1024              /* frames the callback mixes per step */
#define SFX_TAIL_MS 600             /* rendered after an effect ends, for its release */
#define SFX_MAX_SECONDS 5
#define SILENCE 16                  /* |sample| below this counts as silence when trimming */
#define IDLE_WAIT_MS 5

typedef struct {
    int16_t *pcm;
    int frames;
} Sample;

typedef struct {
    const Sample *sample;           /* NULL = free */
    int pos;
} Voice;

typedef enum { MUSIC_NONE, MUSIC_PLAY, MUSIC_STOP } MusicCmd;

/* Shared between the music thread and the callback, guarded by music_lock. */
static SDL_Mutex *music_lock;
static int16_t *ring;
static int ring_cap, ring_read, ring_count;
static MusicCmd music_cmd;
static int music_cmd_song;
static bool music_ended;            /* the song's data ended (it doesn't loop) ... */
static int music_end_left;          /* ... and this many buffered frames are still to play */

static SDL_Thread *music_thread;
static SDL_AtomicInt music_quit;
static Synth *music_synth;          /* owned by the music thread after start */

/* Owned by the callback; the request queue and volumes are guarded by the stream lock,
 * which SDL holds while the callback runs. */
static SDL_AudioStream *stream;
static Sample sfx_cache[ADLIB_SFX_COUNT];
static Voice voices[SFX_VOICES];
static int sfx_requests[8];
static int nsfx_requests;
static int music_volume = 100, sfx_volume = 100;
static unsigned underruns;          /* callback found the music buffer empty */

/* --- sound-effect cache ------------------------------------------------------------------- */

static bool render_sfx(Synth *s, const uint8_t *ds, size_t len, double tick_hz, int n, Sample *out)
{
    int cap = SYNTH_RATE * SFX_MAX_SECONDS;
    int16_t *pcm = SDL_malloc((size_t)cap * sizeof *pcm);
    if (!pcm || !synth_init(s, ds, len, tick_hz)) {
        SDL_free(pcm);
        return false;
    }
    adlib_enable_rhythm(&s->drv);
    adlib_sfx_start(&s->drv, n);

    int frames = 0, tail = SYNTH_RATE * SFX_TAIL_MS / 1000;
    while (frames < cap && (adlib_sfx_active(&s->drv) || tail > 0)) {
        int step = SDL_min(RENDER_CHUNK, cap - frames);
        if (!adlib_sfx_active(&s->drv))
            tail -= step;
        synth_render(s, pcm + frames, step);
        frames += step;
    }
    while (frames > 0 && SDL_abs(pcm[frames - 1]) < SILENCE)
        frames--;
    out->pcm = SDL_realloc(pcm, (size_t)SDL_max(frames, 1) * sizeof *pcm);
    out->frames = frames;
    if (!out->pcm)
        out->pcm = pcm;
    return true;
}

static bool build_sfx_cache(const uint8_t *ds, size_t len, double tick_hz)
{
    Synth *s = SDL_malloc(sizeof *s);
    if (!s)
        return false;
    size_t bytes = 0;
    bool ok = true;
    for (int n = 0; ok && n < ADLIB_SFX_COUNT; n++) {
        ok = render_sfx(s, ds, len, tick_hz, n, &sfx_cache[n]);
        bytes += (size_t)sfx_cache[n].frames * sizeof(int16_t);
    }
    SDL_free(s);
    SDL_Log("audio: %d sound effects cached (%zu KB)", ADLIB_SFX_COUNT, bytes / 1024);
    return ok;
}

/* --- music thread ------------------------------------------------------------------------- */

/* After a song change the old song keeps playing from the buffer until the first chunk of
 * the new one is ready; then the buffer is swapped in one step, so there is no gap. */
static int SDLCALL music_main(void *unused)
{
    int16_t chunk[RENDER_CHUNK];
    bool flush = false;
    while (!SDL_GetAtomicInt(&music_quit)) {
        SDL_LockMutex(music_lock);
        MusicCmd cmd = music_cmd;
        int song = music_cmd_song;
        music_cmd = MUSIC_NONE;
        int space = ring_cap - ring_count;
        SDL_UnlockMutex(music_lock);

        if (cmd == MUSIC_PLAY)
            adlib_play(&music_synth->drv, song);
        else if (cmd == MUSIC_STOP)
            adlib_stop(&music_synth->drv);
        flush |= cmd != MUSIC_NONE;

        if (!flush && space < RENDER_CHUNK) {
            SDL_Delay(IDLE_WAIT_MS);
            continue;
        }
        synth_render(music_synth, chunk, RENDER_CHUNK);

        SDL_LockMutex(music_lock);
        if (music_cmd == MUSIC_NONE) {      /* else it belongs to a song that was replaced */
            if (flush) {
                ring_read = ring_count = 0;
                music_ended = false;
                flush = false;
            }
            int w = (ring_read + ring_count) % ring_cap;
            int first = SDL_min(RENDER_CHUNK, ring_cap - w);
            SDL_memcpy(ring + w, chunk, (size_t)first * sizeof *chunk);
            SDL_memcpy(ring, chunk + first, (size_t)(RENDER_CHUNK - first) * sizeof *chunk);
            ring_count += RENDER_CHUNK;
            if (!music_ended && !adlib_music_active(&music_synth->drv)) {
                music_ended = true;
                music_end_left = ring_count;
            }
        }
        SDL_UnlockMutex(music_lock);
    }
    return 0;
}

/* Takes up to `frames` rendered music frames; returns how many there were. */
static int take_music(int16_t *out, int frames)
{
    SDL_LockMutex(music_lock);
    int n = SDL_min(frames, ring_count);
    int first = SDL_min(n, ring_cap - ring_read);
    SDL_memcpy(out, ring + ring_read, (size_t)first * sizeof *out);
    SDL_memcpy(out + first, ring, (size_t)(n - first) * sizeof *out);
    ring_read = (ring_read + n) % ring_cap;
    ring_count -= n;
    music_end_left = SDL_max(music_end_left - n, 0);
    SDL_UnlockMutex(music_lock);
    if (n < frames)
        underruns++;
    return n;
}

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
    start_requested_sfx();

    int frames = additional / (int)sizeof(int16_t);
    while (frames > 0) {
        int n = SDL_min(frames, MIX_CHUNK);
        int got = take_music(music, n);
        for (int i = 0; i < n; i++)
            mix[i] = i < got ? music[i] * music_volume / 100 : 0;

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

bool audio_init(const Settings *s, const char *ds_rel)
{
    size_t size;
    uint8_t *ds = assets_load(ds_rel, &size);
    if (!ds) {
        SDL_Log("audio: run tools/export_driver_data.py to create %s", ds_rel);
        return false;
    }
    Uint64 t0 = SDL_GetTicks();
    bool ok = build_sfx_cache(ds, size, s->tick_hz);

    music_synth = SDL_malloc(sizeof *music_synth);
    ring_cap = SDL_max(SYNTH_RATE * s->music_buffer_ms / 1000, RENDER_CHUNK * 2);
    ring = SDL_malloc((size_t)ring_cap * sizeof *ring);
    music_lock = SDL_CreateMutex();
    ok = ok && music_synth && ring && music_lock && synth_init(music_synth, ds, size, s->tick_hz);
    SDL_free(ds);
    if (!ok) {
        SDL_Log("audio: initialisation failed");
        audio_shutdown();
        return false;
    }
    SDL_SetAtomicInt(&music_quit, 0);
    music_thread = SDL_CreateThread(music_main, "music", NULL);

    SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, SYNTH_RATE};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!music_thread || !stream) {
        SDL_Log("audio: no output device (%s); running silent", SDL_GetError());
        audio_shutdown();
        return false;
    }
    audio_set_volume(s->master_volume, s->music_volume, s->sfx_volume);
    SDL_ResumeAudioStreamDevice(stream);
    SDL_Log("audio: driver '%s', OPL at %d Hz, tick %.4f Hz, music buffer %d ms, ready in %llu ms",
            SDL_GetCurrentAudioDriver(), SYNTH_RATE, s->tick_hz, s->music_buffer_ms,
            (unsigned long long)(SDL_GetTicks() - t0));
    return true;
}

void audio_shutdown(void)
{
    SDL_DestroyAudioStream(stream);     /* stops the callback first */
    stream = NULL;
    if (music_thread) {
        SDL_SetAtomicInt(&music_quit, 1);
        SDL_WaitThread(music_thread, NULL);
        music_thread = NULL;
        if (underruns)
            SDL_Log("audio: music buffer ran empty %u times", underruns);
    }
    SDL_DestroyMutex(music_lock);
    music_lock = NULL;
    SDL_free(ring);
    ring = NULL;
    SDL_free(music_synth);
    music_synth = NULL;
    for (int n = 0; n < ADLIB_SFX_COUNT; n++) {
        SDL_free(sfx_cache[n].pcm);
        sfx_cache[n] = (Sample){0};
    }
    SDL_memset(voices, 0, sizeof voices);
}

static void music_command(MusicCmd cmd, int song)
{
    if (!music_lock)
        return;
    SDL_LockMutex(music_lock);
    music_cmd = cmd;
    music_cmd_song = song;
    music_ended = false;
    SDL_UnlockMutex(music_lock);
}

bool audio_music_finished(void)
{
    if (!music_lock)
        return false;
    SDL_LockMutex(music_lock);
    bool done = music_ended && music_end_left == 0;
    SDL_UnlockMutex(music_lock);
    return done;
}

void audio_play_song(int song)
{
    if (song >= 0 && song < ADLIB_SONG_COUNT)
        music_command(MUSIC_PLAY, song);
}

void audio_stop_music(void) { music_command(MUSIC_STOP, 0); }

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
    if (!stream)
        return;
    SDL_SetAudioStreamGain(stream, SDL_clamp(master, 0, 100) / 100.0f);
    SDL_LockAudioStream(stream);
    music_volume = SDL_clamp(music, 0, 100);
    sfx_volume = SDL_clamp(sfx, 0, 100);
    SDL_UnlockAudioStream(stream);
}
