/* The game's own font (fonts.sqz, exported to export/font/): 41 glyphs of 8 x 12 pixels,
 * "0-9 ! ? . $ _ A-Z", no lowercase.
 *
 * Drawn the original way (image 0x756): each glyph opaque on its black cell (the PNG uses
 * the game's text palette, DS:04C4: yellow on black), a space as a black cell. Lowercase
 * letters are shown as capitals; characters the font lacks as spaces.
 */
#ifndef FOXRV_FONT_H
#define FOXRV_FONT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    SDL_Texture *tex;           /* font_sheet.png: the glyphs side by side */
    int w, h;                   /* glyph size */
    int8_t glyph[128];          /* ASCII -> glyph index, -1 = none */
} Font;

bool font_load(Font *f, SDL_Renderer *r);
void font_free(Font *f);

int font_width(const Font *f, const char *text);
/* `bright` 0..255 darkens the text (255 = as is). */
void font_draw(const Font *f, SDL_Renderer *r, int x, int y, const char *text, uint8_t bright);
void font_draw_centered(const Font *f, SDL_Renderer *r, int center_x, int y, const char *text, uint8_t bright);

#endif
