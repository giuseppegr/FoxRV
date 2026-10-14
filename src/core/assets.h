/* The original game's files: FOX.COM and the .SQZ data files of Titus the Fox (DOS), in a
 * folder next to the executable. Nothing is extracted to disk: files are decoded in memory
 * when needed, and FOX.COM's data segment is unpacked once at startup.
 */
#ifndef FOXRV_ASSETS_H
#define FOXRV_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ASSETS_DS_SIZE 0x10000

/* Finds the game folder: `dir` next to the executable, then relative to the working
 * directory (or `dir` itself if absolute). It must hold FOX.COM; file names are matched
 * ignoring case. Unpacks FOX.COM. On failure, assets_error() says what's wrong. */
bool assets_init(const char *dir);
const char *assets_error(void);
const char *assets_dir(void);

/* A game file (e.g. "level0.sqz") decoded, or NULL with an error logged. Free with SDL_free. */
uint8_t *assets_load_sqz(const char *name, size_t *size);

/* FOX.COM's data segment (DS = 0x68F), zero-padded to 64 KB. Valid after assets_init. */
const uint8_t *assets_data_segment(void);

#endif
