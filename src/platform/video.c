#include "platform/video.h"

#include <math.h>

#include "platform/gfx.h"
#include "platform/icon_pixels.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *view;       /* view_w x view_h game pixels, view_res texels each */
static int view_w, view_h, view_res;
static SDL_FRect dst;           /* where the view lands in the window */
static uint8_t fade = 255;
static float cfg_aspect;
static int cfg_min_view_h;
static int cfg_view_w, cfg_view_h;     /* fixed view size; 0 = fill the window */
static char capture_path[PATH_MAX_LEN];

/* View size in game pixels, and the whole-number pixel scale k, for a pw x ph output. */
static void layout(int pw, int ph, int *view_wp, int *view_hp, int *kp)
{
    /* Area the view may cover: the whole window, or the largest rect of a forced aspect. */
    int aw = pw, ah = ph;
    if (cfg_aspect > 0) {
        if ((float)pw / (float)ph > cfg_aspect)
            aw = (int)lroundf(ph * cfg_aspect);
        else
            ah = (int)lroundf(pw / cfg_aspect);
    }

    if (cfg_view_w > 0) {
        *view_wp = cfg_view_w;
        *view_hp = cfg_view_h;
        *kp = SDL_max(1, SDL_min(aw / cfg_view_w, ah / cfg_view_h));
        return;
    }
    int k = SDL_max(1, ah / cfg_min_view_h);
    while (k > 1 && aw / k < VIEW_MIN_W)
        k--;
    *view_wp = SDL_clamp(aw / k, VIEW_MIN_W, VIEW_MAX_W);
    *view_hp = SDL_clamp(ah / k, VIEW_MIN_H, VIEW_MAX_H);
    *kp = k;
}

/* Picks the pixel scale and view size for the current output size; recreates the view
 * texture when its size changes. */
static void adapt_to_window(void)
{
    int pw, ph;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &pw, &ph) || pw <= 0 || ph <= 0)
        return;
    int w, h, k;
    layout(pw, ph, &w, &h, &k);

    dst.w = (float)(w * k);
    dst.h = (float)(h * k);
    dst.x = floorf((pw - dst.w) / 2);
    dst.y = floorf((ph - dst.h) / 2);

    /* Upscaled art keeps its detail: each game pixel gets as many texels as the art has,
     * but never more than it will cover on screen. */
    int res = SDL_min(gfx_upscale(), k);
    if (!view || w != view_w || h != view_h || res != view_res) {
        SDL_DestroyTexture(view);
        view = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w * res, h * res);
        if (!view) {
            SDL_Log("video: can't create %dx%d view: %s", w * res, h * res, SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(view, SDL_SCALEMODE_NEAREST);
        view_w = w;
        view_h = h;
        view_res = res;
    }
}

/* upscale = auto: the art factor for the pixel scale at startup; one that divides it, so
 * every smoothed step is the same size on screen, else the largest. */
static int auto_upscale(void)
{
    int pw, ph;
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(display);
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && mode) {
        pw = (int)lroundf(mode->w * mode->pixel_density);
        ph = (int)lroundf(mode->h * mode->pixel_density);
    } else if (!SDL_GetCurrentRenderOutputSize(renderer, &pw, &ph) || pw <= 0 || ph <= 0) {
        return 1;
    }
    int w, h, k;
    layout(pw, ph, &w, &h, &k);
    if (k <= 4)
        return k;
    for (int f = 4; f >= 2; f--)
        if (k % f == 0)
            return f;
    return 4;
}

/* The FoxRV icon for the title bar and taskbar; the 16 px art is a variant for small sizes. */
static void set_icon(void)
{
    SDL_Surface *icon = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32, (void *)icon_32, 32 * 4);
    SDL_Surface *small = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_RGBA32, (void *)icon_16, 16 * 4);
    if (icon && small)
        SDL_AddSurfaceAlternateImage(icon, small);
    if (!icon || !SDL_SetWindowIcon(window, icon))
        SDL_Log("video: can't set the icon: %s", SDL_GetError());
    SDL_DestroySurface(small);
    SDL_DestroySurface(icon);
}

bool video_init(const Settings *s, const char *title)
{
    cfg_aspect = s->aspect;
    cfg_min_view_h = s->min_view_height;
    cfg_view_w = s->view_w;
    cfg_view_h = s->view_h;

#ifdef __3DS__
    /* The top screen. */
    int w = 400, h = 240;
    SDL_WindowFlags flags = 0;
#else
    /* w x h is also the window's size when leaving fullscreen. */
    int h = s->min_view_height * s->scale;
    int w = (int)lroundf(h * (s->aspect > 0 ? s->aspect : 16.0f / 9.0f));
    if (cfg_view_w > 0) {
        w = cfg_view_w * s->scale;
        h = cfg_view_h * s->scale;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (s->fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
#endif
    if (!SDL_CreateWindowAndRenderer(title, w, h, flags, &window, &renderer)) {
        SDL_Log("video: %s", SDL_GetError());
        return false;
    }
#ifndef __3DS__
    set_icon();
    SDL_SetWindowMinimumSize(window, VIEW_MIN_W, VIEW_MIN_H);
#endif
    SDL_SetRenderVSync(renderer, s->vsync ? 1 : 0);
    SDL_Log("video: renderer '%s', window %dx%d", SDL_GetRendererName(renderer), w, h);

    gfx_set_upscale(s->upscale > 0 ? s->upscale : auto_upscale());
    SDL_Log("video: art upscale x%d", gfx_upscale());
    adapt_to_window();
    return view != NULL;
}

void video_shutdown(void)
{
    SDL_DestroyTexture(view);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    view = NULL;
    renderer = NULL;
    window = NULL;
}

SDL_Renderer *video_renderer(void) { return renderer; }
int video_view_w(void) { return view_w; }
int video_view_h(void) { return view_h; }
void video_set_fade(uint8_t level) { fade = level; }

void video_toggle_fullscreen(void)
{
    bool on = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(window, !on);
}

void video_begin_frame(void)
{
    adapt_to_window();
    SDL_SetRenderTarget(renderer, view);
    SDL_SetRenderScale(renderer, (float)view_res, (float)view_res);     /* draw in game pixels */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void video_compose(void)
{
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetTextureColorMod(view, fade, fade, fade);
    SDL_RenderTexture(renderer, view, NULL, &dst);
}

void video_output_size(int *w, int *h)
{
    if (!SDL_GetCurrentRenderOutputSize(renderer, w, h))
        *w = *h = 0;
}

void video_view_rect(SDL_FRect *r) { *r = dst; }

int video_refresh_rate(void)
{
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    return mode && mode->refresh_rate >= 1.0f ? (int)lroundf(mode->refresh_rate) : 60;
}

void video_present(void)
{
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    if (capture_path[0]) {
        SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
        if (!shot || !SDL_SaveBMP(shot, capture_path))
            SDL_Log("video: capture failed: %s", SDL_GetError());
        SDL_DestroySurface(shot);
        capture_path[0] = '\0';
    }
    SDL_RenderPresent(renderer);
}

void video_capture_next(const char *path)
{
    SDL_strlcpy(capture_path, path, sizeof capture_path);
}
