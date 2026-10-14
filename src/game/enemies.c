/* Comments name the original routines by image offset. Handlers work on the raw records with
 * the original's offsets, signedness and byte/word widths.
 *
 * The original's screen is 20 x 12 tiles; tests that mean "on screen" or "off screen" are
 * widened by the extra columns/rows of the view (see Ctx.xcols / Ctx.xrows). Distances that
 * are about the player (alert ranges, 640 px "lost") are kept. */
#include "game/enemies.h"

#include <stdlib.h>
#include <string.h>

#include "game/anim.h"
#include "game/player.h"

#define ORIG_VIEW_W 320
#define ORIG_VIEW_H 192
#define KEY_SPRITE 0x65         /* sprite = key + 0x65 */
#define SHOT_SPRITE 0x1E        /* projectile sprite = word + 0x1E */
#define SHOT_SPEED 0xB0
#define SHOT_KNOCK 70
#define LOST_DISTANCE 640

typedef struct {
    Enemies *en;
    const Level *lv;
    Player *p;
    Objects *o;
    unsigned frame;
    PlayerEvents *ev;
    int camcol, camrow;         /* camera position in tiles */
    int xcols, xrows;           /* view size beyond the original's 20 x 12 tiles */
} Ctx;

/* --- record access ------------------------------------------------------------------------ */

static int w(const uint8_t *e, int o) { return (int16_t)(e[o] | e[o + 1] << 8); }
static unsigned uw(const uint8_t *e, int o) { return e[o] | (unsigned)e[o + 1] << 8; }
static void sw(uint8_t *e, int o, int v)
{
    e[o] = (uint8_t)v;
    e[o + 1] = (uint8_t)(v >> 8);
}
static int sar(int v, int n) { return v >= 0 ? v >> n : -((-v + (1 << n) - 1) >> n); }

#define X(e) w(e, EN_X)
#define Y(e) w(e, EN_Y)
#define SPEED(e) w(e, EN_SPEED)
#define STATE(e) ((e)[EN_STATE])
#define KILLED(e) ((e)[7] & 0xC0)
#define EVENT(e) ((e)[7] & 0x20)
#define ONSCREEN(e) ((e)[5] & 0x20)

int enemy_x(const Enemies *en, int i) { return w(en->r[i], EN_X); }
int enemy_y(const Enemies *en, int i) { return w(en->r[i], EN_Y); }

/* --- animation (0x66DF / 0x66EF / 0x66FF) ------------------------------------------------- */

static void next_seq(uint8_t *e)
{
    unsigned a = uw(e, EN_ANIM);
    do
        a += 2;
    while (enemy_words_contain(a) && enemy_word(a) >= 0);
    sw(e, EN_ANIM, (int)(a + 2));
}

static void prev_seq(uint8_t *e)
{
    unsigned a = uw(e, EN_ANIM);
    do
        a -= 2;
    while (enemy_words_contain(a) && enemy_word(a) >= 0);
    sw(e, EN_ANIM, (int)(a - 2));
}

/* Follows jump words (a negative word jumps back that many bytes). */
static unsigned follow(unsigned a)
{
    for (int guard = 0; guard < 16 && enemy_word(a) < 0; guard++)
        a += enemy_word(a);
    return a;
}

static void animate(uint8_t *e, Obj *d)
{
    if (KILLED(e)) {
        d->sprite = (uint16_t)((uw(e, EN_KEY) & ~0x2000u) + KEY_SPRITE);
        e[5] |= 0x20;
        return;
    }
    e[7] &= ~0x20;
    unsigned a = follow(uw(e, EN_ANIM));
    int v = enemy_word(a);
    if (v == ENEMY_REMOVE_WORD) {   /* hidden (and harmless) until the handler moves on */
        d->sprite = OBJ_NONE;
        sw(e, EN_KEY, 0x05AA);
        return;
    }
    e[7] |= (uint8_t)((v >> 8) & 0x20);
    unsigned key = ((unsigned)v & ~0x2000u) | ((unsigned)(e[9] & 0x80) << 8);
    sw(e, EN_KEY, (int)key);
    d->sprite = (uint16_t)(key + KEY_SPRITE);
    a += 2;
    if (enemy_word(a) < 0)
        a += enemy_word(a);
    sw(e, EN_ANIM, (int)a);
    e[5] |= 0x20;
}

/* --- shared helpers ----------------------------------------------------------------------- */

static int dist(int a, int b) { return abs(a - b); }

static void aim(Ctx *c, uint8_t *e)                       /* 0x641C: toward Titus */
{
    int s = e[0x13];
    sw(e, EN_SPEED, X(e) > c->p->x ? s : -s);
}

static void turn_home(uint8_t *e)                         /* 0x60CA */
{
    int s = abs(SPEED(e));
    sw(e, EN_SPEED, w(e, 0xF) > X(e) ? -s : s);
}

/* 0x6767: floor flag of the tile at the feet; also returns the tile. */
static int floor_at(Ctx *c, const uint8_t *e, int *col, int *row)
{
    *row = Y(e) >> 4;
    *col = (int)((unsigned)uw(e, EN_X) >> 4);
    return level_floor(c->lv, *col, *row);
}

static bool wall_at(Ctx *c, int col, int row) { return level_wall(c->lv, col, row) & 1; }

/* 0x60E4: the next column in the direction of travel. */
static int ahead(const uint8_t *e, int col) { return col - (SPEED(e) > 0 ? 1 : -1); }

static void walk(uint8_t *e)                              /* x -= speed, bouncing off x < 0 */
{
    int s = SPEED(e);
    sw(e, EN_X, X(e) - s);
    if (X(e) < 0) {
        sw(e, EN_X, X(e) + s);
        sw(e, EN_SPEED, -s);
    }
}

static void snap_y(uint8_t *e) { e[2] &= 0xF0; }

/* Is the home position (+0F, +11) far enough off screen to teleport back to it? */
static bool home_hidden(Ctx *c, const uint8_t *e, int row_limit, int col_limit)
{
    unsigned dr = (unsigned)((w(e, 0x11) >> 4) - c->camrow);
    unsigned dc = (unsigned)((w(e, 0xF) >> 4) - c->camcol);
    return dr > (unsigned)(row_limit + c->xrows) || dc >= (unsigned)(col_limit + c->xcols);
}

static void go_home(uint8_t *e)
{
    sw(e, EN_X, w(e, 0xF));
    sw(e, EN_Y, w(e, 0x11));
    STATE(e) = 0;
}

static Obj *free_shot(Enemies *en)                         /* 0x5B2D */
{
    for (int i = 0; i < ENEMY_SHOTS; i++)
        if (en->shots[i].sprite == OBJ_NONE)
            return &en->shots[i];
    return NULL;
}

/* 0x6696: a projectile from the two words after the event frame (sprite, height). */
static void spawn_shot(Ctx *c, const uint8_t *e, Obj *s)
{
    unsigned a = uw(e, EN_ANIM);
    int spr = ((enemy_word(a - 4) & 0x1FFF) + 0x8000 + SHOT_SPRITE) & 0xFFFF;
    int vx = SHOT_SPEED;
    if (X(e) >= c->p->x) {
        vx = -vx;
        spr ^= 0x8000;
    }
    *s = (Obj){.rider = RIDER_NONE};
    s->x = s->prev_x = X(e);
    s->y = s->prev_y = Y(e) - (int8_t)enemy_word(a - 2);
    s->sprite = (uint16_t)spr;
    s->vx = vx;
    s->x += sar(vx, 4);
}

/* --- death (0x5C58) ----------------------------------------------------------------------- */

static void dying(Ctx *c, uint8_t *e)
{
    if (!(e[7] & 0x40)) {
        e[7] |= 0x40;
        int cls = enemy_class(uw(e, EN_KEY) & 0x1FFF);
        if ((cls & 0xF0) != 0xF0) { /* a corpse frame that stays put */
            sw(e, EN_KEY, (int)((uw(e, EN_KEY) & 0x8000) | (unsigned)enemy_death_key(cls)));
            sw(e, EN_SPEED, 0);
            STATE(e) = 0xFF;
            return;
        }
        sw(e, EN_SPEED, -10);       /* jumps up, then falls off the screen */
        STATE(e) = 0;
    }
    if (STATE(e) == 0xFF)
        return;
    int v = SPEED(e);
    sw(e, EN_Y, Y(e) + v);
    if (c->o->star)
        c->o->carry.y += v;         /* the impact star follows the body */
    if (v < 20)
        sw(e, EN_SPEED, v + 1);
}

/* --- behaviours (table DS:8EB5) ----------------------------------------------------------- */

static void patrol(uint8_t *e)                            /* 0x65C9: types 0, 1 */
{
    sw(e, EN_X, X(e) - SPEED(e));
    int d = X(e) - w(e, 0xF);
    if ((unsigned)abs(d) > uw(e, 0x11)) {
        int s = abs(SPEED(e));
        sw(e, EN_SPEED, d < 0 ? -s : s);   /* back toward the centre */
    }
}

static void shooter(Ctx *c, uint8_t *e)                   /* 0x65F1: type 2 */
{
    if (!ONSCREEN(e))
        return;
    uint8_t dir = e[0x12];
    int face = 0;
    if (dir & 0xC0)
        face = (dir & 0x40) ? 0 : -1;
    else
        face = X(e) >= c->p->x ? 0 : -1;
    sw(e, EN_SPEED, face);
    if (STATE(e) == 0) {
        if (e[0xF])
            e[0xF]--;
        if (e[0xF] != 0)
            return;
        if (dist(c->p->y, Y(e)) > 24 || (unsigned)dist(c->p->x, X(e)) > (uw(e, 0x11) & 0x3FFF))
            return;
        int lock = dir >> 6;
        if (lock == 1 && c->p->x > X(e))
            return;
        if (lock >= 2 && X(e) > c->p->x)
            return;
        STATE(e) = 30;
        next_seq(e);
        return;
    }
    STATE(e)--;
    if (!EVENT(e))
        return;
    sw(e, EN_ANIM, (int)uw(e, EN_ANIM) + 4);
    Obj *s = free_shot(c->en);
    if (s) {
        spawn_shot(c, e, s);
        e[0xF] = e[0x10];           /* reload time */
    }
    STATE(e) = 0;
}

static void leaper(Ctx *c, uint8_t *e)                    /* 0x64C9: types 3, 4 */
{
    Player *p = c->p;
    if (STATE(e) == 0) {
        patrol(e);
        if (!ONSCREEN(e))
            return;
        int dy = Y(e) - p->y;
        if (dy < 0 || dy > 255 || e[0x13] < dy)
            return;
        int dx = X(e) - p->x;
        uint8_t behind = dx < 0 ? 0x80 : 0;
        if ((e[5] & 0x80) != behind || abs(dx) >= 48)
            return;
        if ((unsigned)abs(p->x - w(e, 0xF)) > uw(e, 0x11))
            return;
        STATE(e)++;
        int n = 0, sum = 0;
        do {                        /* smallest n with 1 + 2 + ... + n >= dy */
            n++;
            sum = (sum + n) & 0xFF;
        } while (dy > sum);
        sw(e, 0x14, -n);
        sw(e, 0x16, Y(e));
        next_seq(e);
        return;
    }
    if (!ONSCREEN(e))
        return;
    if (STATE(e) == 1) {            /* rising */
        sw(e, EN_X, X(e) - (int16_t)(SPEED(e) * 4));
        int vy = w(e, 0x14);
        sw(e, EN_Y, Y(e) + vy);
        if (vy + 1 != 0) {
            sw(e, 0x14, vy + 1);
            if (Y(e) > w(e, 0x16) - e[0x13])
                return;
        }
        next_seq(e);
        STATE(e)++;
        sw(e, 0x14, 0);
        int s = abs(SPEED(e));
        sw(e, EN_SPEED, X(e) > w(e, 0xF) ? -s : s);
    } else if (STATE(e) == 2) {     /* falling */
        sw(e, EN_X, X(e) - SPEED(e));
        int vy = w(e, 0x14);
        sw(e, EN_Y, Y(e) + vy);
        sw(e, 0x14, vy + 1);
        if (Y(e) < w(e, 0x16))
            return;
        sw(e, EN_Y, w(e, 0x16));
        sw(e, EN_X, X(e) - SPEED(e));
        STATE(e) = 0;
        prev_seq(e);
        prev_seq(e);
    }
}

static void swooper(Ctx *c, uint8_t *e)                   /* 0x642E: types 5, 6 */
{
    patrol(e);
    if (!ONSCREEN(e))
        return;
    if (STATE(e) == 0) {
        int dy = Y(e) - c->p->y;
        int vy = dy < 0 ? 2 : -2;
        if (abs(dy) > e[0x13] || dist(X(e), c->p->x) > 40)
            return;
        sw(e, 0x16, Y(e));
        sw(e, 0x14, vy);
        STATE(e)++;
        next_seq(e);
    } else if (STATE(e) == 1) {
        sw(e, EN_Y, Y(e) + w(e, 0x14));
        if (dist(Y(e), w(e, 0x16)) < e[0x13])
            return;
        sw(e, 0x14, -w(e, 0x14));
        next_seq(e);
        STATE(e)++;
    } else if (STATE(e) == 2) {
        sw(e, EN_Y, Y(e) + w(e, 0x14));
        if (Y(e) != w(e, 0x16))
            return;
        prev_seq(e);
        prev_seq(e);
        STATE(e) = 0;
    }
}

/* The walk of the gravity walkers (types 7, 8, 14): fall, land, turn at walls, walk. */
static bool gravity_walk(Ctx *c, uint8_t *e)
{
    int col, row;
    if (floor_at(c, e, &col, &row) == 0) {
        if (uw(e, 0x15) < 16)
            sw(e, 0x15, w(e, 0x15) + 1);
        sw(e, EN_Y, Y(e) + w(e, 0x15));
        return false;               /* falling: no sideways move */
    }
    if (w(e, 0x15) != 0)
        aim(c, e);                  /* landed: face Titus again */
    sw(e, 0x15, 0);
    snap_y(e);
    if (wall_at(c, ahead(e, col), row - 1))
        sw(e, EN_SPEED, -SPEED(e));
    walk(e);
    return true;
}

static void striker(Ctx *c, uint8_t *e)                   /* 0x62E0: type 7 */
{
    Player *p = c->p;
    switch (STATE(e)) {
    case 0:
        if (Y(e) > p->y || (unsigned)dist(X(e), p->x) > uw(e, 0x17) || dist(Y(e), p->y) > 200)
            return;
        STATE(e)++;
        next_seq(e);
        aim(c, e);
        return;
    case 3:                          /* keeps walking until the strike's event frame */
        if (EVENT(e)) {
            STATE(e) = 1;
            return;
        }
        /* fall through */
    case 1:
        if (!gravity_walk(c, e))
            return;
        if (dist(p->x, X(e)) > LOST_DISTANCE || dist(p->y, Y(e)) >= 400) {
            STATE(e) = 2;
            return;
        }
        if (STATE(e) == 1) {
            /* The original reads this width from the table by key, not by sprite. */
            const SpriteInfo *s = sprite_info(uw(e, EN_KEY) & 0x1FFF);
            int reach = (s ? s->w : 0) + 6;
            if (dist(p->x, X(e)) <= reach && dist(p->y, Y(e)) <= 8) {
                STATE(e) = 3;
                next_seq(e);
            }
        }
        return;
    case 2:
        if (home_hidden(c, e, 13, 21)) {
            go_home(e);
            prev_seq(e);
        }
        return;
    }
}

static void wanderer(Ctx *c, uint8_t *e)                  /* 0x6219: types 8, 14 */
{
    Player *p = c->p;
    switch (STATE(e)) {
    case 0:                          /* wakes up while out of sight */
        if (dist(X(e), p->x) > 340 + (c->en->view_w - ORIG_VIEW_W) ||
            dist(Y(e), p->y) >= 230 + (c->en->view_h - ORIG_VIEW_H)) {
            STATE(e)++;
            next_seq(e);
            aim(c, e);
        }
        return;
    case 1:
        if (gravity_walk(c, e) && dist(p->x, X(e)) >= LOST_DISTANCE)
            STATE(e)++;
        return;
    case 2:
        if (home_hidden(c, e, 12, 19)) {
            go_home(e);
            prev_seq(e);
        }
        return;
    }
}

static void periodic(Ctx *c, uint8_t *e)                  /* 0x60F2: type 9 */
{
    Player *p = c->p;
    int col, row;
    switch (STATE(e)) {
    case 0:
        if ((unsigned)dist(p->x, X(e)) <= uw(e, 0x17) && dist(p->y, Y(e)) <= 60) {
            STATE(e)++;
            next_seq(e);
            aim(c, e);
        }
        return;
    case 1:
        c->en->clock++;
        if (!(c->en->clock & 4) && !(c->frame & 0x1FF))
            next_seq(e);             /* now and then, the other action */
        if (!(c->frame & 0x7F)) {
            STATE(e) = 3;
            next_seq(e);
            goto action;             /* the original runs the state-3 code in the same frame */
        }
        if (floor_at(c, e, &col, &row) == 0)
            turn_home(e);
        snap_y(e);
        if (wall_at(c, ahead(e, col), row - 1))
            sw(e, EN_SPEED, -SPEED(e));
        walk(e);
        if (dist(p->x, X(e)) >= 1280)
            STATE(e) = 2;
        return;
    case 2:
        if (home_hidden(c, e, 12, 25)) {
            go_home(e);
            prev_seq(e);
        }
        return;
    case 3:
    action:
        if (!ONSCREEN(e)) {
            Obj scratch = {.sprite = OBJ_NONE};
            next_seq(e);
            sw(e, EN_ANIM, (int)uw(e, EN_ANIM) - 2);
            animate(e, &scratch);
        } else if (!EVENT(e)) {
            return;
        }
        aim(c, e);
        STATE(e) = 1;
        return;
    }
}

static void alerter(Ctx *c, uint8_t *e)                   /* 0x5FCE: type 10 */
{
    Player *p = c->p;
    int col, row, d;
    switch (STATE(e)) {
    case 0:
        if (p->sneak || (unsigned)dist(p->x, X(e)) > uw(e, 0x17) || dist(p->y, Y(e)) > 26)
            return;
        STATE(e)++;
        next_seq(e);
        aim(c, e);
        return;
    case 1:
        if (p->sneak)
            return;
        d = dist(p->x, X(e));
        if (uw(e, 0x17) < (unsigned)d) {
            prev_seq(e);            /* lost sight of Titus */
            STATE(e)--;
            return;
        }
        if (uw(e, 0x17) < (unsigned)(d + 50) || dist(p->y, Y(e)) > 60)
            return;
        STATE(e)++;
        next_seq(e);
        return;
    case 2:
        if (floor_at(c, e, &col, &row) == 0)
            turn_home(e);
        snap_y(e);
        if (wall_at(c, ahead(e, col), row - 1))
            turn_home(e);
        walk(e);
        if (dist(p->x, X(e)) >= LOST_DISTANCE)
            STATE(e)++;
        return;
    case 3:
        if (home_hidden(c, e, 13, 21)) {
            go_home(e);
            prev_seq(e);
            prev_seq(e);
        }
        return;
    }
}

static void attacker(Ctx *c, uint8_t *e)                  /* 0x5ECA: type 11 */
{
    Player *p = c->p;
    int col, row, d;
    switch (STATE(e)) {
    case 0:
        if ((unsigned)dist(p->x, X(e)) <= uw(e, 0x17) && dist(p->y, Y(e)) <= 26) {
            STATE(e)++;
            next_seq(e);
            aim(c, e);
        }
        return;
    case 1:
        if (floor_at(c, e, &col, &row) == 0)
            turn_home(e);
        snap_y(e);
        if (wall_at(c, col, row - 1))
            sw(e, EN_SPEED, -SPEED(e));
        walk(e);
        d = dist(p->x, X(e));
        if (d >= LOST_DISTANCE) {
            STATE(e)++;
            return;
        }
        if (e[0x14])
            e[0x14]--;
        if (e[0x14] != 0 || d > 64 || dist(p->y, Y(e)) > 20)
            return;
        aim(c, e);
        STATE(e) = 3;
        next_seq(e);
        e[0x14] = 20;
        return;
    case 2:
        if (home_hidden(c, e, 13, 21)) {
            go_home(e);
            prev_seq(e);
        }
        return;
    case 3:
        if (!EVENT(e))
            return;
        Obj *s = free_shot(c->en);
        if (s) {
            sw(e, EN_ANIM, (int)uw(e, EN_ANIM) + 4);
            spawn_shot(c, e, s);
        }
        prev_seq(e);
        STATE(e) = 1;
        return;
    }
}

static void bouncer(uint8_t *e)                           /* 0x5E60: type 12 */
{
    switch (STATE(e)) {
    case 0:
        next_seq(e);
        sw(e, 0x11, w(e, 0xF));
        sw(e, 0x14, Y(e));
        STATE(e) = 1;
        return;
    case 1:
        sw(e, EN_Y, Y(e) - w(e, 0x11));
        sw(e, 0x11, w(e, 0x11) - 1);
        if (w(e, 0x11) == 0)
            STATE(e) = 2;
        return;
    case 2:
        sw(e, EN_Y, Y(e) + w(e, 0x11));
        sw(e, 0x11, w(e, 0x11) + 1);
        if (Y(e) < w(e, 0x14))
            return;
        sw(e, EN_Y, w(e, 0x14));
        e[0x11] = e[0x13];
        STATE(e) = 3;
        prev_seq(e);
        return;
    case 3:
        if (--e[0x11] == 0)
            STATE(e) = 0;
        return;
    }
}

static void jumper(Ctx *c, uint8_t *e)                    /* 0x5DBB: type 13 */
{
    switch (STATE(e)) {
    case 0: {
        int s = abs(SPEED(e)), dx = c->p->x - X(e);
        sw(e, EN_SPEED, dx >= 0 ? -s : s);
        if (uw(e, 0x17) < (unsigned)abs(dx) || dist(c->p->y, Y(e)) > 40)
            return;
        next_seq(e);
        STATE(e)++;
        sw(e, 0x15, 10);
        return;
    }
    case 1:
        sw(e, EN_X, X(e) - SPEED(e));
        sw(e, EN_Y, Y(e) - w(e, 0x15));
        sw(e, 0x15, w(e, 0x15) - 1);
        if (w(e, 0x15) == 0) {
            next_seq(e);
            STATE(e)++;
        }
        return;
    case 2:
        sw(e, EN_X, X(e) - SPEED(e));
        sw(e, EN_Y, Y(e) + w(e, 0x15));
        sw(e, 0x15, w(e, 0x15) + 1);
        if (uw(e, 0x15) > 10) {
            STATE(e)++;
            next_seq(e);
            e[0x15] = e[0x14];
        }
        return;
    case 3:
        if (--e[0x15] == 0) {
            prev_seq(e);
            prev_seq(e);
            prev_seq(e);
            STATE(e) = 0;
        }
        return;
    }
}

static void dropper(Ctx *c, uint8_t *e)                   /* 0x5D4E: type 17 */
{
    unsigned count = uw(e, 0x13) + 1;
    if (uw(e, 0x11) > count) {
        sw(e, 0x13, (int)count);
        return;
    }
    if (uw(e, 0xF) < (unsigned)dist(X(e), c->p->x)) {
        sw(e, 0x13, 0);
        return;
    }
    if (w(e, 0x15) < c->p->y - Y(e))
        return;
    Obj *b = NULL;
    for (int i = 0; i < MAX_OBJECTS && !b; i++)
        if (c->o->obj[i].sprite == OBJ_NONE)
            b = &c->o->obj[i];
    if (b) {
        next_seq(e);
        /* The original keeps only the sprite number (bit 13, "live", is dropped), and the
         * object update removes such falling objects at once. Kept as is. */
        b->sprite = (uint16_t)((enemy_word(uw(e, EN_ANIM)) & 0x1FFF) | 0x8000);
        b->x = b->prev_x = X(e);
        b->y = b->prev_y = Y(e);
        b->mode = 3;
        b->vy = 0;
        c->o->gate = 4;
        prev_seq(e);
    }
    sw(e, 0x13, 0);
}

static void guard(Ctx *c, uint8_t *e)                     /* 0x5CB8: type 18 */
{
    Player *p = c->p;
    int cx = w(e, 0x14), cy = w(e, 0x16), hw = w(e, 0xF), hh = w(e, 0x11);
    bool inside = p->x >= cx - hw && p->x <= cx + hw && p->y >= cy - hh && p->y <= cy + hh;
    int tx = inside ? p->x : cx, ty = inside ? p->y : cy;
    int s = abs(SPEED(e));
    if (tx != X(e)) {
        s = tx > X(e) ? -s : s;
        sw(e, EN_SPEED, s);
        sw(e, EN_X, X(e) - s);
    }
    int vy = (int8_t)e[0x13];
    if (ty != Y(e))
        sw(e, EN_Y, Y(e) - (ty > Y(e) ? -vy : vy));
}

static void run_handler(Ctx *c, uint8_t *e)
{
    int type = uw(e, EN_TYPE) & 0x1FFF;
    bool immortal = type == 12 || type == 14 || type == 15 || type == 17;
    if (immortal) {
        e[7] &= 0x3F;
    } else if (KILLED(e)) {
        dying(c, e);
        return;
    }
    switch (type) {
    case 0: case 1: patrol(e); break;
    case 2: shooter(c, e); break;
    case 3: case 4: leaper(c, e); break;
    case 5: case 6: swooper(c, e); break;
    case 7: striker(c, e); break;
    case 8: case 14: wanderer(c, e); break;
    case 9: periodic(c, e); break;
    case 10: alerter(c, e); break;
    case 11: attacker(c, e); break;
    case 12: bouncer(e); break;
    case 13: jumper(c, e); break;
    case 15: case 16: break;          /* static */
    case 17: dropper(c, e); break;
    case 18: guard(c, e); break;
    default: break;
    }
}

/* --- frame -------------------------------------------------------------------------------- */

static Ctx make_ctx(Enemies *en, const Level *lv, Player *p, Objects *o, unsigned frame, PlayerEvents *ev)
{
    Ctx c = {en, lv, p, o, frame, ev, 0, 0, 0, 0};
    c.camcol = en->cam_x >> 4;
    c.camrow = en->cam_y >> 4;
    c.xcols = (en->view_w + 15) / 16 - ORIG_VIEW_W / 16;
    c.xrows = (en->view_h + 15) / 16 - ORIG_VIEW_H / 16;
    return c;
}

void enemies_reset(Enemies *en, const Level *lv)                       /* 0x58CC */
{
    int cx = en->cam_x, cy = en->cam_y, vw = en->view_w, vh = en->view_h;
    memset(en, 0, sizeof *en);
    en->cam_x = cx;
    en->cam_y = cy;
    en->view_w = vw;
    en->view_h = vh;
    memcpy(en->r, lv->enemies, sizeof en->r);
    for (int i = 0; i < ENEMY_RECORDS; i++) {
        uint8_t *e = en->r[i];
        en->prev_x[i] = X(e);
        en->prev_y[i] = Y(e);
        en->disp[i] = (Obj){.sprite = OBJ_NONE, .rider = RIDER_NONE};
        if (uw(e, EN_KEY) == 0xFFFF)
            continue;
        int key = uw(e, EN_KEY) & 0x1FFF;
        sw(e, EN_ANIM, (int)enemy_anim_start(key));
        en->boss_count += enemy_is_boss(key);
    }
    for (int i = 0; i < ENEMY_SHOTS; i++)
        en->shots[i] = (Obj){.sprite = OBJ_NONE, .rider = RIDER_NONE};
    en->boss_hp = lv->boss_hp;
}

void enemies_update(Enemies *en, const Level *lv, Player *p, Objects *o, unsigned frame, PlayerEvents *ev)
{
    Ctx c = make_ctx(en, lv, p, o, frame, ev);
    if (en->boss_cooldown > 0)
        en->boss_cooldown--;
    for (int i = 0; i < ENEMY_RECORDS; i++) {                          /* 0x5B45 */
        en->prev_x[i] = X(en->r[i]);
        en->prev_y[i] = Y(en->r[i]);
        if (uw(en->r[i], EN_KEY) != 0xFFFF)
            run_handler(&c, en->r[i]);
    }

    for (int i = 0; i < ENEMY_SHOTS; i++) {                            /* 0x5AC8 */
        Obj *s = &en->shots[i];
        s->prev_x = s->x;
        s->prev_y = s->y;
        if (s->sprite == OBJ_NONE)
            continue;
        bool skip_vertical = s->vx == 0;
        if (s->vx != 0) {
            s->x += sar(s->vx, 4);
            int col = (int)((unsigned)s->x >> 4) - c.camcol;
            if (col < 0 || col > 20 + c.xcols) {
                s->sprite = OBJ_NONE;
                continue;
            }
            skip_vertical = col == 20 + c.xcols;
        }
        if (!skip_vertical) {
            s->y += sar(s->vy, 4);
            if ((int)((unsigned)s->y >> 4) - c.camrow < 0) {
                s->sprite = OBJ_NONE;
                continue;
            }
        }
        if (!p->grace && sprite_boxes_touch(s->x, s->y, s->sprite, p->x, p->y, p->sprite)) {
            player_hurt(p, o, s->x - s->vx, SHOT_KNOCK, ev);
            s->sprite = OBJ_NONE;
        }
    }
}

/* 0x5C04: Titus touches enemy `e` (display record `d`). */
static void contact(Ctx *c, uint8_t *e, const Obj *d)
{
    Player *p = c->p;
    if (p->grace || !sprite_boxes_touch(p->x, p->y, p->sprite, d->x, d->y, d->sprite))
        return;                      /* (immune: the enemy doesn't turn round either) */
    if (e[EN_TYPE] != 11 && (uw(e, EN_KEY) & 0x1FFF) != 0xB2)
        sw(e, EN_SPEED, -SPEED(e));  /* it turns round */
    if ((unsigned)((d->sprite & 0x1FFF) - 0x9A) <= 2)
        p->knock_add = 1;
    int knock = w(e, EN_KNOCK);
    if (knock)
        player_hurt(p, c->o, X(e), knock, c->ev);
}

/* 0x5937: which enemies are on screen, their animation, Titus touching them, and thrown
 * things hitting them. */
void enemies_display(Enemies *en, Player *p, Objects *o, PlayerEvents *ev)
{
    Ctx c = make_ctx(en, NULL, p, o, 0, ev);
    for (int i = 0; i < ENEMY_RECORDS; i++)
        en->disp[i].sprite = OBJ_NONE;
    en->ndisp = 0;
    for (int i = 0; i < ENEMY_RECORDS; i++) {
        uint8_t *e = en->r[i];
        if (uw(e, EN_KEY) == 0xFFFF)
            continue;
        e[5] &= ~0x20;
        int dx = X(e) - en->cam_x, dy = Y(e) - en->cam_y;
        if (dx < -32 || dx > en->view_w + 32 || dy < 0 || dy > en->view_h + 32) {
            if (KILLED(e))
                sw(e, EN_KEY, 0xFFFF);   /* dead and out of sight: gone */
            continue;
        }
        e[5] |= 0x20;
        Obj *d = &en->disp[en->ndisp++];
        *d = (Obj){.rider = i, .x = X(e), .y = Y(e), .prev_x = en->prev_x[i], .prev_y = en->prev_y[i]};
        animate(e, d);
        /* The original lights a boss up (sprite bit 14) on the hit's frame only; here it
         * blinks for the whole 10 frames it can't be hit again. */
        if (en->boss_cooldown & 2 && d->sprite != OBJ_NONE && enemy_is_boss((d->sprite & 0x1FFF) - KEY_SPRITE))
            d->sprite |= 0x4000;
        if (KILLED(e))
            continue;
        if (p->knockback == 0) {
            if (uw(e, EN_KEY) == 0x05AA)
                continue;            /* hidden */
            if (enemy_type_contact(uw(e, EN_TYPE) & 0x1FFF))
                contact(&c, e, d);
        }

        Obj *hit = NULL;
        if (o->gate) {
            for (int k = 0; k < MAX_OBJECTS && !hit; k++) {
                Obj *b = &o->obj[k];
                if (b->sprite == OBJ_NONE || (b->mode & 1) || (b->vx == 0 && (b->vy == 0 || b->fall < 10)))
                    continue;
                if (!(obj_flags(b->sprite) & OBJF_HARMLESS) && sprite_boxes_touch(b->x, b->y, b->sprite, d->x, d->y, d->sprite))
                    hit = b;
            }
        }
        if (!hit && p->throwing && !p->carry && o->carry.sprite != OBJ_NONE &&
            sprite_boxes_touch(o->carry.x, o->carry.y, o->carry.sprite, d->x, d->y, d->sprite)) {
            en->boss_cooldown = 0;
            o->carry.sprite = SPR_STAR;  /* 0x5AB0: impact star */
            o->carry.vx = o->carry.vy = 0;
            o->star = 5;
            hit = &o->carry;
        }
        if (!hit)
            continue;
        if ((hit->sprite & 0xFF) != 0x49)
            hit->vx = -hit->vx;
        if (hit != &o->carry && (hit->sprite & 0x1FFF) >= OBJECT_SPRITES) {
            /* A thrown enemy is knocked out too: it falls through the floors and off the map
             * (the original's +B bit 0 with the live bit), hitting nothing more. */
            hit->mode |= 1;
            hit->sprite |= OBJ_LIVE;
        }
        events_sfx(ev, SFX_ENEMY_HIT);
        p->throwing = false;
        if (enemy_is_boss((d->sprite & 0x1FFF) - KEY_SPRITE)) {
            if (en->boss_cooldown)
                continue;
            en->boss_cooldown = 10;
            d->sprite |= 0x4000;
            if (--en->boss_hp != 0)
                continue;
            en->boss_count--;
        }
        e[7] |= 0x80;
    }
}
