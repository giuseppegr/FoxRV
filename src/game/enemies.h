/* Enemies: the 50 records of the level (DS:7699, 26 bytes each, kept byte for byte as in the
 * original so the 19 behaviour handlers port line by line), the on-screen display records
 * (DS:5570) and the enemy projectiles (DS:5600).
 *
 * Per game frame, in the original's order: enemies_update (handlers 0x5B45, projectiles
 * 0x5AC8), then enemies_display (visibility, animation, contact and hits, 0x5937).
 */
#ifndef FOXRV_ENEMIES_H
#define FOXRV_ENEMIES_H

#include <stdbool.h>
#include <stdint.h>

#include "game/events.h"
#include "game/level.h"
#include "game/objects.h"

struct Player;

#define ENEMY_SHOTS 4

/* Record offsets (the original's field layout). */
enum {
    EN_X = 0x0, EN_Y = 0x2,     /* feet, px */
    EN_KEY = 0x4,               /* animation key | 0x2000 on screen | 0x8000 mirrored; FFFF gone */
    EN_TYPE = 0x6,              /* behaviour (low 13 bits); byte 7: 0x20 event, 0x40 dying, 0x80 hit */
    EN_SPEED = 0x8,             /* px per frame, x -= speed; its sign is the facing */
    EN_ANIM = 0xA,              /* DS address of the current animation word */
    EN_KNOCK = 0xC,             /* knock-back given to Titus; 0 = harmless */
    EN_STATE = 0xE,             /* handler state; FF = corpse */
};

typedef struct {
    uint8_t r[ENEMY_RECORDS][ENEMY_RECORD_SIZE];
    int prev_x[ENEMY_RECORDS], prev_y[ENEMY_RECORDS];
    Obj disp[ENEMY_RECORDS];    /* on-screen enemies; .rider = record index */
    int ndisp;
    Obj shots[ENEMY_SHOTS];
    int boss_hp;                /* 64E8 */
    int boss_count;             /* 64FD: bosses alive */
    int boss_cooldown;          /* 651B */
    uint8_t clock;              /* 6500 (type 9) */
    int cam_x, cam_y, view_w, view_h;   /* set by the game each frame */
} Enemies;

/* Level start and after every death: records as in the level, animations restarted. */
void enemies_reset(Enemies *en, const Level *lv);
void enemies_update(Enemies *en, const Level *lv, struct Player *p, Objects *o, unsigned frame, PlayerEvents *ev);
void enemies_display(Enemies *en, struct Player *p, Objects *o, PlayerEvents *ev);

int enemy_x(const Enemies *en, int i);
int enemy_y(const Enemies *en, int i);

#endif
