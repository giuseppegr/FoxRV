/* Indexed images, the 16-colour VGA palette and GPU textures built from them.
 *
 * Images keep their palette indices in memory so textures can be rebuilt ("baked") whenever
 * the palette changes: each level replaces colour 14, and sprites use it too.
 *
 * Art can be upscaled when loaded with Scale2x (factor 2), Scale3x (3) or Scale2x twice (4):
 * staircase edges become smooth slopes without new colours. Textures are then `factor` times
 * bigger, but every draw call still takes sizes and positions in game pixels; the video view
 * has the matching extra resolution. Tiles are scaled in palette indices, so rebaking with a
 * new palette needs no new pass.
 */
#ifndef FOXRV_GFX_H
#define FOXRV_GFX_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define TILE_SIZE 16

typedef struct {
    int w, h;
    uint8_t *px;            /* w*h palette indices */
} IndexedImage;

typedef struct {
    SDL_Color c[16];
} Palette;

/* 1 = none, 2..4. Set once at startup, before any art is loaded. */
void gfx_set_upscale(int factor);
int gfx_upscale(void);

bool gfx_alloc_indexed(IndexedImage *img, int w, int h);    /* all colour 0 */
void gfx_free_indexed(IndexedImage *img);

/* From 16 RGB triples with 6-bit VGA components (as stored in the game). */
void palette_from_vga6(Palette *pal, const uint8_t *rgb);
/* `n` 6-bit VGA colours to 8 bits (c * 255 / 63), for pictures and text. */
void gfx_colours_from_vga6(SDL_Color *out, const uint8_t *rgb, int n);

/* RGBA texture of `img` through `pal`; index 0 becomes transparent when asked. */
SDL_Texture *gfx_bake(SDL_Renderer *r, const IndexedImage *img, const Palette *pal, bool transparent0);

/* A w x h picture of palette indices in true colour (e.g. the 256-colour full-screen
 * pictures), as an RGBA32 surface to edit before making a texture. NULL with an error logged. */
SDL_Surface *gfx_surface_from_indexed(const uint8_t *px, int w, int h, const SDL_Color *pal);
/* Texture from a surface, upscaled, nearest-neighbour sampled, alpha-blended. */
SDL_Texture *gfx_texture(SDL_Renderer *r, SDL_Surface *s);
/* The same for a sheet of cell_w x cell_h pictures (e.g. glyphs), each upscaled on its own. */
SDL_Texture *gfx_texture_cells(SDL_Renderer *r, SDL_Surface *s, int cell_w, int cell_h);

/* A level tileset: 256 tiles of 16x16 laid out 16 per row. */
typedef struct {
    IndexedImage img;
    SDL_Texture *tex;
} Tileset;

bool tileset_load(Tileset *ts, const IndexedImage *sheet);   /* copies the sheet */
void tileset_bake(Tileset *ts, SDL_Renderer *r, const Palette *pal);
void tileset_draw(const Tileset *ts, SDL_Renderer *r, int tile, float x, float y);
void tileset_free(Tileset *ts);

/* All sprites packed into one indexed image, so drawing never switches textures. */
typedef struct {
    int count;
    SDL_Rect *rect;         /* each sprite's place in the atlas */
    IndexedImage img;
    SDL_Texture *tex;
} SpriteAtlas;

bool atlas_load(SpriteAtlas *a);             /* SPREXP.SQZ, sizes from FOX.COM */
void atlas_bake(SpriteAtlas *a, SDL_Renderer *r, const Palette *pal);
/* Draws sprite `n` with its top-left corner at (x, y). */
void atlas_draw(const SpriteAtlas *a, SDL_Renderer *r, int n, float x, float y, bool flip);
/* The same, lit up (drawn a second time, added): a boss taking a hit. */
void atlas_draw_flash(const SpriteAtlas *a, SDL_Renderer *r, int n, float x, float y, bool flip);
void atlas_free(SpriteAtlas *a);

#endif
