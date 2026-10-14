#include "game/level.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "core/assets.h"

#define MAX_LEVELS 32

static LevelInfo levels[MAX_LEVELS];
static int nlevels;

bool level_list_load(void)
{
    cJSON *en = assets_load_json("entities/enemies.json");
    cJSON *snd = assets_load_json("entities/sound_events.json");
    nlevels = 0;
    const cJSON *lv;
    cJSON_ArrayForEach(lv, cJSON_GetObjectItemCaseSensitive(en, "levels")) {
        if (nlevels == MAX_LEVELS)
            break;
        const cJSON *file = cJSON_GetObjectItemCaseSensitive(lv, "file");
        if (!cJSON_IsString(file))
            continue;
        LevelInfo *li = &levels[nlevels];
        SDL_strlcpy(li->file, file->valuestring, sizeof li->file);
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(lv, "name");
        SDL_strlcpy(li->title, cJSON_IsString(name) ? name->valuestring : li->file, sizeof li->title);
        char *dot = strchr(li->file, '.');
        if (dot)
            *dot = '\0';
        for (char *p = li->file; *p; p++)
            *p = (char)SDL_tolower(*p);
        char c = li->file[0] ? li->file[strlen(li->file) - 1] : '0';
        li->slot = c >= 'a' ? c - 'a' + 10 : c - '0';
        li->song = -1;
        int play = json_int(lv, "level", nlevels + 1);
        const cJSON *song;
        cJSON_ArrayForEach(song, cJSON_GetObjectItemCaseSensitive(snd, "songs")) {
            const cJSON *n;
            cJSON_ArrayForEach(n, cJSON_GetObjectItemCaseSensitive(song, "levels"))
                if (cJSON_IsNumber(n) && n->valueint == play)
                    li->song = json_int(song, "song", -1);
        }
        nlevels++;
    }
    cJSON_Delete(en);
    cJSON_Delete(snd);
    return nlevels > 0;
}

int level_count(void) { return nlevels; }

const LevelInfo *level_info(int index)
{
    return index >= 0 && index < nlevels ? &levels[index] : NULL;
}

/* Records byte for byte as in the level file ("raw" in the entity JSON, one entry per level
 * in play order); records not listed are unused (all FF). */
static const cJSON *load_records(const cJSON *json, int index, const char *list, uint8_t *out, int count, int size)
{
    memset(out, 0xFF, (size_t)count * size);
    const cJSON *entry = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "levels"), index);
    const cJSON *e;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(entry, list)) {
        int i = json_int(e, "index", -1);
        const cJSON *raw = cJSON_GetObjectItemCaseSensitive(e, "raw");
        if (i < 0 || i >= count || !cJSON_IsString(raw))
            continue;
        const char *p = raw->valuestring;
        for (int k = 0; k < size && *p; k++) {
            out[i * size + k] = (uint8_t)strtol(p, (char **)&p, 16);
            while (*p == ' ')
                p++;
        }
    }
    return entry;
}

static void load_entities(Level *lv)
{
    cJSON *json = assets_load_json("entities/enemies.json");
    const cJSON *entry = load_records(json, lv->index, "enemies", &lv->enemies[0][0], ENEMY_RECORDS, ENEMY_RECORD_SIZE);
    lv->boss_hp = json_int(entry, "boss_hit_points", 0);
    cJSON_Delete(json);
    json = assets_load_json("entities/elevators.json");
    load_records(json, lv->index, "elevators", &lv->elevators[0][0], ELEVATOR_RECORDS, ELEVATOR_RECORD_SIZE);
    cJSON_Delete(json);
}

static void pair(const cJSON *gate, const char *key, int *col, int *row)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(gate, key);
    *col = cJSON_IsNumber(cJSON_GetArrayItem(a, 0)) ? cJSON_GetArrayItem(a, 0)->valueint : 255;
    *row = cJSON_IsNumber(cJSON_GetArrayItem(a, 1)) ? cJSON_GetArrayItem(a, 1)->valueint : 255;
}

static void load_gates(Level *lv, const cJSON *list)
{
    const cJSON *g;
    cJSON_ArrayForEach(g, list) {
        if (lv->ngates == MAX_GATES)
            break;
        Gate *gt = &lv->gates[lv->ngates];
        pair(g, "entrance", &gt->entrance_col, &gt->entrance_row);
        pair(g, "screen", &gt->screen_col, &gt->screen_row);
        pair(g, "exit", &gt->exit_col, &gt->exit_row);
        gt->noscroll = json_int(g, "noscroll", 0) != 0;
        /* Unused records are FF, and a few hold junk outside the map that can never match. */
        if (gt->entrance_row < lv->rows && gt->exit_row < lv->rows)
            lv->ngates++;
    }
}

static void copy_flags(uint8_t out[256], const cJSON *list)
{
    for (int i = 0; i < 256; i++) {
        const cJSON *v = cJSON_GetArrayItem(list, i);
        out[i] = cJSON_IsNumber(v) ? (uint8_t)v->valueint : 0;
    }
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
    char rel[64];

    SDL_snprintf(rel, sizeof rel, "levels/%s.json", info->file);
    cJSON *json = assets_load_json(rel);
    if (!json)
        return false;
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
    SDL_strlcpy(lv->title, cJSON_IsString(title) ? title->valuestring : info->file, sizeof lv->title);
    lv->rows = json_int(json, "rows", 0);
    const cJSON *pos = cJSON_GetObjectItemCaseSensitive(json, "start");
    lv->start_x = json_int(pos, "x", 0);
    lv->start_y = json_int(pos, "y", 0);
    pos = cJSON_GetObjectItemCaseSensitive(json, "finish");
    lv->finish_x = json_int(pos, "x", 0);
    lv->finish_y = json_int(pos, "y", 0);
    lv->cam_limit_row = json_int(json, "unknown_a", lv->rows);
    lv->cam_limit_col = json_int(json, "unknown_b", 236);

    const cJSON *flags = cJSON_GetObjectItemCaseSensitive(json, "flags");
    copy_flags(lv->wall, cJSON_GetObjectItemCaseSensitive(flags, "horizontal"));
    copy_flags(lv->floor, cJSON_GetObjectItemCaseSensitive(flags, "floor"));
    copy_flags(lv->ceiling, cJSON_GetObjectItemCaseSensitive(flags, "ceiling"));

    const cJSON *b;
    cJSON_ArrayForEach(b, cJSON_GetObjectItemCaseSensitive(json, "bonuses")) {
        if (lv->nbonuses == MAX_BONUSES)
            break;
        lv->bonuses[lv->nbonuses++] = (Bonus){
            (uint8_t)json_int(b, "bonus_tile", 0), (uint8_t)json_int(b, "replace_tile", 0),
            json_int(b, "tile_x", 0), json_int(b, "tile_y", 0),
        };
    }
    const cJSON *o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(json, "objects")) {
        int slot = json_int(o, "index", lv->nobjects);
        if (lv->nobjects == MAX_OBJECTS || slot < 0 || slot >= MAX_OBJECTS)
            continue;
        lv->objects[lv->nobjects++] = (LevelObject){
            slot, (uint16_t)((json_int(o, "sprite", 0) & 0x1FFF) | json_int(o, "flags", 0) << 13),
            json_int(o, "x", 0), json_int(o, "y", 0),
        };
    }
    load_gates(lv, cJSON_GetObjectItemCaseSensitive(json, "gates"));
    bool ok = palette_from_vga6(&lv->pal, cJSON_GetObjectItemCaseSensitive(json, "palette_vga6"));
    cJSON_Delete(json);

    size_t size = 0;
    SDL_snprintf(rel, sizeof rel, "levels/%s_tilemap.bin", info->file);
    lv->map = ok ? assets_load(rel, &size) : NULL;
    if (!lv->map || lv->rows <= 0 || size < (size_t)lv->rows * MAP_COLS) {
        SDL_Log("%s: bad palette or tilemap", info->file);
        level_free(lv);
        return false;
    }
    /* The bonus tiles are written into the map at load, like the original (tail 90A). */
    for (int i = 0; i < lv->nbonuses; i++) {
        const Bonus *bn = &lv->bonuses[i];
        if (bn->row < lv->rows)
            lv->map[bn->row * MAP_COLS + bn->col] = bn->tile;
    }
    build_tile_animation(lv);
    load_entities(lv);
    return true;
}

void level_free(Level *lv)
{
    SDL_free(lv->map);
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
