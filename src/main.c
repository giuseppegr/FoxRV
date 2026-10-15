/* FOXRV: Titus the Fox engine.
 *
 *   foxrv                     intro, then the main menu
 *   foxrv play-number         straight into that level
 *   foxrv [play-number] --shot FILE.bmp [--at SECONDS] [--press FRAME:ACTION,...]
 *                             save the frame after SECONDS of game time (default 1) and exit;
 *                             --press presses [input] actions at game frames, e.g.
 *                             "120:action,200:down"; without --press, --shot starts in level 1
 *   foxrv --selftest          headless physics checks against the original's values
 *   foxrv --dump-opl SONG [TICKS]
 *                             print the driver's OPL writes as "tick reg val" (no audio device)
 *   foxrv --dump-pcspk SONG [TICKS]
 *                             print the PC speaker driver's divisors as "tick divisor", and
 *                             "tick off" when the song ends (no audio device)
 *   --debug                   with any of the above: print the log (off by default; on
 *                             Windows the game has no console window unless it's given)
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/adlib_driver.h"
#include "audio/pcspk_driver.h"
#include "core/assets.h"
#include "core/config.h"
#include "game/game.h"
#include "game/selftest.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "platform/video.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

/* Windows builds are GUI programs (no console window). Output that was asked for goes to
 * the terminal the game was started from, else to a new console; output already
 * redirected to a file or pipe stays there. */
static void console_open(void)
{
#ifdef _WIN32
    bool out = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN;
    bool err = GetFileType(GetStdHandle(STD_ERROR_HANDLE)) != FILE_TYPE_UNKNOWN;
    if (out && err)
        return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        AllocConsole();
    if (!out)
        freopen("CONOUT$", "w", stdout);
    if (!err)
        freopen("CONOUT$", "w", stderr);
#endif
}

static void log_print(void *user, int category, SDL_LogPriority priority, const char *message)
{
    fprintf(stderr, "%s\n", message);
    fflush(stderr);
}

static void log_discard(void *user, int category, SDL_LogPriority priority, const char *message)
{
}

/* Removes `flag` from the arguments; returns whether it was there. */
static bool take_flag(int *argc, char **argv, const char *flag)
{
    for (int i = 1; i < *argc; i++) {
        if (!strcmp(argv[i], flag)) {
            memmove(&argv[i], &argv[i + 1], (size_t)(*argc - i) * sizeof *argv);
            (*argc)--;
            return true;
        }
    }
    return false;
}

#if FOXRV_WITH_OPL
static void opl_print(void *user, uint8_t reg, uint8_t val)
{
    printf("%d %d %d\n", *(int *)user, reg, val);
}

static int dump_opl(int song, int ticks)
{
    static AdlibDriver d;
    int tick = 0;
    adlib_init(&d, assets_data_segment(), ASSETS_DS_SIZE, opl_print, &tick);
    adlib_play(&d, song);
    while (tick < ticks) {
        tick++;
        adlib_music_tick(&d);
    }
    return 0;
}
#endif

static int dump_pcspk(int song, int ticks)
{
    static PcspkDriver d;
    pcspk_init(&d, assets_data_segment(), ASSETS_DS_SIZE);
    pcspk_play(&d, song);
    unsigned writes = d.writes;
    for (int tick = 1; tick <= ticks && pcspk_active(&d); tick++) {
        pcspk_tick(&d);
        if (d.writes != writes)
            printf("%d %u\n", tick, d.divisor);
        if (!pcspk_active(&d))
            printf("%d off\n", tick);
        writes = d.writes;
    }
    return 0;
}

#define MAX_PRESSES 64

typedef struct {
    unsigned frame;
    Action action;
} Press;

/* "120:action,200:down" -> presses; returns how many. */
static int parse_presses(const char *spec, Press *out)
{
    int n = 0;
    char buf[1024];
    SDL_strlcpy(buf, spec, sizeof buf);
    char *save = NULL;
    for (char *tok = SDL_strtok_r(buf, ",", &save); tok && n < MAX_PRESSES; tok = SDL_strtok_r(NULL, ",", &save)) {
        char *colon = strchr(tok, ':');
        Action a = colon ? input_action_by_name(colon + 1) : ACT_COUNT;
        if (a == ACT_COUNT) {
            SDL_Log("--press: can't read '%s'", tok);
            continue;
        }
        out[n++] = (Press){(unsigned)atoi(tok), a};
    }
    return n;
}

/* Sections not owned by Settings: [input] and the game's [player] / [camera]. */
static bool other_config(const char *section, const char *key, const char *value)
{
    return input_config(section, key, value) || game_config(section, key, value);
}

static void load_settings(Settings *s)
{
    settings_defaults(s);
    if (config_load(s, "engine.ini", other_config))
        return;
    const char *base = SDL_GetBasePath();
    char path[PATH_MAX_LEN];
    SDL_snprintf(path, sizeof path, "%sengine.ini", base ? base : "");
    if (!config_load(s, path, other_config)) {
        SDL_Log("config: no engine.ini found, using defaults");
        config_set_save_path(path);     /* the Options screen creates it next to the binary */
    }
}

int main(int argc, char **argv)
{
    bool debug = take_flag(&argc, argv, "--debug");
    bool headless = argc >= 2 && (!strcmp(argv[1], "--selftest") || !strcmp(argv[1], "--dump-opl") ||
                                  !strcmp(argv[1], "--dump-pcspk"));
    if (debug || headless)          /* the headless modes print their results */
        console_open();
    SDL_SetLogOutputFunction(debug ? log_print : log_discard, NULL);

    Settings cfg;
    load_settings(&cfg);
    if (!assets_init(cfg.game_dir)) {
        if (headless)
            fprintf(stderr, "%s\n", assets_error());
        else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FoxRV", assets_error(), NULL);
        return 1;
    }

#if FOXRV_WITH_OPL
    if (argc >= 3 && !strcmp(argv[1], "--dump-opl"))
        return dump_opl(atoi(argv[2]), argc >= 4 ? atoi(argv[3]) : 2000);
#endif
    if (argc >= 3 && !strcmp(argv[1], "--dump-pcspk"))
        return dump_pcspk(atoi(argv[2]), argc >= 4 ? atoi(argv[3]) : 2000);
    if (argc >= 2 && !strcmp(argv[1], "--selftest"))
        return selftest_run();
    int start_level = 0;            /* 0 = intro and menu */
    const char *shot = NULL;
    double shot_at = 1.0;
    static Press presses[MAX_PRESSES];
    int npresses = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc)
            shot_at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--press") && i + 1 < argc)
            npresses = parse_presses(argv[++i], presses);
        else
            start_level = SDL_max(atoi(argv[i]), 1);
    }
    if (shot && start_level == 0 && npresses == 0)
        start_level = 1;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    static Game game;
    bool ok = video_init(&cfg, "FoxRV");
    if (ok) {
        input_init();
        audio_init(&cfg);           /* silent if it fails */
        ok = game_init(&game, start_level);
    }

    /* Fixed-step game logic (the original runs at 72.8/2 Hz); rendering at the display rate,
     * interpolating between the last two game frames. */
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 step = (Uint64)(freq / cfg.logic_hz);
    Uint64 last = SDL_GetPerformanceCounter(), acc = 0;
    Uint64 frame_ns = cfg.max_fps > 0 ? SDL_NS_PER_SECOND / (Uint64)cfg.max_fps : 0;
    Uint64 next_frame = SDL_GetTicksNS(), fps_since = next_frame;
    int frames = 0;
    bool running = ok;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_handle_event(&e);
            if (e.type == SDL_EVENT_QUIT)
                running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT) && !e.key.repeat)
                video_toggle_fullscreen();
        }

        Uint64 now = SDL_GetPerformanceCounter();
        acc += now - last;
        last = now;
        if (acc > step * 8)             /* after a stall, don't try to catch up */
            acc = step * 8;
        while (acc >= step) {
            acc -= step;
            for (int i = 0; i < npresses; i++)
                if (presses[i].frame == game.frames + 1)
                    input_inject(presses[i].action);
            input_tick();
            if (input_pressed(ACT_FULLSCREEN))
                video_toggle_fullscreen();
            game_update(&game);
            if (game.quit)
                running = false;
        }

        bool capture = shot && game.frames >= (unsigned)(shot_at * cfg.logic_hz);
        if (capture)
            video_capture_next(shot);
        video_begin_frame();
        game_render(&game, (float)acc / (float)step);
        video_compose();
        game_render_ui(&game);
        video_present();
        if (capture)
            running = false;
        frames++;
        if (SDL_GetTicksNS() - fps_since >= SDL_NS_PER_SECOND) {
            game.fps = frames;
            /* vsync that doesn't wait (some drivers, Wine / Winlator) leaves the frame rate
             * unbounded, thousands per second: cap it at the display's refresh rate instead. */
            int hz = video_refresh_rate();
            if (cfg.vsync && !frame_ns && frames > hz * 3 / 2 + 10) {
                frame_ns = SDL_NS_PER_SECOND / (Uint64)hz;
                next_frame = SDL_GetTicksNS();
                SDL_Log("video: vsync doesn't hold the frame rate (%d fps): capped at %d", frames, hz);
            }
            frames = 0;
            fps_since = SDL_GetTicksNS();
        }

        /* Frame cap: sleep until the next frame slot. The game logic runs on its own fixed
         * clock above, so the cap changes smoothness and CPU use, never game speed. */
        if (frame_ns) {
            next_frame += frame_ns;
            Uint64 t = SDL_GetTicksNS();
            if (next_frame > t)
                SDL_DelayPrecise(next_frame - t);
            else if (t - next_frame > frame_ns)
                next_frame = t;         /* fell behind: don't burst to catch up */
        } else if (!cfg.vsync) {
            SDL_Delay(1);
        }
    }

    audio_shutdown();
    game_shutdown(&game);
    input_shutdown();
    video_shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}
