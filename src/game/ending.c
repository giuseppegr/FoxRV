/* Comments name the original routines by image offset. */
#include "game/ending.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/fox_ds.h"

#define MAX_WORDS 32
#define MAX_PATH 64
#define SLIDE_FRAMES 31             /* "THE END" letters (image 0x3964), as for "GAME OVER" */
#define LETTERS_HOLD 72             /* then this long, or until fire */
#define HEARTS_MAX (30 * 36)        /* the original waits for fire forever */

/* Immediates of the scene's code (image 0x372F-0x37C2): Titus is the player record, the
 * bride the object record at DS:52B2. */
#define TITUS_X (-100)
#define TITUS_Y 180
#define TITUS_SPRITE 0x157          /* in his suit */
#define BRIDE_X 420
#define BRIDE_Y 180
#define BRIDE_SPRITE 0x151
#define RUN_SPEED 3                 /* add/sub word [..], 3 at 0x3765 / 0x3771 */
#define MEET_DISTANCE 0x1C          /* runs while bride x > titus x + 1Ch (0x3780) */
#define KISS_SPRITE 0x815A          /* the kissing couple, mirrored (bit 15) */
#define KISS_SHIFT 0x18             /* the couple moves 18h px left (0x3792) */
#define BURST_RISE 0x10             /* the burst ring: y -= 10h, then 10h frames of y += 1 */
#define BURST_FRAMES 0x10
#define HEART_SPRITE 0x163
#define HEART_PATH_MIN 18           /* pairs before the path's loop word */

typedef struct {
    int w[MAX_WORDS];
    int n;
} Words;

static struct {
    bool ok;
    int titus_x, titus_y, titus_sprite, bride_x, bride_y, bride_sprite;
    Words titus_run, bride_run, burst;
    int speed, meet, kiss_sprite, kiss_shift, burst_rise, burst_frames, heart_sprite;
    int path[MAX_PATH][2];
    int npath;
} d;

/* A sequence of animation words from the data segment, up to and including its jump word. */
static void read_words(unsigned ds_addr, Words *out)
{
    out->n = 0;
    while (out->n < MAX_WORDS) {
        int w = ds_s16(ds_addr + 2 * (unsigned)out->n);
        out->w[out->n++] = w;
        if (w < 0)
            break;
    }
}

bool ending_load(void)
{
    memset(&d, 0, sizeof d);
    d.titus_x = TITUS_X;
    d.titus_y = TITUS_Y;
    d.titus_sprite = TITUS_SPRITE;
    read_words(DS_ENDING_TITUS_ANIM, &d.titus_run);
    d.bride_x = BRIDE_X;
    d.bride_y = BRIDE_Y;
    d.bride_sprite = BRIDE_SPRITE;
    read_words(DS_ENDING_BRIDE_ANIM, &d.bride_run);
    d.speed = RUN_SPEED;
    d.meet = MEET_DISTANCE;
    d.kiss_sprite = KISS_SPRITE;
    d.kiss_shift = KISS_SHIFT;
    read_words(DS_ENDING_BURST_ANIM, &d.burst);
    d.burst_rise = BURST_RISE;
    d.burst_frames = BURST_FRAMES;
    d.heart_sprite = HEART_SPRITE;
    /* (x, y) byte pairs until the word that jumps back over the 18 of them. */
    for (unsigned a = DS_ENDING_HEART_PATH; d.npath < MAX_PATH; a += 2) {
        if (d.npath >= HEART_PATH_MIN && ds_s16(a) == -2 * HEART_PATH_MIN)
            break;
        d.path[d.npath][0] = (int)ds_u8(a);
        d.path[d.npath][1] = (int)ds_u8(a + 1);
        d.npath++;
    }
    d.ok = d.titus_run.n > 1 && d.bride_run.n > 1 && d.burst.n > 1 && d.npath > 0;
    if (!d.ok)
        SDL_Log("ending: no wedding scene in this FOX.COM");
    return d.ok;
}

/* 0x3811: advance, then follow a jump word (negative: back that many bytes). */
static int step(const Words *w, int *pos)
{
    if (w->n == 0)
        return 0;
    int p = *pos + 1;
    if (p < w->n && w->w[p] < 0)
        p += w->w[p] / 2;
    if (p < 0 || p >= w->n)
        p = 0;
    *pos = p;
    return w->w[p];
}

static void place(Obj *o, int x, int y, int sprite)
{
    *o = (Obj){.x = x, .y = y, .prev_x = x, .prev_y = y, .sprite = (uint16_t)sprite, .rider = RIDER_NONE};
}

void ending_start(Ending *e)
{
    memset(e, 0, sizeof *e);
    place(&e->titus, d.titus_x, d.titus_y, d.titus_sprite);
    place(&e->bride, d.bride_x, d.bride_y, d.bride_sprite);
    e->stage = d.ok ? END_RUN : END_LETTERS;
}

void ending_update(Ending *e, bool fire)
{
    Obj *t = &e->titus, *b = &e->bride;
    t->prev_x = t->x;
    t->prev_y = t->y;
    b->prev_x = b->x;
    b->prev_y = b->y;
    e->frames++;
    switch (e->stage) {
    case END_RUN:                    /* 0x375F */
        t->x += d.speed;
        t->sprite = (uint16_t)step(&d.titus_run, &e->titus_anim);
        b->x -= d.speed;
        b->sprite = (uint16_t)step(&d.bride_run, &e->bride_anim);
        if (b->x > t->x + d.meet)
            break;
        b->sprite = (uint16_t)d.kiss_sprite;    /* 0x378C: they meet */
        b->x -= d.kiss_shift;
        b->prev_x = b->x;
        e->titus_anim = 0;
        t->sprite = (uint16_t)d.burst.w[0];
        t->y -= d.burst_rise;
        t->prev_y = t->y;
        e->stage = END_KISS;
        e->frames = 0;
        break;
    case END_KISS:                   /* 0x37A8: the ring bursts, sinking back */
        t->sprite = (uint16_t)step(&d.burst, &e->titus_anim);
        t->y++;
        if (e->frames >= d.burst_frames) {
            e->stage = END_HEARTS;
            e->frames = 0;
        }
        break;
    case END_HEARTS:                 /* 0x37C2: the heart hops along its path */
        t->sprite = (uint16_t)d.heart_sprite;
        t->x = t->prev_x = d.path[e->path][0];
        t->y = t->prev_y = d.path[e->path][1];
        e->path = (e->path + 1) % d.npath;
        if (fire || e->frames >= HEARTS_MAX) {
            e->stage = END_LETTERS;
            e->frames = 0;
        }
        break;
    case END_LETTERS:                /* 0x37FD: "THE END" */
        if (e->frames >= SLIDE_FRAMES + LETTERS_HOLD || (e->frames > SLIDE_FRAMES && fire))
            e->stage = END_DONE;
        break;
    case END_DONE:
        break;
    }
}
