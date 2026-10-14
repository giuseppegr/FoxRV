#include "platform/font.h"

#include <string.h>

#include "core/assets.h"
#include "platform/gfx.h"

bool font_load(Font *f, SDL_Renderer *r)
{
    memset(f, 0, sizeof *f);
    memset(f->glyph, -1, sizeof f->glyph);
    cJSON *json = assets_load_json("font/font.json");
    if (!json)
        return false;
    f->w = json_int(json, "glyph_width", 8);
    f->h = json_int(json, "glyph_height", 12);
    const cJSON *g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(json, "glyphs")) {
        const cJSON *ch = cJSON_GetObjectItemCaseSensitive(g, "char");
        int i = json_int(g, "index", -1);
        if (cJSON_IsString(ch) && ch->valuestring[0] > 0 && i >= 0)
            f->glyph[(int)ch->valuestring[0]] = (int8_t)i;
    }
    cJSON_Delete(json);

    SDL_Surface *s = gfx_load_surface("font/font_sheet.png");
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
