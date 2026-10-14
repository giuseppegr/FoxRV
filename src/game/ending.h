/* The wedding scene that "Just Married" (file slot G) plays instead of a level (image
 * 0x370E): Titus and the bride run toward each other, meet in a kiss with a burst ring, then
 * a little heart hops around them until fire is pressed, and "THE END" slides in.
 * Positions and animations come from player.json "ending" (tools/export_player.py).
 */
#ifndef FOXRV_ENDING_H
#define FOXRV_ENDING_H

#include <stdbool.h>

#include "game/objects.h"

typedef enum { END_RUN, END_KISS, END_HEARTS, END_LETTERS, END_DONE } EndingStage;

typedef struct {
    EndingStage stage;
    int frames;                 /* in the current stage */
    Obj titus, bride;           /* x, y and sprite word, drawn like objects */
    int titus_anim, bride_anim; /* positions in their animation words */
    int path;                   /* position on the heart's path */
} Ending;

bool ending_load(void);         /* false if player.json has no ending (old export) */
void ending_start(Ending *e);
/* One game frame; `fire` ends the hearts. Ends in END_DONE. */
void ending_update(Ending *e, bool fire);

#endif
