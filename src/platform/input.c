#include "platform/input.h"

#include <string.h>

#define MAX_KEYS 4
#define MAX_BUTTONS 4
#define MAX_AXES 2
#define MAX_PADS 4
#define AXIS_DEADZONE 12000     /* a stick counts past this (37%) ... */
#define AXIS_MAX 32767
#define PAD_UP_THRESHOLD 75     /* ... except up: percent of the travel, then held this many */
#define PAD_UP_DELAY 3          /* game frames (worn sticks and D-pads slip into up) */

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
    "Space, Return, pad:b",
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
static bool held_keys[ACT_COUNT], latched_keys[ACT_COUNT];     /* keyboard (and injected) only */
static int up_threshold = PAD_UP_THRESHOLD, up_delay = PAD_UP_DELAY;
static int up_frames;           /* ticks the gamepad has held up */
static char typed[16];
static int ntyped;

#define AXIS_REST 8000          /* capture: an axis must be back under this ... */
#define AXIS_CAPTURE 24000      /* ... before a push past this counts */
#define CANCEL_HOLD_MS 1000     /* capture: Back held this long cancels (no keyboard needed) */

static struct {
    bool active, aborted, got;
    PadInput in;
    bool armed[SDL_GAMEPAD_AXIS_COUNT];
    Uint64 back_since;          /* when Back went down, 0 = up */
} cap;

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
    if (!SDL_strcasecmp(key, "pad_up_threshold")) {
        input_set_pad_up(SDL_atoi(value), up_delay);
        return true;
    }
    if (!SDL_strcasecmp(key, "pad_up_delay")) {
        input_set_pad_up(up_threshold, SDL_atoi(value));
        return true;
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        if (!SDL_strcasecmp(key, action_names[a])) {
            parse_binding(&binds[a], value);
            return true;
        }
    }
    return false;
}

static void latch(bool (*match)(const Binding *, int), int code, bool *also)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        if (match(&binds[a], code)) {
            latched[a] = true;
            also[a] = true;
        }
    }
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

/* While capturing: the next gamepad press or axis push, or Escape. Back counts when it's
 * released, so that holding it can cancel instead. */
static void capture_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (e->key.scancode == SDL_SCANCODE_ESCAPE && !e->key.repeat)
            cap.aborted = true;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
            if (!cap.back_since)
                cap.back_since = SDL_GetTicks();
            break;
        }
        if (!cap.got)
            cap.in = (PadInput){false, e->gbutton.button, 0};
        cap.got = true;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK && cap.back_since) {
            cap.back_since = 0;
            if (!cap.got)
                cap.in = (PadInput){false, SDL_GAMEPAD_BUTTON_BACK, 0};
            cap.got = true;
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int ax = e->gaxis.axis, v = e->gaxis.value;
        if (ax < 0 || ax >= SDL_GAMEPAD_AXIS_COUNT)
            break;
        if (SDL_abs(v) < AXIS_REST) {
            cap.armed[ax] = true;
        } else if (cap.armed[ax] && SDL_abs(v) > AXIS_CAPTURE && !cap.got) {
            cap.in = (PadInput){true, ax, v < 0 ? -1 : 1};
            cap.got = true;
            cap.armed[ax] = false;
        }
        break;
    }
    default:
        break;
    }
}

void input_handle_event(const SDL_Event *e)
{
    if (cap.active && e->type != SDL_EVENT_GAMEPAD_ADDED && e->type != SDL_EVENT_GAMEPAD_REMOVED) {
        capture_event(e);
        return;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: {
        if (!e->key.repeat)
            latch(has_key, e->key.scancode, latched_keys);
        SDL_Keycode k = e->key.key;
        int c = k == SDLK_BACKSPACE ? '\b' : (k >= SDLK_0 && k <= SDLK_9) || (k >= SDLK_A && k <= SDLK_Z) ? SDL_toupper((int)k) : 0;
        if (c && !(e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) && ntyped < (int)sizeof typed)
            typed[ntyped++] = (char)c;
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        latch(has_button, e->gbutton.button, latched);
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

static bool sample_keys(const Binding *b)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < b->nkeys; i++)
        if (keys[b->keys[i]])
            return true;
    return false;
}

/* `threshold`: how far an axis must be pushed, 0..AXIS_MAX. */
static bool sample_pads(const Binding *b, int threshold)
{
    for (int p = 0; p < MAX_PADS; p++) {
        if (!pads[p])
            continue;
        for (int i = 0; i < b->nbuttons; i++)
            if (SDL_GetGamepadButton(pads[p], b->buttons[i]))
                return true;
        for (int i = 0; i < b->naxes; i++)
            if (SDL_GetGamepadAxis(pads[p], b->axes[i].axis) * b->axes[i].sign > threshold)
                return true;
    }
    return false;
}

void input_tick(void)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        prev[a] = held[a];
        held_keys[a] = !cap.active && (sample_keys(&binds[a]) || latched_keys[a]);
        int threshold = a == ACT_UP ? up_threshold * AXIS_MAX / 100 : AXIS_DEADZONE;
        bool pad = !cap.active && (sample_pads(&binds[a], threshold) || (latched[a] && !latched_keys[a]));
        if (a == ACT_UP) {              /* the pad's up counts once held long enough */
            up_frames = pad ? up_frames + 1 : 0;
            pad = up_frames > up_delay;
        }
        held[a] = held_keys[a] || pad;
        latched[a] = latched_keys[a] = false;
    }
}

void input_inject(Action a)
{
    if (a < ACT_COUNT)
        latched[a] = latched_keys[a] = true;
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
bool input_held_by_keyboard(Action a) { return held_keys[a]; }

void input_set_pad_up(int threshold, int delay)
{
    up_threshold = SDL_clamp(threshold, PAD_UP_THRESHOLD_MIN, PAD_UP_THRESHOLD_MAX);
    up_delay = SDL_clamp(delay, 0, PAD_UP_DELAY_MAX);
}

int input_pad_up_threshold(void) { return up_threshold; }
int input_pad_up_delay(void) { return up_delay; }
void input_pad_up_defaults(int *threshold, int *delay)
{
    *threshold = PAD_UP_THRESHOLD;
    *delay = PAD_UP_DELAY;
}

/* --- gamepad remapping -------------------------------------------------------------------- */

bool input_pad_equal(const PadInput *a, const PadInput *b)
{
    return a->axis == b->axis && a->code == b->code && (!a->axis || a->sign == b->sign);
}

/* The 3DS's own names. SDL maps its buttons by position, like the Xbox names below: B is
 * the bottom one (south), A the right one (east). */
#ifdef __3DS__
#define NAME_LSTICK "Circle Pad"
#define NAME_RSTICK "C-Stick"
#define NAME_LT "ZL"
#define NAME_RT "ZR"
#else
#define NAME_LSTICK "L-Stick"
#define NAME_RSTICK "R-Stick"
#define NAME_LT "LT"
#define NAME_RT "RT"
#endif

const char *input_pad_name(const PadInput *in)
{
    static char buf[32];
    if (in->axis) {
        const char *stick = in->code == SDL_GAMEPAD_AXIS_LEFTX || in->code == SDL_GAMEPAD_AXIS_LEFTY ? NAME_LSTICK : NAME_RSTICK;
        switch (in->code) {
        case SDL_GAMEPAD_AXIS_LEFTX:
        case SDL_GAMEPAD_AXIS_RIGHTX:
            SDL_snprintf(buf, sizeof buf, "%s %s", stick, in->sign < 0 ? "Left" : "Right");
            return buf;
        case SDL_GAMEPAD_AXIS_LEFTY:
        case SDL_GAMEPAD_AXIS_RIGHTY:
            SDL_snprintf(buf, sizeof buf, "%s %s", stick, in->sign < 0 ? "Up" : "Down");
            return buf;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return NAME_LT;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return NAME_RT;
        default: return "?";
        }
    }
    switch (in->code) {
#ifdef __3DS__
    case SDL_GAMEPAD_BUTTON_SOUTH: return "B";
    case SDL_GAMEPAD_BUTTON_EAST: return "A";
    case SDL_GAMEPAD_BUTTON_WEST: return "Y";
    case SDL_GAMEPAD_BUTTON_NORTH: return "X";
    case SDL_GAMEPAD_BUTTON_BACK: return "Select";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R";
#else
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
#endif
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS Click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS Click";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-Pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-Pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-Pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-Pad Right";
    default: {
        const char *name = SDL_GetGamepadStringForButton((SDL_GamepadButton)in->code);
        return name ? name : "?";
    }
    }
}

static int pad_inputs(const Binding *b, PadInput *out)
{
    int n = 0;
    for (int i = 0; i < b->nbuttons; i++)
        out[n++] = (PadInput){false, b->buttons[i], 0};
    for (int i = 0; i < b->naxes; i++)
        out[n++] = (PadInput){true, b->axes[i].axis, b->axes[i].sign};
    return n;
}

int input_pad_get(Action a, PadInput *out)
{
    return pad_inputs(&binds[a], out);
}

int input_pad_default(Action a, PadInput *out)
{
    Binding b;
    parse_binding(&b, default_bindings[a]);
    return pad_inputs(&b, out);
}

static void remove_pad_input(Binding *b, const PadInput *in)
{
    if (in->axis) {
        for (int i = 0; i < b->naxes; i++)
            if (b->axes[i].axis == (SDL_GamepadAxis)in->code && b->axes[i].sign == in->sign)
                b->axes[i--] = b->axes[--b->naxes];
    } else {
        for (int i = 0; i < b->nbuttons; i++)
            if (b->buttons[i] == (SDL_GamepadButton)in->code)
                b->buttons[i--] = b->buttons[--b->nbuttons];
    }
}

void input_pad_set(Action a, const PadInput *in, int n)
{
    Binding *b = &binds[a];
    b->nbuttons = b->naxes = 0;
    for (int i = 0; i < n; i++) {
        for (int other = 0; other < ACT_COUNT; other++)
            if (other != (int)a)
                remove_pad_input(&binds[other], &in[i]);
        if (in[i].axis && b->naxes < MAX_AXES)
            b->axes[b->naxes++] = (AxisBind){(SDL_GamepadAxis)in[i].code, in[i].sign};
        else if (!in[i].axis && b->nbuttons < MAX_BUTTONS)
            b->buttons[b->nbuttons++] = (SDL_GamepadButton)in[i].code;
    }
}

void input_describe_keys(Action a, char *buf, size_t size)
{
    const Binding *b = &binds[a];
    SDL_strlcpy(buf, b->nkeys ? "" : "-", size);
    for (int i = 0; i < b->nkeys; i++) {
        if (i)
            SDL_strlcat(buf, ", ", size);
        SDL_strlcat(buf, SDL_GetScancodeName(b->keys[i]), size);
    }
}

void input_describe_pad(Action a, char *buf, size_t size)
{
    PadInput in[INPUT_MAX_PAD];
    int n = input_pad_get(a, in);
    SDL_strlcpy(buf, n ? "" : "-", size);
    for (int i = 0; i < n; i++) {
        if (i)
            SDL_strlcat(buf, ", ", size);
        SDL_strlcat(buf, input_pad_name(&in[i]), size);
    }
}

const char *input_action_name(Action a)
{
    return a < ACT_COUNT ? action_names[a] : "";
}

void input_binding_spec(Action a, char *buf, size_t size)
{
    const Binding *b = &binds[a];
    char item[64];
    buf[0] = '\0';
    for (int i = 0; i < b->nkeys; i++) {
        SDL_snprintf(item, sizeof item, "%s%s", buf[0] ? ", " : "", SDL_GetScancodeName(b->keys[i]));
        SDL_strlcat(buf, item, size);
    }
    for (int i = 0; i < b->nbuttons; i++) {
        SDL_snprintf(item, sizeof item, "%spad:%s", buf[0] ? ", " : "", SDL_GetGamepadStringForButton(b->buttons[i]));
        SDL_strlcat(buf, item, size);
    }
    for (int i = 0; i < b->naxes; i++) {
        SDL_snprintf(item, sizeof item, "%spad:%c%s", buf[0] ? ", " : "", b->axes[i].sign < 0 ? '-' : '+',
                     SDL_GetGamepadStringForAxis(b->axes[i].axis));
        SDL_strlcat(buf, item, size);
    }
}

bool input_pad_connected(void)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i])
            return true;
    return false;
}

void input_capture_start(void)
{
    cap.active = true;
    cap.aborted = cap.got = false;
    cap.back_since = 0;
    for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ax++) {
        cap.armed[ax] = true;
        for (int p = 0; p < MAX_PADS; p++)
            if (pads[p] && SDL_abs(SDL_GetGamepadAxis(pads[p], (SDL_GamepadAxis)ax)) >= AXIS_REST)
                cap.armed[ax] = false;
    }
}

void input_capture_stop(void)
{
    cap.active = false;
}

int input_capture_poll(PadInput *out)
{
    if (cap.back_since && SDL_GetTicks() - cap.back_since >= CANCEL_HOLD_MS) {
        cap.back_since = 0;
        cap.aborted = true;
    }
    if (cap.aborted) {
        cap.active = cap.aborted = false;
        return -1;
    }
    if (!cap.got)
        return 0;
    cap.got = false;
    *out = cap.in;
    return 1;
}
