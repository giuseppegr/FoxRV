/* The whole game: the front-end screens (intro, menu, level codes, between-levels card,
 * pause) and one play session: the current level, Titus, the camera, lives and energy, and
 * the original's main-loop rules around them (finish, gates, death, game over, music after
 * jingles).
 */
#ifndef FOXRV_GAME_H
#define FOXRV_GAME_H

#include <stdbool.h>

#include "game/camera.h"
#include "game/elevators.h"
#include "game/ending.h"
#include "game/enemies.h"
#include "game/level.h"
#include "game/menu.h"
#include "game/objects.h"
#include "game/player.h"
#include "platform/gfx.h"

typedef enum {
    DIFF_HIGH,                  /* the original: a hit costs 2 energy */
    DIFF_MEDIUM,                /* 1 energy */
    DIFF_LOW,                   /* 1 energy, then 2 s immune to hits (Titus blinks) */
    DIFF_COUNT
} Difficulty;

typedef struct {
    Difficulty difficulty;      /* [player] difficulty, and the main menu */
    PlayerTuning player;
    CameraTuning camera;
    bool interpolate;           /* draw between game frames on fast displays */
    bool invincible;
    int start_lives;            /* -1 = original */
    bool restart_level;         /* after a death: the original's respawn at the last checkpoint
                                 * (false, default) or the whole level again from its start */
} GameTuning;

typedef enum {
    MODE_INTRO,                 /* TITUS logo, then the title picture */
    MODE_MENU,                  /* main menu on the wallpaper */
    MODE_CODE,                  /* level code entry */
    MODE_CARD,                  /* between levels: level, name, bonuses, lives */
    MODE_PLAY,                  /* in a level, see GamePhase */
    MODE_PAUSE,
    MODE_NOTICE,                /* black screen with a checkpoint or level code, then back */
    MODE_CREDITS,               /* after the ending: the developers' photo */
} GameMode;

typedef enum {
    PHASE_PLAY,
    PHASE_DYING,                /* Titus flies off the screen (song 1) */
    PHASE_GAME_OVER,            /* "GAME OVER" slides in (song 2) */
    PHASE_LEVEL_DONE,           /* at the finish: frozen while song 4 plays */
    PHASE_ENDING,               /* "Just Married": the wedding scene, then "THE END" */
} GamePhase;

/* What happens when the screen has faded out. */
typedef enum { GO_NONE, GO_TITLE, GO_MENU, GO_CARD, GO_LEVEL, GO_CREDITS, GO_NOTICE, GO_PLAY } GameGo;

typedef struct {
    GameMode mode;
    GamePhase phase;
    int phase_frames;
    bool quit;                  /* the player chose to quit */

    Level level;
    Player player;
    Objects objects;
    Enemies enemies;
    Elevators elevators;
    Ending ending;
    Camera camera;
    bool camera_frozen;         /* DS:6502: a no-scroll gate holds the camera */
    Tileset tiles;
    SpriteAtlas atlas;
    bool level_loaded;

    MenuArt art;
    MenuList menu;
    CodeEntry code;
    int intro_stage;            /* 0 logo, 1 title picture */
    int intro_frames;           /* also counts the credits */
    int next_level;             /* play index the card leads to */
    bool card_resumes;          /* the card returns to the level instead of loading one */
    int card_frames;
    int card_count;             /* bonus-to-life counting on the card (image 0x38D) */

    int death_vy;
    int lives, energy, bonuses;     /* DS:041B / 0419 / 041A */
    int energy_frames;              /* DS:6506: the energy meter shows while > 0 */
    int dying;                      /* DS:6516 */
    bool game_over;                 /* DS:6517 */
    unsigned frames;                /* DS:64FE */
    int tile_phase;
    int song, sfx;

    int fade, fade_dir;         /* debug fade (F) */
    int trans, trans_dir;       /* screen transitions: 0 black .. 255 visible */
    GameGo go;

    bool overlay;
    int warp;                   /* debug_warp: next target (gates, then the finish) */
    int fps;
    char notice[2][32];         /* lines of the notice screen; notice[0][0] set = one is due */
    int notice_frames;
} Game;

/* engine.ini sections [player] and [camera] (ConfigHandler signature). */
bool game_config(const char *section, const char *key, const char *value);
GameTuning *game_tuning(void);

/* start_level: play number (1-based) to start in directly, or 0 for the intro and menu. */
bool game_init(Game *g, int start_level);
void game_shutdown(Game *g);
void game_update(Game *g);
/* alpha: 0..1, how far the display is between the previous and the current game frame. */
void game_render(Game *g, float alpha);

#endif
