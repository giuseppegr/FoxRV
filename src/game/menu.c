#include "game/menu.h"

#include <stdio.h>
#include <string.h>

#include "core/assets.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/video.h"

#define SCREEN_W 320
#define SCREEN_H 200
#define ITEM_SPACING 14
#define DIM 150                 /* brightness of the items not chosen */
#define REPEAT_FIRST 12         /* frames before a held direction repeats, then every */
#define REPEAT_NEXT 4
#define WRONG_FRAMES 54
#define MAX_LEVEL_CODES 32

/* menu.png: the baked-in START / PASSWORD lines (erased), and the cursor diamond (kept). */
static const SDL_Rect baked_items = {0, 145, 320, 55};     /* all black below the subtitle */
static const SDL_Rect cursor_rect = {120, 160, 6, 7};

static char codes[MAX_LEVEL_CODES][CODE_LEN + 1];
static int ncodes;

/* --- art ---------------------------------------------------------------------------------- */

static SDL_Texture *picture(SDL_Renderer *r, const char *rel)
{
    SDL_Surface *s = gfx_load_surface(rel);
    SDL_Texture *t = gfx_texture(r, s);
    SDL_DestroySurface(s);
    return t;
}

bool menu_art_load(MenuArt *a, SDL_Renderer *r)
{
    memset(a, 0, sizeof *a);
    bool ok = font_load(&a->font, r);
    a->logo = picture(r, "images/titus.png");
    a->title = picture(r, "images/titre.png");
    a->credits = picture(r, "images/levela.png");

    SDL_Surface *menu = gfx_load_surface("images/menu.png");
    if (menu) {
        SDL_Surface *cur = SDL_CreateSurface(cursor_rect.w, cursor_rect.h, SDL_PIXELFORMAT_RGBA32);
        if (cur) {
            for (int y = 0; y < cursor_rect.h; y++) {
                const uint8_t *src = (const uint8_t *)menu->pixels + (size_t)(cursor_rect.y + y) * menu->pitch + cursor_rect.x * 4;
                uint8_t *dst = (uint8_t *)cur->pixels + (size_t)y * cur->pitch;
                memcpy(dst, src, (size_t)cursor_rect.w * 4);
                for (int x = 0; x < cursor_rect.w; x++)     /* black around the diamond: see-through */
                    if (!dst[x * 4] && !dst[x * 4 + 1] && !dst[x * 4 + 2])
                        dst[x * 4 + 3] = 0;
            }
            a->cursor = gfx_texture(r, cur);
            SDL_DestroySurface(cur);
            a->cursor_w = cursor_rect.w;
            a->cursor_h = cursor_rect.h;
        }
        SDL_FillSurfaceRect(menu, &baked_items, SDL_MapSurfaceRGB(menu, 0, 0, 0));
        a->wallpaper = gfx_texture(r, menu);
        SDL_DestroySurface(menu);
    }
    ok = ok && a->logo && a->title && a->wallpaper && a->cursor;
    if (!ok)
        SDL_Log("menu: some pictures are missing (run tools/export_images.py and export_font.py)");
    level_codes_load();
    return ok;
}

void menu_art_free(MenuArt *a)
{
    font_free(&a->font);
    SDL_DestroyTexture(a->logo);
    SDL_DestroyTexture(a->title);
    SDL_DestroyTexture(a->credits);
    SDL_DestroyTexture(a->wallpaper);
    SDL_DestroyTexture(a->cursor);
    memset(a, 0, sizeof *a);
}

int menu_origin_x(void) { return (video_view_w() - SCREEN_W) / 2; }
int menu_origin_y(void) { return (video_view_h() - SCREEN_H) / 2; }

void menu_draw_picture(SDL_Renderer *r, SDL_Texture *pic)
{
    if (!pic)
        return;
    float w, h;
    SDL_GetTextureSize(pic, &w, &h);
    w /= (float)gfx_upscale();
    h /= (float)gfx_upscale();
    SDL_FRect d = {(float)((video_view_w() - (int)w) / 2), (float)((video_view_h() - (int)h) / 2), w, h};
    SDL_RenderTexture(r, pic, NULL, &d);
}

void menu_dim(SDL_Renderer *r, uint8_t alpha)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
    SDL_RenderFillRect(r, NULL);
}

/* --- list menus --------------------------------------------------------------------------- */

static bool confirm(void) { return input_pressed(ACT_ACTION) || input_pressed(ACT_JUMP); }

/* -1 / +1 for up / down: on the press, then repeating while held. */
static int vertical(int *repeat)
{
    int dir = input_held(ACT_UP) ? -1 : input_held(ACT_DOWN) ? 1 : 0;
    if (!dir) {
        *repeat = 0;
        return 0;
    }
    if (input_pressed(ACT_UP) || input_pressed(ACT_DOWN)) {
        *repeat = REPEAT_FIRST;
        return dir;
    }
    if (--*repeat > 0)
        return 0;
    *repeat = REPEAT_NEXT;
    return dir;
}

void menu_list_init(MenuList *m, const char *const *items, int count, int sel)
{
    memset(m, 0, sizeof *m);
    m->count = SDL_min(count, MENU_MAX_ITEMS);
    for (int i = 0; i < m->count; i++)
        m->items[i] = items[i];
    m->sel = SDL_clamp(sel, 0, m->count - 1);
}

int menu_list_update(MenuList *m)
{
    if (m->count == 0)
        return MENU_NONE;
    int d = vertical(&m->repeat);
    if (d)
        m->sel = (m->sel + d + m->count) % m->count;
    if (confirm())
        return m->sel;
    if (input_pressed(ACT_MENU))
        return MENU_BACK;
    return MENU_NONE;
}

void menu_list_draw(const MenuList *m, const MenuArt *a, SDL_Renderer *r, int center_x, int y, unsigned frame)
{
    for (int i = 0; i < m->count; i++, y += ITEM_SPACING) {
        bool on = i == m->sel;
        font_draw_centered(&a->font, r, center_x, y, m->items[i], on ? 255 : DIM);
        if (on && a->cursor) {
            int left = center_x - font_width(&a->font, m->items[i]) / 2;
            int bob = (frame >> 3) & 1;  /* a gentle nudge toward the item */
            SDL_FRect d = {(float)(left - a->cursor_w - 4 + bob), (float)(y + (a->font.h - a->cursor_h) / 2),
                           (float)a->cursor_w, (float)a->cursor_h};
            SDL_RenderTexture(r, a->cursor, NULL, &d);
        }
    }
}

/* --- level codes -------------------------------------------------------------------------- */

bool level_codes_load(void)
{
    ncodes = 0;
    size_t size;
    char *text = assets_load("levcodes/codes_dosbox.txt", &size);
    if (!text)
        return false;
    for (char *line = text; line && *line;) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        int level;
        char code[8];
        if (sscanf(line, "%d %7s", &level, code) == 2 && strlen(code) == CODE_LEN && level >= 1 &&
            level <= MAX_LEVEL_CODES) {
            SDL_strlcpy(codes[level - 1], code, sizeof codes[0]);
            ncodes = SDL_max(ncodes, level);
        }
        line = next;
    }
    SDL_free(text);
    return ncodes > 0;
}

const char *level_code(int index)
{
    return index >= 0 && index < ncodes && codes[index][0] ? codes[index] : NULL;
}

static int level_for_code(const char *code)
{
    for (int i = 0; i < ncodes; i++)
        if (!SDL_strcasecmp(codes[i], code))
            return i;
    return -1;
}

static const char hex[] = "0123456789ABCDEF";

void code_entry_start(CodeEntry *c)
{
    memset(c, 0, sizeof *c);
    input_clear_chars();
}

int code_entry_update(CodeEntry *c)
{
    if (c->wrong > 0) {
        if (--c->wrong == 0)
            code_entry_start(c);
        return MENU_NONE;
    }
    for (int ch; (ch = input_take_char()) != 0;) {
        if (ch == '\b' && c->len > 0)
            c->digits[--c->len] = '\0';
        else if (strchr(hex, ch) && c->len < CODE_LEN)
            c->digits[c->len++] = (char)ch;
    }
    int d = vertical(&c->repeat);
    if (d)
        c->pick = (c->pick - d + 16) % 16;  /* up = next digit */
    if (input_pressed(ACT_MENU)) {
        if (c->len == 0)
            return MENU_BACK;
        c->digits[--c->len] = '\0';
    }
    if (!confirm())
        return MENU_NONE;
    if (c->len < CODE_LEN) {
        c->digits[c->len++] = hex[c->pick];
        return MENU_NONE;
    }
    int level = level_for_code(c->digits);
    if (level < 0)
        c->wrong = WRONG_FRAMES;
    return level < 0 ? MENU_NONE : level;
}

void code_entry_draw(const CodeEntry *c, const MenuArt *a, SDL_Renderer *r, int center_x, int y, unsigned frame)
{
    if (c->wrong > 0) {
        font_draw_centered(&a->font, r, center_x, y + ITEM_SPACING, "!  WRONG CODE  !", 255);
        return;
    }
    char line[16] = "CODE  ";       /* the original's "CODE  @@@@" */
    for (int i = 0; i < CODE_LEN; i++) {
        char ch = i < c->len ? c->digits[i] : '_';
        if (i == c->len && (frame & 8))
            ch = hex[c->pick];      /* the digit up/down offers, blinking */
        line[6 + i] = ch;
    }
    line[6 + CODE_LEN] = '\0';
    font_draw_centered(&a->font, r, center_x, y + ITEM_SPACING, line, 255);
}

/* --- text screens ------------------------------------------------------------------------- */

void menu_draw_notice(const MenuArt *a, SDL_Renderer *r, const char *const *lines, int count, int y)
{
    for (int i = 0; i < count; i++)
        if (lines[i][0])
            font_draw_centered(&a->font, r, video_view_w() / 2, menu_origin_y() + y + 24 * i, lines[i], 255);
}

/* --- between levels ----------------------------------------------------------------------- */

void menu_draw_card(const MenuArt *a, SDL_Renderer *r, int level_number, const char *title, int bonuses, int lives)
{
    int oy = menu_origin_y(), cx = video_view_w() / 2;
    char line[48];
    SDL_snprintf(line, sizeof line, "LEVEL  %d", level_number);
    font_draw_centered(&a->font, r, cx, oy + 60, line, 255);
    font_draw_centered(&a->font, r, cx, oy + 76, title, 255);
    /* Label, then the number right-aligned in 5 places, as in the original; the two lines are
     * the same width, so centring each keeps the numbers aligned. (The original starts them at
     * column 10, 8 px left of centre.) */
    SDL_snprintf(line, sizeof line, "%-13s%5d", "EXTRA BONUS", bonuses);
    font_draw_centered(&a->font, r, cx, oy + 120, line, 255);
    SDL_snprintf(line, sizeof line, "%-13s%5d", "LIVES", lives);
    font_draw_centered(&a->font, r, cx, oy + 132, line, 255);
}
