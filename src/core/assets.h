/* Locating and reading the extracted game data (the export/ tree written by tools/). */
#ifndef FOXRV_ASSETS_H
#define FOXRV_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

/* Finds the export directory: `hint` as given, then relative to the working directory and
 * to the executable (and its parent). Returns false if none contains levels/level0.json. */
bool assets_init(const char *hint);

/* Full path of a file inside the export directory. The result is valid until the 4th next
 * call. */
const char *assets_path(const char *rel);

/* Whole file in a NUL-terminated buffer (free with SDL_free), or NULL with an error logged. */
void *assets_load(const char *rel, size_t *size);

/* Parsed JSON (free with cJSON_Delete), or NULL with an error logged. */
cJSON *assets_load_json(const char *rel);

/* JSON helpers: the number at `key`, or `fallback` if it's missing or not a number. */
int json_int(const cJSON *obj, const char *key, int fallback);

#endif
