#include "platform/input.h"

#include <string.h>

#define MAX_KEYS 4
#define MAX_BUTTONS 4
#define MAX_AXES 2
#define MAX_PADS 4
#define AXIS_DEADZONE 12000

typedef struct {
    SDL_GamepadAxis axis;
    int sign;               /* +1 or -1 */
} AxisBind;

typedef struct {
    SDL_Scancode keys[MAX_KEYS];
    int nkeys;
    SDL_GamepadButton buttons[MAX_BUTTONS];
    int nbuttons;
    AxisBind axes[MAX_AXES];
    int naxes;
} Binding;

static const char *const action_names[ACT_COUNT] = {
    "left", "right", "up", "down", "jump", "action", "pause", "menu", "fullscreen",
    "debug_prev_level", "debug_next_level", "debug_next_song", "debug_sfx", "debug_fade",
    "debug_overlay", "debug_die", "debug_warp",
};

/* Defaults; each [input] line in engine.ini replaces one action's list. */
static const char *const default_bindings[ACT_COUNT] = {
    "Left, pad:dpleft, pad:-leftx",
    "Right, pad:dpright, pad:+leftx",
    "Up, pad:dpup, pad:-lefty",
    "Down, pad:dpdown, pad:+lefty",
    "pad:a",
    "Space, Return, pad:x",
    "P, pad:start",
    "Escape, pad:back",
    "F11",
    "PageUp, pad:leftshoulder",
    "PageDown, pad:rightshoulder",
    "M, pad:y",
    "S",
    "F",
    "F2",
    "F1",
    "F3",
};

static Binding binds[ACT_COUNT];
static SDL_Gamepad *pads[MAX_PADS];
static bool held[ACT_COUNT], prev[ACT_COUNT], latched[ACT_COUNT];
static char typed[16];
static int ntyped;

static bool parse_binding(Binding *b, const char *spec)
{
    Binding out = {0};
    char buf[256];
    SDL_strlcpy(buf, spec, sizeof buf);
    bool ok = true;
    char *save = NULL;
    for (char *tok = SDL_strtok_r(buf, ",", &save); tok; tok = SDL_strtok_r(NULL, ",", &save)) {
        while (*tok == ' ')
            tok++;
        char *end = tok + strlen(tok);
        while (end > tok && end[-1] == ' ')
            *--end = '\0';
        if (!*tok)
            continue;

        if (!SDL_strncasecmp(tok, "pad:", 4)) {
            const char *name = tok + 4;
            if ((*name == '+' || *name == '-') && out.naxes < MAX_AXES) {
                SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(name + 1);
                if (ax != SDL_GAMEPAD_AXIS_INVALID) {
                    out.axes[out.naxes++] = (AxisBind){ax, *name == '-' ? -1 : 1};
                    continue;
                }
            } else if (out.nbuttons < MAX_BUTTONS) {
                SDL_GamepadButton bt = SDL_GetGamepadButtonFromString(name);
                if (bt != SDL_GAMEPAD_BUTTON_INVALID) {
                    out.buttons[out.nbuttons++] = bt;
                    continue;
                }
            }
        } else if (out.nkeys < MAX_KEYS) {
            SDL_Scancode sc = SDL_GetScancodeFromName(tok);
            if (sc != SDL_SCANCODE_UNKNOWN) {
                out.keys[out.nkeys++] = sc;
                continue;
            }
        }
        SDL_Log("input: can't bind '%s'", tok);
        ok = false;
    }
    *b = out;
    return ok;
}

void input_init(void)
{
    for (int a = 0; a < ACT_COUNT; a++)
        if (binds[a].nkeys + binds[a].nbuttons + binds[a].naxes == 0)
            parse_binding(&binds[a], default_bindings[a]);
}

void input_shutdown(void)
{
    for (int i = 0; i < MAX_PADS; i++) {
        SDL_CloseGamepad(pads[i]);
        pads[i] = NULL;
    }
}

bool input_config(const char *section, const char *key, const char *value)
{
    if (SDL_strcasecmp(section, "input"))
        return false;
    for (int a = 0; a < ACT_COUNT; a++) {
        if (!SDL_strcasecmp(key, action_names[a])) {
            parse_binding(&binds[a], value);
            return true;
        }
    }
    return false;
}

static void latch(bool (*match)(const Binding *, int), int code)
{
    for (int a = 0; a < ACT_COUNT; a++)
        if (match(&binds[a], code))
            latched[a] = true;
}

static bool has_key(const Binding *b, int sc)
{
    for (int i = 0; i < b->nkeys; i++)
        if (b->keys[i] == (SDL_Scancode)sc)
            return true;
    return false;
}

static bool has_button(const Binding *b, int bt)
{
    for (int i = 0; i < b->nbuttons; i++)
        if (b->buttons[i] == (SDL_GamepadButton)bt)
            return true;
    return false;
}

void input_handle_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: {
        if (!e->key.repeat)
            latch(has_key, e->key.scancode);
        SDL_Keycode k = e->key.key;
        int c = k == SDLK_BACKSPACE ? '\b' : (k >= SDLK_0 && k <= SDLK_9) || (k >= SDLK_A && k <= SDLK_Z) ? SDL_toupper((int)k) : 0;
        if (c && !(e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) && ntyped < (int)sizeof typed)
            typed[ntyped++] = (char)c;
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        latch(has_button, e->gbutton.button);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        for (int i = 0; i < MAX_PADS; i++) {
            if (!pads[i]) {
                pads[i] = SDL_OpenGamepad(e->gdevice.which);
                if (pads[i])
                    SDL_Log("input: gamepad '%s' connected", SDL_GetGamepadName(pads[i]));
                break;
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (int i = 0; i < MAX_PADS; i++) {
            if (pads[i] && SDL_GetGamepadID(pads[i]) == e->gdevice.which) {
                SDL_Log("input: gamepad '%s' disconnected", SDL_GetGamepadName(pads[i]));
                SDL_CloseGamepad(pads[i]);
                pads[i] = NULL;
            }
        }
        break;
    default:
        break;
    }
}

static bool sample(const Binding *b)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < b->nkeys; i++)
        if (keys[b->keys[i]])
            return true;
    for (int p = 0; p < MAX_PADS; p++) {
        if (!pads[p])
            continue;
        for (int i = 0; i < b->nbuttons; i++)
            if (SDL_GetGamepadButton(pads[p], b->buttons[i]))
                return true;
        for (int i = 0; i < b->naxes; i++)
            if (SDL_GetGamepadAxis(pads[p], b->axes[i].axis) * b->axes[i].sign > AXIS_DEADZONE)
                return true;
    }
    return false;
}

void input_tick(void)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        prev[a] = held[a];
        held[a] = sample(&binds[a]) || latched[a];
        latched[a] = false;
    }
}

void input_inject(Action a)
{
    if (a < ACT_COUNT)
        latched[a] = true;
}

Action input_action_by_name(const char *name)
{
    for (int a = 0; a < ACT_COUNT; a++)
        if (!SDL_strcasecmp(name, action_names[a]))
            return (Action)a;
    return ACT_COUNT;
}

int input_take_char(void)
{
    if (ntyped == 0)
        return 0;
    int c = typed[0];
    memmove(typed, typed + 1, (size_t)--ntyped);
    return c;
}

void input_clear_chars(void) { ntyped = 0; }

bool input_held(Action a) { return held[a]; }
bool input_pressed(Action a) { return held[a] && !prev[a]; }
bool input_released(Action a) { return !held[a] && prev[a]; }
