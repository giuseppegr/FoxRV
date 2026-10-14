#include "core/assets.h"

#include <SDL3/SDL.h>

#include "core/config.h"

static char root[PATH_MAX_LEN];

static bool has_export(const char *dir)
{
    char probe[PATH_MAX_LEN + 32];
    SDL_snprintf(probe, sizeof probe, "%s/levels/level0.json", dir);
    return SDL_GetPathInfo(probe, NULL);
}

bool assets_init(const char *hint)
{
    const char *base = SDL_GetBasePath();     /* ends with a separator; owned by SDL */
    char candidates[4][PATH_MAX_LEN];
    int n = 0;
    SDL_strlcpy(candidates[n++], hint, PATH_MAX_LEN);
    if (base) {
        SDL_snprintf(candidates[n++], PATH_MAX_LEN, "%s%s", base, hint);
        SDL_snprintf(candidates[n++], PATH_MAX_LEN, "%s../%s", base, hint);
    }
    for (int i = 0; i < n; i++) {
        if (has_export(candidates[i])) {
            SDL_strlcpy(root, candidates[i], sizeof root);
            SDL_Log("assets: using %s", root);
            return true;
        }
    }
    SDL_Log("assets: no export directory found for '%s' (run the tools/export_*.py scripts)", hint);
    return false;
}

const char *assets_path(const char *rel)
{
    static char bufs[4][PATH_MAX_LEN * 2];
    static int next;
    char *buf = bufs[next++ & 3];
    SDL_snprintf(buf, sizeof bufs[0], "%s/%s", root, rel);
    return buf;
}

void *assets_load(const char *rel, size_t *size)
{
    const char *path = assets_path(rel);
    void *data = SDL_LoadFile(path, size);
    if (!data)
        SDL_Log("assets: can't read %s: %s", path, SDL_GetError());
    return data;
}

cJSON *assets_load_json(const char *rel)
{
    char *text = assets_load(rel, NULL);
    if (!text)
        return NULL;
    cJSON *json = cJSON_Parse(text);
    if (!json) {
        const char *at = cJSON_GetErrorPtr();
        SDL_Log("assets: %s is not valid JSON (near '%.20s')", rel, at ? at : "?");
    }
    SDL_free(text);
    return json;
}

int json_int(const cJSON *obj, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}
