#include "game/anim.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

#include "core/fox_ds.h"

#define ENEMY_SPRITE_BASE 0x65      /* enemy sprite = key + 0x65 (image 0x673D) */
#define EVENT_FRAME 0x2000
#define ENEMY_KEYS (0x163 - 0x65 + 1)
#define ENEMY_TYPES 19
/* New-game values, immediates at image 0x2D7, and the caps at 0x5BAA / 0x5BBB. */
#define START_LIVES 2
#define START_ENERGY 16
#define MAX_LIVES 9
#define MAX_ENERGY 16

typedef struct {
    int count;
    int *sprite;
    int next_seq, next_frame;
} Sequence;

static Sequence *seqs;
static int nseqs;
static int *words;
static int nwords;
static int states[PLAYER_STATE_COUNT];
static SpriteInfo *sprites;
static int nsprites;
static const PlayerStart start = {START_LIVES, START_ENERGY, MAX_LIVES, MAX_ENERGY};
static int *ewords;
static int newords;
static uint8_t eclass[256], eboss[256];
static int ecarry[7], edeath[6];
static bool econtact[ENEMY_TYPES];

/* The enemy word table split into sequences, each ending with a jump word (a negative word
 * jumps back that many bytes, possibly into another sequence). */
static bool load_sequences(void)
{
    nseqs = 0;
    for (int i = 0; i < newords; i++)
        if (ewords[i] < 0 || i == newords - 1)
            nseqs++;
    seqs = SDL_calloc((size_t)SDL_max(nseqs, 1), sizeof *seqs);
    /* Word index -> (sequence, position in it), to resolve the jumps. */
    int *word_seq = SDL_calloc((size_t)SDL_max(newords, 1), sizeof *word_seq);
    int *word_pos = SDL_calloc((size_t)SDL_max(newords, 1), sizeof *word_pos);
    int *seq_last = SDL_calloc((size_t)SDL_max(nseqs, 1), sizeof *seq_last);
    bool ok = seqs && word_seq && word_pos && seq_last;
    for (int i = 0, q = 0, first = 0; ok && i < newords; i++) {
        word_seq[i] = q;
        word_pos[i] = i - first;
        if (ewords[i] >= 0 && i < newords - 1)
            continue;
        Sequence *sq = &seqs[q];
        sq->sprite = SDL_calloc((size_t)(i - first + 1), sizeof *sq->sprite);
        ok = sq->sprite != NULL;
        for (int k = first; ok && k <= i; k++) {
            int v = ewords[k];
            if (v == ENEMY_REMOVE_WORD)
                sq->sprite[sq->count++] = ANIM_REMOVE;
            else if (v >= 0)
                sq->sprite[sq->count++] = (v & ~EVENT_FRAME) + ENEMY_SPRITE_BASE;
        }
        sq->next_seq = q;
        sq->next_frame = 0;
        seq_last[q++] = i;
        first = i + 1;
    }
    for (int q = 0; ok && q < nseqs; q++) {
        int jump = ewords[seq_last[q]];
        int target = seq_last[q] + jump / 2;
        if (jump < 0 && !(jump & 1) && target >= 0) {
            seqs[q].next_seq = word_seq[target];
            seqs[q].next_frame = word_pos[target];
        }
    }
    SDL_free(seq_last);
    SDL_free(word_seq);
    SDL_free(word_pos);
    return ok;
}

static bool load_player(void)
{
    /* Titus's words: from the first state's to the end of the furthest sequence. */
    unsigned ptr[PLAYER_STATE_COUNT], base = 0xFFFF, end = 0;
    for (int i = 0; i < PLAYER_STATE_COUNT; i++) {
        ptr[i] = ds_u16(DS_PLAYER_STATES + 2 * i);
        if (ptr[i])
            base = SDL_min(base, ptr[i]);
    }
    for (int i = 0; i < PLAYER_STATE_COUNT; i++) {
        unsigned p = ptr[i];
        while (p && p < 0xFFFE && ds_s16(p) >= 0)
            p += 2;
        if (p)
            end = SDL_max(end, p + 2);
    }
    nwords = end > base ? (int)(end - base) / 2 : 0;
    words = SDL_calloc((size_t)SDL_max(nwords, 1), sizeof *words);
    for (int i = 0; words && i < nwords; i++)
        words[i] = ds_s16(base + 2 * i);
    for (int i = 0; i < PLAYER_STATE_COUNT; i++)
        states[i] = ptr[i] && (int)(ptr[i] - base) / 2 < nwords ? (int)(ptr[i] - base) / 2 : -1;

    nsprites = 0;
    while (ds_u16(DS_SPRITE_SIZES + 2 * nsprites))
        nsprites++;
    sprites = SDL_calloc((size_t)SDL_max(nsprites, 1), sizeof *sprites);
    for (int i = 0; sprites && i < nsprites; i++) {
        sprites[i] = (SpriteInfo){
            .hot_x = ds_s8(DS_SPRITE_HOT + 2 * i), .hot_y = ds_s8(DS_SPRITE_HOT + 2 * i + 1),
            .box_w = (int)ds_u8(DS_SPRITE_BOX + 2 * i), .box_h = (int)ds_u8(DS_SPRITE_BOX + 2 * i + 1),
            .w = (int)ds_u8(DS_SPRITE_SIZES + 2 * i), .h = (int)ds_u8(DS_SPRITE_SIZES + 2 * i + 1),
            .obj_flags = i < OBJECT_SPRITES ? (int)ds_u8(DS_OBJECT_FLAGS + i) : 0,
            .fall_limit = i < OBJECT_SPRITES ? (int)ds_u8(DS_OBJECT_FALL_LIMIT + i) : 15,
        };
    }

    newords = (DS_ENEMY_WORDS_END - DS_ENEMY_WORDS) / 2;
    ewords = SDL_calloc((size_t)newords, sizeof *ewords);
    for (int i = 0; ewords && i < newords; i++)
        ewords[i] = ds_s16(DS_ENEMY_WORDS + 2 * i);
    SDL_memset(eclass, 0, sizeof eclass);
    SDL_memset(eboss, 0, sizeof eboss);
    for (int i = 0; i < ENEMY_KEYS; i++)
        eclass[i] = (uint8_t)ds_u8(DS_ENEMY_CLASS + i);
    /* Image 0x26C9: keys first..first+count-1 of each pair are bosses. */
    for (unsigned a = DS_BOSS_LIST; a < 0xFFFC && ds_u16(a) != 0xFFFF; a += 4)
        for (unsigned key = ds_u16(a); key < ds_u16(a) + ds_u16(a + 2) && key < ENEMY_KEYS; key++)
            eboss[key] = 1;
    for (int i = 0; i < 7; i++)
        ecarry[i] = (int)ds_u16(DS_ENEMY_CARRY_KEYS + 2 * i);
    for (int i = 0; i < 6; i++)
        edeath[i] = (int)ds_u16(DS_ENEMY_DEATH_KEYS + 2 * i);
    for (int i = 0; i < ENEMY_TYPES; i++)
        econtact[i] = ds_u16(DS_ENEMY_HANDLERS + 2 * i) != ENEMY_NO_CONTACT;
    return words && sprites && ewords;
}

bool anim_load(void)
{
    if (load_player() && load_sequences())
        return true;
    anim_free();
    return false;
}

void anim_free(void)
{
    for (int i = 0; seqs && i < nseqs; i++)
        SDL_free(seqs[i].sprite);
    SDL_free(seqs);
    SDL_free(words);
    SDL_free(sprites);
    SDL_free(ewords);
    seqs = NULL;
    words = NULL;
    sprites = NULL;
    ewords = NULL;
    nseqs = nwords = nsprites = newords = 0;
}

static bool anim_valid(AnimPos a)
{
    return a.seq >= 0 && a.seq < nseqs && a.frame >= 0 && a.frame < seqs[a.seq].count;
}

AnimPos anim_step(AnimPos a)
{
    if (!anim_valid(a))
        return a;
    if (++a.frame >= seqs[a.seq].count) {
        const Sequence *q = &seqs[a.seq];
        a = (AnimPos){q->next_seq, q->next_frame};
    }
    return a;
}

int anim_sprite(AnimPos a)
{
    return anim_valid(a) ? seqs[a.seq].sprite[a.frame] : ANIM_REMOVE;
}

int player_anim_start(int state)
{
    return state >= 0 && state < PLAYER_STATE_COUNT ? states[state] : -1;
}

/* A negative word jumps back -word bytes, i.e. -word/2 words (image 0x4E2A). */
static int follow(int pos)
{
    if (pos >= 0 && pos < nwords && words[pos] < 0)
        pos += words[pos] / 2;
    return pos;
}

int player_anim_peek(int pos)
{
    pos = follow(pos);
    return pos >= 0 && pos < nwords ? words[pos] : 0;
}

int player_anim_step(int *pos)
{
    int p = follow(*pos);
    if (p < 0 || p >= nwords)
        return 0;
    *pos = p + 1;
    return words[p];
}

const SpriteInfo *sprite_info(int sprite)
{
    return sprite >= 0 && sprite < nsprites ? &sprites[sprite] : NULL;
}

int sprite_count(void) { return nsprites; }
const PlayerStart *player_start_values(void) { return &start; }

int sprite_left(int sprite, int x, bool mirrored)
{
    const SpriteInfo *s = sprite_info(sprite);
    if (!s)
        return x;
    return x - (mirrored ? s->w - s->hot_x : s->hot_x);
}

int sprite_top(int sprite, int y)
{
    const SpriteInfo *s = sprite_info(sprite);
    return s ? y + 1 - s->hot_y : y;
}

/* --- enemy tables ------------------------------------------------------------------------- */

bool enemy_words_contain(unsigned ds_addr)
{
    return ds_addr >= ENEMY_WORDS_DS && ds_addr < ENEMY_WORDS_DS + 2u * (unsigned)newords;
}

int enemy_word(unsigned ds_addr)
{
    if (!enemy_words_contain(ds_addr))
        return 0;
    return ewords[(ds_addr - ENEMY_WORDS_DS) / 2];
}

unsigned enemy_anim_start(int key)
{
    for (int i = 0; i < newords; i++)
        if (ewords[i] == key)
            return ENEMY_WORDS_DS + 2u * (unsigned)i;
    return ENEMY_WORDS_DS;
}

int enemy_class(int key) { return key >= 0 && key < 256 ? eclass[key] : 0xFF; }
int enemy_carry_key(int cls) { return ecarry[(cls & 0x0F) % 7]; }
int enemy_death_key(int cls) { return edeath[((cls >> 4) & 0x0F) % 6]; }
bool enemy_is_boss(int key) { return key >= 0 && key < 256 && eboss[key] != 0; }
bool enemy_type_contact(int type) { return type >= 0 && type < ENEMY_TYPES && econtact[type]; }

bool sprite_boxes_touch(int ax, int ay, int asprite, int bx, int by, int bsprite)
{
    const SpriteInfo *sa = sprite_info(asprite & 0x1FFF), *sb = sprite_info(bsprite & 0x1FFF);
    if (!sa || !sb || abs(ax - bx) >= 64 || abs(ay - by) >= 70)
        return false;
    /* Vertical: the lower one's box (height - 3) must reach the other's feet. */
    const SpriteInfo *low = ay >= by ? sa : sb;
    int ly = ay >= by ? ay : by, oy = ay >= by ? by : ay;
    if (ly - (low->box_h - 3) >= oy)
        return false;
    /* Horizontal: from the left edges (x - hot_x, mirroring ignored), half the box width. */
    int aleft = ax - (sa->hot_x & 0xFF), bleft = bx - (sb->hot_x & 0xFF);
    if (bleft < aleft)
        return bleft + sb->box_w / 2 > aleft;
    return aleft + sa->box_w / 2 > bleft;
}
