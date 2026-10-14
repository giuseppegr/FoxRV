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
/* Held through its keyboard keys (or input_inject), not a gamepad. */
bool input_held_by_keyboard(Action a);

/* Up on a gamepad, which worn sticks and D-pads slip into: the stick must be pushed
 * `threshold` percent of its travel (other directions: 37%), and up held (stick or D-pad)
 * `delay` game frames before it counts. engine.ini [input] pad_up_threshold / pad_up_delay. */
#define PAD_UP_THRESHOLD_MIN 30
#define PAD_UP_THRESHOLD_MAX 95
#define PAD_UP_DELAY_MAX 12
void input_set_pad_up(int threshold, int delay);
int input_pad_up_threshold(void);
int input_pad_up_delay(void);
void input_pad_up_defaults(int *threshold, int *delay);

/* Text entry (keyboard only), e.g. level codes: the next key typed since the last call as
 * '0'-'9' / 'A'-'Z', or '\b' for Backspace; 0 when none. input_clear_chars drops the backlog. */
int input_take_char(void);
void input_clear_chars(void);

/* --- gamepad remapping (Options screen) ----------------------------------------------------- */

#define INPUT_MAX_PAD 6         /* gamepad inputs per action: 4 buttons + 2 axis directions */

/* One gamepad input: a button, or an axis pushed one way. */
typedef struct {
    bool axis;
    int code;                   /* SDL_GamepadButton, or SDL_GamepadAxis when `axis` */
    int sign;                   /* axes: +1 / -1 */
} PadInput;

bool input_pad_equal(const PadInput *a, const PadInput *b);
/* For people, Xbox layout: "A", "D-Pad Left", "L-Stick Up", "RT". */
const char *input_pad_name(const PadInput *in);

/* An action's gamepad inputs, now or by default; return how many. */
int input_pad_get(Action a, PadInput *out);
int input_pad_default(Action a, PadInput *out);
/* Replaces an action's gamepad inputs, taking them away from every other action. */
void input_pad_set(Action a, const PadInput *in, int n);

/* Display strings: "Space, Return" / "D-Pad Left, L-Stick Left"; "-" when none. */
void input_describe_keys(Action a, char *buf, size_t size);
void input_describe_pad(Action a, char *buf, size_t size);
/* The action's engine.ini name and line, e.g. "left" and "Left, pad:dpleft, pad:-leftx". */
const char *input_action_name(Action a);
void input_binding_spec(Action a, char *buf, size_t size);

bool input_pad_connected(void);

/* Waits for the next gamepad button press or stick / trigger push; meanwhile the gamepads and
 * keyboard trigger no actions. Poll returns 1 with *out set, -1 if Escape was pressed (which
 * also ends the capture), 0 while waiting. An axis already pushed must go back to rest first. */
void input_capture_start(void);
void input_capture_stop(void);
int input_capture_poll(PadInput *out);

#endif
