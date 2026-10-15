/* PC speaker sound: the game's PC speaker driver on an emulated speaker. It costs next to
 * nothing, so the music is rendered right in the device callback, without a thread or a
 * buffer ahead (the 3DS runs threads cooperatively on one core). The sound effects are new
 * (speaker_sfx.c): the original played none on the speaker. */
#include "platform/audio_music.h"

#include <SDL3/SDL.h>

#include "audio/speaker.h"
#include "core/assets.h"

static Speaker *speaker;

static bool init(const Settings *s)
{
    speaker = SDL_malloc(sizeof *speaker);
    if (!speaker || !speaker_init(speaker, assets_data_segment(), ASSETS_DS_SIZE, s->tick_hz)) {
        SDL_free(speaker);
        speaker = NULL;
        return false;
    }
    return true;
}

static void shutdown(void)
{
    SDL_free(speaker);
    speaker = NULL;
}

static bool build_sfx(const Settings *s, Sample *cache, int count)
{
    for (int n = 0; n < count; n++) {
        const ToneStep *steps = speaker_sfx(n);
        cache[n].pcm = SDL_malloc((size_t)speaker_tone_frames(steps) * sizeof *cache[n].pcm);
        if (!cache[n].pcm)
            return false;
        cache[n].frames = speaker_tone(steps, cache[n].pcm);
    }
    return true;
}

/* Songs without a PC speaker version (song 8) stop the music, so they count as finished. */
static void command(MusicCmd cmd, int song)
{
    if (cmd == MUSIC_PLAY)
        pcspk_play(&speaker->drv, song);
    else if (cmd == MUSIC_STOP)
        pcspk_stop(&speaker->drv);
}

static int take(int16_t *out, int frames)
{
    speaker_render(speaker, out, frames);
    return frames;
}

static bool finished(void) { return !pcspk_active(&speaker->drv); }

const MusicSource music_speaker = {"PC speaker", SPEAKER_RATE, init, shutdown, build_sfx, command, take, finished};
