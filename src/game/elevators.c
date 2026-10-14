/* Comments name the original routines by image offset. */
#include "game/elevators.h"

#include <string.h>

#define MAX_SPEED 8             /* image 0x3157: speed >= 8 disables the elevator */
#define VIS_MARGIN 16           /* 0x408C: visible within 16 px left/right, and down to 16 px */

static int word(const uint8_t *r, int o) { return (int16_t)(r[o] | r[o + 1] << 8); }

void elevators_load(Elevators *el, const Level *lv)
{
    memset(el->e, 0, sizeof el->e);
    for (int i = 0; i < ELEVATOR_RECORDS; i++) {
        const uint8_t *r = lv->elevators[i];
        Elevator *e = &el->e[i];
        uint16_t key = (uint16_t)word(r, 4);
        e->sprite = key == ELEVATOR_NONE || r[7] >= MAX_SPEED ? ELEVATOR_NONE : (uint16_t)(key + ELEVATOR_SPRITE);
        e->speed = (int8_t)r[7];
        e->run = (uint16_t)word(r, 0xA);
        e->start_x = word(r, 0xC);
        e->start_y = word(r, 0xE);
        e->start_dir = r[0x10];
    }
    elevators_reset(el);
}

void elevators_reset(Elevators *el)
{
    for (int i = 0; i < ELEVATOR_RECORDS; i++) {
        Elevator *e = &el->e[i];
        e->x = e->prev_x = e->start_x;
        e->y = e->prev_y = e->start_y;
        e->dir = e->start_dir;
        e->count = 0;
        e->visible = false;
    }
}

void elevators_hold(Elevators *el)
{
    for (int i = 0; i < ELEVATOR_RECORDS; i++) {
        el->e[i].prev_x = el->e[i].x;
        el->e[i].prev_y = el->e[i].y;
    }
}

void elevators_update(Elevators *el)
{
    for (int i = 0; i < ELEVATOR_RECORDS; i++) {
        Elevator *e = &el->e[i];
        e->prev_x = e->x;
        e->prev_y = e->y;
        e->visible = false;
        if (e->sprite == ELEVATOR_NONE)
            continue;
        bool moved = true;
        switch (e->dir) {
        case ELEV_UP: e->y -= e->speed; break;
        case ELEV_DOWN: e->y += e->speed; break;
        case ELEV_RIGHT: e->x += e->speed; break;
        case ELEV_LEFT: e->x -= e->speed; break;
        default: moved = false; break;
        }
        if (moved && ++e->count == e->run) {     /* 0x40D0: end of the run, turn back */
            e->dir ^= 2;
            e->count = 0;
        }
        /* The original tests against its 320 x 192 screen; here, the whole view. */
        int dx = e->x - el->view_x, dy = e->y - el->view_y;
        e->visible = dx + VIS_MARGIN >= 0 && dx - VIS_MARGIN <= el->view_w && dy >= 0 &&
                     dy - VIS_MARGIN <= el->view_h;
    }
}
