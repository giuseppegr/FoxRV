/* Level objects: crates, balls, springs, the flying carpet... (DS:52A0, 40 records) and the
 * "carry slot" (DS:528E) that holds what Titus carries, a thrown projectile, or a short
 * effect (landing dust, impact star). A port of the object update (image 0x3BC7) and of the
 * object part of the animation routine (0x3A20). The player's side (pick up, throw, stand on)
 * is in player.c.
 */
#ifndef FOXRV_OBJECTS_H
#define FOXRV_OBJECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "game/events.h"
#include "game/level.h"

struct Player;

#define OBJ_NONE 0xFFFF         /* sprite word of an empty record */
#define OBJ_LIVE 0x2000         /* sprite word bit: animates, makes landing dust, may fall through */
#define OBJ_MIRROR 0x8000
#define RIDER_NONE (-1)
#define RIDER_PLAYER (-2)

/* Special object sprites. */
enum {
    SPR_CARPET = 0x31,          /* 0x31 rolled, 0x32 unrolling, 0x33/0x34 flying */
    SPR_SPRING = 0x36,          /* 0x36 pressed, 0x37 free */
    SPR_IDOL = 0x38,            /* 0x38/0x39; level 9 needs it carried to the end */
    SPR_STAR = 0x2D,            /* impact of a projectile */
    SPR_DUST = 0x2E,            /* landing dust 0x2E..0x30 */
    SPR_CRATE = 0x26,
    SPR_BALL = 0x27,            /* survives water (floor 8); bouncy and heavy */
    SPR_BLOCK = 0x53,           /* rolls when stood on, like the skateboard */
    SPR_SKATEBOARD = 0x5E,
};

typedef struct {
    int x, y;                   /* feet, map pixels */
    int prev_x, prev_y;         /* at the start of the frame, for render interpolation */
    uint16_t sprite;            /* +4: number | flags; OBJ_NONE = empty */
    int vx, vy;                 /* +6 / +E, 1/16 px per frame */
    int fall;                   /* +C: frames spent falling */
    uint8_t riders;             /* +8 (springs): bit 0 an object, bit 1 the player */
    int rider;                  /* +9 (springs): RIDER_* or an object index */
    uint8_t mode;               /* +B: bit 0 falls through floors, bit 1 hurts the player */
} Obj;

typedef struct {
    Obj obj[MAX_OBJECTS];
    Obj carry;                  /* DS:528E */
    int gate;                   /* 6511: objects update only while > 0 (4 after any movement) */
    int carpet_state;           /* 6503: 0 idle/ridden, 1 flying, 2 used up */
    int carpet_timer;           /* 6504 */
    int dust;                   /* 6505: landing dust timer (carry slot) */
    int star;                   /* 6515: impact star timer (carry slot) */
    int last_drop;              /* 64E9: slot of the last dropped object */
    bool idol_saved;            /* 64F8 / 64FA: where the idol returns after a death */
    int idol_x, idol_y;
    int view_x, view_y, view_w, view_h;     /* the visible area, for projectiles */
} Objects;

int obj_flags(uint16_t sprite);             /* OBJF_* of a sprite word */
bool obj_bouncy(uint16_t sprite);           /* image 0x5333 */

/* Level start and respawn (0x40E8): every object back where the level puts it. */
void objects_reset(Objects *o, const Level *lv);
/* Before a game frame: remember positions for interpolation. */
void objects_begin_frame(Objects *o);
void objects_update(Objects *o, const Level *lv, struct Player *p, unsigned frame, PlayerEvents *ev);
void objects_animate(Objects *o, struct Player *p, unsigned frame);

/* Image 0x5245: a platform object under a falling box (feet y, left edge x - image_w/2,
 * collision width box_w), skipping object `self` (-1 for none). Returns its index and the
 * y to stand at, or -1. */
int objects_below(const Objects *o, int x, int y, int image_w, int box_w, int self, int *top);

/* Puts `src` in the first free object slot (image 0x4CE9); returns the slot or -1. */
int objects_insert(Objects *o, const Obj *src);

#endif
