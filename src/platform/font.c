#include "platform/font.h"

#include <string.h>

#include "core/assets.h"
#include "core/fox_ds.h"
#include "core/sqz.h"
#include "platform/gfx.h"

/* FONTS.SQZ: 8 x 12 glyphs of 4 planes x 12 bytes, in this order (the text routine at image
 * 0x756 maps a character to glyph ch - 0x30, minus 2 more above 9). */
#define GLYPH_W 8
#define GLYPH_H 12
#define GLYPH_BYTES 48
#define GLYPH_CHARS "0123456789!?.$_ABCDEFGHIJKLMNOPQRSTUVWXYZ"

bool font_load(Font *f, SDL_Renderer *r)
{
    memset(f, 0, sizeof *f);
    memset(f->glyph, -1, sizeof f->glyph);
    f->w = GLYPH_W;
    f->h = GLYPH_H;
    size_t size;
    uint8_t *data = assets_load_sqz("fonts.sqz", &size);
    int count = (int)SDL_strlen(GLYPH_CHARS);
    if (!data || size != (size_t)count * GLYPH_BYTES) {
        SDL_Log("font: unexpected FONTS.SQZ");
        SDL_free(data);
        return false;
    }
    for (int i = 0; i < count; i++)
        f->glyph[(int)GLYPH_CHARS[i]] = (int8_t)i;

    /* The glyphs side by side, coloured with the text palette (yellow on black). */
    IndexedImage sheet;
    SDL_Surface *s = NULL;
    if (gfx_alloc_indexed(&sheet, count * GLYPH_W, GLYPH_H)) {
        uint8_t glyph[GLYPH_W * GLYPH_H];
        for (int i = 0; i < count; i++) {
            planar_to_indexed(data + i * GLYPH_BYTES, GLYPH_W, GLYPH_H, glyph);
            for (int y = 0; y < GLYPH_H; y++)
                memcpy(sheet.px + (size_t)y * sheet.w + i * GLYPH_W, glyph + y * GLYPH_W, GLYPH_W);
        }
        SDL_Color pal[16];
        gfx_colours_from_vga6(pal, ds_ptr(DS_TEXT_PALETTE), 16);
        s = gfx_surface_from_indexed(sheet.px, sheet.w, sheet.h, pal);
        gfx_free_indexed(&sheet);
    }
    SDL_free(data);
    f->tex = gfx_texture_cells(r, s, f->w, f->h);
    SDL_DestroySurface(s);
    return f->tex != NULL;
}

void font_free(Font *f)
{
    SDL_DestroyTexture(f->tex);
    memset(f, 0, sizeof *f);
}

int font_width(const Font *f, const char *text)
{
    return (int)strlen(text) * f->w;
}

void font_draw(const Font *f, SDL_Renderer *r, int x, int y, const char *text, uint8_t bright)
{
    if (!f->tex)
        return;
    SDL_SetTextureColorMod(f->tex, bright, bright, bright);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    for (const char *p = text; *p; p++, x += f->w) {
        int c = SDL_toupper((unsigned char)*p);
        int gi = c < 128 ? f->glyph[c] : -1;
        SDL_FRect d = {(float)x, (float)y, (float)f->w, (float)f->h};
        if (gi < 0) {
            SDL_RenderFillRect(r, &d);  /* a space: solid black, as in the original */
            continue;
        }
        int s = gfx_upscale();
        SDL_FRect src = {(float)(gi * f->w * s), 0, (float)(f->w * s), (float)(f->h * s)};
        SDL_RenderTexture(r, f->tex, &src, &d);
    }
}

void font_draw_centered(const Font *f, SDL_Renderer *r, int center_x, int y, const char *text, uint8_t bright)
{
    font_draw(f, r, center_x - font_width(f, text) / 2, y, text, bright);
}
