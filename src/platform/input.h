/* Player input as abstract actions, fed by keyboard and any number of gamepads.
 *
 * State is sampled once per game-logic tick (input_tick), so `pressed`/`released` fire exactly
 * once per tick. Presses that start and end between two ticks are latched from events so a
 * quick tap is never lost.
 */
#ifndef FOXRV_INPUT_H
#define FOXRV_INPUT_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef enum {
    ACT_LEFT,
    ACT_RIGHT,
    ACT_UP,
    ACT_DOWN,
    ACT_JUMP,
    ACT_ACTION,         /* pick up / throw */
    ACT_PAUSE,
    ACT_MENU,
    ACT_FULLSCREEN,
    /* sandbox / debug */
    ACT_DBG_PREV_LEVEL,
    ACT_DBG_NEXT_LEVEL,
    ACT_DBG_NEXT_SONG,
    ACT_DBG_SFX,
    ACT_DBG_FADE,
    ACT_DBG_OVERLAY,
    ACT_DBG_DIE,
    ACT_DBG_WARP,
    ACT_COUNT
} Action;

void input_init(void);
void input_shutdown(void);

/* Replaces an action's bindings from an engine.ini [input] line, e.g.
 *   jump = Space, pad:a
 *   left = Left, A, pad:dpleft, pad:-leftx
 * Keys use SDL scancode names; pad buttons/axes use SDL gamepad names ("-" = negative axis).
 * Has the ConfigHandler signature so it can be passed to config_load directly. */
bool input_config(const char *section, const char *key, const char *value);

void input_handle_event(const SDL_Event *e);
void input_tick(void);

/* Scripted input (foxrv --press): the action counts as pressed at the next tick. */
void input_inject(Action a);
/* By its engine.ini name ("left", "action", ...); ACT_COUNT if unknown. */
Action input_action_by_name(const char *name);

bool input_held(Action a);
bool input_pressed(Action a);
bool input_released(Action a);

/* Text entry (keyboard only), e.g. level codes: the next key typed since the last call as
 * '0'-'9' / 'A'-'Z', or '\b' for Backspace; 0 when none. input_clear_chars drops the backlog. */
int input_take_char(void);
void input_clear_chars(void);

#endif
