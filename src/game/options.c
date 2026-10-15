#include "game/options.h"

#include <string.h>

#include "core/config.h"
#include "game/game.h"
#include "platform/video.h"

#define MESSAGE_FRAMES (3 * 36)

/* Layout, in interface pixels (window pixels / the interface scale). */
#define PANEL_W 500
#define PANEL_H (242 + ROW_COUNT * ROW_H)
#define MARGIN 14
#define GLYPH 8                 /* SDL's debug font: 8 x 8 */
#define LINE 12
#define ROW_H 14
#define COL_KEYS (MARGIN + 128)
#define COL_PAD (COL_KEYS + 120)

enum { ROW_DIFFICULTY, ROW_UP_THRESHOLD, ROW_UP_DELAY, ROW_MAP, ROW_RESET, ROW_BACK, ROW_COUNT };
static const char *const row_labels[ROW_COUNT] = {
    "Difficulty", "Pad up: stick push", "Pad up: hold time", "Map controller", "Reset controller", "Back",
};
static const char *const row_help[ROW_COUNT] = {
    NULL,                       /* the difficulty's own description */
    "How far the stick must go up. Higher: fewer false ups.",
    "How long the pad must hold up. Longer: fewer false ups.",
    "Press each control in turn on the controller.",
    "Default controller layout and pad up settings.",
    "Return to the main menu.",
};
#define THRESHOLD_STEP 5
#define LOGIC_HZ 36.413

static const char *const difficulty_labels[DIFF_COUNT] = {"High", "Medium", "Low"};
static const char *const difficulty_ini[DIFF_COUNT] = {"high", "medium", "low"};
static const char *const difficulty_help[DIFF_COUNT] = {
    "The original: a hit costs 2 energy.",
    "A hit costs 1 energy.",
    "A hit costs 1 energy, then 2 s without hits.",
};

static const struct {
    Action action;
    const char *label;
} mapped[OPTIONS_MAPPED] = {
    {ACT_LEFT, "Left"},  {ACT_RIGHT, "Right"},           {ACT_UP, "Up / climb"}, {ACT_DOWN, "Down / crouch"},
    {ACT_JUMP, "Jump"},  {ACT_ACTION, "Pick up / throw"}, {ACT_PAUSE, "Pause"},   {ACT_MENU, "Menu / back"},
};

/* --- update ------------------------------------------------------------------------------- */

static void say(Options *o, const char *text)
{
    SDL_strlcpy(o->message, text, sizeof o->message);
    o->message_frames = MESSAGE_FRAMES;
}

typedef char Spec[256];

static void snapshot(Spec *specs)
{
    for (int a = 0; a < ACT_COUNT; a++)
        input_binding_spec((Action)a, specs[a], sizeof specs[a]);
}

/* Writes the [input] lines that changed since `before`. */
static void save_bindings(Options *o, const Spec *before, const char *done)
{
    bool ok = true;
    for (int a = 0; a < ACT_COUNT; a++) {
        Spec now;
        input_binding_spec((Action)a, now, sizeof now);
        if (strcmp(now, before[a]))
            ok = config_save_value("input", input_action_name((Action)a), now) && ok;
    }
    say(o, ok ? done : "Couldn't save engine.ini: the layout lasts until you quit.");
}

static bool captured_before(const Options *o, int count, const PadInput *in)
{
    for (int i = 0; i < count; i++)
        if (input_pad_equal(&o->captured[i], in))
            return true;
    return false;
}

/* A direction mapped to a button keeps the left stick too, unless the stick went elsewhere. */
static void apply_mapping(Options *o)
{
    static Spec before[ACT_COUNT];
    snapshot(before);
    for (int i = 0; i < OPTIONS_MAPPED; i++) {
        Action a = mapped[i].action;
        PadInput in[2] = {o->captured[i]};
        int n = 1;
        if (!in[0].axis && (a == ACT_LEFT || a == ACT_RIGHT || a == ACT_UP || a == ACT_DOWN)) {
            PadInput def[INPUT_MAX_PAD];
            int nd = input_pad_default(a, def);
            for (int k = 0; k < nd && n < 2; k++)
                if (def[k].axis && !captured_before(o, OPTIONS_MAPPED, &def[k]))
                    in[n++] = def[k];
        }
        input_pad_set(a, in, n);
    }
    save_bindings(o, before, "Controller mapped and saved.");
}

static void reset_pad(Options *o)
{
    static Spec before[ACT_COUNT];
    snapshot(before);
    for (int a = 0; a < ACT_COUNT; a++) {
        PadInput def[INPUT_MAX_PAD];
        int n = input_pad_default((Action)a, def);
        input_pad_set((Action)a, def, n);
    }
    int threshold, delay;
    input_pad_up_defaults(&threshold, &delay);
    input_set_pad_up(threshold, delay);
    char v[16];
    SDL_snprintf(v, sizeof v, "%d", threshold);
    bool ok = config_save_value("input", "pad_up_threshold", v);
    SDL_snprintf(v, sizeof v, "%d", delay);
    ok = config_save_value("input", "pad_up_delay", v) && ok;
    save_bindings(o, before, ok ? "Controller reset to the default layout." : "Couldn't save engine.ini.");
}

/* Left / right (or select, `step` 1) on a row with a value: change it, wrapping, and save. */
static void change_value(Options *o, int row, int step, int *difficulty)
{
    char v[16];
    bool ok = true;
    if (row == ROW_DIFFICULTY) {
        *difficulty = (*difficulty + step + DIFF_COUNT) % DIFF_COUNT;
        ok = config_save_value("player", "difficulty", difficulty_ini[*difficulty]);
    } else if (row == ROW_UP_THRESHOLD) {
        int t = input_pad_up_threshold() + step * THRESHOLD_STEP;
        t = t > PAD_UP_THRESHOLD_MAX ? PAD_UP_THRESHOLD_MIN : t < PAD_UP_THRESHOLD_MIN ? PAD_UP_THRESHOLD_MAX : t;
        input_set_pad_up(t, input_pad_up_delay());
        SDL_snprintf(v, sizeof v, "%d", t);
        ok = config_save_value("input", "pad_up_threshold", v);
    } else if (row == ROW_UP_DELAY) {
        int d = (input_pad_up_delay() + step + PAD_UP_DELAY_MAX + 1) % (PAD_UP_DELAY_MAX + 1);
        input_set_pad_up(input_pad_up_threshold(), d);
        SDL_snprintf(v, sizeof v, "%d", d);
        ok = config_save_value("input", "pad_up_delay", v);
    }
    if (!ok)
        say(o, "Couldn't save engine.ini: the change lasts until you quit.");
}

static bool has_value(int row) { return row == ROW_DIFFICULTY || row == ROW_UP_THRESHOLD || row == ROW_UP_DELAY; }

static void row_value(int row, int difficulty, char *buf, size_t size)
{
    buf[0] = '\0';
    if (row == ROW_DIFFICULTY)
        SDL_strlcpy(buf, difficulty_labels[difficulty], size);
    else if (row == ROW_UP_THRESHOLD)
        SDL_snprintf(buf, size, "%d%%", input_pad_up_threshold());
    else if (row == ROW_UP_DELAY && input_pad_up_delay() == 0)
        SDL_strlcpy(buf, "off", size);
    else if (row == ROW_UP_DELAY)
        SDL_snprintf(buf, size, "%d ms", (int)(input_pad_up_delay() * 1000 / LOGIC_HZ + 0.5));
}

static void mapping_frame(Options *o)
{
    PadInput in;
    int r = input_capture_poll(&in);
    if (r < 0) {
        o->mapping = -1;
        o->wait_release = true;
        say(o, "Mapping cancelled: nothing changed.");
        return;
    }
    if (r == 0)
        return;
    for (int i = 0; i < o->mapping; i++) {
        if (input_pad_equal(&o->captured[i], &in)) {
            char text[96];
            SDL_snprintf(text, sizeof text, "%s is already on %s.", input_pad_name(&in), mapped[i].label);
            say(o, text);
            return;
        }
    }
    o->captured[o->mapping++] = in;
    o->message_frames = 0;
    if (o->mapping < OPTIONS_MAPPED)
        return;
    input_capture_stop();
    o->mapping = -1;
    o->wait_release = true;
    apply_mapping(o);
}

void options_open(Options *o)
{
    memset(o, 0, sizeof *o);
    menu_list_init(&o->list, row_labels, ROW_COUNT, ROW_DIFFICULTY);
    o->mapping = -1;
    o->wait_release = true;     /* the press that opened the window */
}

bool options_update(Options *o, int *difficulty)
{
    o->frames++;
    if (o->message_frames > 0)
        o->message_frames--;
    if (o->mapping >= 0) {
        mapping_frame(o);
        return false;
    }
    if (o->wait_release) {
        static const Action controls[] = {ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_ACTION, ACT_MENU};
        for (size_t i = 0; i < SDL_arraysize(controls); i++)
            if (input_held(controls[i]))
                return false;
        o->wait_release = false;
    }

    int step = 0;
    if (has_value(o->list.sel))
        step = input_pressed(ACT_RIGHT) ? 1 : input_pressed(ACT_LEFT) ? -1 : 0;
    switch (menu_list_update(&o->list)) {
    case ROW_DIFFICULTY:
    case ROW_UP_THRESHOLD:
    case ROW_UP_DELAY:
        step = 1;
        break;
    case ROW_MAP:
        o->mapping = 0;
        o->message_frames = 0;
        input_capture_start();
        break;
    case ROW_RESET:
        reset_pad(o);
        break;
    case ROW_BACK:
    case MENU_BACK:
        return true;
    default:
        break;
    }
    if (step)
        change_value(o, o->list.sel, step, difficulty);
    return false;
}

/* --- draw --------------------------------------------------------------------------------- */

typedef struct {
    SDL_Renderer *r;
    SDL_Color color;
    uint8_t bright;
    float x, y;                 /* the panel's top-left corner */
} Ui;

/* The text colour at `pct` percent, with the fade. */
static void ink(const Ui *u, int pct, uint8_t alpha)
{
    int k = pct * u->bright / 100;
    SDL_SetRenderDrawColor(u->r, (Uint8)(u->color.r * k / 255), (Uint8)(u->color.g * k / 255),
                           (Uint8)(u->color.b * k / 255), alpha);
}

/* Text at (x, y) in the panel, cut to `max` characters. */
static void text(const Ui *u, float x, float y, const char *s, int pct, int max)
{
    char buf[128];
    SDL_strlcpy(buf, s, sizeof buf);
    if (max > 3 && (int)strlen(buf) > max)
        SDL_strlcpy(buf + max - 3, "...", 4);
    ink(u, pct, 255);
    SDL_RenderDebugText(u->r, u->x + x, u->y + y, buf);
}

static void fill(const Ui *u, float x, float y, float w, float h, int pct, uint8_t alpha)
{
    ink(u, pct, alpha);
    SDL_RenderFillRect(u->r, &(SDL_FRect){u->x + x, u->y + y, w, h});
}

static void separator(const Ui *u, float y)
{
    fill(u, MARGIN, y, PANEL_W - 2 * MARGIN, 1, 35, 255);
}

static void draw_rows(const Options *o, const Ui *u, int difficulty, float y)
{
    bool mapping = o->mapping >= 0;
    for (int i = 0; i < ROW_COUNT; i++, y += ROW_H) {
        bool on = i == o->list.sel && !mapping;
        if (on) {
            fill(u, MARGIN - 6, y - 3, PANEL_W - 2 * MARGIN + 12, ROW_H, 100, 40);
            fill(u, MARGIN - 6, y - 3, 3, ROW_H, 100, 255);
        }
        text(u, MARGIN, y, row_labels[i], on ? 100 : 65, 30);
        if (has_value(i)) {
            char v[32], value[48];
            row_value(i, difficulty, v, sizeof v);
            SDL_snprintf(value, sizeof value, on ? "<  %s  >" : "   %s", v);
            text(u, COL_PAD, y, value, on ? 100 : 65, 30);
        } else if (i == ROW_MAP && !input_pad_connected()) {
            text(u, COL_PAD, y, "no controller connected", 45, 30);
        }
    }
}

static void draw_controls(const Options *o, const Ui *u, float y)
{
    text(u, MARGIN, y, "Action", 55, 16);
    text(u, COL_KEYS, y, "Keyboard", 55, 15);
    text(u, COL_PAD, y, "Controller", 55, 28);
    y += LINE + 4;
    for (int i = 0; i < OPTIONS_MAPPED; i++, y += LINE) {
        Action a = mapped[i].action;
        char keys[128], pad[128];
        input_describe_keys(a, keys, sizeof keys);
        if (a == ACT_JUMP && !strcmp(keys, "-"))
            input_describe_keys(ACT_UP, keys, sizeof keys);     /* up always jumps too */
        input_describe_pad(a, pad, sizeof pad);
        int pct = 85;
        if (o->mapping >= 0) {
            if (i < o->mapping) {
                SDL_strlcpy(pad, input_pad_name(&o->captured[i]), sizeof pad);
            } else if (i == o->mapping) {
                fill(u, MARGIN - 6, y - 2, PANEL_W - 2 * MARGIN + 12, LINE, 100, 40);
                fill(u, MARGIN - 6, y - 2, 3, LINE, 100, 255);
                SDL_strlcpy(pad, (o->frames / 12) & 1 ? "" : "> press a button <", sizeof pad);
                pct = 100;
            } else {
                pct = 45;
            }
        }
        text(u, MARGIN, y, mapped[i].label, pct, 16);
        text(u, COL_KEYS, y, keys, pct, 15);
        text(u, COL_PAD, y, pad, i == o->mapping ? 100 : pct, 28);
    }
}

void options_draw(const Options *o, int difficulty, SDL_Renderer *r, SDL_Color color, uint8_t bright)
{
    int w, h;
    video_output_size(&w, &h);
    if (w <= 0 || h <= 0)
        return;
    /* Whole interface pixels, as large as still fits the panel with some room around it. */
    int scale = SDL_max(1, SDL_min(w / (PANEL_W + 60), h / (PANEL_H + 30)));
    SDL_SetRenderScale(r, (float)scale, (float)scale);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    Ui u = {r, color, bright, SDL_floorf((w / scale - PANEL_W) / 2.0f), SDL_floorf((h / scale - PANEL_H) / 2.0f)};

    SDL_SetRenderDrawColor(r, 10, 10, 14, (Uint8)(235 * bright / 255));
    SDL_RenderFillRect(r, &(SDL_FRect){u.x, u.y, PANEL_W, PANEL_H});
    ink(&u, 80, 255);
    SDL_RenderRect(r, &(SDL_FRect){u.x, u.y, PANEL_W, PANEL_H});

    /* Title, twice the size. */
    SDL_SetRenderScale(r, (float)scale * 2, (float)scale * 2);
    ink(&u, 100, 255);
    SDL_RenderDebugText(r, (u.x + MARGIN) / 2, (u.y + MARGIN) / 2, "OPTIONS");
    SDL_SetRenderScale(r, (float)scale, (float)scale);

    float y = MARGIN + 2 * GLYPH + 10;
    separator(&u, y);
    y += 12;
    draw_rows(o, &u, difficulty, y);
    y += ROW_COUNT * ROW_H + 6;

    char help[96];
    if (o->mapping >= 0)
        SDL_snprintf(help, sizeof help, "Mapping %d of %d: %s", o->mapping + 1, OPTIONS_MAPPED, mapped[o->mapping].label);
    else
        SDL_strlcpy(help, o->list.sel == ROW_DIFFICULTY ? difficulty_help[difficulty] : row_help[o->list.sel], sizeof help);
    text(&u, MARGIN, y, help, 70, 59);
    y += LINE + 8;

    separator(&u, y);
    y += 10;
    draw_controls(o, &u, y);
    y += LINE + 4 + OPTIONS_MAPPED * LINE + 8;

    separator(&u, y);
    y += 10;
    const char *footer = o->message_frames > 0 ? o->message
                         : o->mapping >= 0     ? "Press a button or push a stick.  Esc: cancel"
                                               : "Up/Down move  Left/Right change  Enter select  Esc back";
    text(&u, MARGIN, y, footer, o->message_frames > 0 ? 100 : 55, 59);

    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
