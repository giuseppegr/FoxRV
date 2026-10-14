/* The camera, following the original's rules (image 0x4146) on a wider view.
 *
 * The original screen is 20 x 12 tiles and the camera moves one tile per frame toward a
 * target: the column/row where the player should appear (e.g. column 2 when running right,
 * so the screen shows what's ahead). Here targets and thresholds are scaled to the actual
 * view size. Like the original, the camera stops as soon as the player is in the target
 * column/row, so a new target can be picked on the next frame. It moves one original tile per
 * frame: drawn gliding between game frames (original) or in 16 px steps (tiles); or at a
 * configurable, eased speed (smooth).
 */
#ifndef FOXRV_CAMERA_H
#define FOXRV_CAMERA_H

#include <stdbool.h>

#include "game/enemies.h"
#include "game/level.h"
#include "game/player.h"

typedef enum {
    CAM_ORIGINAL,               /* the original's speed (one tile per frame), drawn smoothly */
    CAM_TILES,                  /* the original's 16 px steps */
    CAM_SMOOTH,                 /* speed and ease below */
} CameraMode;

typedef struct {
    CameraMode mode;
    int speed;                  /* smooth: top speed, px per frame on a 320 px wide view */
    int ease;                   /* smooth: slow down near the target: step <= distance / ease; 0 = off */
} CameraTuning;

typedef struct {
    float x, y;                 /* top-left corner in map pixels */
    float prev_x, prev_y;       /* before this frame, for render interpolation */
    bool hscroll, vscroll;      /* moving toward a target (DS:6520 / 6521) */
    int htarget, vtarget;       /* target screen column (of 20) / row (of 12) */
} Camera;

void camera_tuning_defaults(CameraTuning *t);

/* Places the camera around the player immediately (level start, respawn). */
void camera_reset(Camera *c, const Level *lv, const Player *p, int view_w, int view_h);
/* Gates (image 0x3B91): jump to the original screen whose top-left tile is (col, row),
 * centred in the wider view; anywhere in the map, ignoring the level's camera limits. */
void camera_show(Camera *c, const Level *lv, int col, int row, int view_w, int view_h);
void camera_update(Camera *c, const Level *lv, const Player *p, const Enemies *en, const CameraTuning *t,
                   int view_w, int view_h);

#endif
