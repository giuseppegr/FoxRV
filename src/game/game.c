#include "game/game.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio/adlib_driver.h"
#include "core/assets.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "platform/video.h"

#define HZ 36                       /* game frames per second, near enough for timeouts */
#define DEATH_FRAMES 60             /* image 0x387E */
#define DEATH_VY 15
#define DEATH_VY_MIN (-16)
#define JINGLE_MAX (8 * HZ)         /* longest wait for a jingle (no audio device: no end) */
#define SLIDE_FRAMES 31             /* image 0x3964: "GAME OVER" / "THE END" letters */
#define GAME_OVER_WAIT 150          /* then until the jingle ends, at most this many frames */
#define CREDITS_MIN HZ
#define CREDITS_MAX (30 * HZ)
#define SPRITE_DEATH 13
#define SPRITE_GAME 333             /* "GAME" / "OVER" lettering (0x14D / 0x14E) */
#define SPRITE_OVER 334
#define SPRITE_THE 335              /* "THE" / "END" (0x14F / 0x150) */
#define SPRITE_END 336
#define SONG_LEVEL_CARD 0
#define SONG_LIFE_LOST 1
#define SONG_GAME_OVER 2
#define SONG_LEVEL_DONE 4
#define SONG_CREDITS 9
#define SONG_TITLE 15
#define FADE_STEP 8
#define TRANS_STEP 24               /* screen transitions: about 11 frames each way */
#define ENERGY_SHOW_FRAMES 50       /* DS:6506: the meter shows this long after a change */
#define ENERGY_X 96                 /* image 0x7DD: meter at (96, 9) on the 320 px screen, */
#define ENERGY_Y 9                  /* a unit every 8 px: a 4 x 16 bar, or a 4 x 4 square */
#define NOTICE_FRAMES (2 * HZ)      /* the checkpoint / code screen, unless fire skips it */
#define NOTICE_MIN 12
#define LOGO_FRAMES (3 * HZ)
#define TITLE_FRAMES (20 * HZ)
#define CARD_MIN 12                 /* the card ignores keys this long (a held fire) */
#define CARD_MAX (6 * HZ)
#define CARD_COUNT_STEP 15          /* image 0x3AA: one bonus every 15 frames, 10 = a life */
#define BONUSES_PER_LIFE 10
#define GRACE_FRAMES (2 * HZ)       /* low difficulty: immune to hits after one */
#define GRACE_BLINK 2               /* Titus shown / hidden for this many game frames */

static GameTuning tuning = {.interpolate = true, .start_lives = -1, .restart_level = false};
static bool tuning_ready;

static const char *const difficulty_names[DIFF_COUNT] = {"HIGH", "MEDIUM", "LOW"};
static const char *const main_items[] = {"START GAME", "PASSWORD", "OPTIONS", "QUIT"};
enum { MAIN_START, MAIN_PASSWORD, MAIN_OPTIONS, MAIN_QUIT };
static const char *const pause_items[] = {"CONTINUE", "RESTART LEVEL", "MAIN MENU", "QUIT GAME"};
enum { PAUSE_CONTINUE, PAUSE_RESTART, PAUSE_MENU, PAUSE_QUIT };

GameTuning *game_tuning(void)
{
    if (!tuning_ready) {
        player_tuning_defaults(&tuning.player);
        camera_tuning_defaults(&tuning.camera);
        tuning_ready = true;
    }
    return &tuning;
}

static bool parse_bool(const char *v)
{
    return !SDL_strcasecmp(v, "1") || !SDL_strcasecmp(v, "true") || !SDL_strcasecmp(v, "yes") ||
           !SDL_strcasecmp(v, "on");
}

bool game_config(const char *section, const char *key, const char *value)
{
    GameTuning *t = game_tuning();
    int v = atoi(value);
    if (!SDL_strcasecmp(section, "engine") && !SDL_strcasecmp(key, "interpolate")) {
        t->interpolate = parse_bool(value);
        return true;
    }
    if (!SDL_strcasecmp(section, "input") && !SDL_strcasecmp(key, "pad_up_jumps")) {
        t->pad_up_jumps = parse_bool(value);
        return true;
    }
    if (!SDL_strcasecmp(section, "camera")) {
        if (!SDL_strcasecmp(key, "mode")) {
            if (!SDL_strcasecmp(value, "original")) t->camera.mode = CAM_ORIGINAL;
            else if (!SDL_strcasecmp(value, "tiles")) t->camera.mode = CAM_TILES;
            else if (!SDL_strcasecmp(value, "smooth")) t->camera.mode = CAM_SMOOTH;
            else return false;
        } else if (!SDL_strcasecmp(key, "speed")) t->camera.speed = SDL_clamp(v, 1, 64);
        else if (!SDL_strcasecmp(key, "ease")) t->camera.ease = SDL_clamp(v, 0, 64);
        else return false;
        return true;
    }
    if (SDL_strcasecmp(section, "player"))
        return false;
    static const struct { const char *name; int *field; } ints[] = {
        {"accel", &tuning.player.accel}, {"friction", &tuning.player.friction},
        {"max_walk", &tuning.player.max_walk}, {"max_sneak", &tuning.player.max_sneak},
        {"max_crawl", &tuning.player.max_crawl}, {"jump_step", &tuning.player.jump_step},
        {"jump_min", &tuning.player.jump_min}, {"jump_frames", &tuning.player.jump_frames},
        {"jump_cooldown", &tuning.player.jump_cooldown}, {"gravity", &tuning.player.gravity},
        {"max_fall", &tuning.player.max_fall}, {"ladder_speed", &tuning.player.ladder_speed},
        {"drop_crouch_frames", &tuning.player.drop_crouch_frames},
    };
    for (size_t i = 0; i < SDL_arraysize(ints); i++) {
        if (!SDL_strcasecmp(key, ints[i].name)) {
            *ints[i].field = SDL_clamp(v, 0, 1000);
            return true;
        }
    }
    if (!SDL_strcasecmp(key, "difficulty")) {
        int d = 0;
        while (d < DIFF_COUNT && SDL_strcasecmp(value, difficulty_names[d]))
            d++;
        if (d == DIFF_COUNT)
            return false;
        t->difficulty = (Difficulty)d;
    } else if (!SDL_strcasecmp(key, "invincible")) t->invincible = parse_bool(value);
    else if (!SDL_strcasecmp(key, "start_lives")) t->start_lives = SDL_clamp(v, 0, 9);
    else if (!SDL_strcasecmp(key, "on_death")) t->restart_level = SDL_strcasecmp(value, "checkpoint") != 0;
    else return false;
    return true;
}

/* --- helpers ------------------------------------------------------------------------------ */

static void play_song(Game *g, int song)
{
    g->song = song;
    if (song >= 0)
        audio_play_song(song);
    else
        audio_stop_music();
}

/* Asks for the black notice screen (shown at the end of the frame, after a fade). */
static void show_notice(Game *g, const char *line1, const char *line2)
{
    SDL_strlcpy(g->notice[0], line1, sizeof g->notice[0]);
    SDL_strlcpy(g->notice[1], line2, sizeof g->notice[1]);
    SDL_Log("notice: %s %s", line1, line2);
}

static bool confirm_pressed(void) { return input_pressed(ACT_ACTION) || input_pressed(ACT_JUMP); }

static bool any_pressed(void)
{
    return confirm_pressed() || input_pressed(ACT_MENU) || input_pressed(ACT_PAUSE);
}

/* A jingle has played out (or taken too long, e.g. without an audio device). */
static bool jingle_over(int frames, int min_frames)
{
    return frames >= min_frames && (audio_music_finished() || frames >= JINGLE_MAX);
}

/* Fades out, does `next` while the screen is black, then fades back in. */
static void go(Game *g, GameGo next)
{
    if (g->go == GO_NONE) {
        g->go = next;
        g->trans_dir = -TRANS_STEP;
    }
}

static void new_game(Game *g)
{
    const PlayerStart *ps = player_start_values();
    g->lives = tuning.start_lives >= 0 ? tuning.start_lives : ps->start_lives;
    g->energy = ps->start_energy;
    g->bonuses = 0;
}

/* The visible area, which some object, enemy and elevator rules depend on. */
static void share_view(Game *g)
{
    Objects *o = &g->objects;
    Enemies *en = &g->enemies;
    Elevators *el = &g->elevators;
    o->view_x = en->cam_x = el->view_x = (int)g->camera.x;
    o->view_y = en->cam_y = el->view_y = (int)g->camera.y;
    o->view_w = en->view_w = el->view_w = video_view_w();
    o->view_h = en->view_h = el->view_h = video_view_h();
}

/* For frames where nothing moves: draw everything where it is, without interpolating. */
static void hold_motion(Game *g)
{
    if (!g->level_loaded)
        return;
    Player *p = &g->player;
    p->prev_x = p->x;
    p->prev_y = p->y;
    g->camera.prev_x = g->camera.x;
    g->camera.prev_y = g->camera.y;
    objects_begin_frame(&g->objects);
    elevators_hold(&g->elevators);
    Enemies *en = &g->enemies;
    for (int i = 0; i < en->ndisp; i++) {
        en->disp[i].prev_x = en->disp[i].x;
        en->disp[i].prev_y = en->disp[i].y;
    }
    for (int i = 0; i < ENEMY_SHOTS; i++) {
        en->shots[i].prev_x = en->shots[i].x;
        en->shots[i].prev_y = en->shots[i].y;
    }
}

/* Loads level `index` (play order) from scratch: map and bonuses, objects, enemies,
 * elevators, Titus at the start with full energy, and the level music. */
static bool enter_level(Game *g, int index)
{
    int n = level_count();
    index = (index % n + n) % n;
    Level next;
    if (!level_load(&next, index))
        return false;
    tileset_free(&g->tiles);
    level_free(&g->level);
    g->level = next;
    g->level_loaded = true;
    if (tileset_load(&g->tiles, &g->level.tileset))
        tileset_bake(&g->tiles, video_renderer(), &g->level.pal);
    atlas_bake(&g->atlas, video_renderer(), &g->level.pal);   /* sprites use colour 14 too */

    player_spawn(&g->player, &g->level);
    g->objects.idol_saved = false;
    camera_reset(&g->camera, &g->level, &g->player, video_view_w(), video_view_h());
    g->camera_frozen = false;
    share_view(g);
    objects_reset(&g->objects, &g->level);
    enemies_reset(&g->enemies, &g->level);
    elevators_load(&g->elevators, &g->level);
    g->energy = player_start_values()->start_energy;   /* 0x3851 */
    g->mode = MODE_PLAY;
    g->phase = PHASE_PLAY;
    g->phase_frames = 0;
    g->dying = 0;
    g->game_over = false;
    g->notice[0][0] = '\0';
    g->energy_frames = 0;
    g->warp = 0;
    play_song(g, level_info(index)->song);
    SDL_Log("level %d: %s (%s), song %d", index + 1, g->level.title, level_info(index)->file, g->song);
    return true;
}

/* After a death with on_death = checkpoint: the original's respawn (0x398D, 0x38AA). */
static void respawn(Game *g)
{
    Player *p = &g->player;
    player_respawn(p);
    camera_reset(&g->camera, &g->level, p, video_view_w(), video_view_h());
    g->camera_frozen = false;
    share_view(g);
    objects_reset(&g->objects, &g->level);
    enemies_reset(&g->enemies, &g->level);
    elevators_reset(&g->elevators);
    g->energy = player_start_values()->start_energy;
    g->dying = 0;
    g->phase = PHASE_PLAY;
}

static void open_menu(Game *g, int sel)
{
    g->mode = MODE_MENU;
    menu_list_init(&g->menu, main_items, (int)SDL_arraysize(main_items), sel);
    if (g->song != SONG_TITLE)
        play_song(g, SONG_TITLE);
}

/* The screen is black: switch to what go() asked for. */
static void apply_go(Game *g)
{
    switch (g->go) {
    case GO_TITLE:
        g->mode = MODE_INTRO;
        g->intro_stage = 1;
        g->intro_frames = 0;
        play_song(g, SONG_TITLE);
        break;
    case GO_MENU:
        open_menu(g, MAIN_START);
        break;
    case GO_CARD:
        g->mode = MODE_CARD;
        g->card_frames = 0;
        g->card_count = 0;
        if (g->card_resumes)
            respawn(g);
        play_song(g, SONG_LEVEL_CARD);
        break;
    case GO_CREDITS:
        g->mode = MODE_CREDITS;
        g->intro_frames = 0;
        play_song(g, SONG_CREDITS);
        break;
    case GO_NOTICE:
        g->mode = MODE_NOTICE;
        g->notice_frames = 0;
        break;
    case GO_PLAY:
        g->mode = MODE_PLAY;
        g->notice[0][0] = '\0';
        break;
    case GO_LEVEL:
        if (g->card_resumes) {   /* back to the level as it was left */
            g->card_resumes = false;
            g->mode = MODE_PLAY;
            play_song(g, level_info(g->level.index)->song);
        } else if (!enter_level(g, g->next_level)) {
            open_menu(g, MAIN_START);
        }
        break;
    case GO_NONE:
        break;
    }
    g->go = GO_NONE;
}

/* 0x5BE7 + 0x4FC0: lose a life (or the game), then the death sequence starts next frame. */
static void kill_player(Game *g)
{
    if (tuning.invincible || g->phase != PHASE_PLAY)
        return;
    if (g->lives == 0)
        g->game_over = true;
    else
        g->lives--;
    g->dying = 2;
}

static void lose_energy(Game *g)    /* 0x5BCC */
{
    g->energy_frames = ENERGY_SHOW_FRAMES;
    if (g->dying || tuning.invincible)
        return;
    if (--g->energy < 0) {
        g->energy = 0;
        kill_player(g);
    }
}

static void gain_energy(Game *g)    /* 0x5BA1 */
{
    g->energy_frames = ENERGY_SHOW_FRAMES;
    if (++g->energy > player_start_values()->max_energy) {
        g->energy = player_start_values()->max_energy;
        g->bonuses++;
    }
}

bool game_init(Game *g, int start_level)
{
    memset(g, 0, sizeof *g);
    game_tuning();
    if (!level_list_load() || !anim_load() || !atlas_load(&g->atlas))
        return false;
    menu_art_load(&g->art, video_renderer());   /* menus still work with pieces missing */
    ending_load();
    g->fade = 255;
    g->song = -1;
    new_game(g);
    if (start_level > 0) {          /* straight into a level (command line) */
        g->trans = 255;
        return enter_level(g, start_level - 1);
    }
    g->mode = MODE_INTRO;           /* the TITUS logo fades in */
    g->trans_dir = TRANS_STEP;
    return true;
}

void game_shutdown(Game *g)
{
    tileset_free(&g->tiles);
    level_free(&g->level);
    atlas_free(&g->atlas);
    menu_art_free(&g->art);
    anim_free();
}

/* --- in a level --------------------------------------------------------------------------- */

static void debug_keys(Game *g)
{
    if (input_pressed(ACT_DBG_PREV_LEVEL)) enter_level(g, g->level.index - 1);
    if (input_pressed(ACT_DBG_NEXT_LEVEL)) enter_level(g, g->level.index + 1);
    if (input_pressed(ACT_DBG_NEXT_SONG)) {
        play_song(g, (g->song + 1) % ADLIB_SONG_COUNT);
        SDL_Log("song %d", g->song);
    }
    if (input_pressed(ACT_DBG_SFX)) {
        audio_play_sfx(g->sfx);
        SDL_Log("sfx %d", g->sfx);
        g->sfx = (g->sfx + 1) % ADLIB_SFX_COUNT;
    }
    if (input_pressed(ACT_DBG_FADE))
        g->fade_dir = g->fade > 0 ? -FADE_STEP : FADE_STEP;
    if (input_pressed(ACT_DBG_OVERLAY))
        g->overlay = !g->overlay;
    if (input_pressed(ACT_DBG_DIE))
        kill_player(g);
    if (input_pressed(ACT_DBG_WARP) && g->phase == PHASE_PLAY) {
        /* Cycles through the gate entrances, the elevators (just above them), the finish. */
        const Level *lv = &g->level;
        Player *p = &g->player;
        const Elevator *lifts[ELEVATOR_RECORDS];
        int nlifts = 0;
        for (int i = 0; i < ELEVATOR_RECORDS; i++)
            if (g->elevators.e[i].sprite != ELEVATOR_NONE)
                lifts[nlifts++] = &g->elevators.e[i];
        int n = g->warp++ % (lv->ngates + nlifts + 1);
        const char *what = "finish";
        if (n < lv->ngates) {
            what = "gate";
            p->x = lv->gates[n].entrance_col * 16 + 8;
            p->y = lv->gates[n].entrance_row * 16;
        } else if (n < lv->ngates + nlifts) {
            what = "elevator";
            p->x = lifts[n - lv->ngates]->x + 16;
            p->y = lifts[n - lv->ngates]->y - 24;
        } else {
            p->x = lv->finish_x + 8;
            p->y = lv->finish_y;
        }
        p->vx = p->vy = 0;
        p->prev_x = p->x;
        p->prev_y = p->y;
        camera_reset(&g->camera, lv, p, video_view_w(), video_view_h());
        g->camera_frozen = false;
        SDL_Log("warp to %s: x %d y %d", what, p->x, p->y);
    }
}

static void handle_events(Game *g, const PlayerEvents *ev)
{
    if (ev->song >= 0)
        play_song(g, ev->song);
    for (int i = 0; i < ev->nsfx; i++)
        audio_play_sfx(ev->sfx[i]);
    int per_hurt = tuning.difficulty == DIFF_HIGH ? 2 : 1;
    for (int i = 0; i < ev->energy_loss + ev->hurts * per_hurt; i++)
        lose_energy(g);
    if (ev->hurts && tuning.difficulty == DIFF_LOW)
        g->player.grace = GRACE_FRAMES;
    for (int i = 0; i < ev->energy; i++)
        gain_energy(g);
    if (ev->checkpoint)
        show_notice(g, "CHECKPOINT", "");
    if (ev->code_reveal) {          /* image 0x533 ("level 14's code" tiles: 0x4F9A) */
        int lv = ev->code_level14 ? 13 : g->level.index;
        const char *code = level_code(lv);
        char line1[32], line2[32];
        SDL_snprintf(line1, sizeof line1, "CODE  %s", code ? code : "____");
        SDL_snprintf(line2, sizeof line2, "LEVEL  %d", lv + 1);
        show_notice(g, line1, line2);
    }
    if (ev->died)
        kill_player(g);
}

/* 0x3AB5: standing on the finish tile (or the one right/below it) ends the level, once the
 * bosses are beaten and, in "Walls of Sand", with the idol in hand. */
static bool at_finish(const Game *g)
{
    const Player *p = &g->player;
    const Level *lv = &g->level;
    if (g->enemies.boss_count > 0)
        return false;
    if (level_info(lv->index)->slot == SLOT_IDOL_NEEDED) {
        int n = g->objects.carry.sprite & SPRITE_NUMBER;
        if (g->objects.carry.sprite == OBJ_NONE || (n != SPR_IDOL && n != SPR_IDOL + 1))
            return false;
    }
    int x = p->x & ~15, y = p->y & ~15;
    return (lv->finish_x == x || lv->finish_x == x - 16) && (lv->finish_y == y || lv->finish_y == y - 16);
}

/* 0x3B37: crouching still on a gate's entrance (the drop-through timer runs) takes Titus to
 * its exit, and the camera to its screen. */
static void use_gate(Game *g)
{
    Player *p = &g->player;
    if (!p->drop_through)
        return;
    int col = p->x >> 4, row = p->y >> 4;
    for (int i = 0; i < g->level.ngates; i++) {
        const Gate *gt = &g->level.gates[i];
        if (gt->entrance_col != col || gt->entrance_row != row)
            continue;
        p->vx = p->vy = 0;
        p->x = p->prev_x = gt->exit_col * 16;
        p->y = p->prev_y = gt->exit_row * 16;
        camera_show(&g->camera, &g->level, gt->screen_col, gt->screen_row, video_view_w(), video_view_h());
        g->camera_frozen = gt->noscroll;
        share_view(g);
        objects_begin_frame(&g->objects);   /* what Titus carries jumps along, too */
        g->objects.carry.prev_x = g->objects.carry.x = p->x;
        g->objects.carry.prev_y = g->objects.carry.y = p->y;
        g->trans = 0;                /* the original wipes the screen here */
        g->trans_dir = TRANS_STEP;
        return;
    }
}

/* A ladder's bottom within a jump above Titus: the original climbs on by jumping into it. */
static bool ladder_above(const Game *g)
{
    const Player *p = &g->player;
    int row = (p->y >> 4) - 2;      /* the ceiling over the body row, as collide() probes it */
    for (int col = (p->x - 4) >> 4; col <= (p->x + 5) >> 4; col++)
        for (int r = row; r >= row - 3 && r >= 0; r--)
            if (level_ceiling(&g->level, col, r) == CEIL_LADDER)
                return true;
    return false;
}

static void play_frame(Game *g)
{
    /* The keyboard's Up jumps, as in 1991. On a gamepad only the jump buttons do (a worn stick
     * or D-pad slips into up), unless pad_up_jumps; up there still climbs onto a ladder above. */
    bool up = input_held(ACT_UP);
    bool up_jumps = input_held_by_keyboard(ACT_UP) || (up && (tuning.pad_up_jumps || ladder_above(g)));
    const PlayerInput in = {
        .left = input_held(ACT_LEFT), .right = input_held(ACT_RIGHT),
        .up = up, .down = input_held(ACT_DOWN),
        .jump = up_jumps || input_held(ACT_JUMP),
        .fire = input_held(ACT_ACTION),
    };
    Objects *o = &g->objects;
    Enemies *en = &g->enemies;
    /* 0x3AD6: "Just Married" isn't played; once its bosses (none) are beaten, the wedding
     * scene starts, on the first screen of its map. */
    if (level_info(g->level.index)->slot == SLOT_ENDING && en->boss_count == 0) {
        g->phase = PHASE_ENDING;
        g->phase_frames = 0;
        ending_start(&g->ending);
        /* The scene is a small map (21 x 12 tiles here): centre it in the view. */
        int cols = 1;
        for (int c = 0; c < MAP_COLS; c++)
            for (int row = 0; row < g->level.rows; row++)
                if (level_tile(&g->level, c, row))
                    cols = c + 1;
        g->camera = (Camera){0};
        g->camera.x = g->camera.prev_x = (float)((cols * TILE_SIZE - video_view_w()) / 2);
        g->camera.y = g->camera.prev_y = (float)((g->level.rows * TILE_SIZE - video_view_h()) / 2);
        g->camera_frozen = true;
        return;
    }
    share_view(g);
    objects_begin_frame(o);

    /* The original's order (main loop 0x317): elevators 0x4028, objects 0x3BC7, Titus 0x434B,
     * enemies 0x5B45, enemy projectiles 0x5AC8, enemy display and hits 0x5937, finish and
     * gates 0x3AB5, animation 0x39F5, camera 0x4146. */
    PlayerEvents ev = {.song = -1};
    elevators_update(&g->elevators);
    objects_update(o, &g->level, &g->player, g->frames, &ev);
    player_update(&g->player, &g->level, o, en, &g->elevators, &in, &tuning.player, &ev);
    enemies_update(en, &g->level, &g->player, o, g->frames, &ev);
    enemies_display(en, &g->player, o, &ev);
    bool finished = at_finish(g);
    if (!finished)
        use_gate(g);
    objects_animate(o, &g->player, g->frames);
    handle_events(g, &ev);
    if (g->camera_frozen) {
        g->camera.prev_x = g->camera.x;
        g->camera.prev_y = g->camera.y;
    } else {
        camera_update(&g->camera, &g->level, &g->player, en, &tuning.camera, video_view_w(), video_view_h());
    }

    /* 0x3827: the finish wins over a death in the same frame. */
    if (finished) {
        g->dying = 0;
        g->game_over = false;
        g->phase = PHASE_LEVEL_DONE;
        g->phase_frames = 0;
        g->notice[0][0] = '\0';
        play_song(g, SONG_LEVEL_DONE);
        return;
    }
    /* 0x45FD then 0x3827: the death sequence starts when the countdown reaches 1. */
    if (g->dying > 0)
        g->dying--;
    if (g->game_over) {
        g->phase = PHASE_GAME_OVER;
        g->phase_frames = 0;
        play_song(g, SONG_GAME_OVER);
    } else if (g->dying == 1) {
        g->phase = PHASE_DYING;
        g->phase_frames = 0;
        g->death_vy = DEATH_VY;
        player_drop(&g->player, &g->objects);
        g->player.sprite = SPRITE_DEATH;
        play_song(g, SONG_LIFE_LOST);
    }
    /* A checkpoint or code tile: the screen goes black for the notice, then the game goes on
     * (not when dying: the death sequence goes first and the notice is dropped). */
    if (g->notice[0][0]) {
        if (g->phase == PHASE_PLAY && !g->dying)
            go(g, GO_NOTICE);
        else
            g->notice[0][0] = '\0';
    }
}

/* 0x3860: Titus jumps up and falls off the screen while song 1 plays; then the level starts
 * again from the beginning (or, the original way, at the last checkpoint), after the card. */
static void dying_frame(Game *g)
{
    Player *p = &g->player;
    hold_motion(g);
    if (++g->phase_frames <= DEATH_FRAMES) {
        g->death_vy = SDL_max(g->death_vy - 1, DEATH_VY_MIN);
        p->y -= g->death_vy;
        return;
    }
    if (!jingle_over(g->phase_frames, DEATH_FRAMES))  /* 0x38E8: until the song ends */
        return;
    g->card_resumes = !tuning.restart_level;
    g->next_level = g->level.index;
    go(g, GO_CARD);
}

static void game_over_frame(Game *g)    /* 0x3910 */
{
    hold_motion(g);
    g->phase_frames++;
    if (g->phase_frames < SLIDE_FRAMES + 20 ||
        (!audio_music_finished() && g->phase_frames < SLIDE_FRAMES + GAME_OVER_WAIT))
        return;
    go(g, GO_MENU);
}

/* 0x3B0A: song 4, everything frozen until it ends, then the next level's card. */
static void level_done_frame(Game *g)
{
    hold_motion(g);
    if (!jingle_over(++g->phase_frames, HZ))
        return;
    int next = g->level.index + 1;
    if (next >= level_count()) {         /* no "Just Married" level: straight to the credits */
        go(g, GO_CREDITS);
        return;
    }
    g->next_level = next;
    g->card_resumes = false;
    go(g, GO_CARD);
}

/* The wedding scene, then the credits (image 0x1C59). */
static void ending_frame(Game *g)
{
    hold_motion(g);
    ending_update(&g->ending, confirm_pressed());
    if (g->ending.stage == END_DONE)
        go(g, GO_CREDITS);
}

static void open_pause(Game *g)
{
    g->mode = MODE_PAUSE;
    menu_list_init(&g->menu, pause_items, (int)SDL_arraysize(pause_items), PAUSE_CONTINUE);
    audio_pause(true);
}

static void level_frame(Game *g)
{
    if (g->energy_frames > 0)           /* 0x7B7 */
        g->energy_frames--;
    if ((g->frames & 3) == 0)           /* 0x4146: tile animation every 4 frames */
        g->tile_phase = (g->tile_phase + 1) % 3;
    debug_keys(g);
    if (input_pressed(ACT_PAUSE) || input_pressed(ACT_MENU)) {
        hold_motion(g);
        open_pause(g);
        return;
    }

    switch (g->phase) {
    case PHASE_PLAY: play_frame(g); break;
    case PHASE_DYING: dying_frame(g); break;
    case PHASE_GAME_OVER: game_over_frame(g); break;
    case PHASE_LEVEL_DONE: level_done_frame(g); break;
    case PHASE_ENDING: ending_frame(g); break;
    }

    /* 0x0358: when a jingle has ended, the level music starts again. */
    int level_song = level_info(g->level.index)->song;
    if (g->phase == PHASE_PLAY && level_song >= 0 && g->song != level_song && audio_music_finished())
        play_song(g, level_song);
}

/* --- front end ---------------------------------------------------------------------------- */

static void intro_frame(Game *g)
{
    g->intro_frames++;
    if (g->intro_stage == 0) {
        if (g->intro_frames > LOGO_FRAMES || any_pressed())
            go(g, GO_TITLE);
    } else if (g->intro_frames > TITLE_FRAMES || any_pressed()) {
        go(g, GO_MENU);
    }
}

static void start_game(Game *g, int level)
{
    new_game(g);
    g->next_level = level;
    g->card_resumes = false;
    go(g, GO_CARD);
}

static void menu_frame(Game *g)
{
    switch (menu_list_update(&g->menu)) {
    case MAIN_START:
        start_game(g, 0);
        break;
    case MAIN_PASSWORD:
        g->mode = MODE_CODE;
        code_entry_start(&g->code);
        break;
    case MAIN_OPTIONS:
        g->mode = MODE_OPTIONS;
        options_open(&g->options);
        break;
    case MAIN_QUIT:
        g->quit = true;
        break;
    case MENU_BACK:                     /* Esc: to QUIT first, then quits */
        if (g->menu.sel == MAIN_QUIT)
            g->quit = true;
        g->menu.sel = MAIN_QUIT;
        break;
    default:
        break;
    }
}

static void options_frame(Game *g)
{
    int difficulty = tuning.difficulty;
    bool done = options_update(&g->options, &difficulty);
    tuning.difficulty = (Difficulty)difficulty;
    if (done)
        open_menu(g, MAIN_OPTIONS);
}

static void code_frame(Game *g)
{
    int r = code_entry_update(&g->code);
    if (r == MENU_BACK)
        open_menu(g, MAIN_PASSWORD);
    else if (r >= 0)
        start_game(g, r);
}

/* Image 0x373: the card waits for a key; first, every 10 extra bonuses become a life. */
static void card_frame(Game *g)
{
    g->card_frames++;
    if (g->bonuses >= BONUSES_PER_LIFE || g->card_count > 0) {
        if (g->card_frames % CARD_COUNT_STEP == 0) {
            g->bonuses--;
            if (++g->card_count == BONUSES_PER_LIFE) {
                g->card_count = 0;
                g->lives = SDL_min(g->lives + 1, player_start_values()->max_lives);
            }
        }
        return;
    }
    if (g->card_frames >= CARD_MIN && (confirm_pressed() || g->card_frames >= CARD_MAX))
        go(g, GO_LEVEL);
}

static void notice_frame(Game *g)
{
    hold_motion(g);
    g->notice_frames++;
    if (g->notice_frames >= NOTICE_FRAMES || (g->notice_frames >= NOTICE_MIN && confirm_pressed()))
        go(g, GO_PLAY);
}

static void credits_frame(Game *g)
{
    g->intro_frames++;
    if (g->intro_frames >= CREDITS_MIN && (any_pressed() || jingle_over(g->intro_frames, CREDITS_MAX)))
        go(g, GO_MENU);
}

static void pause_frame(Game *g)
{
    hold_motion(g);
    int r = menu_list_update(&g->menu);
    if (input_pressed(ACT_PAUSE) || r == MENU_BACK)
        r = PAUSE_CONTINUE;
    if (r == MENU_NONE)
        return;
    audio_pause(false);
    g->mode = MODE_PLAY;
    switch (r) {
    case PAUSE_RESTART:
        g->next_level = g->level.index;
        g->card_resumes = false;
        go(g, GO_LEVEL);
        break;
    case PAUSE_MENU:
        go(g, GO_MENU);
        break;
    case PAUSE_QUIT:
        g->quit = true;
        break;
    default:
        break;
    }
}

void game_update(Game *g)
{
    g->frames++;
    g->fade = SDL_clamp(g->fade + g->fade_dir, 0, 255);

    if (g->go != GO_NONE) {             /* fading out: everything holds still */
        hold_motion(g);
        g->trans -= TRANS_STEP;
        if (g->trans <= 0) {
            g->trans = 0;
            apply_go(g);
            g->trans_dir = TRANS_STEP;
        }
        return;
    }
    if (g->trans_dir > 0) {
        g->trans += g->trans_dir;
        if (g->trans >= 255) {
            g->trans = 255;
            g->trans_dir = 0;
        }
    }

    switch (g->mode) {
    case MODE_INTRO: intro_frame(g); break;
    case MODE_MENU: menu_frame(g); break;
    case MODE_CODE: code_frame(g); break;
    case MODE_CARD: card_frame(g); break;
    case MODE_PLAY: level_frame(g); break;
    case MODE_PAUSE: pause_frame(g); break;
    case MODE_NOTICE: notice_frame(g); break;
    case MODE_CREDITS: credits_frame(g); break;
    case MODE_OPTIONS: options_frame(g); break;
    }
}

/* --- render ------------------------------------------------------------------------------- */

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static void draw_sprite(Game *g, SDL_Renderer *r, int number, int x, int y, bool mirrored, int cx, int cy)
{
    atlas_draw(&g->atlas, r, number, (float)(sprite_left(number, x, mirrored) - cx),
               (float)(sprite_top(number, y) - cy), mirrored);
}

#define SPRITE_FLASH 0x4000         /* sprite word bit: a boss taking a hit (image 0x1EA5) */

/* `enemy`: the sprite word may carry SPRITE_FLASH (in objects that bit is a level flag). */
static void draw_obj(Game *g, SDL_Renderer *r, const Obj *b, bool enemy, float alpha, int cx, int cy)
{
    if (b->sprite == OBJ_NONE)
        return;
    int x = (int)lroundf(lerp((float)b->prev_x, (float)b->x, alpha));
    int y = (int)lroundf(lerp((float)b->prev_y, (float)b->y, alpha));
    int n = b->sprite & SPRITE_NUMBER;
    bool mirrored = (b->sprite & OBJ_MIRROR) != 0;
    if (enemy && (b->sprite & SPRITE_FLASH))
        atlas_draw_flash(&g->atlas, r, n, (float)(sprite_left(n, x, mirrored) - cx), (float)(sprite_top(n, y) - cy),
                         mirrored);
    else
        draw_sprite(g, r, n, x, y, mirrored, cx, cy);
}

static void draw_overlay(Game *g, SDL_Renderer *r, int cx, int cy)
{
    const Level *lv = &g->level;
    int vw = video_view_w(), vh = video_view_h();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int row = SDL_max(cy / TILE_SIZE, 0); row <= SDL_min((cy + vh) / TILE_SIZE, lv->rows - 1); row++) {
        for (int col = SDL_max(cx / TILE_SIZE, 0); col <= SDL_min((cx + vw) / TILE_SIZE, MAP_COLS - 1); col++) {
            float x = (float)(col * TILE_SIZE - cx), y = (float)(row * TILE_SIZE - cy);
            int fl = level_floor(lv, col, row), wl = level_wall(lv, col, row), cl = level_ceiling(lv, col, row);
            if (wl) {
                SDL_SetRenderDrawColor(r, 255, 40, 40, wl == WALL_SOLID ? 70 : 140);
                SDL_RenderFillRect(r, &(SDL_FRect){x, y, TILE_SIZE, TILE_SIZE});
            }
            if (fl) {
                static const SDL_Color c[FLOOR_FLAG_COUNT] = {
                    {0}, {40, 255, 40, 255}, {120, 220, 255, 255}, {80, 180, 255, 255}, {40, 120, 255, 255},
                    {255, 255, 0, 255}, {255, 160, 0, 255}, {255, 0, 255, 255}, {255, 0, 0, 255},
                    {255, 0, 0, 255}, {255, 0, 0, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
                    {255, 255, 255, 255},
                };
                SDL_Color k = c[fl < FLOOR_FLAG_COUNT ? fl : 0];
                SDL_SetRenderDrawColor(r, k.r, k.g, k.b, 220);
                SDL_RenderFillRect(r, &(SDL_FRect){x, y, TILE_SIZE, 2});
            }
            if (cl) {
                SDL_SetRenderDrawColor(r, cl == CEIL_LADDER ? 255 : 200, cl == CEIL_LADDER ? 160 : 200, 0, 220);
                SDL_RenderFillRect(r, &(SDL_FRect){x, y + TILE_SIZE - 2, TILE_SIZE, 2});
            }
        }
    }
    /* Object collision boxes (width centred on x, height up from the feet). */
    for (int i = 0; i < MAX_OBJECTS; i++) {
        const Obj *b = &g->objects.obj[i];
        const SpriteInfo *s = sprite_info(b->sprite & SPRITE_NUMBER);
        if (b->sprite == OBJ_NONE || !s)
            continue;
        SDL_SetRenderDrawColor(r, 0, 255, 255, (obj_flags(b->sprite) & OBJF_PLATFORM) ? 255 : 120);
        SDL_RenderRect(r, &(SDL_FRect){(float)(b->x - s->box_w / 2 - cx), (float)(b->y - s->box_h - cy),
                                       (float)s->box_w, (float)s->box_h});
    }
    /* Elevator boxes (from the top-left corner), brighter while they can be stood on. */
    for (int i = 0; i < ELEVATOR_RECORDS; i++) {
        const Elevator *e = &g->elevators.e[i];
        const SpriteInfo *s = sprite_info(e->sprite & SPRITE_NUMBER);
        if (e->sprite == ELEVATOR_NONE || !s)
            continue;
        SDL_SetRenderDrawColor(r, 255, 0, 255, e->visible ? 255 : 120);
        SDL_RenderRect(r, &(SDL_FRect){(float)(e->x - cx), (float)(e->y - cy), (float)s->box_w, (float)s->box_h});
    }
    /* Finish (red) and gate entrances (orange). */
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    SDL_RenderRect(r, &(SDL_FRect){(float)(lv->finish_x - cx), (float)(lv->finish_y - cy), 32, 32});
    SDL_SetRenderDrawColor(r, 255, 160, 0, 255);
    for (int i = 0; i < lv->ngates; i++)
        SDL_RenderRect(r, &(SDL_FRect){(float)(lv->gates[i].entrance_col * 16 - cx),
                                       (float)(lv->gates[i].entrance_row * 16 - cy), 16, 16});
    /* The player's probe points: feet (x-4, x+5), wall (x +/- 8, one row up), head. */
    const Player *p = &g->player;
    float px = (float)(p->x - cx), py = (float)(p->y - cy);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderFillRect(r, &(SDL_FRect){px - 4, py, 1, 1});
    SDL_RenderFillRect(r, &(SDL_FRect){px + 5, py, 1, 1});
    SDL_RenderFillRect(r, &(SDL_FRect){px - 8, py - 8, 1, 1});
    SDL_RenderFillRect(r, &(SDL_FRect){px + 8, py - 8, 1, 1});
    SDL_RenderFillRect(r, &(SDL_FRect){px, py - 24, 1, 1});
}

/* Image 0x7C7: the energy meter, in colour 1, centred like the original's: one 4 x 16 bar
 * per unit left, a 4 x 4 square (rows 6-9) per unit lost. */
static void draw_energy(Game *g, SDL_Renderer *r)
{
    int max = player_start_values()->max_energy;
    int x0 = (video_view_w() - 320) / 2 + ENERGY_X;
    SDL_Color c = g->level.pal.c[1];
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    for (int i = 0; i < max; i++) {
        float x = (float)(x0 + i * 8);
        if (i < g->energy)
            SDL_RenderFillRect(r, &(SDL_FRect){x, ENERGY_Y, 4, 16});
        else
            SDL_RenderFillRect(r, &(SDL_FRect){x, ENERGY_Y + 6, 4, 4});
    }
}

/* Two letter sprites sliding in from both sides (image 0x392F). */
static void draw_slide(Game *g, SDL_Renderer *r, int frames, int left, int right, int cx, int cy)
{
    int vw = video_view_w(), vh = video_view_h();
    int slide = SDL_min(frames, SLIDE_FRAMES) * 8;
    int y = cy + 100 + (vh - 200) / 2;
    draw_sprite(g, r, left, cx - 0x76 + slide + (vw - 320) / 2, y, false, cx, cy);
    draw_sprite(g, r, right, cx + 0x1B6 - slide + (vw - 320) / 2, y, false, cx, cy);
}

static void render_level(Game *g, float alpha)
{
    SDL_Renderer *r = video_renderer();
    const Level *lv = &g->level;
    if (!g->level_loaded)
        return;
    if (!tuning.interpolate)
        alpha = 1.0f;
    float cam_a = tuning.camera.mode != CAM_TILES ? alpha : 1.0f;
    int cx = (int)lroundf(lerp(g->camera.prev_x, g->camera.x, cam_a));
    int cy = (int)lroundf(lerp(g->camera.prev_y, g->camera.y, cam_a));

    int vw = video_view_w(), vh = video_view_h();
    int tx0 = (int)floorf(cx / (float)TILE_SIZE), ty0 = (int)floorf(cy / (float)TILE_SIZE);
    for (int ty = SDL_max(ty0, 0); ty <= SDL_min((cy + vh) / TILE_SIZE, lv->rows - 1); ty++)
        for (int tx = SDL_max(tx0, 0); tx <= SDL_min((cx + vw) / TILE_SIZE, MAP_COLS - 1); tx++)
            tileset_draw(&g->tiles, r, level_shown_tile(lv, level_tile(lv, tx, ty), g->tile_phase),
                         (float)(tx * TILE_SIZE - cx), (float)(ty * TILE_SIZE - cy));

    if (g->phase == PHASE_ENDING) {      /* 0x3722: only the two of them */
        draw_obj(g, r, &g->ending.bride, false, alpha, cx, cy);
        draw_obj(g, r, &g->ending.titus, false, alpha, cx, cy);
        if (g->ending.stage >= END_LETTERS)
            draw_slide(g, r, g->ending.frames, SPRITE_THE, SPRITE_END, cx, cy);
        return;
    }

    /* The original draws its sprite list from the last record down to Titus (0x26A9):
     * elevators, enemy projectiles, enemies, objects, the carry slot, then Titus on top. */
    const Objects *o = &g->objects;
    const Enemies *en = &g->enemies;
    for (int i = ELEVATOR_RECORDS - 1; i >= 0; i--) {
        const Elevator *e = &g->elevators.e[i];
        if (e->sprite == ELEVATOR_NONE)
            continue;
        int x = (int)lroundf(lerp((float)e->prev_x, (float)e->x, alpha));
        int y = (int)lroundf(lerp((float)e->prev_y, (float)e->y, alpha));
        draw_sprite(g, r, e->sprite & SPRITE_NUMBER, x, y, (e->sprite & SPRITE_MIRROR) != 0, cx, cy);
    }
    for (int i = ENEMY_SHOTS - 1; i >= 0; i--)
        draw_obj(g, r, &en->shots[i], false, alpha, cx, cy);
    for (int i = en->ndisp - 1; i >= 0; i--)
        draw_obj(g, r, &en->disp[i], true, alpha, cx, cy);
    for (int i = MAX_OBJECTS - 1; i >= 0; i--)
        draw_obj(g, r, &o->obj[i], false, alpha, cx, cy);
    draw_obj(g, r, &o->carry, false, alpha, cx, cy);

    const Player *p = &g->player;
    int px = (int)lroundf(lerp((float)p->prev_x, (float)p->x, alpha));
    int py = (int)lroundf(lerp((float)p->prev_y, (float)p->y, alpha));
    if (p->grace == 0 || (p->grace / GRACE_BLINK) % 2 == 0)     /* immune: blinking */
        draw_sprite(g, r, p->sprite & SPRITE_NUMBER, px, py, (p->sprite & SPRITE_MIRROR) != 0, cx, cy);

    if (g->phase == PHASE_GAME_OVER)
        draw_slide(g, r, g->phase_frames, SPRITE_GAME, SPRITE_OVER, cx, cy);
    if (g->phase == PHASE_PLAY && g->energy_frames > 0)
        draw_energy(g, r);
    if (g->overlay) {
        draw_overlay(g, r, cx, cy);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDebugTextFormat(r, 4, vh - 22, "%d %s  lives %d energy %d bonus %d", lv->index + 1, lv->title,
                                  g->lives, g->energy, g->bonuses);
        SDL_RenderDebugTextFormat(r, 4, vh - 12, "st %d x %d y %d vx %d vy %d fl %d carry %04X song %d %d fps",
                                  p->state, p->x, p->y, p->vx, p->vy, level_floor(lv, p->x >> 4, p->y >> 4),
                                  g->objects.carry.sprite, g->song, g->fps);
    }
}

/* Menu screens: a view taller than 200 px parts the wallpaper (up) and the items below its
 * title (down), by up to MENU_GAP in all. */
#define MENU_GAP 16
static int menu_spread(void) { return SDL_clamp(video_view_h() - 200, 0, MENU_GAP) / 2; }

void game_render(Game *g, float alpha)
{
    SDL_Renderer *r = video_renderer();
    int cx = video_view_w() / 2, oy = menu_origin_y(), spread = menu_spread();
    const MenuArt *a = &g->art;
    switch (g->mode) {
    case MODE_INTRO:
        menu_draw_picture(r, g->intro_stage == 0 ? a->logo : a->title);
        break;
    case MODE_CREDITS:
        menu_draw_picture(r, a->credits);
        break;
    case MODE_NOTICE: {
        const char *lines[] = {g->notice[0], g->notice[1]};
        menu_draw_notice(a, r, lines, 2, g->notice[1][0] ? 80 : 92);
        break;
    }
    case MODE_MENU:
        menu_draw_picture_at(r, a->wallpaper, -spread);
        menu_list_draw(&g->menu, a, r, cx, oy + 146 + spread, g->frames);  /* 4 items down to y 200 */
        break;
    case MODE_OPTIONS:
        menu_draw_picture_at(r, a->wallpaper, -spread);
        menu_dim(r, 170);
        break;
    case MODE_CODE:
        menu_draw_picture_at(r, a->wallpaper, -spread);
        code_entry_draw(&g->code, a, r, cx, oy + 152 + spread, g->frames);
        break;
    case MODE_CARD: {
        int n = g->card_resumes ? g->level.index : g->next_level;
        const LevelInfo *li = level_info(n);
        menu_draw_card(a, r, n + 1, li ? li->title : "", g->bonuses, g->lives);
        break;
    }
    case MODE_PLAY:
        render_level(g, alpha);
        break;
    case MODE_PAUSE:
        render_level(g, 1.0f);
        menu_dim(r, 160);
        font_draw_centered(&a->font, r, cx, oy + 64, "PAUSE", 255);
        menu_list_draw(&g->menu, a, r, cx, oy + 96, g->frames);
        break;
    }
    video_set_fade((uint8_t)(g->fade * g->trans / 255));
}

void game_render_ui(Game *g)
{
    if (g->mode != MODE_OPTIONS)
        return;
    SDL_Color c = g->art.font.color;
    if (c.a == 0)                       /* no font: the text palette's yellow */
        c = (SDL_Color){255, 255, 85, 255};
    options_draw(&g->options, tuning.difficulty, video_renderer(), c, (uint8_t)(g->fade * g->trans / 255));
}
