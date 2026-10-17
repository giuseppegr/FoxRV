#include "game/level.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "core/assets.h"
#include "core/fox_ds.h"
#include "core/sqz.h"

#define MAX_LEVELS LEVEL_SLOTS

/* A level file, decoded: rows x 256 map, 256 tiles of 4 planes x 16 rows x 2 bytes, then the
 * tail. Offsets inside the tail: */
#define TILESET_BYTES (256 * 128)
#define TAIL_BYTES 3060
#define TAIL_FLAGS 0x000            /* wall, floor and ceiling flag of each tile */
#define TAIL_OBJECTS 0x300          /* 40 x 6: sprite word, x, y */
#define TAIL_CAM_ROW 0x3F0          /* soft lower camera limit (row) */
#define TAIL_START 0x3F2            /* x, y */
#define TAIL_ENEMIES 0x3F6          /* 50 x 26 */
#define TAIL_BONUSES 0x90A          /* 100 x 4: bonus tile, replacement, column, row */
#define TAIL_CAM_COL 0xA9A          /* last camera column */
#define TAIL_GATES 0xA9C            /* 20 x 7: entrance, screen, exit (col, row), no-scroll */
#define TAIL_ELEVATORS 0xB28        /* 10 x 20 */
#define TAIL_FINISH 0xBF0           /* x, y */
#define OBJECT_RECORD_SIZE 6
#define BONUS_RECORD_SIZE 4
#define GATE_RECORD_SIZE 7

static LevelInfo levels[MAX_LEVELS];
static int nlevels;

static int by_play_number(const void *a, const void *b)
{
    const LevelInfo *la = a, *lb = b;
    return (int)ds_u8(DS_LEVEL_ORDER + la->slot) - (int)ds_u8(DS_LEVEL_ORDER + lb->slot);
}

/* The file slots that have a play number (DS:523C), in play order. */
bool level_list_load(void)
{
    nlevels = 0;
    unsigned name = DS_LEVEL_NAMES;
    for (int slot = 0; slot < LEVEL_SLOTS; slot++) {
        const char *title = (const char *)ds_ptr(name);
        name += (unsigned)SDL_strlen(title) + 1;
        if (ds_u8(DS_LEVEL_ORDER + slot) == 0xFF)
            continue;
        LevelInfo *li = &levels[nlevels++];
        SDL_snprintf(li->file, sizeof li->file, "level%c", "0123456789abcdefghij"[slot]);
        SDL_strlcpy(li->title, title, sizeof li->title);
        li->slot = slot;
        li->song = (int)ds_u8(DS_LEVEL_SONG + slot);
    }
    SDL_qsort(levels, (size_t)nlevels, sizeof *levels, by_play_number);
    return nlevels > 0;
}

int level_count(void) { return nlevels; }

const LevelInfo *level_info(int index)
{
    return index >= 0 && index < nlevels ? &levels[index] : NULL;
}

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }

static bool all_ff(const uint8_t *rec, int size)
{
    for (int i = 0; i < size; i++)
        if (rec[i] != 0xFF)
            return false;
    return true;
}

/* Enemy and elevator records byte for byte as in the file; one whose word +4 is FFFF is
 * unused, and is kept all FF. */
static void copy_records(uint8_t *out, const uint8_t *in, int count, int size)
{
    for (int i = 0; i < count; i++, in += size, out += size) {
        if (u16(in + 4) == 0xFFFF)
            memset(out, 0xFF, (size_t)size);
        else
            memcpy(out, in, (size_t)size);
    }
}

static void read_tail(Level *lv, const uint8_t *tail)
{
    memcpy(lv->wall, tail + TAIL_FLAGS, 256);
    memcpy(lv->floor, tail + TAIL_FLAGS + 256, 256);
    memcpy(lv->ceiling, tail + TAIL_FLAGS + 512, 256);
    lv->cam_limit_row = (int)u16(tail + TAIL_CAM_ROW);
    lv->start_x = (int)u16(tail + TAIL_START);
    lv->start_y = (int)u16(tail + TAIL_START + 2);
    lv->cam_limit_col = (int)u16(tail + TAIL_CAM_COL);
    lv->finish_x = (int)u16(tail + TAIL_FINISH);
    lv->finish_y = (int)u16(tail + TAIL_FINISH + 2);

    for (int i = 0; i < MAX_OBJECTS; i++) {
        const uint8_t *rec = tail + TAIL_OBJECTS + i * OBJECT_RECORD_SIZE;
        if (!all_ff(rec, OBJECT_RECORD_SIZE))
            lv->objects[lv->nobjects++] = (LevelObject){i, (uint16_t)u16(rec), (int)u16(rec + 2), (int)u16(rec + 4)};
    }
    for (int i = 0; i < MAX_BONUSES; i++) {
        const uint8_t *rec = tail + TAIL_BONUSES + i * BONUS_RECORD_SIZE;
        if (!all_ff(rec, BONUS_RECORD_SIZE))
            lv->bonuses[lv->nbonuses++] = (Bonus){rec[0], rec[1], rec[2], rec[3]};
    }
    for (int i = 0; i < MAX_GATES; i++) {
        const uint8_t *rec = tail + TAIL_GATES + i * GATE_RECORD_SIZE;
        if (all_ff(rec, GATE_RECORD_SIZE))
            continue;
        /* A few levels hold junk outside the map that can never match. */
        if (rec[1] < lv->rows && rec[5] < lv->rows)
            lv->gates[lv->ngates++] = (Gate){rec[0], rec[1], rec[2], rec[3], rec[4], rec[5], rec[6] != 0};
    }
    copy_records(&lv->enemies[0][0], tail + TAIL_ENEMIES, ENEMY_RECORDS, ENEMY_RECORD_SIZE);
    copy_records(&lv->elevators[0][0], tail + TAIL_ELEVATORS, ELEVATOR_RECORDS, ELEVATOR_RECORD_SIZE);
}

/* A block that kills from above and is solid from below is solid from the sides too, as the
 * levels flag it everywhere but one: "Road Works Ahead" leaves its wall flag clear, so the
 * original walks through it. Pools (deadly floor, no ceiling) stay open. */
static void fix_deadly_blocks(Level *lv)
{
    for (int t = 0; t < 256; t++)
        if (lv->floor[t] >= FLOOR_DEADLY1 && lv->floor[t] <= FLOOR_DEADLY3 &&
            (lv->ceiling[t] & 0x7F) == CEIL_SOLID && lv->wall[t] == WALL_NONE)
            lv->wall[t] = WALL_SOLID;
}

/* The 256 tiles as a sheet of 16 x 16 tiles. */
static bool read_tileset(Level *lv, const uint8_t *tiles)
{
    if (!gfx_alloc_indexed(&lv->tileset, 16 * TILE_SIZE, 16 * TILE_SIZE))
        return false;
    uint8_t px[TILE_SIZE * TILE_SIZE];
    for (int t = 0; t < 256; t++) {
        planar_to_indexed(tiles + t * 128, TILE_SIZE, TILE_SIZE, px);
        for (int y = 0; y < TILE_SIZE; y++)
            memcpy(lv->tileset.px + (size_t)((t / 16) * TILE_SIZE + y) * lv->tileset.w + (t % 16) * TILE_SIZE,
                   px + y * TILE_SIZE, TILE_SIZE);
    }
    return true;
}

/* Image 0x3176: ceiling bit 7 starts a group of 3 tiles that rotate through the phases. */
static void build_tile_animation(Level *lv)
{
    for (int t = 0; t < 256;) {
        if ((lv->ceiling[t] & 0x80) && t + 2 < 256) {
            for (int k = 0; k < 3; k++)
                for (int phase = 0; phase < 3; phase++)
                    lv->shown[phase][t + k] = (uint8_t)(t + (k + phase) % 3);
            t += 3;
        } else {
            for (int phase = 0; phase < 3; phase++)
                lv->shown[phase][t] = (uint8_t)t;
            t++;
        }
    }
}

bool level_load(Level *lv, int index)
{
    memset(lv, 0, sizeof *lv);
    const LevelInfo *info = level_info(index);
    if (!info)
        return false;
    lv->index = index;
    SDL_strlcpy(lv->title, info->title, sizeof lv->title);

    char name[32];
    SDL_snprintf(name, sizeof name, "%s.sqz", info->file);
    size_t size;
    uint8_t *data = assets_load_sqz(name, &size);
    if (!data)
        return false;
    size_t fixed = TILESET_BYTES + TAIL_BYTES;
    lv->rows = size > fixed && (size - fixed) % MAP_COLS == 0 ? (int)((size - fixed) / MAP_COLS) : 0;
    if (lv->rows <= 0 || lv->rows != (int)ds_u8(DS_LEVEL_ROWS + info->slot)) {
        SDL_Log("%s: not a level of this FOX.COM (%zu bytes)", name, size);
        SDL_free(data);
        return false;
    }
    size_t map_bytes = (size_t)lv->rows * MAP_COLS;
    lv->map = SDL_malloc(map_bytes);
    bool ok = lv->map && read_tileset(lv, data + map_bytes);
    if (ok) {
        memcpy(lv->map, data, map_bytes);
        read_tail(lv, data + map_bytes + TILESET_BYTES);
        fix_deadly_blocks(lv);
    }
    SDL_free(data);
    if (!ok) {
        level_free(lv);
        return false;
    }

    /* The game palette with this level's colour 14 (image 0x30F3). */
    uint8_t rgb[48];
    memcpy(rgb, ds_ptr(DS_GAME_PALETTE), sizeof rgb);
    memcpy(rgb + 3 * LEVEL_COLOUR_INDEX, ds_ptr(DS_LEVEL_COLOUR + 3 * info->slot), 3);
    palette_from_vga6(&lv->pal, rgb);
    lv->boss_hp = (int)ds_u8(DS_LEVEL_BOSS_HP + info->slot);

    /* The bonus tiles are written into the map at load, like the original (tail 90A). */
    for (int i = 0; i < lv->nbonuses; i++) {
        const Bonus *bn = &lv->bonuses[i];
        if (bn->row < lv->rows)
            lv->map[bn->row * MAP_COLS + bn->col] = bn->tile;
    }
    build_tile_animation(lv);
    return true;
}

void level_free(Level *lv)
{
    SDL_free(lv->map);
    gfx_free_indexed(&lv->tileset);
    memset(lv, 0, sizeof *lv);
}

int level_tile(const Level *lv, int col, int row)
{
    if (col < 0 || col >= MAP_COLS || row < 0 || row >= lv->rows)
        return 0;
    return lv->map[row * MAP_COLS + col];
}

int level_floor(const Level *lv, int col, int row) { return lv->floor[level_tile(lv, col, row)]; }
int level_wall(const Level *lv, int col, int row) { return lv->wall[level_tile(lv, col, row)]; }
int level_ceiling(const Level *lv, int col, int row) { return lv->ceiling[level_tile(lv, col, row)] & 0x7F; }

int level_shown_tile(const Level *lv, int tile, int phase)
{
    return lv->shown[phase % 3][tile & 0xFF];
}

int level_take_bonus(Level *lv, int col, int row)
{
    for (int i = 0; i < lv->nbonuses; i++) {
        const Bonus *bn = &lv->bonuses[i];
        if (bn->col == col && bn->row == row) {
            int tile = bn->tile;
            if (row >= 0 && row < lv->rows)
                lv->map[row * MAP_COLS + col] = bn->replace;
            return tile;
        }
    }
    return -1;
}
