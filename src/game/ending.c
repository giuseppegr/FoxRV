/* Comments name the original routines by image offset. */
#include "game/ending.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/assets.h"

#define MAX_WORDS 32
#define MAX_PATH 64
#define SLIDE_FRAMES 31             /* "THE END" letters (image 0x3964), as for "GAME OVER" */
#define LETTERS_HOLD 72             /* then this long, or until fire */
#define HEARTS_MAX (30 * 36)        /* the original waits for fire forever */

typedef struct {
    int w[MAX_WORDS];
    int n;
} Words;

static struct {
    bool ok;
    int titus_x, titus_y, titus_sprite, bride_x, bride_y, bride_sprite;
    Words titus_run, bride_run, burst;
    int speed, meet, kiss_sprite, kiss_shift, burst_rise, burst_frames, heart_sprite;
    int path[MAX_PATH][2];
    int npath;
} d;

static void read_words(const cJSON *list, Words *out)
{
    out->n = SDL_min(cJSON_GetArraySize(list), MAX_WORDS);
    for (int i = 0; i < out->n; i++)
        out->w[i] = cJSON_GetArrayItem(list, i)->valueint;
}

bool ending_load(void)
{
    memset(&d, 0, sizeof d);
    cJSON *json = assets_load_json("entities/player.json");
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(json, "ending");
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(e, "titus"), *b = cJSON_GetObjectItemCaseSensitive(e, "bride");
    if (e) {
        d.titus_x = json_int(t, "x", -100);
        d.titus_y = json_int(t, "y", 180);
        d.titus_sprite = json_int(t, "sprite", 0);
        read_words(cJSON_GetObjectItemCaseSensitive(t, "anim"), &d.titus_run);
        d.bride_x = json_int(b, "x", 420);
        d.bride_y = json_int(b, "y", 180);
        d.bride_sprite = json_int(b, "sprite", 0);
        read_words(cJSON_GetObjectItemCaseSensitive(b, "anim"), &d.bride_run);
        d.speed = json_int(e, "run_speed", 3);
        d.meet = json_int(e, "meet_distance", 28);
        d.kiss_sprite = json_int(e, "kiss_sprite", 0);
        d.kiss_shift = json_int(e, "kiss_shift", 24);
        read_words(cJSON_GetObjectItemCaseSensitive(e, "burst_anim"), &d.burst);
        d.burst_rise = json_int(e, "burst_rise", 16);
        d.burst_frames = json_int(e, "burst_frames", 16);
        d.heart_sprite = json_int(e, "heart_sprite", 0);
        const cJSON *p;
        cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(e, "heart_path")) {
            if (d.npath == MAX_PATH)
                break;
            d.path[d.npath][0] = cJSON_GetArrayItem(p, 0)->valueint;
            d.path[d.npath][1] = cJSON_GetArrayItem(p, 1)->valueint;
            d.npath++;
        }
        d.ok = d.titus_run.n > 1 && d.bride_run.n > 1 && d.burst.n > 1 && d.npath > 0;
    }
    cJSON_Delete(json);
    if (!d.ok)
        SDL_Log("ending: no wedding scene in player.json (run tools/export_player.py)");
    return d.ok;
}

/* 0x3811: advance, then follow a jump word (negative: back that many bytes). */
static int step(const Words *w, int *pos)
{
    if (w->n == 0)
        return 0;
    int p = *pos + 1;
    if (p < w->n && w->w[p] < 0)
        p += w->w[p] / 2;
    if (p < 0 || p >= w->n)
        p = 0;
    *pos = p;
    return w->w[p];
}

static void place(Obj *o, int x, int y, int sprite)
{
    *o = (Obj){.x = x, .y = y, .prev_x = x, .prev_y = y, .sprite = (uint16_t)sprite, .rider = RIDER_NONE};
}

void ending_start(Ending *e)
{
    memset(e, 0, sizeof *e);
    place(&e->titus, d.titus_x, d.titus_y, d.titus_sprite);
    place(&e->bride, d.bride_x, d.bride_y, d.bride_sprite);
    e->stage = d.ok ? END_RUN : END_LETTERS;
}

void ending_update(Ending *e, bool fire)
{
    Obj *t = &e->titus, *b = &e->bride;
    t->prev_x = t->x;
    t->prev_y = t->y;
    b->prev_x = b->x;
    b->prev_y = b->y;
    e->frames++;
    switch (e->stage) {
    case END_RUN:                    /* 0x375F */
        t->x += d.speed;
        t->sprite = (uint16_t)step(&d.titus_run, &e->titus_anim);
        b->x -= d.speed;
        b->sprite = (uint16_t)step(&d.bride_run, &e->bride_anim);
        if (b->x > t->x + d.meet)
            break;
        b->sprite = (uint16_t)d.kiss_sprite;    /* 0x378C: they meet */
        b->x -= d.kiss_shift;
        b->prev_x = b->x;
        e->titus_anim = 0;
        t->sprite = (uint16_t)d.burst.w[0];
        t->y -= d.burst_rise;
        t->prev_y = t->y;
        e->stage = END_KISS;
        e->frames = 0;
        break;
    case END_KISS:                   /* 0x37A8: the ring bursts, sinking back */
        t->sprite = (uint16_t)step(&d.burst, &e->titus_anim);
        t->y++;
        if (e->frames >= d.burst_frames) {
            e->stage = END_HEARTS;
            e->frames = 0;
        }
        break;
    case END_HEARTS:                 /* 0x37C2: the heart hops along its path */
        t->sprite = (uint16_t)d.heart_sprite;
        t->x = t->prev_x = d.path[e->path][0];
        t->y = t->prev_y = d.path[e->path][1];
        e->path = (e->path + 1) % d.npath;
        if (fire || e->frames >= HEARTS_MAX) {
            e->stage = END_LETTERS;
            e->frames = 0;
        }
        break;
    case END_LETTERS:                /* 0x37FD: "THE END" */
        if (e->frames >= SLIDE_FRAMES + LETTERS_HOLD || (e->frames > SLIDE_FRAMES && fire))
            e->stage = END_DONE;
        break;
    case END_DONE:
        break;
    }
}
