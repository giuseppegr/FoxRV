#include "game/camera.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>

#define ORIG_COLS 20
#define ORIG_ROWS 12
#define TILE 16
#define LOOK_RANGE 200          /* an enemy this close behind recentres the view */

void camera_tuning_defaults(CameraTuning *t)
{
    *t = (CameraTuning){.mode = CAM_ORIGINAL, .speed = 8, .ease = 6};
}

/* Scale from the original 320 x 192 play area to the view. */
static float sx(int view_w) { return view_w / (float)(ORIG_COLS * TILE); }
static float sy(int view_h) { return view_h / (float)(ORIG_ROWS * TILE); }

/* Where the player stands on screen, in original columns/rows. */
static int screen_col(const Camera *c, const Player *p, int view_w)
{
    return (int)floorf((p->x - c->x) / (TILE * sx(view_w)));
}

static int screen_row(const Camera *c, const Player *p, int view_h)
{
    return (int)floorf((p->y - c->y) / (TILE * sy(view_h)));
}

/* Camera position that puts the player at a target column/row. */
static float x_for(const Player *p, int col, int view_w) { return p->x - (col * TILE + TILE / 2) * sx(view_w); }
static float y_for(const Player *p, int row, int view_h) { return p->y - row * TILE * sy(view_h); }

static float max_x(const Level *lv, const Player *p, int view_w)          /* 0x2BB5 */
{
    int last_col = lv->cam_limit_col;
    if ((p->x >> 4) - ORIG_COLS > last_col)
        last_col = MAP_COLS - ORIG_COLS;
    float m = (last_col + ORIG_COLS) * TILE - view_w;
    return SDL_max(m, 0.0f);
}

static float max_y(const Level *lv, int view_h)
{
    return SDL_max((float)(lv->rows * TILE - view_h), 0.0f);
}

/* Tail 3F0, the soft lower limit L: the original's 12-row screen stops at row L + 11, so the
 * line (L + 12) * 16 is the feet line of the floor below it. The view's bottom edge goes one
 * row lower than that, so the floor Titus walks on is shown. */
static float limit_line(const Level *lv)
{
    return (float)SDL_min((lv->cam_limit_row + ORIG_ROWS + 1) * TILE, lv->rows * TILE);
}

/* Moves `pos` toward `goal`; returns true once there (or blocked). `chase` is the player's
 * speed on this axis in px per frame. */
static bool approach(float *pos, float goal, float lo, float hi, const CameraTuning *t, float scale, float chase)
{
    goal = SDL_clamp(goal, lo, hi);
    float d = goal - *pos;
    bool tiles = t->mode == CAM_TILES;
    float step = tiles ? TILE : TILE * scale;
    if (t->mode == CAM_SMOOTH) {    /* full speed far away, slowing down over the last stretch */
        step = t->speed * scale;
        if (t->ease > 0)
            step = SDL_min(step, SDL_max(1.0f, fabsf(d) / t->ease));
        step = SDL_max(step, chase + 1);    /* never slower than the player: it would trail forever */
    }
    if (fabsf(d) <= (tiles ? TILE / 2.0f : step)) {
        if (!tiles)
            *pos = goal;
        return true;
    }
    *pos += d > 0 ? step : -step;
    if (tiles)
        *pos = roundf(*pos / TILE) * TILE;
    *pos = SDL_clamp(*pos, lo, hi);
    return false;
}

/* 0x42FA: when standing still, centre if an enemy is close behind, else look ahead. */
static int look_target(const Enemies *en, const Player *p)
{
    bool facing_left = (p->sprite & SPRITE_MIRROR) != 0;
    for (int i = 0; i < en->ndisp; i++) {
        const Obj *e = &en->disp[i];
        if (e->sprite == OBJ_NONE)
            continue;
        bool left_of_player = e->x < p->x;
        if (left_of_player != facing_left && abs(e->x - p->x) < LOOK_RANGE)
            return 10;
    }
    return facing_left ? 17 : 3;
}

static void horizontal(Camera *c, const Level *lv, const Player *p, const Enemies *en, const CameraTuning *t,
                       int view_w)  /* 0x423C */
{
    int dx = screen_col(c, p, view_w);
    if (!c->hscroll) {
        bool decided = false;
        if (p->same_frames >= 22) {
            if ((p->state & 15) == PS_WALK) {
                decided = true;      /* walking: never the recentring fallback */
                if (abs(p->vx) == 64) {
                    if (p->vx >= 0 && dx >= 13) {
                        c->htarget = 2;
                        c->hscroll = true;
                    } else if (p->vx < 0 && dx <= 7) {
                        c->htarget = 18;
                        c->hscroll = true;
                    }
                } else {
                    c->htarget = (p->sprite & SPRITE_MIRROR) ? 18 : 2;
                    c->hscroll = true;
                }
            } else if (p->vx == 0 && p->vy == 0 && p->same_frames == 33) {
                c->htarget = look_target(en, p);
                c->hscroll = true;
                decided = true;
            }
        }
        if (!decided && (dx > 18 || dx < 2)) {
            c->htarget = 10;
            c->hscroll = true;
        }
    }
    if (!c->hscroll)
        return;
    /* Done once the player is in the target column, as in the original (0x42DE): a camera
     * still finishing the last pixels would not look for a new target in the meantime. */
    if (dx == c->htarget ||
        approach(&c->x, x_for(p, c->htarget, view_w), 0, max_x(lv, p, view_w), t, sx(view_w), abs(p->vx) / 16.0f))
        c->hscroll = false;
}

static void vertical(Camera *c, const Level *lv, const Player *p, const CameraTuning *t, int view_h)    /* 0x4174 */
{
    float s = sy(view_h), top = 0, bottom = max_y(lv, view_h);
    if (p->vy == 0)
        c->vscroll = false;
    int dy = screen_row(c, p, view_h);
    if (!c->vscroll) {
        int target = -1;
        if (p->vy != 0 || p->on_ladder)
            target = dy >= 10 ? 3 : dy <= 2 ? 9 : -1;
        else
            target = dy >= 11 ? 10 : dy <= 2 ? 9 : -1;
        if (target >= 0) {
            c->vtarget = target;
            c->vscroll = true;
        }
    }

    /* Soft lower limit: the view's bottom edge stays on the line unless the player is below. */
    float line = limit_line(lv);
    float limit_y = line - view_h;
    float chase = abs(p->vy) / 16.0f;
    if (p->y <= line && c->y > limit_y + TILE) {
        approach(&c->y, limit_y, top, bottom, t, s, chase); /* pushed back up */
        return;
    }
    float above = limit_y - c->y;
    if (above >= 0 && above < 5 * TILE && line - p->y >= 0 && line - p->y < 112) {
        approach(&c->y, limit_y, top, bottom, t, s, chase); /* creep down to the limit */
        return;
    }
    if (!c->vscroll)
        return;
    float goal = y_for(p, c->vtarget, view_h);
    if (goal > c->y && p->y <= line)
        goal = SDL_min(goal, SDL_max(limit_y, c->y));      /* not below the limit */
    if (dy == c->vtarget || approach(&c->y, goal, top, bottom, t, s, chase))  /* 0x420C */
        c->vscroll = false;
}

static void clamp(Camera *c, const Level *lv, const Player *p, int view_w, int view_h)
{
    float mx = max_x(lv, p, view_w), my = max_y(lv, view_h);
    c->x = mx > 0 ? SDL_clamp(c->x, 0.0f, mx) : (MAP_COLS * TILE - view_w) / 2.0f;
    c->y = lv->rows * TILE > view_h ? SDL_clamp(c->y, 0.0f, my) : (lv->rows * TILE - view_h) / 2.0f;
}

void camera_reset(Camera *c, const Level *lv, const Player *p, int view_w, int view_h)
{
    *c = (Camera){0};
    c->x = x_for(p, 10, view_w);
    c->y = y_for(p, 9, view_h);
    float line = limit_line(lv);
    if (p->y <= line)
        c->y = SDL_min(c->y, line - view_h);
    clamp(c, lv, p, view_w, view_h);
    c->prev_x = c->x;
    c->prev_y = c->y;
}

void camera_show(Camera *c, const Level *lv, int col, int row, int view_w, int view_h)
{
    *c = (Camera){0};
    float mx = (float)(MAP_COLS * TILE - view_w), my = (float)(lv->rows * TILE - view_h);
    c->x = col * TILE - (view_w - ORIG_COLS * TILE) / 2.0f;
    c->y = row * TILE - (view_h - ORIG_ROWS * TILE) / 2.0f;
    c->x = roundf(mx > 0 ? SDL_clamp(c->x, 0.0f, mx) : mx / 2);
    c->y = roundf(my > 0 ? SDL_clamp(c->y, 0.0f, my) : my / 2);
    c->prev_x = c->x;
    c->prev_y = c->y;
}

void camera_update(Camera *c, const Level *lv, const Player *p, const Enemies *en, const CameraTuning *t,
                   int view_w, int view_h)
{
    c->prev_x = c->x;
    c->prev_y = c->y;
    horizontal(c, lv, p, en, t, view_w);
    vertical(c, lv, p, t, view_h);
    clamp(c, lv, p, view_w, view_h);
}
