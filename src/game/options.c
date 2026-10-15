#include "game/options.h"

#include <string.h>

#include "core/config.h"
#include "game/game.h"
#include "platform/audio.h"
#include "platform/video.h"

#define MESSAGE_FRAMES (3 * 36)

#define GLYPH 8                 /* SDL's debug font: 8 x 8 */

/* Builds without the OPL emulator have only the PC speaker, so no Sound row. */
enum {
    ROW_DIFFICULTY,
#if FOXRV_WITH_OPL
    ROW_SOUND,
#endif
    ROW_UP_THRESHOLD, ROW_UP_DELAY, ROW_MAP, ROW_RESET, ROW_BACK, ROW_COUNT
};
static const char *const row_labels[ROW_COUNT] = {
    "Difficulty",
#if FOXRV_WITH_OPL
    "Sound",
#endif
    "Pad up: stick push", "Pad up: hold time", "Map controller", "Reset controller", "Back",
};
static const char *const row_help[ROW_COUNT] = {
    NULL,                       /* the difficulty's own description */
#if FOXRV_WITH_OPL
    "AdLib music card, or the PC speaker's beeps.",
#endif
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
#if FOXRV_WITH_OPL
    } else if (row == ROW_SOUND) {
        AudioDevice d = audio_device() == AUDIO_ADLIB ? AUDIO_SPEAKER : AUDIO_ADLIB;
        if (!audio_set_device(d))
            say(o, "No sound device: the game runs silent.");
        ok = config_save_value("audio", "device", d == AUDIO_ADLIB ? "adlib" : "speaker");
#endif
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

static bool has_value(int row)
{
#if FOXRV_WITH_OPL
    if (row == ROW_SOUND)
        return true;
#endif
    return row == ROW_DIFFICULTY || row == ROW_UP_THRESHOLD || row == ROW_UP_DELAY;
}

static void row_value(int row, int difficulty, char *buf, size_t size)
{
    buf[0] = '\0';
    if (row == ROW_DIFFICULTY)
        SDL_strlcpy(buf, difficulty_labels[difficulty], size);
#if FOXRV_WITH_OPL
    else if (row == ROW_SOUND)
        SDL_strlcpy(buf, audio_device() == AUDIO_ADLIB ? "AdLib" : "PC speaker", size);
#endif
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
#if FOXRV_WITH_OPL
    case ROW_SOUND:
#endif
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

/* Where things go, in interface pixels (window pixels / the interface scale). Cuts are in
 * characters; a column at 0 isn't drawn. */
typedef struct {
    float w, h, margin, line, row_h;
    float gap_title, gap_rows, gap_help, gap_sep_before, gap_sep_after;
    int title_scale, help_lines;
    bool header, compact;
    float col_value, col_keys, col_pad;
    int cut_label, cut_value, cut_action, cut_keys, cut_pad, cut_text;
} Layout;

/* Roomy, for windows; and one that fits the original 320 x 200 screen. */
static const Layout full = {
    500, 242 + ROW_COUNT * 14, 14, 12, 14,
    10, 12, 6, 8, 10,
    2, 1,
    true, false,
    262, 142, 262,
    30, 30, 16, 15, 28, 59,
};
static const Layout compact_keys = {
    320, 200, 6, 9, 9,
    4, 4, 3, 2, 4,
    1, 2,
    false, true,
    170, 112, 208,
    20, 18, 13, 11, 13, 38,
};
static const Layout compact_pad = {     /* no keyboard: the controller column gets its room */
    320, 200, 6, 9, 9,
    4, 4, 3, 2, 4,
    1, 2,
    false, true,
    170, 0, 132,
    20, 18, 15, 0, 22, 38,
};

typedef struct {
    SDL_Renderer *r;
    SDL_Color color;
    uint8_t bright;
    float x, y;                 /* the panel's top-left corner */
    const Layout *l;
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

/* Text over up to `lines` lines of `max` characters, broken at spaces. */
static void text_wrapped(const Ui *u, float x, float y, const char *s, int pct, int max, int lines)
{
    for (int n = 0; *s && n < lines; n++) {
        int len = (int)strlen(s);
        if (len > max && n < lines - 1) {
            len = max;
            while (len > 0 && s[len] != ' ')
                len--;
            if (len == 0)
                len = max;
        }
        char buf[128];
        SDL_strlcpy(buf, s, SDL_min((size_t)len + 1, sizeof buf));
        text(u, x, y + n * u->l->line, buf, pct, max);
        s += len;
        while (*s == ' ')
            s++;
    }
}

static void fill(const Ui *u, float x, float y, float w, float h, int pct, uint8_t alpha)
{
    ink(u, pct, alpha);
    SDL_RenderFillRect(u->r, &(SDL_FRect){u->x + x, u->y + y, w, h});
}

static void separator(const Ui *u, float y)
{
    fill(u, u->l->margin, y, u->l->w - 2 * u->l->margin, 1, 35, 255);
}

/* The bar behind the selected line of text at `y`. */
static void highlight(const Ui *u, float y, float h)
{
    float x = u->l->margin - SDL_min(u->l->margin - 2, 6), top = y - SDL_floorf((h - GLYPH) / 2);
    fill(u, x, top, u->l->w - 2 * x, h, 100, 40);
    fill(u, x, top, 3, h, 100, 255);
}

static void draw_rows(const Options *o, const Ui *u, int difficulty, float y)
{
    const Layout *l = u->l;
    bool mapping = o->mapping >= 0;
    for (int i = 0; i < ROW_COUNT; i++, y += l->row_h) {
        bool on = i == o->list.sel && !mapping;
        if (on)
            highlight(u, y, l->row_h);
        text(u, l->margin, y, row_labels[i], on ? 100 : 65, l->cut_label);
        if (has_value(i)) {
            char v[32], value[48];
            row_value(i, difficulty, v, sizeof v);
            SDL_snprintf(value, sizeof value, on ? "<  %s  >" : "   %s", v);
            text(u, l->col_value, y, value, on ? 100 : 65, l->cut_value);
        } else if (i == ROW_MAP && !input_pad_connected()) {
            text(u, l->col_value, y, l->compact ? "no controller" : "no controller connected", 45, l->cut_value);
        }
    }
}

static void draw_controls(const Options *o, const Ui *u, float y)
{
    const Layout *l = u->l;
    if (l->header) {
        text(u, l->margin, y, "Action", 55, l->cut_action);
        if (l->col_keys)
            text(u, l->col_keys, y, "Keyboard", 55, l->cut_keys);
        text(u, l->col_pad, y, "Controller", 55, l->cut_pad);
        y += l->line + 4;
    }
    for (int i = 0; i < OPTIONS_MAPPED; i++, y += l->line) {
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
                highlight(u, y, l->line);
                SDL_strlcpy(pad, (o->frames / 12) & 1 ? "" : l->compact ? "> press <" : "> press a button <", sizeof pad);
                pct = 100;
            } else {
                pct = 45;
            }
        }
        text(u, l->margin, y, mapped[i].label, pct, l->cut_action);
        if (l->col_keys)
            text(u, l->col_keys, y, keys, pct, l->cut_keys);
        text(u, l->col_pad, y, pad, i == o->mapping ? 100 : pct, l->cut_pad);
    }
}

/* The first controller input of an action, e.g. "A". */
static void first_pad_name(Action a, char *buf, size_t size)
{
    input_describe_pad(a, buf, size);
    char *comma = strchr(buf, ',');
    if (comma)
        *comma = '\0';
}

/* The hints at the bottom, for the keyboard or, without one, the controller. */
static void footer_hint(const Options *o, const Layout *l, char *buf, size_t size)
{
    if (SDL_HasKeyboard()) {
        if (o->mapping >= 0)
            SDL_strlcpy(buf, l->compact ? "Press a button.  Esc: cancel" : "Press a button or push a stick.  Esc: cancel", size);
        else
            SDL_strlcpy(buf, l->compact ? "Arrows move/change  Enter  Esc back" : "Up/Down move  Left/Right change  Enter select  Esc back", size);
        return;
    }
    if (o->mapping >= 0) {
        PadInput back = {false, SDL_GAMEPAD_BUTTON_BACK, 0};
        SDL_snprintf(buf, size, "Press a button.  Hold %s: cancel", input_pad_name(&back));
        return;
    }
    char ok[32], back[32];
    first_pad_name(ACT_ACTION, ok, sizeof ok);
    first_pad_name(ACT_MENU, back, sizeof back);
    SDL_snprintf(buf, size, "%s: select  %s: back", ok, back);
}

/* Over the game view: the roomy layout where it fits, else the compact one. */
void options_draw(const Options *o, int difficulty, SDL_Renderer *r, SDL_Color color, uint8_t bright)
{
    SDL_FRect area;
    video_view_rect(&area);
    if (area.w <= 0 || area.h <= 0)
        return;
    const Layout *l = &full;
    if (area.w < full.w + 60 || area.h < full.h + 30)
        l = SDL_HasKeyboard() ? &compact_keys : &compact_pad;
    /* Whole interface pixels, as large as still fits the panel (the roomy one with some room
     * around it). */
    float room_w = l->compact ? 0 : 60, room_h = l->compact ? 0 : 30;
    int scale = SDL_max(1, (int)SDL_min(area.w / (l->w + room_w), area.h / (l->h + room_h)));
    SDL_SetRenderScale(r, (float)scale, (float)scale);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    Ui u = {r, color, bright, SDL_floorf(area.x / scale + (area.w / scale - l->w) / 2.0f),
            SDL_floorf(area.y / scale + (area.h / scale - l->h) / 2.0f), l};

    SDL_SetRenderDrawColor(r, 10, 10, 14, (Uint8)(235 * bright / 255));
    SDL_RenderFillRect(r, &(SDL_FRect){u.x, u.y, l->w, l->h});
    ink(&u, 80, 255);
    SDL_RenderRect(r, &(SDL_FRect){u.x, u.y, l->w, l->h});

    /* Title, twice the size in the roomy layout. */
    float top = l->compact ? 5 : l->margin;
    SDL_SetRenderScale(r, (float)(scale * l->title_scale), (float)(scale * l->title_scale));
    ink(&u, 100, 255);
    SDL_RenderDebugText(r, (u.x + l->margin) / l->title_scale, (u.y + top) / l->title_scale, "OPTIONS");
    SDL_SetRenderScale(r, (float)scale, (float)scale);

    float y = top + l->title_scale * GLYPH + l->gap_title;
    separator(&u, y);
    y += l->gap_rows;
    draw_rows(o, &u, difficulty, y);
    y += ROW_COUNT * l->row_h + l->gap_help;

    /* The compact layout shows messages here, where they have two lines. */
    char help[96];
    bool message = o->message_frames > 0 && l->compact;
    if (message)
        SDL_strlcpy(help, o->message, sizeof help);
    else if (o->mapping >= 0)
        SDL_snprintf(help, sizeof help, "Mapping %d of %d: %s", o->mapping + 1, OPTIONS_MAPPED, mapped[o->mapping].label);
    else
        SDL_strlcpy(help, o->list.sel == ROW_DIFFICULTY ? difficulty_help[difficulty] : row_help[o->list.sel], sizeof help);
    text_wrapped(&u, l->margin, y, help, message ? 100 : 70, l->cut_text, l->help_lines);
    y += l->help_lines * l->line + l->gap_sep_before;

    separator(&u, y);
    y += l->gap_sep_after;
    draw_controls(o, &u, y);
    y += (l->header ? l->line + 4 : 0) + OPTIONS_MAPPED * l->line + l->gap_sep_before;

    separator(&u, y);
    y += l->gap_sep_after;
    char hint[96];
    footer_hint(o, l, hint, sizeof hint);
    bool footer_message = o->message_frames > 0 && !l->compact;
    text(&u, l->margin, y, footer_message ? o->message : hint, footer_message ? 100 : 55, l->cut_text);

    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
