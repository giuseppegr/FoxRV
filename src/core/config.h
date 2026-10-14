/* Engine settings, loaded from engine.ini.
 *
 * Every value has a compiled-in default, so a missing file or key is never an error. Sections
 * that belong to other modules (e.g. [input]) are forwarded to them through config_load's
 * handler hook.
 */
#ifndef FOXRV_CONFIG_H
#define FOXRV_CONFIG_H

#include <stdbool.h>

#define PATH_MAX_LEN 512

typedef struct {
    /* [video] */
    int scale;              /* initial window height = scale x min_view_height */
    bool fullscreen;
    bool vsync;
    int max_fps;            /* frame-rate cap; 0 = none (vsync alone paces the frames) */
    float aspect;           /* forced aspect of the game view; 0 = fill the window */
    int min_view_height;    /* fewest game pixels shown vertically; sets the pixel scale */
    int upscale;            /* art smoothing at load: 1 = none, 2..4; 0 = auto */

    /* [audio] */
    int master_volume;      /* 0..100 */
    int music_volume;       /* 0..100 */
    int sfx_volume;         /* 0..100 */
    int music_buffer_ms;    /* music rendered ahead of playback */
    double tick_hz;         /* music driver tick rate; 72.826 = PIT divisor 0x4000 */

    /* [engine] */
    double logic_hz;        /* fixed game-logic rate; 36.41 = original, other values = game speed */
    char game_dir[PATH_MAX_LEN];    /* the original game's files, next to the executable */
} Settings;

/* Called for keys in sections Settings doesn't own. Return false to report an unknown key. */
typedef bool (*ConfigHandler)(const char *section, const char *key, const char *value);

void settings_defaults(Settings *s);

/* Loads `path` over the current values. Returns false if the file couldn't be read. The
 * file read becomes the one config_save_value writes to. */
bool config_load(Settings *s, const char *path, ConfigHandler other);

/* Where config_save_value writes when no file was loaded. */
void config_set_save_path(const char *path);
/* Writes one setting into the ini file, keeping everything else: the key's line gets the new
 * value (its comment stays), or the key is added to its section, or the section to the end. */
bool config_save_value(const char *section, const char *key, const char *value);

#endif
