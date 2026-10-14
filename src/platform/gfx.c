#include "platform/gfx.h"

#include <stdlib.h>
#include <string.h>

#include "core/assets.h"
#include "lodepng.h"

#define ATLAS_W 1024
#define ATLAS_PAD 1

static int upscale = 1;

void gfx_set_upscale(int factor) { upscale = SDL_clamp(factor, 1, 4); }
int gfx_upscale(void) { return upscale; }

/* --- Scale2x / Scale3x ------------------------------------------------------------------- */

/* One cell of an image, scaled on its own: a neighbour past its edge is the nearest edge
 * pixel (clamp) or `outside`. */
typedef struct {
    const uint32_t *px;
    int stride, x0, y0, w, h;
    bool clamp;
    uint32_t outside;
} Cell;

static uint32_t cell_at(const Cell *c, int x, int y)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) {
        if (!c->clamp)
            return c->outside;
        x = SDL_clamp(x, 0, c->w - 1);
        y = SDL_clamp(y, 0, c->h - 1);
    }
    return c->px[(size_t)(c->y0 + y) * c->stride + c->x0 + x];
}

/* AdvMAME2x / AdvMAME3x (the Scale2x/3x rules): an edge pixel takes its neighbours' colour
 * where they meet diagonally, so staircases turn into smooth slopes; no new colours appear. */
static void scale_cell(const Cell *c, int n, uint32_t *out, int out_stride)
{
    for (int y = 0; y < c->h; y++) {
        for (int x = 0; x < c->w; x++) {
            uint32_t A = cell_at(c, x - 1, y - 1), B = cell_at(c, x, y - 1), C = cell_at(c, x + 1, y - 1);
            uint32_t D = cell_at(c, x - 1, y), E = cell_at(c, x, y), F = cell_at(c, x + 1, y);
            uint32_t G = cell_at(c, x - 1, y + 1), H = cell_at(c, x, y + 1), I = cell_at(c, x + 1, y + 1);
            uint32_t e[9] = {E, E, E, E, E, E, E, E, E};
            if (B != H && D != F) {
                if (n == 2) {
                    e[0] = D == B ? D : E;
                    e[1] = B == F ? F : E;
                    e[2] = D == H ? D : E;
                    e[3] = H == F ? F : E;
                } else {
                    e[0] = D == B ? D : E;
                    e[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                    e[2] = B == F ? F : E;
                    e[3] = (D == B && E != G) || (D == H && E != A) ? D : E;
                    e[5] = (B == F && E != I) || (H == F && E != C) ? F : E;
                    e[6] = D == H ? D : E;
                    e[7] = (D == H && E != I) || (H == F && E != G) ? H : E;
                    e[8] = H == F ? F : E;
                }
            }
            uint32_t *o = out + (size_t)(c->y0 + y) * n * out_stride + (size_t)(c->x0 + x) * n;
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++)
                    o[(size_t)j * out_stride + i] = e[j * n + i];
        }
    }
}

/* One pass by n (2 or 3) of a w x h image cut into cw x ch cells. */
static uint32_t *scale_pass(const uint32_t *px, int w, int h, int cw, int ch, int n, bool clamp, uint32_t outside)
{
    uint32_t *out = SDL_malloc((size_t)w * n * h * n * sizeof *out);
    if (!out)
        return NULL;
    for (int y0 = 0; y0 < h; y0 += ch)
        for (int x0 = 0; x0 < w; x0 += cw) {
            Cell c = {px, w, x0, y0, SDL_min(cw, w - x0), SDL_min(ch, h - y0), clamp, outside};
            scale_cell(&c, n, out, w * n);
        }
    return out;
}

/* The image scaled by the current factor (4 = Scale2x twice); cw/ch of 0 = one cell. */
static uint32_t *scale_image(const uint32_t *px, int w, int h, int cw, int ch, bool clamp, uint32_t outside)
{
    cw = cw > 0 ? cw : w;
    ch = ch > 0 ? ch : h;
    if (upscale != 4)
        return scale_pass(px, w, h, cw, ch, upscale, clamp, outside);
    uint32_t *half = scale_pass(px, w, h, cw, ch, 2, clamp, outside);
    uint32_t *out = half ? scale_pass(half, w * 2, h * 2, cw * 2, ch * 2, 2, clamp, outside) : NULL;
    SDL_free(half);
    return out;
}

/* Index 0 past the edges when `transparent_edges` (a sprite), the edge pixels otherwise. */
static bool upscale_indexed(IndexedImage *img, int cw, int ch, bool transparent_edges)
{
    if (upscale == 1)
        return true;
    size_t n = (size_t)img->w * img->h;
    uint32_t *wide = SDL_malloc(n * sizeof *wide);
    if (!wide)
        return false;
    for (size_t i = 0; i < n; i++)
        wide[i] = img->px[i];
    uint32_t *big = scale_image(wide, img->w, img->h, cw, ch, !transparent_edges, 0);
    SDL_free(wide);
    uint8_t *px = big ? malloc(n * upscale * upscale) : NULL;   /* freed with free() */
    if (!px) {
        SDL_free(big);
        return false;
    }
    for (size_t i = 0; i < n * upscale * upscale; i++)
        px[i] = (uint8_t)big[i];
    SDL_free(big);
    free(img->px);
    img->px = px;
    img->w *= upscale;
    img->h *= upscale;
    return true;
}

static SDL_Surface *upscale_surface(SDL_Surface *s, int cw, int ch)
{
    uint32_t *px = SDL_malloc((size_t)s->w * s->h * sizeof *px);
    if (!px)
        return NULL;
    for (int y = 0; y < s->h; y++)
        memcpy(px + (size_t)y * s->w, (const uint8_t *)s->pixels + (size_t)y * s->pitch, (size_t)s->w * 4);
    uint32_t *big = scale_image(px, s->w, s->h, cw, ch, true, 0);
    SDL_free(px);
    SDL_Surface *out = big ? SDL_CreateSurface(s->w * upscale, s->h * upscale, SDL_PIXELFORMAT_RGBA32) : NULL;
    if (out)
        for (int y = 0; y < out->h; y++)
            memcpy((uint8_t *)out->pixels + (size_t)y * out->pitch, big + (size_t)y * out->w, (size_t)out->w * 4);
    SDL_free(big);
    return out;
}

/* --- images ------------------------------------------------------------------------------ */

bool gfx_load_indexed(const char *rel, IndexedImage *out)
{
    size_t size;
    unsigned char *file = assets_load(rel, &size);
    if (!file)
        return false;

    /* color_convert = 0 keeps the raw palette indices instead of expanding to RGBA. */
    LodePNGState st;
    lodepng_state_init(&st);
    st.decoder.color_convert = 0;
    unsigned char *px = NULL;
    unsigned w, h;
    unsigned err = lodepng_decode(&px, &w, &h, &st, file, size);
    bool indexed8 = st.info_png.color.colortype == LCT_PALETTE && st.info_png.color.bitdepth == 8;
    lodepng_state_cleanup(&st);
    SDL_free(file);

    if (err || !indexed8) {
        SDL_Log("gfx: %s: %s", rel, err ? lodepng_error_text(err) : "not an 8-bit indexed PNG");
        free(px);
        return false;
    }
    for (size_t i = 0; i < (size_t)w * h; i++)
        px[i] &= 0x0F;
    out->w = (int)w;
    out->h = (int)h;
    out->px = px;
    return true;
}

void gfx_free_indexed(IndexedImage *img)
{
    free(img->px);      /* allocated by lodepng (malloc) or atlas_load (calloc) */
    memset(img, 0, sizeof *img);
}

bool palette_from_vga6(Palette *pal, const cJSON *rgb_list)
{
    if (cJSON_GetArraySize(rgb_list) != 16)
        return false;
    for (int i = 0; i < 16; i++) {
        const cJSON *rgb = cJSON_GetArrayItem(rgb_list, i);
        uint8_t v[3];
        for (int k = 0; k < 3; k++) {
            int c = cJSON_GetArrayItem(rgb, k) ? cJSON_GetArrayItem(rgb, k)->valueint & 0x3F : 0;
            v[k] = (uint8_t)(c << 2 | c >> 4);     /* 6-bit DAC value to 8 bits */
        }
        pal->c[i] = (SDL_Color){v[0], v[1], v[2], 255};
    }
    return true;
}

SDL_Texture *gfx_bake(SDL_Renderer *r, const IndexedImage *img, const Palette *pal, bool transparent0)
{
    SDL_Color *rgba = SDL_malloc((size_t)img->w * img->h * sizeof *rgba);
    if (!rgba)
        return NULL;
    for (int i = 0; i < img->w * img->h; i++) {
        rgba[i] = pal->c[img->px[i]];
        if (transparent0 && img->px[i] == 0)
            rgba[i].a = 0;
    }
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img->w, img->h);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, rgba, img->w * (int)sizeof *rgba);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(tex, transparent0 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    } else {
        SDL_Log("gfx: can't create %dx%d texture: %s", img->w, img->h, SDL_GetError());
    }
    SDL_free(rgba);
    return tex;
}

SDL_Surface *gfx_load_surface(const char *rel)
{
    size_t size;
    unsigned char *file = assets_load(rel, &size);
    if (!file)
        return NULL;
    unsigned char *px = NULL;
    unsigned w, h;
    unsigned err = lodepng_decode32(&px, &w, &h, file, size);
    SDL_free(file);
    if (err) {
        SDL_Log("gfx: %s: %s", rel, lodepng_error_text(err));
        return NULL;
    }
    SDL_Surface *s = SDL_CreateSurface((int)w, (int)h, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        for (unsigned y = 0; y < h; y++)
            memcpy((uint8_t *)s->pixels + (size_t)y * s->pitch, px + (size_t)y * w * 4, (size_t)w * 4);
    } else {
        SDL_Log("gfx: %s: %s", rel, SDL_GetError());
    }
    free(px);
    return s;
}

SDL_Texture *gfx_texture(SDL_Renderer *r, SDL_Surface *s)
{
    return gfx_texture_cells(r, s, 0, 0);
}

SDL_Texture *gfx_texture_cells(SDL_Renderer *r, SDL_Surface *s, int cell_w, int cell_h)
{
    if (!s)
        return NULL;
    SDL_Surface *big = NULL;
    if (upscale > 1) {
        SDL_Surface *rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        big = rgba ? upscale_surface(rgba, cell_w, cell_h) : NULL;
        SDL_DestroySurface(rgba);
        if (!big) {
            SDL_Log("gfx: can't upscale a %dx%d picture", s->w, s->h);
            return NULL;
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, big ? big : s);
    SDL_DestroySurface(big);
    if (tex) {
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

/* --- tileset ---------------------------------------------------------------------------- */

bool tileset_load(Tileset *ts, const char *rel)
{
    memset(ts, 0, sizeof *ts);
    if (!gfx_load_indexed(rel, &ts->img))
        return false;
    if (!upscale_indexed(&ts->img, TILE_SIZE, TILE_SIZE, false)) {    /* each tile on its own */
        SDL_Log("gfx: %s: can't upscale", rel);
        gfx_free_indexed(&ts->img);
        return false;
    }
    return true;
}

void tileset_bake(Tileset *ts, SDL_Renderer *r, const Palette *pal)
{
    SDL_DestroyTexture(ts->tex);
    ts->tex = gfx_bake(r, &ts->img, pal, false);
}

void tileset_draw(const Tileset *ts, SDL_Renderer *r, int tile, float x, float y)
{
    int size = TILE_SIZE * upscale;
    int per_row = ts->img.w / size;
    SDL_FRect src = {(float)(tile % per_row * size), (float)(tile / per_row * size), (float)size, (float)size};
    SDL_FRect d = {x, y, TILE_SIZE, TILE_SIZE};
    SDL_RenderTexture(r, ts->tex, &src, &d);
}

void tileset_free(Tileset *ts)
{
    SDL_DestroyTexture(ts->tex);
    gfx_free_indexed(&ts->img);
}

/* --- sprite atlas ----------------------------------------------------------------------- */

bool atlas_load(SpriteAtlas *a)
{
    memset(a, 0, sizeof *a);
    cJSON *json = assets_load_json("sprites/sprites.json");
    if (!json)
        return false;
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(json, "sprites");
    a->count = cJSON_GetArraySize(list);
    a->rect = SDL_calloc((size_t)SDL_max(a->count, 1), sizeof *a->rect);
    IndexedImage *imgs = SDL_calloc((size_t)SDL_max(a->count, 1), sizeof *imgs);
    bool ok = a->rect && imgs;

    /* Shelf packing in file order: sprites of one animation are similar in size. */
    int x = 0, y = 0, shelf_h = 0;
    for (int i = 0; ok && i < a->count; i++) {
        const cJSON *spr = cJSON_GetArrayItem(list, i);
        const cJSON *file = cJSON_GetObjectItemCaseSensitive(spr, "file");
        char rel[256];
        SDL_snprintf(rel, sizeof rel, "sprites/%s", cJSON_IsString(file) ? file->valuestring : "?");
        if (!gfx_load_indexed(rel, &imgs[i]) || !upscale_indexed(&imgs[i], 0, 0, true)) {
            ok = false;
            break;
        }
        if (x + imgs[i].w > ATLAS_W * upscale) {
            x = 0;
            y += shelf_h + ATLAS_PAD;
            shelf_h = 0;
        }
        a->rect[i] = (SDL_Rect){x, y, imgs[i].w, imgs[i].h};
        x += imgs[i].w + ATLAS_PAD;
        shelf_h = SDL_max(shelf_h, imgs[i].h);
    }

    if (ok) {
        a->img.w = ATLAS_W * upscale;
        a->img.h = y + shelf_h;
        a->img.px = calloc((size_t)a->img.w * a->img.h, 1);
        ok = a->img.px != NULL;
    }
    for (int i = 0; ok && i < a->count; i++) {
        const SDL_Rect *rc = &a->rect[i];
        for (int row = 0; row < rc->h; row++)
            memcpy(a->img.px + (size_t)(rc->y + row) * a->img.w + rc->x, imgs[i].px + (size_t)row * rc->w, (size_t)rc->w);
    }

    for (int i = 0; imgs && i < a->count; i++)
        gfx_free_indexed(&imgs[i]);
    SDL_free(imgs);
    cJSON_Delete(json);
    if (!ok) {
        atlas_free(a);
        return false;
    }
    SDL_Log("gfx: %d sprites packed into %dx%d", a->count, a->img.w, a->img.h);
    return true;
}

void atlas_bake(SpriteAtlas *a, SDL_Renderer *r, const Palette *pal)
{
    SDL_DestroyTexture(a->tex);
    a->tex = gfx_bake(r, &a->img, pal, true);
}

void atlas_draw(const SpriteAtlas *a, SDL_Renderer *r, int n, float x, float y, bool flip)
{
    if (n < 0 || n >= a->count)
        return;
    const SDL_Rect *rc = &a->rect[n];
    SDL_FRect src = {(float)rc->x, (float)rc->y, (float)rc->w, (float)rc->h};
    SDL_FRect d = {x, y, (float)(rc->w / upscale), (float)(rc->h / upscale)};
    SDL_RenderTextureRotated(r, a->tex, &src, &d, 0.0, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void atlas_draw_flash(const SpriteAtlas *a, SDL_Renderer *r, int n, float x, float y, bool flip)
{
    atlas_draw(a, r, n, x, y, flip);
    SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_ADD);
    atlas_draw(a, r, n, x, y, flip);
    SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_BLEND);
}

void atlas_free(SpriteAtlas *a)
{
    SDL_DestroyTexture(a->tex);
    gfx_free_indexed(&a->img);
    SDL_free(a->rect);
    memset(a, 0, sizeof *a);
}
