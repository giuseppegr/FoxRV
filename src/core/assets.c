#include "core/assets.h"

#include <SDL3/SDL.h>

#include "core/config.h"
#include "core/sqz.h"

#define DS_IMAGE_OFFSET 0x68F0      /* DS = segment 0x68F of the load image */

static char root[PATH_MAX_LEN];
static char error[PATH_MAX_LEN + 256];
static uint8_t ds[ASSETS_DS_SIZE];

/* Full path of `name` inside `dir`, matched ignoring case (a DOS disk may be in capitals). */
static bool find_file(const char *dir, const char *name, char *out, size_t cap)
{
    int count = 0;
    char **found = SDL_GlobDirectory(dir, name, SDL_GLOB_CASEINSENSITIVE, &count);
    bool ok = found && count > 0;
    if (ok)
        SDL_snprintf(out, cap, "%s/%s", dir, found[0]);
    SDL_free(found);
    return ok;
}

static bool is_absolute(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

static bool unpack_data_segment(const char *path)
{
    size_t size, len;
    uint8_t *com = SDL_LoadFile(path, &size);
    uint8_t *exe = com ? fox_unpack(com, size, &len) : NULL;
    SDL_free(com);
    size_t image = exe ? (size_t)(exe[8] | exe[9] << 8) * 16 : 0;    /* header paragraphs */
    if (!exe || len <= image + DS_IMAGE_OFFSET) {
        SDL_snprintf(error, sizeof error, "%s is not a FOX.COM this engine can read "
                     "(it should be the 1992 DOS release of Titus the Fox).", path);
        SDL_free(exe);
        return false;
    }
    size_t n = SDL_min(len - image - DS_IMAGE_OFFSET, (size_t)ASSETS_DS_SIZE);
    SDL_memset(ds, 0, sizeof ds);
    SDL_memcpy(ds, exe + image + DS_IMAGE_OFFSET, n);
    SDL_free(exe);
    return true;
}

bool assets_init(const char *dir)
{
    const char *base = SDL_GetBasePath();     /* ends with a separator; owned by SDL */
    char candidates[2][PATH_MAX_LEN];
    int n = 0;
    if (base && !is_absolute(dir))
        SDL_snprintf(candidates[n++], PATH_MAX_LEN, "%s%s", base, dir);
    SDL_strlcpy(candidates[n++], dir, PATH_MAX_LEN);

    char fox[PATH_MAX_LEN * 2];
    for (int i = 0; i < n; i++) {
        if (!find_file(candidates[i], "fox.com", fox, sizeof fox))
            continue;
        SDL_strlcpy(root, candidates[i], sizeof root);
        Uint64 t0 = SDL_GetTicksNS();
        if (!unpack_data_segment(fox)) {
            SDL_Log("assets: %s", error);
            return false;
        }
        SDL_Log("assets: game files in %s (FOX.COM unpacked in %.1f ms)", root,
                (double)(SDL_GetTicksNS() - t0) / 1e6);
        return true;
    }
    SDL_snprintf(error, sizeof error, "The files of Titus the Fox were not found.\n\n"
                 "Copy FOX.COM and the .SQZ files of the original DOS game into:\n%s", candidates[0]);
    SDL_Log("assets: no FOX.COM in %s%s%s", candidates[0], n > 1 ? " or " : "", n > 1 ? candidates[1] : "");
    return false;
}

const char *assets_error(void) { return error; }
const char *assets_dir(void) { return root; }
const uint8_t *assets_data_segment(void) { return ds; }

uint8_t *assets_load_sqz(const char *name, size_t *size)
{
    char path[PATH_MAX_LEN * 2];
    if (!find_file(root, name, path, sizeof path)) {
        SDL_Log("assets: %s is missing from %s", name, root);
        return NULL;
    }
    size_t raw_size;
    uint8_t *raw = SDL_LoadFile(path, &raw_size);
    if (!raw) {
        SDL_Log("assets: can't read %s: %s", path, SDL_GetError());
        return NULL;
    }
    uint8_t *data = sqz_decode(raw, raw_size, size);
    SDL_free(raw);
    if (!data)
        SDL_Log("assets: %s is not a valid SQZ file", path);
    return data;
}
