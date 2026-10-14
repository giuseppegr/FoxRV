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
    s->master_volume = 80;
    s->music_volume = 100;
    s->sfx_volume = 100;
    s->music_buffer_ms = 2000;
    s->tick_hz = PIT_HZ / 0x4000;
    s->logic_hz = PIT_HZ / 0x4000 / 2;    /* the original: one game frame every 2 ticks */
    SDL_strlcpy(s->export_dir, "export", sizeof s->export_dir);
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

static bool apply(Settings *s, const char *sec, const char *key, const char *val)
{
    if (!SDL_strcasecmp(sec, "video")) {
        if (!SDL_strcasecmp(key, "scale")) s->scale = SDL_max(1, atoi(val));
        else if (!SDL_strcasecmp(key, "fullscreen")) s->fullscreen = parse_bool(val);
        else if (!SDL_strcasecmp(key, "vsync")) s->vsync = parse_bool(val);
        else if (!SDL_strcasecmp(key, "max_fps")) s->max_fps = SDL_clamp(atoi(val), 0, 1000);
        else if (!SDL_strcasecmp(key, "aspect")) s->aspect = parse_aspect(val);
        else if (!SDL_strcasecmp(key, "min_view_height")) s->min_view_height = SDL_clamp(atoi(val), 200, 1080);
        else if (!SDL_strcasecmp(key, "upscale")) s->upscale = !SDL_strcasecmp(val, "auto") ? 0 : SDL_clamp(atoi(val), 1, 4);
        else return false;
    } else if (!SDL_strcasecmp(sec, "audio")) {
        if (!SDL_strcasecmp(key, "master_volume")) s->master_volume = SDL_clamp(atoi(val), 0, 100);
        else if (!SDL_strcasecmp(key, "music_volume")) s->music_volume = SDL_clamp(atoi(val), 0, 100);
        else if (!SDL_strcasecmp(key, "sfx_volume")) s->sfx_volume = SDL_clamp(atoi(val), 0, 100);
        else if (!SDL_strcasecmp(key, "music_buffer_ms")) s->music_buffer_ms = SDL_clamp(atoi(val), 100, 30000);
        else if (!SDL_strcasecmp(key, "tick_hz")) s->tick_hz = SDL_clamp(atof(val), 10.0, 500.0);
        else return false;
    } else if (!SDL_strcasecmp(sec, "engine")) {
        if (!SDL_strcasecmp(key, "logic_hz")) s->logic_hz = SDL_clamp(atof(val), 10.0, 500.0);
        else if (!SDL_strcasecmp(key, "export_dir")) SDL_strlcpy(s->export_dir, val, sizeof s->export_dir);
        else return false;
    } else {
        return false;
    }
    return true;
}

bool config_load(Settings *s, const char *path, ConfigHandler other)
{
    size_t size;
    char *text = SDL_LoadFile(path, &size);
    if (!text)
        return false;

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
