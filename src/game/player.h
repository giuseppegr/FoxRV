/* Titus: a port of the player update (image 0x434B) and its tile collision (0x4684).
 *
 * Units follow the original: positions in whole pixels, (x, y) = the middle of the feet line;
 * speeds in 1/16 px per game frame, y growing downwards. Field comments give the original's
 * data-segment address.
 */
#ifndef FOXRV_PLAYER_H
#define FOXRV_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "game/elevators.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/level.h"
#include "game/objects.h"

#define SPRITE_MIRROR 0x8000    /* sprite word bit: drawn mirrored (facing left) */
#define SPRITE_NUMBER 0x1FFF

/* Action states (DS:5287); +16 while carrying. */
enum {
    PS_IDLE, PS_WALK, PS_JUMP, PS_CRAWL, PS_SKID, PS_CROUCH, PS_LADDER, PS_PICKUP, PS_THROW,
    PS_SNEAK_IDLE, PS_SNEAK_WALK, PS_STUNNED, PS_HURT, PS_HURT2,
};

typedef struct {
    bool left, right, up, down;
    bool jump;                  /* original: up (Up and the jump buttons) */
    bool fire;                  /* pick up / throw */
} PlayerInput;

/* Physics constants; defaults are the original's ([player] in engine.ini). */
typedef struct {
    int accel;                  /* 16: added per frame while a direction is held */
    int friction;               /* 12 */
    int max_walk;               /* 64 (4 px/frame); also in the air and on ladders */
    int max_sneak;              /* 48 */
    int max_crawl;              /* 32 */
    int jump_step;              /* 32: subtracted from vy per jump frame */
    int jump_min;               /* 48: minimum upward speed of a jump frame */
    int jump_frames;            /* 3 */
    int jump_cooldown;          /* 6: frames after landing before the next jump */
    int gravity;                /* 16 */
    int max_fall;               /* 192 */
    int ladder_speed;           /* 64 */
    int drop_crouch_frames;     /* 15: crouching this long drops through floor 5 */
} PlayerTuning;

typedef struct Player {
    int x, y;                   /* 527C / 527E */
    int prev_x, prev_y;         /* position before this frame, for render interpolation */
    uint16_t sprite;            /* 5280: number | SPRITE_MIRROR */
    int vx, vy;                 /* 5282 / 528A */
    int shift;                  /* 5284: slipperiness, divides acceleration and friction */
    uint8_t hdir;               /* 5285: last horizontal direction (01 / FF) */
    uint8_t facing;             /* 5286: 00 right, FF left */
    int state;                  /* 5287 */
    int anim;                   /* 5288: position in the animation words */
    int carry;                  /* 6509: 0, or 16 while carrying */
    bool throwing;              /* 650A */
    bool fire_ready;            /* 650B: fire released since the last pick-up */
    bool fire_used;             /* 650C: this fire press already did something */
    bool on_ladder;             /* 650D */
    bool forced_crouch;         /* 650E: a ceiling is too low to stand */
    int cooldown;               /* 650F: jump lock; 6 while airborne */
    int drop_through;           /* 6510 */
    int sneak;                  /* 6512 */
    int stun;                   /* 6513 */
    int knockback;              /* 6514 */
    int knock_add;              /* 6501 */
    bool landed;                /* 651C */
    int jump_frames;            /* 651D */
    int same_frames;            /* 651E: frames with the same state and direction */
    int probe;                  /* 6526: floor probe result: 0, 1 no floor, 2 landed */
    bool hinput, vinput;        /* 6507 / 6508 */
    int respawn_x, respawn_y;   /* 64E4 / 64E6 */
    int grace;                  /* not in the original: frames left immune to hits (low difficulty) */
} Player;

void player_tuning_defaults(PlayerTuning *t);

/* New level: everything cleared, at the start point, which is also the respawn point. */
void player_spawn(Player *p, const Level *lv);
/* After a death: cleared again, at the last checkpoint. */
void player_respawn(Player *p);

void player_update(Player *p, Level *lv, Objects *o, Enemies *en, const Elevators *el, const PlayerInput *in,
                   const PlayerTuning *t, PlayerEvents *ev);

/* Image 0x5B6F: hurt by an enemy or a projectile coming from `from_x`: a hit (ev->hurts, 2
 * energy in the original), knocked back by `knock` (1/16 px per frame) and up, drops what it
 * carries. Nothing happens while `grace` runs. */
void player_hurt(Player *p, Objects *o, int from_x, int knock, PlayerEvents *ev);

/* Puts down what Titus carries, where it is (image 0x4CE9); used on death too. */
void player_drop(Player *p, Objects *o);

/* Collision box height of the current sprite (DS:6BF8). */
int player_height(const Player *p);

#endif
