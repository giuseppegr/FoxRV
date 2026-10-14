/* A level's map and the data the game logic needs, decoded from its LEVELx.SQZ file and the
 * tables in FOX.COM. No rendering state: the tileset texture lives with the renderer.
 */
#ifndef FOXRV_LEVEL_H
#define FOXRV_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

#include "game/anim.h"
#include "platform/gfx.h"

#define MAP_COLS 256
#define MAX_BONUSES 100
#define ENEMY_RECORDS 50
#define ENEMY_RECORD_SIZE 26
#define ELEVATOR_RECORDS 10
#define ELEVATOR_RECORD_SIZE 20
#define MAX_OBJECTS 40
#define MAX_GATES 20

/* Floor flags (handlers at DS:727D). */
enum {
    FLOOR_NONE, FLOOR_SOLID, FLOOR_SLIPPERY1, FLOOR_SLIPPERY2, FLOOR_SLIPPERY3, FLOOR_DROP,
    FLOOR_LADDER, FLOOR_BONUS, FLOOR_DEADLY1, FLOOR_DEADLY2, FLOOR_DEADLY3, FLOOR_CODE,
    FLOOR_CHECKPOINT, FLOOR_CODE14, FLOOR_FLAG_COUNT
};
/* Wall ("horizontal") flags (DS:726F). */
enum { WALL_NONE, WALL_SOLID, WALL_BONUS, WALL_DEADLY, WALL_CODE, WALL_CHECKPOINT, WALL_CODE14, WALL_FLAG_COUNT };
/* Ceiling flags, low 7 bits (DS:7299); bit 7 marks an animated tile group. */
enum { CEIL_NONE, CEIL_SOLID, CEIL_LADDER, CEIL_CHECKPOINT, CEIL_DEADLY, CEIL_FLAG_COUNT };

typedef struct {
    uint8_t tile, replace;      /* tile placed at load, and what it becomes when taken */
    int col, row;
} Bonus;

typedef struct {
    int slot;                   /* object slot 0..39 (tail record index) */
    uint16_t sprite;            /* sprite word: number | flags << 13 */
    int x, y;
} LevelObject;

/* Tail A9C: crouching still on the entrance tile moves Titus to the exit and the camera to
 * the screen position (all in tiles). */
typedef struct {
    int entrance_col, entrance_row;
    int screen_col, screen_row;
    int exit_col, exit_row;
    bool noscroll;              /* the camera stays put until the next gate or death */
} Gate;

typedef struct {
    char file[16];              /* "level0" */
    char title[48];             /* "ON THE FOXY TRAIL" */
    int slot;                   /* file slot: '0'-'9' -> 0-9, 'a'-'j' -> 10-19 (DS:5250) */
    int song;                   /* level music, -1 if none */
} LevelInfo;

/* File slots with special rules at the finish (image 0x3AB5). */
#define SLOT_IDOL_NEEDED 9      /* "Walls of Sand": the idol must be carried to the finish */
#define SLOT_ENDING 16          /* "Just Married": the finish starts the ending */

typedef struct {
    int index;                  /* play order, 0-based */
    char title[64];
    int rows;
    uint8_t *map;               /* rows x MAP_COLS tiles, bonus tiles applied */
    IndexedImage tileset;       /* the 256 tiles, 16 per row */
    uint8_t wall[256], floor[256], ceiling[256];
    uint8_t shown[3][256];      /* tile displayed in each phase of the tile animation */
    Palette pal;
    int start_x, start_y, finish_x, finish_y;
    int cam_limit_row;          /* tail 3F0: soft lower camera limit (top row of the 12-row screen) */
    int cam_limit_col;          /* tail A9A: last camera column (236 = whole map) */
    Bonus bonuses[MAX_BONUSES];
    int nbonuses;
    uint8_t enemies[ENEMY_RECORDS][ENEMY_RECORD_SIZE];   /* tail 3F6, as stored */
    uint8_t elevators[ELEVATOR_RECORDS][ELEVATOR_RECORD_SIZE];   /* tail B28, as stored */
    int boss_hp;                /* DS:50B4 for this level */
    LevelObject objects[MAX_OBJECTS];
    int nobjects;
    Gate gates[MAX_GATES];
    int ngates;
} Level;

/* Level list in play order (from the tables in FOX.COM). */
bool level_list_load(void);
int level_count(void);
const LevelInfo *level_info(int index);

bool level_load(Level *lv, int index);
void level_free(Level *lv);

/* Tile number; 0 outside the map. */
int level_tile(const Level *lv, int col, int row);
int level_floor(const Level *lv, int col, int row);
int level_wall(const Level *lv, int col, int row);
int level_ceiling(const Level *lv, int col, int row);  /* low 7 bits */

/* Tile to draw for map tile `tile` in animation phase 0..2 (stepped every 4 frames). */
int level_shown_tile(const Level *lv, int tile, int phase);

/* If (col, row) holds a bonus, replaces it and returns its tile; otherwise -1 (image 0x4F10). */
int level_take_bonus(Level *lv, int col, int row);

#endif
