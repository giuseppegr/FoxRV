/* AdLib sound: the game's AdLib driver on an emulated OPL chip, built so the device callback
 * never runs the chip emulator:
 *
 *  - Sound effects are rendered completely at startup into a RAM cache (each on its own
 *    emulated chip).
 *  - Music is rendered by a background thread into a ring buffer that it keeps
 *    `music_buffer_ms` ahead of playback. The callback only copies from that buffer, so a
 *    stall in rendering is heard only if it lasts longer than the whole buffer.
 *
 * Difference from the original: there, effects stole the music's bass-drum channel; here they
 * are mixed on top of it.
 */
#include "platform/audio_music.h"

#include <SDL3/SDL.h>

#include "audio/adlib_driver.h"
#include "audio/synth.h"
#include "core/assets.h"

#define RENDER_CHUNK 1024           /* frames the music thread renders per step */
#define SFX_TAIL_MS 600             /* rendered after an effect ends, for its release */
#define SFX_MAX_SECONDS 5
#define SILENCE 16                  /* |sample| below this counts as silence when trimming */
#define IDLE_WAIT_MS 5

/* Shared between the music thread and the callback, guarded by music_lock. */
static SDL_Mutex *music_lock;
static int16_t *ring;
static int ring_cap, ring_read, ring_count;
static MusicCmd music_cmd;
static int music_cmd_song;
static bool music_ended;            /* the song's data ended (it doesn't loop) ... */
static int music_end_left;          /* ... and this many buffered frames are still to play */
static bool music_played;           /* the same for its first time through */
static int music_played_left;

static SDL_Thread *music_thread;
static SDL_AtomicInt music_quit;
static Synth *music_synth;          /* owned by the music thread after start */
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

static bool build_sfx(const Settings *cfg, Sample *cache, int count)
{
    Synth *s = SDL_malloc(sizeof *s);
    if (!s)
        return false;
    bool ok = true;
    for (int n = 0; ok && n < count; n++)
        ok = render_sfx(s, assets_data_segment(), ASSETS_DS_SIZE, cfg->tick_hz, n, &cache[n]);
    SDL_free(s);
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
                music_ended = music_played = false;
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
            if (!music_played && adlib_music_played(&music_synth->drv)) {
                music_played = true;
                music_played_left = ring_count;
            }
        }
        SDL_UnlockMutex(music_lock);
    }
    return 0;
}

/* Takes up to `frames` rendered music frames; returns how many there were. */
static int take(int16_t *out, int frames)
{
    SDL_LockMutex(music_lock);
    int n = SDL_min(frames, ring_count);
    int first = SDL_min(n, ring_cap - ring_read);
    SDL_memcpy(out, ring + ring_read, (size_t)first * sizeof *out);
    SDL_memcpy(out + first, ring, (size_t)(n - first) * sizeof *out);
    ring_read = (ring_read + n) % ring_cap;
    ring_count -= n;
    music_end_left = SDL_max(music_end_left - n, 0);
    music_played_left = SDL_max(music_played_left - n, 0);
    SDL_UnlockMutex(music_lock);
    if (n < frames)
        underruns++;
    return n;
}

/* --- source ------------------------------------------------------------------------------- */

static void shutdown(void)
{
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
    music_cmd = MUSIC_NONE;
    music_ended = music_played = false;
    ring_read = ring_count = music_end_left = music_played_left = 0;
    underruns = 0;
}

static bool init(const Settings *s)
{
    music_synth = SDL_malloc(sizeof *music_synth);
    ring_cap = SDL_max(SYNTH_RATE * s->music_buffer_ms / 1000, RENDER_CHUNK * 2);
    ring = SDL_malloc((size_t)ring_cap * sizeof *ring);
    music_lock = SDL_CreateMutex();
    if (!music_synth || !ring || !music_lock ||
        !synth_init(music_synth, assets_data_segment(), ASSETS_DS_SIZE, s->tick_hz)) {
        shutdown();
        return false;
    }
    SDL_SetAtomicInt(&music_quit, 0);
    music_thread = SDL_CreateThread(music_main, "music", NULL);
    if (!music_thread) {
        shutdown();
        return false;
    }
    return true;
}

static void command(MusicCmd cmd, int song)
{
    SDL_LockMutex(music_lock);
    music_cmd = cmd;
    music_cmd_song = song;
    music_ended = music_played = false;
    SDL_UnlockMutex(music_lock);
}

static bool finished(void)
{
    SDL_LockMutex(music_lock);
    bool done = music_ended && music_end_left == 0;
    SDL_UnlockMutex(music_lock);
    return done;
}

static bool played(void)
{
    SDL_LockMutex(music_lock);
    bool done = music_played && music_played_left == 0;
    SDL_UnlockMutex(music_lock);
    return done;
}

const MusicSource music_opl = {"AdLib", SYNTH_RATE, init, shutdown, build_sfx, command, take, finished, played};
