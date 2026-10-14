/* Comments name the original routines by image offset. Low-byte operations on sprite words
 * (e.g. turning the carpet back into 0x31) keep the flag bits, as in the original. */
#include "game/objects.h"

#include <stdlib.h>
#include <string.h>

#include "game/player.h"

#define TILE 16
#define MAP_X_MIN 8
#define MAP_X_END 0xFF8
#define EDGE_PUSH 32
#define CARPET_TIME 200
#define CARPET_HOVER 8          /* the flying carpet holds this far above Titus's feet */
#define STUN_FRAMES 24
#define HIT_FALL_FRAMES 10      /* an object must fall this long to hurt */
#define OFF_MAP_MARGIN 64

static int sar(int v, int n) { return v >= 0 ? v >> n : -((-v + (1 << n) - 1) >> n); }

static int number(uint16_t sprite) { return sprite & 0x1FFF; }

static const SpriteInfo *info(uint16_t sprite) { return sprite_info(number(sprite)); }

int obj_flags(uint16_t sprite)
{
    const SpriteInfo *s = info(sprite);
    return s ? s->obj_flags : 0;
}

bool obj_bouncy(uint16_t sprite)
{
    return number(sprite) < OBJECT_SPRITES && (obj_flags(sprite) & OBJF_BOUNCY);
}

static int box_w(uint16_t sprite) { const SpriteInfo *s = info(sprite); return s ? s->box_w : 16; }
static int box_h(uint16_t sprite) { const SpriteInfo *s = info(sprite); return s ? s->box_h : 16; }
static int image_w(uint16_t sprite) { const SpriteInfo *s = info(sprite); return s ? s->w : 16; }

static void destroy(Obj *b) { b->sprite = OBJ_NONE; }

/* --- setup -------------------------------------------------------------------------------- */

void objects_reset(Objects *o, const Level *lv)
{
    bool idol_saved = o->idol_saved;
    int ix = o->idol_x, iy = o->idol_y;
    int vx = o->view_x, vy = o->view_y, vw = o->view_w, vh = o->view_h;
    memset(o, 0, sizeof *o);
    for (int i = 0; i < MAX_OBJECTS; i++)
        o->obj[i] = (Obj){.sprite = OBJ_NONE, .rider = RIDER_NONE};
    o->carry = (Obj){.sprite = OBJ_NONE, .rider = RIDER_NONE};
    for (int i = 0; i < lv->nobjects; i++) {        /* 0x410C */
        const LevelObject *src = &lv->objects[i];
        Obj *b = &o->obj[src->slot];
        b->sprite = src->sprite;
        b->x = b->prev_x = src->x;
        b->y = b->prev_y = src->y;
    }
    o->gate = 4;
    o->last_drop = -1;
    o->idol_saved = idol_saved;
    o->idol_x = ix;
    o->idol_y = iy;
    o->view_x = vx;
    o->view_y = vy;
    o->view_w = vw;
    o->view_h = vh;
    if (idol_saved) {                                /* 0x38BD */
        for (int i = 0; i < MAX_OBJECTS; i++) {
            int n = number(o->obj[i].sprite);
            if (o->obj[i].sprite != OBJ_NONE && (n == SPR_IDOL || n == SPR_IDOL + 1)) {
                o->obj[i].x = o->obj[i].prev_x = ix;
                o->obj[i].y = o->obj[i].prev_y = iy;
                break;
            }
        }
    }
}

void objects_begin_frame(Objects *o)
{
    for (int i = 0; i < MAX_OBJECTS; i++) {
        o->obj[i].prev_x = o->obj[i].x;
        o->obj[i].prev_y = o->obj[i].y;
    }
    o->carry.prev_x = o->carry.x;
    o->carry.prev_y = o->carry.y;
}

int objects_insert(Objects *o, const Obj *src)
{
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (o->obj[i].sprite == OBJ_NONE) {
            o->obj[i] = *src;
            return i;
        }
    }
    return -1;
}

/* --- platforms ---------------------------------------------------------------------------- */

/* 0x5353: does box (left, y, w) land on object `t`? */
static bool lands_on(const Obj *t, int left, int y, int w, int *top)
{
    if (abs(t->x - left) > 64 || abs(t->y - y) > 70)
        return false;
    int tw = box_w(t->sprite), th = box_h(t->sprite);
    int tleft = t->x - tw / 2;
    if (tleft <= left) {
        if (tleft + tw <= left)
            return false;
    } else if (left + w <= tleft) {
        return false;
    }
    if (t->y - th / 8 < y || t->y - th > y)
        return false;
    *top = t->y - th;
    return true;
}

int objects_below(const Objects *o, int x, int y, int img_w, int bw, int self, int *top)    /* 0x5245 */
{
    int left = x - img_w / 2;
    for (int i = 0; i < MAX_OBJECTS; i++) {
        const Obj *t = &o->obj[i];
        if (i == self || t->sprite == OBJ_NONE || !(obj_flags(t->sprite) & OBJF_PLATFORM))
            continue;
        if (lands_on(t, left, y, bw, top))
            return i;
    }
    return -1;
}

/* --- update (0x3BC7) ---------------------------------------------------------------------- */

static void carpet(Objects *o, Obj *b, const Player *p, unsigned frame)   /* 0x3C09 */
{
    int lo = b->sprite & 0xFF;
    if ((frame & 3) == 0 && b->vy > 0 && o->carpet_state != 2 && (lo == SPR_CARPET || lo == SPR_CARPET + 1)) {
        if (lo == SPR_CARPET + 1)
            b->vx = 0;
        b->vx = sar(b->vx, 1);
        b->sprite++;                /* unrolls a step while falling */
        o->carpet_state = 1;
        o->carpet_timer = CARPET_TIME;
        return;
    }
    if (lo != SPR_CARPET + 2 && lo != SPR_CARPET + 3)
        return;
    o->gate = 4;
    if (o->carpet_state != 0) {     /* flying: hover at Titus's height */
        b->fall = 0;
        int target = p->y - CARPET_HOVER;
        b->vy = b->y == target ? 0 : b->y < target ? 16 : -16;
    }
    if (o->carpet_timer == 0) {
        b->sprite = SPR_CARPET;     /* used up: flags cleared too */
        b->vx = 0;
        o->carpet_state = 2;
    }
}

static void move_horizontally(Objects *o, Obj *b, const Level *lv)       /* 0x3C8E */
{
    int row = b->y >> 4;
    if ((b->y & 15) == 0)
        row--;
    int col = b->x >> 4;
    bool reflect = level_wall(lv, col, row) & 1;
    if (!reflect && ((b->x + sar(b->vx, 4)) >> 4) != col) {
        int next = col + (b->vx < 0 ? -1 : 1);
        reflect = next < 0 || next >= MAP_COLS || (level_wall(lv, next, row) & 1);
    }
    if (reflect) {                  /* walls bounce objects back */
        b->vx = -b->vx;
        b->x += sar(b->vx, 4);
    }
    o->gate = 4;
    int friction = abs(b->vy) < 16 ? 3 : 1;
    if (b->vx < 0)
        friction = -friction;
    b->x += sar(b->vx, 4);
    int v = b->vx - friction;
    b->vx = abs(v) < 16 ? 0 : v;
}

static void bounce_up(Objects *o, Obj *b)                                 /* 0x3F3F */
{
    o->gate = 4;
    b->fall = 0;
    int v = 48 - b->vy;
    b->vy = v < 0 ? v : 0;
}

static void land_on_floor(Objects *o, Obj *b)                             /* 0x3EED */
{
    b->y = (b->y & ~15) + TILE;
    if (obj_bouncy(b->sprite)) {
        bounce_up(o, b);
        return;
    }
    b->vy = 0;
    if ((unsigned)((b->sprite & 0xFF) - SPR_CARPET) <= 3) {
        b->sprite = (uint16_t)((b->sprite & 0xFF00) | SPR_CARPET);
        o->carpet_state = 0;
    }
    if ((b->sprite & OBJ_LIVE) && o->carry.sprite == OBJ_NONE) {
        o->dust = 0x20;             /* landing dust in the carry slot */
        o->carry.x = o->carry.prev_x = b->x;
        o->carry.y = o->carry.prev_y = b->y;
        o->carry.sprite = SPR_DUST;
    }
}

/* 0x3E7B: object `i` comes down on object `t` at height `top`. Returns true if it goes on
 * falling this frame (launched by a spring). */
static bool land_on_object(Objects *o, int i, int ti, int top)
{
    Obj *b = &o->obj[i], *t = &o->obj[ti];
    b->fall = 0;
    bool spring = obj_bouncy(t->sprite);
    if (!spring) {
        b->y = top;
        if (obj_bouncy(b->sprite) && (unsigned)b->vy >= 16) {
            bounce_up(o, b);
        } else {
            b->vy = 0;
        }
        return false;
    }
    t->riders |= 1;                 /* 0x3E9E: a spring */
    t->rider = i;
    int faller = sar(-b->vy, 1) + 16, launch = sar(-b->vy, 1) + 32;
    if (faller >= 0 || launch >= 0) {
        b->vy = 0;
        b->y = top;
        return false;
    }
    t->vy = launch;
    b->vy = faller;
    t->vx = sar(b->vx, 1);
    return true;
}

static void hit_player(Obj *b, Player *p, PlayerEvents *ev)               /* 0x3F97 */
{
    if (b->fall < HIT_FALL_FRAMES || p->vy >= 0xC0 || (b->mode & 1) || p->grace)
        return;                     /* (mode 1: a knocked-out enemy falling away) */
    if (abs(p->y - b->y) >= 32 || abs(p->x - b->x) >= 32)
        return;
    int a = b->x, d = p->x, w = box_w(b->sprite);
    if (a > d) {
        a = p->x;
        d = b->x;
        w = 24;
    }
    if (a + w < d)
        return;
    a = b->y;
    d = p->y;
    int h = box_h(b->sprite) - 1;
    if (a < d) {
        a = p->y;
        d = b->y;
        h = 32;
    }
    if (a - h >= d)
        return;
    events_sfx(ev, SFX_PLAYER_HIT);
    p->stun = STUN_FRAMES;
    if (b->mode & 2) {
        ev->energy_loss++;
        b->mode &= ~2;
    }
}

static void gravity(Objects *o, Obj *b, Player *p, PlayerEvents *ev)     /* 0x3F5A */
{
    int n = number(b->sprite);
    int limit = (n & 0xFF) < OBJECT_SPRITES && n < OBJECT_SPRITES ? info(b->sprite)->fall_limit : 15;
    int dy = sar(b->vy, 4);
    if (dy != 0)
        o->gate = 4;
    b->y += dy;
    if ((int8_t)dy < limit) {
        b->vy += 16;
        if (b->vy > 0)
            b->fall++;
    }
    hit_player(b, p, ev);
}

/* Floor flags that hold an object up (0x3E03): anything but ladders, plus solid walls. */
static bool holds(int floor, int wall)
{
    return floor != FLOOR_LADDER && (((wall & 1) | floor) & 7);
}

static void update_one(Objects *o, int i, const Level *lv, Player *p, unsigned frame, PlayerEvents *ev)
{
    Obj *b = &o->obj[i];
    if (b->x <= MAP_X_MIN) {
        b->vx = EDGE_PUSH;
        b->vy = 0;
    }
    if (b->x >= MAP_X_END) {
        b->vx = -EDGE_PUSH;
        b->vy = 0;
    }
    carpet(o, b, p, frame);
    if (b->vx != 0)
        move_horizontally(o, b, lv);

    int row = b->y >> 4, col = b->x >> 4;
    if (b->vy < 0) {                /* rising: stopped by any ceiling flag (0x3D3B) */
        int r = row - ((box_h(b->sprite) >> 4) + 1);
        bool hit = level_ceiling(lv, col, r) != 0;
        /* The original means to test one more row only when the object crosses into it, but
         * compares a pixel y with a row number, so it nearly always does. */
        if (!hit && sar(b->vy, 4) + b->y - row != 0)
            hit = level_ceiling(lv, col, r - 1) != 0;
        if (!hit) {
            gravity(o, b, p, ev);
            return;
        }
        b->vy = 0;
        if (!obj_bouncy(b->sprite))
            b->y &= ~15;
        return;
    }

    int n = number(b->sprite);
    if ((b->mode & 1) || ((obj_flags(b->sprite) & OBJF_FRAGILE) && b->vy >= 0xA0)) {
        if (!(b->sprite & OBJ_LIVE) || b->y >= lv->rows * TILE + OFF_MAP_MARGIN)
            destroy(b);
        else
            gravity(o, b, p, ev);   /* falls through floors */
        return;
    }

    /* The tile the feet are in (0x3DB8). */
    int fl = level_floor(lv, col, row), wl = level_wall(lv, col, row);
    if (b->y <= 6) {
        fl = 0;
    } else if (b->y >= lv->rows * TILE) {
        fl = 0;
        if (b->y >= lv->rows * TILE + OFF_MAP_MARGIN) {
            destroy(b);
            return;
        }
    }
    if (fl == FLOOR_DEADLY2 || (fl == FLOOR_DEADLY1 && n != SPR_BALL)) {
        destroy(b);                 /* fire destroys everything, water all but the ball */
        return;
    }
    if (fl == FLOOR_DEADLY1 || holds(fl, wl)) {
        b->vy = 0;                  /* resting on the floor (0x3D78) */
        if (!obj_bouncy(b->sprite))
            b->y &= ~15;
        return;
    }

    /* Falling: other objects first, then each row crossed this frame (0x3E13). */
    int top, t;
    int cross = ((sar(b->vy, 4) + b->y) >> 4) - row;
    if (cross != 0) {
        t = objects_below(o, b->x, b->y, image_w(b->sprite), box_w(b->sprite), i, &top);
        if (t >= 0) {
            if (land_on_object(o, i, t, top))
                gravity(o, b, p, ev);
            return;
        }
        for (int r = row + 1; cross > 0; r++, cross--) {
            int f2 = level_floor(lv, col, r), w2 = level_wall(lv, col, r);
            if (f2 == FLOOR_DEADLY2) {
                destroy(b);
                return;
            }
            if ((f2 == FLOOR_DEADLY1 && n == SPR_BALL) || holds(f2, w2)) {
                land_on_floor(o, b);
                return;
            }
        }
    }
    t = objects_below(o, b->x, b->y, image_w(b->sprite), box_w(b->sprite), i, &top);
    if (t >= 0 && !land_on_object(o, i, t, top))
        return;
    gravity(o, b, p, ev);
}

void objects_update(Objects *o, const Level *lv, Player *p, unsigned frame, PlayerEvents *ev)
{
    if (o->gate == 0)
        return;
    for (int i = 0; i < MAX_OBJECTS; i++)
        if (o->obj[i].sprite != OBJ_NONE)
            update_one(o, i, lv, p, frame, ev);
}

/* --- animation (0x3A20) ------------------------------------------------------------------- */

static void animate_one(Objects *o, Obj *b, Player *p, unsigned frame)
{
    if (b->sprite == OBJ_NONE || !(b->sprite & OBJ_LIVE))
        return;
    int lo = b->sprite & 0xFF;
    int set = -1;
    if ((frame & 7) == 0 && (lo == SPR_IDOL || lo == SPR_IDOL + 1)) {
        if (lo == SPR_IDOL || (frame & 0x3F) == 0)
            set = lo ^ 1;           /* the idol: blinks open often, closes every 64 frames */
    } else if ((frame & 7) == 0 && (lo == SPR_CARPET + 2 || lo == SPR_CARPET + 3)) {
        set = lo == SPR_CARPET + 2 ? SPR_CARPET + 3 : SPR_CARPET + 2;  /* carpet flaps */
    } else if ((frame & 1) == 0 && (lo == SPR_SPRING || lo == SPR_SPRING + 1)) {
        if (b->riders == 0) {
            set = SPR_SPRING + 1;
        } else {
            b->riders &= o->gate <= 1 ? 1 : 0;
            if (lo != SPR_SPRING) { /* compressing: the rider sinks */
                if (b->rider == RIDER_PLAYER)
                    p->y += 5;
                else if (b->rider >= 0 && b->rider < MAX_OBJECTS)
                    o->obj[b->rider].y += 5;
                o->gate = 3;
            }
            set = SPR_SPRING;
        }
    }
    if (set >= 0)
        b->sprite = (uint16_t)((b->sprite & 0xFF00) | set);
}

void objects_animate(Objects *o, Player *p, unsigned frame)
{
    animate_one(o, &o->carry, p, frame);
    for (int i = 0; i < MAX_OBJECTS; i++)
        animate_one(o, &o->obj[i], p, frame);
}
