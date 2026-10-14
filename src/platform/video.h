/* Window, GPU renderer and the widescreen game view.
 *
 * Game pixels are square and drawn at a whole-number scale (k x k screen pixels each), so the
 * picture is never stretched or filtered. k is the largest scale that still shows at least
 * `min_view_height` game pixels vertically; the view then covers the whole window, so a 16:9
 * window shows more of the level than the original 320x200 screen: 427x240 at 1280x720,
 * 480x270 at 1920x1080. Leftover pixels (less than k) become a thin black border.
 */
#ifndef FOXRV_VIDEO_H
#define FOXRV_VIDEO_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "core/config.h"

#define VIEW_MIN_W 320
#define VIEW_MIN_H 200
#define VIEW_MAX_W 1920
#define VIEW_MAX_H 1080

bool video_init(const Settings *s, const char *title);
void video_shutdown(void);

SDL_Renderer *video_renderer(void);

/* Size of the game view in game pixels; can change between frames when the window does. */
int video_view_w(void);
int video_view_h(void);

/* begin: adapts the view to the current window size, targets it and clears it.
 * end: scales the view to the window and presents. */
void video_begin_frame(void);
void video_end_frame(void);

/* 0 = black, 255 = full brightness. Applied to the whole frame when presenting. */
void video_set_fade(uint8_t level);
void video_toggle_fullscreen(void);

/* Saves the next presented frame (window resolution) as a BMP. */
void video_capture_next(const char *path);

#endif
