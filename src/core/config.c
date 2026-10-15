#include "core/config.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define PIT_HZ 1193182.0

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->scale = 3;
    s->fullscreen = false;
    s->vsync = true;
    s->max_fps = 0;
    s->aspect = 0.0f;
    s->min_view_height = 240;
    s->upscale = 0;
    s->audio_device = FOXRV_WITH_OPL ? AUDIO_ADLIB : AUDIO_SPEAKER;
    s->master_volume = 80;
    s->music_volume = 100;
    s->sfx_volume = 100;
    s->music_buffer_ms = 2000;
    s->tick_hz = PIT_HZ / 0x4000;
    s->logic_hz = PIT_HZ / 0x4000 / 2;    /* the original: one game frame every 2 ticks */
    SDL_strlcpy(s->game_dir, "game", sizeof s->game_dir);
}

static char *trim(char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    char *end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return p;
}

static bool parse_bool(const char *v)
{
    return !SDL_strcasecmp(v, "1") || !SDL_strcasecmp(v, "true") || !SDL_strcasecmp(v, "yes") ||
           !SDL_strcasecmp(v, "on");
}

/* "16:9", "1.777" or "auto" (0). */
static float parse_aspect(const char *v)
{
    if (!SDL_strcasecmp(v, "auto"))
        return 0.0f;
    const char *colon = strchr(v, ':');
    if (colon) {
        float den = (float)atof(colon + 1);
        return den > 0 ? (float)atof(v) / den : 0.0f;
    }
    return (float)atof(v);
}

/* "320x200", or "auto" (0 x 0). */
static void parse_size(const char *v, int *w, int *h)
{
    const char *x = strpbrk(v, "xX");
    if (SDL_strcasecmp(v, "auto") && x) {
        *w = SDL_clamp(atoi(v), 320, 1920);
        *h = SDL_clamp(atoi(x + 1), 200, 1080);
    } else {
        *w = *h = 0;
    }
}

static bool apply(Settings *s, const char *sec, const char *key, const char *val)
{
    if (!SDL_strcasecmp(sec, "video")) {
        if (!SDL_strcasecmp(key, "scale")) s->scale = SDL_max(1, atoi(val));
        else if (!SDL_strcasecmp(key, "fullscreen")) s->fullscreen = parse_bool(val);
        else if (!SDL_strcasecmp(key, "vsync")) s->vsync = parse_bool(val);
        else if (!SDL_strcasecmp(key, "max_fps")) s->max_fps = SDL_clamp(atoi(val), 0, 1000);
        else if (!SDL_strcasecmp(key, "aspect")) s->aspect = parse_aspect(val);
        else if (!SDL_strcasecmp(key, "min_view_height")) s->min_view_height = SDL_clamp(atoi(val), 200, 1080);
        else if (!SDL_strcasecmp(key, "view_size")) parse_size(val, &s->view_w, &s->view_h);
        else if (!SDL_strcasecmp(key, "upscale")) s->upscale = !SDL_strcasecmp(val, "auto") ? 0 : SDL_clamp(atoi(val), 1, 4);
        else return false;
    } else if (!SDL_strcasecmp(sec, "audio")) {
        if (!SDL_strcasecmp(key, "device")) s->audio_device = FOXRV_WITH_OPL && !SDL_strcasecmp(val, "adlib") ? AUDIO_ADLIB : AUDIO_SPEAKER;
        else if (!SDL_strcasecmp(key, "master_volume")) s->master_volume = SDL_clamp(atoi(val), 0, 100);
        else if (!SDL_strcasecmp(key, "music_volume")) s->music_volume = SDL_clamp(atoi(val), 0, 100);
        else if (!SDL_strcasecmp(key, "sfx_volume")) s->sfx_volume = SDL_clamp(atoi(val), 0, 100);
        else if (!SDL_strcasecmp(key, "music_buffer_ms")) s->music_buffer_ms = SDL_clamp(atoi(val), 100, 30000);
        else if (!SDL_strcasecmp(key, "tick_hz")) s->tick_hz = SDL_clamp(atof(val), 10.0, 500.0);
        else return false;
    } else if (!SDL_strcasecmp(sec, "engine")) {
        if (!SDL_strcasecmp(key, "logic_hz")) s->logic_hz = SDL_clamp(atof(val), 10.0, 500.0);
        else if (!SDL_strcasecmp(key, "game_dir")) SDL_strlcpy(s->game_dir, val, sizeof s->game_dir);
        else return false;
    } else {
        return false;
    }
    return true;
}

static char save_path[PATH_MAX_LEN];

void config_set_save_path(const char *path)
{
    SDL_strlcpy(save_path, path, sizeof save_path);
}

bool config_load(Settings *s, const char *path, ConfigHandler other)
{
    size_t size;
    char *text = SDL_LoadFile(path, &size);
    if (!text)
        return false;
    config_set_save_path(path);

    char section[64] = "";
    int lineno = 0;
    for (char *line = text, *next; line; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        lineno++;
        char *p = trim(line);
        if (*p == '\0' || *p == ';' || *p == '#')
            continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                SDL_strlcpy(section, trim(p + 1), sizeof section);
            }
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) {
            SDL_Log("%s:%d: expected key = value", path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = trim(p), *val = trim(eq + 1);
        char *comment = strpbrk(val, ";#");
        if (comment) {
            *comment = '\0';
            val = trim(val);
        }
        if (!apply(s, section, key, val) && !(other && other(section, key, val)))
            SDL_Log("%s:%d: unknown setting [%s] %s", path, lineno, section, key);
    }
    SDL_free(text);
    return true;
}

/* --- saving ------------------------------------------------------------------------------- */

typedef struct {
    char *data;
    size_t len, cap;
    bool ok;
} Buf;

static void put(Buf *b, const char *s, size_t n)
{
    if (!b->ok)
        return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *d = SDL_realloc(b->data, cap);
        if (!d) {
            b->ok = false;
            return;
        }
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void puts_(Buf *b, const char *s) { put(b, s, strlen(s)); }

/* "  key = value   ; comment" -> the same line with the new value; the comment keeps its
 * column when the value fits. */
static void put_replaced(Buf *b, const char *line, size_t len, const char *value)
{
    const char *eq = memchr(line, '=', len);
    size_t head = (size_t)(eq - line) + 1;
    put(b, line, head);
    puts_(b, " ");
    puts_(b, value);
    const char *comment = NULL;
    for (const char *p = eq + 1; p < line + len; p++) {
        if (*p == ';' || *p == '#') {
            comment = p;
            break;
        }
    }
    if (!comment)
        return;
    size_t used = head + 1 + strlen(value), column = (size_t)(comment - line);
    for (size_t i = used; i < column; i++)
        puts_(b, " ");
    if (used >= column)
        puts_(b, " ");
    put(b, comment, (size_t)(line + len - comment));
}

/* Whether `line` (no newline) sets `key`; *section is updated by section headers. */
static bool line_sets(const char *line, size_t len, char *section, size_t section_size, const char *key)
{
    char tmp[512];
    SDL_strlcpy(tmp, line, SDL_min(len + 1, sizeof tmp));
    char *p = trim(tmp);
    if (*p == '[') {
        char *end = strchr(p, ']');
        if (end) {
            *end = '\0';
            SDL_strlcpy(section, trim(p + 1), section_size);
        }
        return false;
    }
    if (*p == ';' || *p == '#')
        return false;
    char *eq = strchr(p, '=');
    if (!eq)
        return false;
    *eq = '\0';
    return !SDL_strcasecmp(trim(p), key);
}

bool config_save_value(const char *section, const char *key, const char *value)
{
    if (!save_path[0])
        return false;
    size_t size = 0;
    char *text = SDL_LoadFile(save_path, &size);   /* a missing file starts empty */
    const char *src = text ? text : "";
    const char *eol = strstr(src, "\r\n") ? "\r\n" : "\n";

    Buf out = {.ok = true};
    char cur[64] = "";
    bool done = false;
    size_t insert_len = 0;          /* output offset after the section's last non-blank line */
    for (const char *line = src; *line;) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        size_t text_len = len > 0 && line[len - 1] == '\r' ? len - 1 : len;
        bool sets = line_sets(line, text_len, cur, sizeof cur, key);
        bool in_section = !SDL_strcasecmp(cur, section);
        if (!done && sets && in_section) {
            put_replaced(&out, line, text_len, value);
            put(&out, line + text_len, (nl ? len + 1 : len) - text_len);
            done = true;
        } else {
            put(&out, line, nl ? len + 1 : len);
            const char *t = line;
            while (t < line + text_len && (*t == ' ' || *t == '\t'))
                t++;
            if (!done && in_section && t < line + text_len)
                insert_len = out.len;
        }
        line = nl ? nl + 1 : line + len;
    }

    char added[512];
    SDL_snprintf(added, sizeof added, "%s = %s%s", key, value, eol);
    if (!done && insert_len > 0) {
        /* the section exists: insert after its last non-blank line */
        Buf merged = {.ok = true};
        put(&merged, out.data, insert_len);
        if (insert_len > 0 && out.data[insert_len - 1] != '\n')
            puts_(&merged, eol);
        puts_(&merged, added);
        put(&merged, out.data + insert_len, out.len - insert_len);
        SDL_free(out.data);
        out = merged;
    } else if (!done) {
        if (out.len > 0 && out.data[out.len - 1] != '\n')
            puts_(&out, eol);
        char header[96];
        SDL_snprintf(header, sizeof header, "%s[%s]%s", out.len > 0 ? eol : "", section, eol);
        puts_(&out, header);
        puts_(&out, added);
    }
    SDL_free(text);

    bool ok = out.ok && SDL_SaveFile(save_path, out.data ? out.data : "", out.len);
    if (!ok)
        SDL_Log("config: can't save %s: %s", save_path, SDL_GetError());
    SDL_free(out.data);
    return ok;
}
