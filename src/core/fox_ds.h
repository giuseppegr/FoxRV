/* Where the game's tables live in FOX.COM's data segment (DS = 0x68F), and readers for them.
 * The tables themselves are read from the player's own FOX.COM at runtime (assets.h); only
 * their offsets are known here. See documentation/OVERVIEW.md for each one.
 *
 * Per-level tables have 20 entries indexed by file slot: '0'-'9' -> 0-9, 'A'-'J' -> 10-19.
 */
#ifndef FOXRV_FOX_DS_H
#define FOXRV_FOX_DS_H

#include <stdint.h>

#include "core/assets.h"

#define LEVEL_SLOTS 20

/* Palettes: 16 colours as 6-bit VGA RGB triples. */
#define DS_TEXT_PALETTE 0x04C4      /* loaded before text screens (image 0x58A) */
#define DS_GAME_PALETTE 0x5057      /* in-game; colour 14 is replaced per level */
#define DS_LEVEL_COLOUR 0x50C8      /* 20 x RGB: colour 14 of each level (image 0x30F3) */
#define LEVEL_COLOUR_INDEX 14

/* Levels. */
#define DS_LEVEL_SONG 0x508C        /* 20 bytes: music */
#define DS_LEVEL_ROWS 0x50A0        /* 20 bytes: map height in tiles */
#define DS_LEVEL_BOSS_HP 0x50B4     /* 20 bytes: hit points shared by the level's bosses */
#define DS_LEVEL_NAMES 0x5104       /* 20 NUL-terminated names */
#define DS_LEVEL_ORDER 0x523C       /* 20 bytes: play number, 1-based; FF = unused slot */

/* Sprites (SPREXP.SQZ). */
#define DS_SPRITE_SIZES 0x6665      /* (width, height) byte pairs, ended by (0, 0) */
#define DS_SPRITE_HOT 0x692F        /* (hot_x, hot_y) signed byte pairs (image 0x1E61) */
#define DS_SPRITE_BOX 0x6BF7        /* (width, height) collision box byte pairs */

/* Titus. */
#define DS_PLAYER_ANIM_WORDS 0x6527 /* the animation word area the states point into */
#define DS_PLAYER_STATES 0x6EBF     /* 30 words: first animation word of each action state */

/* Objects: one byte per sprite below 0x65. */
#define DS_OBJECT_FLAGS 0x6F19      /* OBJF_* */
#define DS_OBJECT_FALL_LIMIT 0x6F60 /* terminal fall speed, px per frame */

/* Enemies. */
#define DS_ENEMY_CARRY_KEYS 0x6FC5  /* 7 words: pose carried, by class low nibble */
#define DS_ENEMY_DEATH_KEYS 0x6FD3  /* 6 words: corpse frame, by class high nibble */
#define DS_ENEMY_CLASS 0x6FDF       /* one byte per enemy key */
#define DS_BOSS_LIST 0x7255         /* (first key, count) word pairs, ended by FFFF (image 0x26C9) */
#define DS_ENEMY_WORDS 0x87D5       /* the enemy animation word table ... */
#define DS_ENEMY_WORDS_END 0x8EB5   /* ... up to here */
#define DS_ENEMY_HANDLERS 0x8EDB    /* contact handler of each behaviour type (image 0x5C04) */
#define ENEMY_NO_CONTACT 0x5A03     /* CS offset of the bare `ret`: touching it is harmless */

/* The wedding scene (image 0x370E): the pointers its code loads. */
#define DS_ENDING_TITUS_ANIM 0x6609
#define DS_ENDING_BRIDE_ANIM 0x65E3
#define DS_ENDING_BURST_ANIM 0x661D
#define DS_ENDING_HEART_PATH 0x663F /* (x, y) byte pairs, one per frame, looping */

static inline unsigned ds_u8(unsigned off) { return assets_data_segment()[off & 0xFFFF]; }
static inline int ds_s8(unsigned off) { return (int8_t)ds_u8(off); }
static inline unsigned ds_u16(unsigned off) { return ds_u8(off) | ds_u8(off + 1) << 8; }
static inline int ds_s16(unsigned off) { return (int16_t)ds_u16(off); }
static inline const uint8_t *ds_ptr(unsigned off) { return assets_data_segment() + (off & 0xFFFF); }

#endif
