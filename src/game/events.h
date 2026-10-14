/* What happened during a game frame, for the game layer (sounds, lives, HUD). The logic
 * modules only record events; game.c turns them into audio and bookkeeping. */
#ifndef FOXRV_EVENTS_H
#define FOXRV_EVENTS_H

#include <stdbool.h>

#define MAX_FRAME_SFX 6

/* Sound effects the original plays (image 0x13E6), by meaning. */
enum { SFX_ENEMY_HIT = 1, SFX_THROW = 3, SFX_PLAYER_HURT = 4, SFX_PLAYER_HIT = 5, SFX_PICK_UP = 9, SFX_BOUNCE = 12 };

typedef struct {
    int song;                   /* jingle to play, or -1 */
    int energy;                 /* energy bonuses collected */
    int energy_loss;            /* energy lost (falling objects) */
    int hurts;                  /* hits by enemies and projectiles: 2 energy each in the original */
    bool died;                  /* touched something deadly or fell out of the map */
    bool code_reveal;           /* a level-code tile was taken */
    bool code_level14;          /* ... revealing level 14's code instead of this level's */
    bool checkpoint;
    int sfx[MAX_FRAME_SFX];
    int nsfx;
} PlayerEvents;

static inline void events_sfx(PlayerEvents *ev, int n)
{
    if (ev->nsfx < MAX_FRAME_SFX)
        ev->sfx[ev->nsfx++] = n;
}

#endif
