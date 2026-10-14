/* Animation tables and sprite placement data, read from FOX.COM's data segment (fox_ds.h).
 *
 *  - Enemy sequences (the word table at DS:87D5): each entry is one game frame; a sequence
 *    ends by jumping to a frame of the same or another sequence.
 *  - Player animation words (DS:6527): the original word area, stepped exactly like the
 *    game does it (a word >= 0 is a sprite for one frame, < 0 jumps back that many bytes).
 *  - Sprite placement: hot spot and collision box of every sprite.
 */
#ifndef FOXRV_ANIM_H
#define FOXRV_ANIM_H

#include <stdbool.h>

#define ANIM_REMOVE (-1)        /* enemy frame that removes the enemy */
#define PLAYER_STATE_COUNT 30

typedef struct {
    int seq, frame;
} AnimPos;

typedef struct {
    int hot_x, hot_y;           /* as stored; see sprite_left/sprite_top */
    int box_w, box_h;           /* collision box */
    int w, h;                   /* image size */
    int obj_flags;              /* OBJF_* when used as an object (sprites < 0x65) */
    int fall_limit;             /* terminal fall speed as an object, px per frame */
} SpriteInfo;

/* Object properties per sprite (DS:6F19). */
enum { OBJF_PLATFORM = 0x01, OBJF_BOUNCY = 0x02, OBJF_HEAVY = 0x04, OBJF_FRAGILE = 0x08, OBJF_HARMLESS = 0x10 };
#define OBJECT_SPRITES 0x65     /* sprites below this can be level objects */

typedef struct {
    int start_lives, start_energy, max_lives, max_energy;
} PlayerStart;

bool anim_load(void);
void anim_free(void);

/* Enemy sequences. */
AnimPos anim_step(AnimPos a);
int anim_sprite(AnimPos a);    /* sprite number, or ANIM_REMOVE */

/* Player animation: index of state `s`'s first word, or -1 if the state has none. */
int player_anim_start(int state);
/* Returns the sprite for the word at *pos and advances *pos (following jump words). */
int player_anim_step(int *pos);
/* The word at pos without advancing (after following a jump word). */
int player_anim_peek(int pos);

/* Enemy animation words, addressed like the original by their DS offset (DS:87D5-8EB5). */
#define ENEMY_WORDS_DS 0x87D5
#define ENEMY_REMOVE_WORD 0x55AA
int enemy_word(unsigned ds_addr);             /* signed word; 0 outside the table */
unsigned enemy_anim_start(int key);           /* first word equal to key (image 0x58CC) */
bool enemy_words_contain(unsigned ds_addr);

/* Per enemy key (sprite - 0x65). */
int enemy_class(int key);                     /* DS:6FDF */
int enemy_carry_key(int cls);                 /* DS:6FC5, by low nibble */
int enemy_death_key(int cls);                 /* DS:6FD3, by high nibble */
bool enemy_is_boss(int key);                  /* DS:70C5 */
bool enemy_type_contact(int type);            /* DS:8EDB */

/* Image 0x5295: do two sprites' collision boxes touch? Sprite numbers may carry flag bits. */
bool sprite_boxes_touch(int ax, int ay, int asprite, int bx, int by, int bsprite);

const SpriteInfo *sprite_info(int sprite);    /* NULL if out of range */
int sprite_count(void);
const PlayerStart *player_start_values(void);

/* Where the original draws sprite `sprite` for an entity at (x, y) (image 0x1E61). */
int sprite_left(int sprite, int x, bool mirrored);
int sprite_top(int sprite, int y);

#endif
