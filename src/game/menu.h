/* Front-end screens, drawn with the original art and font:
 *
 *  - the TITUS logo, the title picture and the credits photo (titus/titre/levela.png);
 *  - the menu wallpaper (menu.png). Its START / PASSWORD lines are part of the picture, so
 *    they are erased and the items are drawn live in the game font, with the picture's own
 *    diamond as the cursor;
 *  - list menus (main menu, pause), level-code entry and the between-levels card.
 *
 * Everything is laid out on the original 320 x 200 screen, centred in the (wider) view.
 */
#ifndef FOXRV_MENU_H
#define FOXRV_MENU_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "platform/font.h"

#define MENU_MAX_ITEMS 8
#define MENU_NONE (-1)          /* menu_*_update: nothing chosen this frame */
#define MENU_BACK (-2)          /* ... the player backed out */
#define CODE_LEN 4

typedef struct {
    Font font;
    SDL_Texture *logo, *title, *wallpaper, *cursor, *credits;
    int cursor_w, cursor_h;
} MenuArt;

bool menu_art_load(MenuArt *a, SDL_Renderer *r);
void menu_art_free(MenuArt *a);

/* Top-left of the 320 x 200 screen in the view. */
int menu_origin_x(void);
int menu_origin_y(void);

/* A full-screen picture at 1:1 in the middle of the view. */
void menu_draw_picture(SDL_Renderer *r, SDL_Texture *pic);
/* Darkens whatever is already drawn (pause menu over the game). */
void menu_dim(SDL_Renderer *r, uint8_t alpha);

typedef struct {
    const char *items[MENU_MAX_ITEMS];
    int count, sel;
    int repeat;                 /* frames until a held direction moves the cursor again */
} MenuList;

void menu_list_init(MenuList *m, const char *const *items, int count, int sel);
/* One game frame of input: up/down move, action/jump choose, menu backs out. Returns the
 * chosen item, MENU_BACK or MENU_NONE. */
int menu_list_update(MenuList *m);
/* Items centred on center_x, from y down; the chosen one bright, with the cursor. */
void menu_list_draw(const MenuList *m, const MenuArt *a, SDL_Renderer *r, int center_x, int y, unsigned frame);

/* Level codes. The original computes them from the PC's BIOS, so the remake uses the codes
 * of the DOSBox setup they were taken from (export/levcodes/codes_dosbox.txt). */
bool level_codes_load(void);
const char *level_code(int index);              /* play index -> "E1AB", or NULL */

typedef struct {
    char digits[CODE_LEN + 1];
    int len;
    int pick;                   /* digit offered by up/down (gamepads), 0..15 */
    int wrong;                  /* frames left showing "WRONG CODE" */
    int repeat;
} CodeEntry;

void code_entry_start(CodeEntry *c);
/* Type hex digits (or pick them with up/down and action); a full code is checked with
 * action. Returns the level's play index, MENU_BACK or MENU_NONE. A code shared by several
 * levels opens the first of them in play order. */
int code_entry_update(CodeEntry *c);
void code_entry_draw(const CodeEntry *c, const MenuArt *a, SDL_Renderer *r, int center_x, int y, unsigned frame);

/* A text screen in the game font: the lines centred from line `y` down, 24 apart (the code
 * screen, image 0x533, prints "CODE" on line 80 and "LEVEL" on line 104). */
void menu_draw_notice(const MenuArt *a, SDL_Renderer *r, const char *const *lines, int count, int y);

/* The between-levels card (image 0x3F8): level number and name, extra bonuses, lives. */
void menu_draw_card(const MenuArt *a, SDL_Renderer *r, int level_number, const char *title, int bonuses, int lives);

#endif
