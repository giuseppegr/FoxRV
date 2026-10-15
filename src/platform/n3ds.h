/* Nintendo 3DS: where the settings live, and errors before there's a window.
 *
 * The 3DS build reads its defaults from romfs:/engine.ini (inside the .3dsx), which is
 * read-only; on first run it copies them to the SD card, where Options can save changes. The
 * original game's files go on the SD card too (FOXRV_3DS_DIR "game/").
 */
#ifndef FOXRV_N3DS_H
#define FOXRV_N3DS_H

#define FOXRV_3DS_DIR "sdmc:/3ds/foxrv/"

/* FOXRV_3DS_DIR "engine.ini", made from the built-in defaults if it isn't there yet. */
const char *n3ds_config_path(void);

/* Shows `message` on the bottom screen until START is pressed. Before SDL_Init only. */
void n3ds_fatal(const char *message);

#endif
