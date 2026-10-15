/* The Options window, opened from the main menu: difficulty and the gamepad layout, both saved
 * to engine.ini. The keyboard layout is fixed here (engine.ini [input] still sets it) and is
 * only listed.
 *
 * Unlike the other screens it doesn't use the original's art: it is a plain window drawn at
 * the window's own resolution in SDL's built-in 8x8 font, in the game font's yellow.
 *
 * "Map controller" walks through the game's actions, waiting on each for a gamepad button
 * (or a stick / trigger push); Escape cancels and leaves the layout as it was.
 */
#ifndef FOXRV_OPTIONS_H
#define FOXRV_OPTIONS_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "game/menu.h"
#include "platform/input.h"

#define OPTIONS_MAPPED 8        /* actions "Map controller" asks for */

typedef struct {
    MenuList list;
    unsigned frames;
    int mapping;                /* action being asked for (0..OPTIONS_MAPPED-1); -1 = not mapping */
    PadInput captured[OPTIONS_MAPPED];
    bool wait_release;          /* ignore the controls until they are let go */
    char message[96];
    int message_frames;
} Options;

void options_open(Options *o);
/* One game frame. `difficulty` (a Difficulty) changes with left / right or select, and is
 * saved. Returns true when the player closes the window. */
bool options_update(Options *o, int *difficulty);
/* Draws the window in window pixels (after video_compose). `bright` 0..255 follows the screen
 * fades. */
void options_draw(const Options *o, int difficulty, SDL_Renderer *r, SDL_Color color, uint8_t bright);

#endif
