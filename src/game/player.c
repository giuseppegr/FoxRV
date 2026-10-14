/* Comments name the original routines by image offset. Arithmetic shifts on speeds are the
 * original's `sar` (sar() below), so negative speeds round the same way. */
#include "game/player.h"

#include <stdlib.h>
#include <string.h>

#define NO_INPUT 0x55           /* "no horizontal input" marker in the state selection */
#define SELECT_NONE 0xAA        /* nothing chosen yet */
#define AIRBORNE 6              /* cooldown value meaning "in the air" */
#define SPR_FALL 10
#define SPR_FALL_CARRY 21
#define SPR_FALL_STUN 15
#define SPR_LADDER_STILL 12
#define SPR_LADDER_STILL_CARRY 23
#define SPR_LADDER_ENTER 14
#define SPR_CRAWL_SLOW 6
#define SPR_YAWN 29
#define YAWN_START 140
#define YAWN_END 175
#define MAP_X_MIN 8
#define MAP_X_END 0xFF8

static int sar(int v, int n) { return v >= 0 ? v >> n : -((-v + (1 << n) - 1) >> n); }

void player_tuning_defaults(PlayerTuning *t)
{
    *t = (PlayerTuning){
        .accel = 16, .friction = 12, .max_walk = 64, .max_sneak = 48, .max_crawl = 32,
        .jump_step = 32, .jump_min = 48, .jump_frames = 3, .jump_cooldown = 6,
        .gravity = 16, .max_fall = 192, .ladder_speed = 64, .drop_crouch_frames = 15,
    };
}

static void clear(Player *p)
{
    int rx = p->respawn_x, ry = p->respawn_y;
    memset(p, 0, sizeof *p);
    p->respawn_x = rx;
    p->respawn_y = ry;
    p->anim = -1;
}

void player_spawn(Player *p, const Level *lv)
{
    p->respawn_x = lv->start_x;      /* image 0x3145 */
    p->respawn_y = lv->start_y;
    clear(p);
    p->x = p->prev_x = lv->start_x;  /* image 0x4136 */
    p->y = p->prev_y = lv->start_y;
}

void player_respawn(Player *p)
{
    clear(p);                        /* image 0x398D / 0x40E8 */
    p->x = p->prev_x = p->respawn_x; /* image 0x38AA */
    p->y = p->prev_y = p->respawn_y;
}

int player_height(const Player *p)
{
    const SpriteInfo *s = sprite_info(p->sprite & SPRITE_NUMBER);
    return s ? s->box_h : 32;
}

/* --- physics helpers ---------------------------------------------------------------------- */

static uint16_t mirror_bit(const Player *p) { return (p->hdir & 0x80) ? SPRITE_MIRROR : 0; }

/* 0x4E12 + 0x4E2A: switch to a state (restarting its animation) and show its next frame. */
static void animate(Player *p, int state)
{
    if (p->state != state) {
        p->state = state;
        p->anim = player_anim_start(state);
    }
    p->sprite = (uint16_t)player_anim_step(&p->anim) | mirror_bit(p);
}

static void accelerate(Player *p, const PlayerTuning *t, int limit)   /* 0x4D7D */
{
    int a = 0;
    if (p->hinput)
        a = (int16_t)(p->facing << 8 | p->hdir) * t->accel;
    a = sar(a, p->shift);
    int v = p->vx + a;
    if (v >= limit)
        v = limit;
    else if (v <= -limit)
        v = -limit;
    p->vx = v;
}

static void jump_impulse(Player *p, const PlayerTuning *t)           /* 0x4DB6 */
{
    int v = p->vy - t->jump_step;
    p->vy = v >= -t->jump_min ? -t->jump_min : v;
}

static void gravity(Player *p, const PlayerTuning *t, int limit)     /* 0x4DD0 */
{
    int v = p->vy + t->gravity;
    p->vy = v >= limit ? limit : v;
}

static void friction(Player *p, const PlayerTuning *t)               /* 0x4DE8 */
{
    int v = abs(p->vx) - (t->friction >> p->shift);
    if (v < 0)
        v = 0;
    p->vx = p->vx < 0 ? -v : v;
}

static void fall(Player *p, const PlayerTuning *t)                   /* 0x4E65 */
{
    p->cooldown = AIRBORNE;
    if (p->knockback)
        return;
    accelerate(p, t, t->max_walk);
    gravity(p, t, t->max_fall);
    int spr = p->stun ? SPR_FALL_STUN : p->carry ? SPR_FALL_CARRY : SPR_FALL;
    p->sprite = (uint16_t)spr | mirror_bit(p);
}

static void no_floor(Player *p) { p->probe |= 1; }                    /* 0x4EA2 */

static void land_keep_y(Player *p)                                    /* 0x4EF6 */
{
    if (p->cooldown > 0)
        p->cooldown--;
    p->jump_frames = 0;
    p->probe = 2;
}

static void land(Player *p)                                           /* 0x4ED8 */
{
    p->landed = true;
    p->shift = 0;
    if (p->vy < 0) {
        no_floor(p);
        return;
    }
    p->y &= ~15;
    p->vy = 0;
    land_keep_y(p);
}

static void hit_wall(Player *p)                                       /* 0x4EA8 */
{
    p->x -= sar(p->vx, 4);
    p->vx = 0;
    if (p->knockback && p->cooldown != AIRBORNE) {
        p->stun = 20;
        p->knockback = 0;
    }
}

/* --- carried objects ---------------------------------------------------------------------- */

/* 0x5322: where an object held in front of Titus goes. */
static int in_front_x(const Player *p) { return p->x + ((p->sprite & SPRITE_MIRROR) ? -10 : 12); }

static void hold_in_front(Player *p, Objects *o)
{
    o->carry.y = p->y - 4;
    o->carry.x = in_front_x(p);
}

static int sprite_box_h(uint16_t sprite)
{
    const SpriteInfo *s = sprite_info(sprite & SPRITE_NUMBER);
    return s ? s->box_h : 16;
}

static Obj *drop_object(Player *p, Objects *o)                       /* 0x4CE9 */
{
    if (o->carry.sprite == OBJ_NONE || !p->carry)
        return NULL;
    Obj b = {.rider = RIDER_NONE};
    b.sprite = o->carry.sprite;
    /* The original marks a carried enemy "falls through floors" (+B = 1), but its sprite lacks
     * the live bit such objects need, so it was deleted as soon as it stopped rising: thrown
     * up, put down or dropped, it just vanished. Here it stays a normal object instead: it
     * falls back, hits enemies on the way down, lands and can be picked up again. */
    b.mode = 0;
    b.x = b.prev_x = o->carry.x;
    b.y = b.prev_y = o->carry.y;
    o->carry.sprite = OBJ_NONE;
    p->carry = 0;
    p->fire_used = true;
    o->gate = 4;
    o->last_drop = objects_insert(o, &b);
    return o->last_drop >= 0 ? &o->obj[o->last_drop] : NULL;
}

void player_drop(Player *p, Objects *o) { drop_object(p, o); }

void player_hurt(Player *p, Objects *o, int from_x, int knock, PlayerEvents *ev)
{
    if (p->grace)
        return;
    events_sfx(ev, SFX_PLAYER_HURT);
    ev->hurts++;
    p->knockback = 24;
    p->stun = 0;
    p->state = PS_IDLE;
    p->vx = p->x > from_x ? knock : -knock;
    p->vy = -128;
    drop_object(p, o);
}

/* --- special tiles ------------------------------------------------------------------------ */

typedef struct {
    Player *p;
    Level *lv;
    Objects *o;
    Enemies *en;
    const Elevators *el;
    const PlayerInput *in;
    const PlayerTuning *t;
    PlayerEvents *ev;
} Ctx;

static bool take_bonus(Ctx *c, int col, int row)                      /* 0x4F10 */
{
    int tile = level_take_bonus(c->lv, col, row);
    if (tile < 0)
        return false;
    c->o->gate = 4;
    if (tile >= 0xFD) {
        c->ev->song = 6;
        c->ev->energy++;
    }
    return true;
}

static void deadly(Ctx *c) { c->ev->died = true; }                    /* 0x4FC0 */

static void code_tile(Ctx *c, int col, int row, bool level14)         /* 0x4FB1 / 0x4F9A */
{
    c->ev->song = 7;
    if (take_bonus(c, col, row)) {
        c->ev->code_reveal = true;
        c->ev->code_level14 = level14;
    }
}

static void checkpoint(Ctx *c, int col, int row)                      /* 0x4FC9 */
{
    c->ev->song = 5;
    if (take_bonus(c, col, row)) {
        c->p->respawn_x = c->p->x;
        c->p->respawn_y = c->p->y;
        c->ev->checkpoint = true;
        int n = c->o->carry.sprite & SPRITE_NUMBER;
        if (c->o->carry.sprite != OBJ_NONE && (n == SPR_IDOL || n == SPR_IDOL + 1)) {
            c->o->idol_saved = true; /* the idol comes back here after a death */
            c->o->idol_x = c->p->x;
            c->o->idol_y = c->p->y;
        }
    }
}

static void ladder_floor(Ctx *c, int col, int row)                    /* 0x502E */
{
    Player *p = c->p;
    if (p->stun) {
        no_floor(p);
        return;
    }
    int s = p->state & 15;
    if (s == PS_WALK || s == PS_CRAWL || s == PS_PICKUP || s == PS_THROW) {
        land(p);                     /* walking over a ladder top */
        return;
    }
    if (s == PS_CROUCH) {            /* crouching on it: climb down */
        no_floor(p);
        p->sprite = SPR_LADDER_ENTER;
        p->y += 8;
    }
    if (level_floor(c->lv, col, row - 1) == FLOOR_LADDER) {
        land_keep_y(p);              /* more ladder above: hang on it */
        p->on_ladder = true;
        return;
    }
    if (s == PS_IDLE || (s == PS_LADDER && c->in->up)) {
        land(p);                     /* top of the ladder */
        return;
    }
    land_keep_y(p);
    p->on_ladder = true;
}

static void floor_tile(Ctx *c, int col, int row)                      /* dispatch DS:727D */
{
    Player *p = c->p;
    switch (level_floor(c->lv, col, row)) {
    case FLOOR_NONE: no_floor(p); break;
    case FLOOR_SOLID: land(p); break;
    case FLOOR_SLIPPERY1: land(p); p->shift = 1; break;
    case FLOOR_SLIPPERY2: land(p); p->shift = 2; break;
    case FLOOR_SLIPPERY3: land(p); p->shift = 3; break;
    case FLOOR_DROP:
        p->shift = 0;
        if (p->drop_through)
            no_floor(p);
        else
            land(p);
        break;
    case FLOOR_LADDER: ladder_floor(c, col, row); break;
    case FLOOR_BONUS: take_bonus(c, col, row); break;
    case FLOOR_DEADLY1:
    case FLOOR_DEADLY2:
    case FLOOR_DEADLY3: deadly(c); break;
    case FLOOR_CODE: code_tile(c, col, row, false); break;
    case FLOOR_CHECKPOINT: checkpoint(c, col, row); break;
    case FLOOR_CODE14: code_tile(c, col, row, true); break;
    default: break;
    }
}

static void drop_out_of_wall(Ctx *c)
{
    Obj *b = drop_object(c->p, c->o);
    if (!b)
        return;
    int col = b->x >> 4, row = b->y >> 4;
    if (level_wall(c->lv, col, row) & 1)
        b->x += (level_wall(c->lv, col - 1, row) & 1) ? 16 : -16;
}

static void ceiling_tile(Ctx *c, int col, int row)                    /* dispatch DS:7299 */
{
    Player *p = c->p;
    switch (level_ceiling(c->lv, col, row)) {
    case CEIL_SOLID:                 /* 0x5088 */
        if (p->vy != 0) {
            p->vy = 0;
            p->y = (p->y & ~15) + 16;
            p->jump_frames = 0xFF;
        } else {
            int spr = p->sprite & 0xFF;
            if (spr != SPR_FALL && spr != SPR_FALL_CARRY && p->cooldown != AIRBORNE) {
                p->forced_crouch = true;
                if (p->carry)        /* 0x50C3: no room to carry it */
                    drop_out_of_wall(c);
            }
        }
        break;
    case CEIL_LADDER:                /* 0x5103 */
        if (p->vy < 0 && p->vx == 0) {
            p->jump_frames = 10;
            p->on_ladder = true;
        }
        break;
    case CEIL_CHECKPOINT: checkpoint(c, col, row); break;
    case CEIL_DEADLY: deadly(c); break;
    default: break;
    }
}

static void wall_tile(Ctx *c, int col, int row, bool full)            /* 0x4817 / 0x4831 */
{
    int flag = level_wall(c->lv, col, row);
    if (!full && flag != WALL_BONUS && flag != WALL_CODE)
        return;
    switch (flag) {
    case WALL_SOLID: hit_wall(c->p); break;
    case WALL_BONUS: take_bonus(c, col, row); break;
    case WALL_DEADLY: deadly(c); break;
    case WALL_CODE: code_tile(c, col, row, false); break;
    case WALL_CHECKPOINT: checkpoint(c, col, row); break;
    case WALL_CODE14: code_tile(c, col, row, true); break;
    default: break;
    }
}

/* --- collision ---------------------------------------------------------------------------- */

/* 0x4761: floor under one probe column, then the ceiling over it and the push-out. `row` is
 * the body row (the tile row just above the feet line). */
static void probe_column(Ctx *c, int col, int row)
{
    Player *p = c->p;
    p->landed = false;
    p->forced_crouch = false;
    p->on_ladder = false;
    if (p->y < 0) {
        fall(p, c->t);
        p->probe = 0xFF;
        return;
    }
    if ((p->state & 15) != PS_JUMP)
        floor_tile(c, col, row + 1);

    if (row < 1 || p->vy > 0)
        return;
    int body_wall = level_wall(c->lv, col, row);
    ceiling_tile(c, col, row - 1);
    if ((body_wall & 1) && p->y > 0) {
        int dx = p->vx > 0 ? -1 : 1; /* first try the side away from the movement */
        if (level_wall(c->lv, col + dx, row) == 0)
            p->x += 2 * dx;
        else if (level_wall(c->lv, col - dx, row) == 0)
            p->x -= 2 * dx;
    }
}

/* Do two spans overlap? The original's box test: from the leftmost (topmost) start, that
 * one's own extent must reach past the other's start. */
static bool spans_overlap(int a, int a_len, int b, int b_len)
{
    return a < b ? a + a_len > b : b + b_len > a;
}

/* 0x4857: standing on a visible elevator when no floor was found. Titus's box is sprite 0's
 * (from x - hot_x, its collision width) and 8 px tall from 6 px above the feet; the
 * elevator's is its sprite's collision box from its top-left corner. */
static bool stand_on_elevator(Ctx *c)
{
    Player *p = c->p;
    if (p->vy < 0 || p->drop_through)
        return false;
    const SpriteInfo *s0 = sprite_info(0);
    int left = p->x - (s0 ? s0->hot_x & 0xFF : 16), width = s0 ? s0->box_w : 28;
    for (int i = 0; i < ELEVATOR_RECORDS; i++) {
        const Elevator *e = &c->el->e[i];
        if (e->sprite == ELEVATOR_NONE || !e->visible || !(e->sprite & 0x2000))
            continue;
        if (abs(e->x - p->x) >= 64 || abs(e->y - p->y) >= 16)
            continue;
        const SpriteInfo *s = sprite_info(e->sprite & SPRITE_NUMBER);
        if (!s || !spans_overlap(left, width, e->x, s->box_w) || !spans_overlap(p->y - 6, 8, e->y, s->box_h))
            continue;
        if (p->same_frames == 14)
            p->same_frames = 16;
        p->y = e->y;
        p->vy = 0;                   /* 0x4EF0 */
        land_keep_y(p);
        switch (e->dir) {            /* ride along (it has already moved this frame) */
        case ELEV_RIGHT: p->x += e->speed; break;
        case ELEV_LEFT: p->x -= e->speed; break;
        case ELEV_DOWN: p->y += e->speed; break;
        default: break;
        }
        return true;
    }
    return false;
}

/* 0x511F: standing on (or bouncing off) a platform object when no floor was found. */
static void stand_on_object(Ctx *c)
{
    Player *p = c->p;
    Objects *o = c->o;
    if (p->vy < 0)
        return;
    const SpriteInfo *s0 = sprite_info(0);   /* the original uses sprite 0's box here */
    int top, i = objects_below(o, p->x, p->y, s0 ? s0->w : 32, s0 ? s0->box_w : 28, -1, &top);
    if (i < 0)
        return;
    Obj *b = &o->obj[i];
    p->y = top;
    int lo = b->sprite & 0xFF;
    uint16_t facing = p->sprite & SPRITE_MIRROR;
    if (lo == SPR_SPRING || lo == SPR_SPRING + 1) {
        b->riders |= 2;
        b->rider = RIDER_PLAYER;
    }
    if (lo == SPR_CARPET + 2 || lo == SPR_CARPET + 3) {
        b->vx = facing ? -0x60 : 0x60;       /* the carpet flies where Titus faces */
        b->sprite = (uint16_t)((b->sprite & ~OBJ_MIRROR) | facing);
        o->gate = 4;
        o->carpet_state = 0;
    } else if (p->same_frames > 10 && (p->state & 15) == PS_IDLE && p->vy == 0 &&
               (lo == SPR_BLOCK || lo == SPR_SKATEBOARD)) {
        b->vx = facing ? -0x30 : 0x30;       /* standing still on it sets it rolling */
        b->sprite = (uint16_t)((b->sprite & ~OBJ_MIRROR) | facing);
        o->gate = 4;
    }
    if (b->vx != 0)
        p->vx = b->vx + (b->vx > 0 ? 16 : 0);

    if (!p->drop_through && p->vy > 0x30 && obj_bouncy(b->sprite)) {     /* 0x51D3 */
        int a = p->vy + (c->in->jump ? 0x30 : -0x10);
        if (c->in->down || a < 0)
            a = 0;
        p->vy = -a;
        p->same_frames = 0;
        if (b->vy == 0) {
            events_sfx(c->ev, SFX_BOUNCE);
            b->vy = a;
            b->y -= sar(b->vy, 4);
            o->gate = 4;
        }
        return;                      /* still airborne: falls (gravity) this frame */
    }
    p->vy = b->vy;                   /* ride it */
    land_keep_y(p);
}

static void collide(Ctx *c)                                           /* 0x4684 */
{
    Player *p = c->p;
    int row = (p->y >> 4) - 1;
    int height = player_height(p);
    int wall_dx = p->vx > 0 ? 8 : p->vx < 0 ? -8 : 0;

    if (p->y > 0 && p->y > (c->lv->rows + 1) * 16)
        deadly(c);

    p->probe = 0;
    int col1 = (p->x - 4) >> 4;
    probe_column(c, col1, row);
    if (p->probe == 1) {
        int col2 = (p->x + 5) >> 4;
        if (col2 != col1)
            probe_column(c, col2, row);
    }
    if (p->probe == 1) {
        if (!p->drop_through && !p->stun) {
            if (stand_on_elevator(c))
                goto walls;          /* unlike objects, keeps the slipperiness */
            stand_on_object(c);
            if (p->probe != 1) {
                p->shift = 0;
                goto walls;
            }
        }
        fall(p, c->t);
    }

walls:
    if (p->y > 0) {
        int col = (p->x + wall_dx) >> 4;
        wall_tile(c, col, row, true);
        for (int r = row - 1; r >= 0 && (height -= 16) > 0; r--)
            wall_tile(c, col, r, false);
    }
}

/* --- state handlers (table DS:6EFB) ------------------------------------------------------- */

static void idle(Ctx *c, int state)                                   /* 0x496C */
{
    Player *p = c->p;
    p->state = state;
    friction(p, c->t);
    int s = state;
    bool mirrored = (p->sprite & SPRITE_MIRROR) != 0, moving_left = p->vx < 0;
    if (abs(p->vx) >= 16 && mirrored == moving_left)
        s = PS_SKID + p->carry;      /* still sliding the way it faces */
    p->anim = player_anim_start(s);
    p->sprite = (uint16_t)player_anim_peek(p->anim) | mirror_bit(p);
}

static void jump(Ctx *c, int state)                                   /* 0x4948 */
{
    Player *p = c->p;
    if (p->jump_frames >= c->t->jump_frames) {
        p->cooldown = AIRBORNE;
        return;
    }
    p->jump_frames++;
    jump_impulse(p, c->t);
    accelerate(p, c->t, c->t->max_walk);
    animate(p, state);
}

static void crouch(Ctx *c, int state)                                 /* 0x49B7 */
{
    Player *p = c->p;
    animate(p, state);
    friction(p, c->t);
    if (p->same_frames == c->t->drop_crouch_frames) {
        p->drop_through = 6;
        p->vy = 0;
    }
}

static void crawl(Ctx *c, int state)                                  /* 0x49D3 */
{
    Player *p = c->p;
    animate(p, state);
    accelerate(p, c->t, c->t->max_crawl);
    if (abs(p->vx) < c->t->max_crawl)
        p->sprite = SPR_CRAWL_SLOW | mirror_bit(p);
}

static void walk(Ctx *c, int state, int limit)                        /* 0x49FA / 0x4A07 */
{
    accelerate(c->p, c->t, limit);
    animate(c->p, state);
}

static void stunned(Ctx *c, int state)                                /* 0x4A14 */
{
    c->p->vx = 0;
    animate(c->p, state);
}

static void hurt(Ctx *c, int state)                                   /* 0x4A26 */
{
    gravity(c->p, c->t, c->t->max_fall);
    animate(c->p, state);
}

static void ladder(Ctx *c, int state)                                 /* 0x4A33 */
{
    Player *p = c->p;
    if (p->hinput)
        accelerate(p, c->t, c->t->max_walk);
    else
        friction(p, c->t);
    if (p->same_frames <= 1)
        p->sprite = p->carry ? SPR_LADDER_STILL_CARRY : SPR_LADDER_STILL;
    if (!p->vinput) {
        p->vy = 0;
        return;
    }
    animate(p, state);

    /* Centre on the ladder column, or step onto a neighbouring one. */
    p->x = (p->x & ~15) + 8;
    int col = p->x >> 4, row = p->y >> 4;
    if (level_floor(c->lv, col, row) != FLOOR_LADDER) {
        if (level_floor(c->lv, col - 1, row) == FLOOR_LADDER)
            p->x -= 16;
        else if (level_floor(c->lv, col + 1, row) == FLOOR_LADDER)
            p->x += 16;
    }
    p->vy = c->in->up ? -c->t->ladder_speed : c->t->ladder_speed;
}

static void take_into_hands(Ctx *c, Obj *b);

static void pick_up(Ctx *c, int state)                               /* 0x4B5B */
{
    Player *p = c->p;
    Objects *o = c->o;
    animate(p, state);
    friction(p, c->t);
    if (p->fire_used)
        return;
    p->fire_used = true;
    if (p->carry) {
        if (p->same_frames == 1) {   /* fire again while crouching: put it down */
            Obj *b = drop_object(p, o);
            if (b) {
                int col = b->x >> 4, row = b->y >> 4;
                if (level_floor(c->lv, col, row) & 7)
                    b->vx = (level_floor(c->lv, col + 1, row) & 7) ? 48 : -48;
            }
        } else {
            hold_in_front(p, o);
        }
        return;
    }
    for (int i = 0; i < MAX_OBJECTS; i++) {
        Obj *b = &o->obj[i];
        if (b->sprite == OBJ_NONE || (b->mode & 1) || abs(p->y - b->y) >= 20)
            continue;               /* (mode 1: a knocked-out enemy falling away) */
        int ahead = (p->sprite & SPRITE_MIRROR) ? p->x - b->x : b->x - p->x;
        if ((unsigned)ahead >= 20)
            continue;
        /* The original's horizontal box test compares Titus's x with itself, so it always
         * passes (image 0x4C1D); only the vertical one matters. */
        int a = b->y, d = p->y, h = sprite_box_h(b->sprite) - 1;
        if (a < d) {
            a = p->y;
            d = b->y;
            h = 10;
        }
        if (a - h >= d)
            continue;
        take_into_hands(c, b);
        return;
    }
    /* Then the enemies on screen (0x4BF9): only some kinds, and only from behind. */
    for (int i = 0; i < c->en->ndisp; i++) {
        Obj *d = &c->en->disp[i];
        if (d->sprite == OBJ_NONE || abs(p->y - d->y) >= 20)
            continue;
        int ahead = (p->sprite & SPRITE_MIRROR) ? p->x - d->x : d->x - p->x;
        int key = (d->sprite & SPRITE_NUMBER) - OBJECT_SPRITES;
        if ((unsigned)ahead >= 20 || (enemy_class(key) & 0x0F) == 0x0F)
            continue;
        int a = d->y, dd = p->y, h = sprite_box_h(d->sprite) - 1;
        if (a < dd) {
            a = p->y;
            dd = d->y;
            h = 10;
        }
        if (a - h >= dd)
            continue;
        if ((d->sprite & 0xFF) >= OBJECT_SPRITES) {
            int behind = p->x - d->x;
            if (d->sprite & OBJ_MIRROR)
                behind = -behind;
            if (behind < 0)
                continue;
            c->en->r[d->rider][EN_KEY] = 0xFF;    /* the enemy is gone until the next death */
            c->en->r[d->rider][EN_KEY + 1] = 0xFF;
            d->sprite = (uint16_t)(enemy_carry_key(enemy_class(key)) + OBJECT_SPRITES);
        }
        take_into_hands(c, d);
        return;
    }
}

static void take_into_hands(Ctx *c, Obj *b)
{
    Player *p = c->p;
    Objects *o = c->o;
    events_sfx(c->ev, SFX_PICK_UP);
    o->dust = 0;
    o->gate = 4;
    o->star = 0;
    o->carry = (Obj){.sprite = b->sprite, .rider = RIDER_NONE, .prev_x = b->prev_x, .prev_y = b->prev_y};
    b->sprite = OBJ_NONE;
    p->carry = 16;
    if ((o->carry.sprite & 0xFF) == SPR_CARPET)
        o->carpet_state = 0;
    hold_in_front(p, o);
}

static void throw_object(Ctx *c, int state)                          /* 0x4AD6 */
{
    Player *p = c->p;
    Objects *o = c->o;
    animate(p, state);
    friction(p, c->t);
    if (!p->carry)
        return;
    int vx = 0, vy = -0xA0;          /* with up held: straight up */
    if (!c->in->jump) {
        vy = 0;
        vx = (p->sprite & SPRITE_MIRROR) ? -0xE0 : 0xE0;
        o->carry.y = p->y - 16;
    }
    if (vy != 0 || ((o->carry.sprite & SPRITE_NUMBER) < OBJECT_SPRITES && (obj_flags(o->carry.sprite) & OBJF_HEAVY))) {
        Obj *b = drop_object(p, o);  /* heavy things fly as objects, slower */
        if (b) {
            b->vy = vy;
            b->vx = vx - sar(vx, 2);
        }
    } else {
        p->throwing = true;          /* light things fly straight, as a projectile */
        o->carry.vx = vx;
        o->carry.vy = vy;
        events_sfx(c->ev, SFX_THROW);
    }
    p->sprite = SPR_FALL | mirror_bit(p);
    p->carry = 0;
}

static void crouch_carrying(Ctx *c, int state)                       /* 0x4ACC */
{
    animate(c->p, state);
    friction(c->p, c->t);
}

static void run_state(Ctx *c, int state)
{
    switch (state) {
    case 0: case 9: case 16: idle(c, state); break;
    case 1: case 17: case 19: walk(c, state, c->t->max_walk); break;
    case 2: case 18: jump(c, state); break;
    case 3: crawl(c, state); break;
    case 5: crouch(c, state); break;
    case 21: crouch_carrying(c, state); break;
    case 6: case 22: ladder(c, state); break;
    case 7: case 23: pick_up(c, state); break;
    case 8: case 24: throw_object(c, state); break;
    case 10: walk(c, state, c->t->max_sneak); break;
    case 11: stunned(c, state); break;
    case 27: drop_object(c->p, c->o); stunned(c, state); break;   /* 0x4A21 */
    case 12: case 13: case 28: case 29: hurt(c, state); break;
    default: idle(c, 0); break;      /* unused table entries */
    }
}

/* 0x43BD: choose the action state from input and status. */
static int select_state(Ctx *c)
{
    Player *p = c->p;
    const PlayerInput *in = c->in;
    int al, ah = NO_INPUT;

    if (p->stun) {
        al = PS_STUNNED;
        goto chosen;
    }
    al = PS_HURT + p->knock_add;
    if (p->knockback)
        goto chosen;
    p->knock_add = 0;

    al = SELECT_NONE;
    if (in->right) {
        al = PS_WALK;
        ah = 0x01;
    }
    if (in->left) {
        al = PS_WALK;
        ah = 0xFF;
    }
    if (p->on_ladder) {
        al = PS_LADDER;
    } else {
        if (p->forced_crouch || (p->cooldown != AIRBORNE && in->down))
            al = ah == NO_INPUT ? PS_CROUCH : PS_CRAWL;
        if (!p->forced_crouch && in->jump && p->cooldown == 0) {
            al = PS_JUMP;
            if (p->state == PS_CROUCH)
                p->sneak = 100;
        }
    }
    if (al == SELECT_NONE)
        al = PS_IDLE;

    if (in->fire && !p->forced_crouch) {
        if (p->throwing)
            goto chosen;
        if (al == PS_CRAWL || al == PS_CROUCH) {
            p->fire_ready = false;
            al = PS_PICKUP;
        } else if (p->carry && p->fire_ready) {
            al = PS_THROW;
        }
    } else {
        p->fire_ready = true;
        p->fire_used = false;
    }

chosen:
    al += p->carry;
    if (ah == NO_INPUT)
        ah = p->facing;
    p->hinput = in->left || in->right;
    p->vinput = in->up || in->down;

    uint8_t dh = (ah & 0x80) ? 0xFF : 0x00, dl = (uint8_t)(dh | ah);
    if (p->hdir != dl || p->facing != dh) {
        p->hdir = dl;
        p->facing = dh;
        p->same_frames = 1;
    } else {
        if ((al == PS_IDLE || al == PS_WALK) && p->sneak)
            al += 9;
        if (p->state != al)
            p->same_frames = 1;
        else if (p->same_frames < 0xFFFF)
            p->same_frames++;
    }
    return al;
}

static int countdown(int v) { return v > 0 ? v - 1 : 0; }

/* 0x450D: the carry slot follows Titus, or flies as a projectile until it leaves the
 * visible area (the original: the screen plus 40 px sideways and 20 px vertically). */
static void carry_follow(Ctx *c)
{
    Player *p = c->p;
    Objects *o = c->o;
    Obj *k = &o->carry;
    if (p->throwing) {
        int x = k->x + sar(k->vx, 4);
        bool gone = (unsigned)x >= MAP_COLS * 16 || x < o->view_x - 40 || x > o->view_x + o->view_w + 40;
        if (!gone) {
            k->x = x;
            int y = k->y + sar(k->vy, 4);
            gone = (unsigned)y >= (unsigned)(c->lv->rows * 16) || y < o->view_y - 20 || y > o->view_y + o->view_h + 20;
            if (!gone)
                k->y = y;
        }
        if (gone) {
            k->sprite = OBJ_NONE;
            p->throwing = false;
        }
        return;
    }
    if (!p->carry)
        return;
    int n = p->sprite & SPRITE_NUMBER, s = p->state & 15;
    if (!p->on_ladder && (p->state == PS_CROUCH + 16 || p->state == PS_PICKUP + 16)) {
        hold_in_front(p, o);
    } else if (n == SPR_LADDER_ENTER || (s != PS_PICKUP && s != PS_THROW)) {
        int x = p->x + 2;            /* overhead */
        if (n == SPR_LADDER_STILL_CARRY || n == SPR_LADDER_STILL_CARRY + 1)
            x += (p->sprite & SPRITE_MIRROR) ? 8 : -10;
        k->x = x;
        k->y = p->y - sprite_box_h(p->sprite) + 1;
    }
}

/* 0x45EA / 0x4654: the impact star and the landing dust in the carry slot. */
static void carry_effects(Objects *o)
{
    if (o->star > 0 && --o->star == 0)
        o->carry.sprite = OBJ_NONE;
    if (o->dust > 0 && --o->dust > 0 && (o->dust & 3) == 0) {
        o->carry.sprite++;
        if ((o->carry.sprite & 0xFF) == SPR_DUST + 3) {
            o->carry.sprite = OBJ_NONE;
            o->dust = 0;
        }
    }
}

void player_update(Player *p, Level *lv, Objects *o, Enemies *en, const Elevators *el, const PlayerInput *in,
                   const PlayerTuning *t, PlayerEvents *ev)
{
    Ctx c = {p, lv, o, en, el, in, t, ev};
    p->prev_x = p->x;
    p->prev_y = p->y;

    run_state(&c, select_state(&c));

    int dx = sar(p->vx, 4);          /* 0x44EA */
    p->x += dx;
    if (p->x < MAP_X_MIN || p->x >= MAP_X_END)
        p->x -= dx;
    p->y += sar(p->vy, 4);
    collide(&c);
    carry_follow(&c);

    o->carpet_timer = countdown(o->carpet_timer);   /* 0x45FD */
    p->drop_through = countdown(p->drop_through);
    o->gate = countdown(o->gate);
    p->sneak = countdown(p->sneak);
    p->knockback = countdown(p->knockback);
    p->grace = countdown(p->grace);
    if (p->vy == 0)
        p->stun = countdown(p->stun);
    if (p->vx == 0 && p->vy == 0)
        p->knockback = 0;
    carry_effects(o);

    /* 0x39F5: a long idle shows the yawn sprite. */
    if (p->state == PS_IDLE && p->landed && p->same_frames >= YAWN_START) {
        int spr = SPR_YAWN;
        if (p->same_frames >= YAWN_END) {
            spr = 0;
            p->same_frames = 0;
        }
        p->sprite = (uint16_t)((p->sprite & 0xFF00) | spr);
    }
}
