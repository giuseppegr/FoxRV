/* Elevators: the moving platforms of the level (tail B28, 10 records at DS:7DCB). A port of
 * the elevator update (image 0x4028), which runs first in every game frame; Titus standing on
 * one is part of the player's collision (0x4857, player.c).
 *
 * An elevator moves `speed` px per frame in its direction for `run` frames, then turns back.
 * It moves on screen or not, but can only be stood on while it is visible.
 */
#ifndef FOXRV_ELEVATORS_H
#define FOXRV_ELEVATORS_H

#include <stdbool.h>
#include <stdint.h>

#include "game/level.h"

#define ELEVATOR_SPRITE 0x1E    /* sprite word = record word +4 plus this */
#define ELEVATOR_NONE 0xFFFF

enum { ELEV_UP, ELEV_RIGHT, ELEV_DOWN, ELEV_LEFT };

typedef struct {
    int x, y;                   /* +00 / +02: top-left of the platform; riders stand at y */
    int prev_x, prev_y;         /* at the start of the frame, for render interpolation */
    uint16_t sprite;            /* +04 + 0x1E: number | flags (bit 13 = can be stood on); NONE = unused */
    uint8_t dir;                /* +06: ELEV_*; other values never move */
    int speed;                  /* +07: signed px per frame */
    int run;                    /* +0A: frames per run */
    int start_x, start_y;       /* +0C / +0E */
    uint8_t start_dir;          /* +10 */
    int count;                  /* +11: frames into the current run */
    bool visible;               /* in the display list (DS:5648) this frame */
} Elevator;

typedef struct {
    Elevator e[ELEVATOR_RECORDS];
    int view_x, view_y, view_w, view_h;     /* set by the game each frame */
} Elevators;

/* Level load: records with speed >= 8 are switched off (image 0x3151). Then, like after
 * every death (0x5905): back at the start, start direction, counter 0. */
void elevators_load(Elevators *el, const Level *lv);
void elevators_reset(Elevators *el);

/* 0x4028: move, turn at the end of a run, and find which ones are visible. */
void elevators_update(Elevators *el);

/* Before a frame that doesn't run elevators_update: stop interpolating. */
void elevators_hold(Elevators *el);

#endif
