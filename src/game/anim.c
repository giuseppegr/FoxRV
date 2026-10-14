#include "game/anim.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

#include "core/assets.h"

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
static PlayerStart start;
static int *ewords;
static int newords;
static uint8_t eclass[256], eboss[256];
static int ecarry[7], edeath[6];
static bool econtact[19];

static void read_ints(const cJSON *list, int *out, int n)
{
    for (int i = 0; i < n; i++) {
        const cJSON *v = cJSON_GetArrayItem(list, i);
        out[i] = cJSON_IsNumber(v) ? v->valueint : 0;
    }
}

static bool load_sequences(void)
{
    cJSON *json = assets_load_json("entities/animations.json");
    if (!json)
        return false;
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(json, "sequences");
    nseqs = cJSON_GetArraySize(list);
    seqs = SDL_calloc((size_t)SDL_max(nseqs, 1), sizeof *seqs);
    for (int i = 0; seqs && i < nseqs; i++) {
        const cJSON *s = cJSON_GetArrayItem(list, i);
        const cJSON *frames = cJSON_GetObjectItemCaseSensitive(s, "frames");
        const cJSON *then = cJSON_GetObjectItemCaseSensitive(s, "then_goes_to");
        Sequence *q = &seqs[i];
        q->count = cJSON_GetArraySize(frames);
        q->sprite = SDL_calloc((size_t)SDL_max(q->count, 1), sizeof *q->sprite);
        for (int f = 0; q->sprite && f < q->count; f++) {
            const cJSON *fr = cJSON_GetArrayItem(frames, f);
            q->sprite[f] = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(fr, "remove"))
                               ? ANIM_REMOVE : json_int(fr, "sprite", ANIM_REMOVE);
        }
        q->next_seq = json_int(then, "sequence", i);
        q->next_frame = json_int(then, "frame", 0);
    }
    cJSON_Delete(json);
    return seqs != NULL;
}

static bool load_player(void)
{
    cJSON *json = assets_load_json("entities/player.json");
    if (!json) {
        SDL_Log("anim: run tools/export_player.py");
        return false;
    }
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(json, "words");
    nwords = cJSON_GetArraySize(w);
    words = SDL_calloc((size_t)SDL_max(nwords, 1), sizeof *words);
    for (int i = 0; words && i < nwords; i++)
        words[i] = cJSON_GetArrayItem(w, i)->valueint;

    const cJSON *st = cJSON_GetObjectItemCaseSensitive(json, "states");
    for (int i = 0; i < PLAYER_STATE_COUNT; i++) {
        const cJSON *v = cJSON_GetArrayItem(st, i);
        states[i] = cJSON_IsNumber(v) && v->valueint < nwords ? v->valueint : -1;
    }

    const cJSON *hot = cJSON_GetObjectItemCaseSensitive(json, "sprite_hot");
    const cJSON *box = cJSON_GetObjectItemCaseSensitive(json, "sprite_box");
    nsprites = SDL_min(cJSON_GetArraySize(hot), cJSON_GetArraySize(box));
    sprites = SDL_calloc((size_t)SDL_max(nsprites, 1), sizeof *sprites);
    for (int i = 0; sprites && i < nsprites; i++) {
        const cJSON *h = cJSON_GetArrayItem(hot, i), *b = cJSON_GetArrayItem(box, i);
        sprites[i] = (SpriteInfo){
            .hot_x = cJSON_GetArrayItem(h, 0)->valueint, .hot_y = cJSON_GetArrayItem(h, 1)->valueint,
            .box_w = cJSON_GetArrayItem(b, 0)->valueint, .box_h = cJSON_GetArrayItem(b, 1)->valueint,
        };
    }
    const cJSON *of = cJSON_GetObjectItemCaseSensitive(json, "object_flags");
    const cJSON *fl = cJSON_GetObjectItemCaseSensitive(json, "object_fall_limit");
    for (int i = 0; sprites && i < nsprites; i++) {
        const cJSON *f = cJSON_GetArrayItem(of, i), *l = cJSON_GetArrayItem(fl, i);
        sprites[i].obj_flags = i < OBJECT_SPRITES && cJSON_IsNumber(f) ? f->valueint : 0;
        sprites[i].fall_limit = i < OBJECT_SPRITES && cJSON_IsNumber(l) ? l->valueint : 15;
    }
    const cJSON *ew = cJSON_GetObjectItemCaseSensitive(json, "enemy_words");
    newords = cJSON_GetArraySize(ew);
    ewords = SDL_calloc((size_t)SDL_max(newords, 1), sizeof *ewords);
    if (ewords)
        read_ints(ew, ewords, newords);
    int tmp[256];
    read_ints(cJSON_GetObjectItemCaseSensitive(json, "enemy_class"), tmp, 256);
    for (int i = 0; i < 256; i++)
        eclass[i] = (uint8_t)tmp[i];
    read_ints(cJSON_GetObjectItemCaseSensitive(json, "enemy_boss"), tmp, 256);
    for (int i = 0; i < 256; i++)
        eboss[i] = (uint8_t)tmp[i];
    read_ints(cJSON_GetObjectItemCaseSensitive(json, "enemy_carry_keys"), ecarry, 7);
    read_ints(cJSON_GetObjectItemCaseSensitive(json, "enemy_death_keys"), edeath, 6);
    const cJSON *ec = cJSON_GetObjectItemCaseSensitive(json, "enemy_contact");
    for (int i = 0; i < 19; i++)
        econtact[i] = cJSON_IsTrue(cJSON_GetArrayItem(ec, i));
    start = (PlayerStart){
        json_int(json, "start_lives", 2), json_int(json, "start_energy", 16),
        json_int(json, "max_lives", 9), json_int(json, "max_energy", 16),
    };
    cJSON_Delete(json);

    /* Image sizes, for placing mirrored sprites. */
    cJSON *meta = assets_load_json("sprites/sprites.json");
    const cJSON *s;
    int i = 0;
    cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(meta, "sprites")) {
        if (sprites && i < nsprites) {
            sprites[i].w = json_int(s, "width", 0);
            sprites[i].h = json_int(s, "height", 0);
        }
        i++;
    }
    cJSON_Delete(meta);
    return words && sprites && ewords;
}

bool anim_load(void)
{
    if (load_sequences() && load_player())
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
bool enemy_type_contact(int type) { return type >= 0 && type < 19 && econtact[type]; }

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
