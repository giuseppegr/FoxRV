/* `foxrv --selftest`: headless checks of the player physics against values derived from the
 * original code (see documentation/OVERVIEW.md, "Gameplay"). Runs on level 1's real data. */
#include "game/selftest.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/elevators.h"
#include "game/enemies.h"
#include "game/level.h"
#include "game/objects.h"
#include "game/player.h"

static int failures;
static Objects objs;
static Enemies ens;
static Elevators els;
static unsigned frame;

static void check(bool ok, const char *name, const char *detail)
{
    printf("%s  %-34s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok)
        failures++;
}

/* One game frame in the original's order: elevators, objects, Titus, enemies, animation. */
static PlayerEvents step(Player *p, Level *lv, const PlayerTuning *t, PlayerInput in)
{
    PlayerEvents ev = {.song = -1};
    objs.view_x = p->x - 213;
    objs.view_y = p->y - 120;
    objs.view_w = 426;
    objs.view_h = 240;
    objects_begin_frame(&objs);
    ens.cam_x = objs.view_x;
    ens.cam_y = objs.view_y;
    ens.view_w = objs.view_w;
    ens.view_h = objs.view_h;
    els.view_x = objs.view_x;
    els.view_y = objs.view_y;
    els.view_w = objs.view_w;
    els.view_h = objs.view_h;
    elevators_update(&els);
    objects_update(&objs, lv, p, ++frame, &ev);
    player_update(p, lv, &objs, &ens, &els, &in, t, &ev);
    enemies_update(&ens, lv, p, &objs, frame, &ev);
    enemies_display(&ens, p, &objs, &ev);
    objects_animate(&objs, p, frame);
    return ev;
}

static bool has_sfx(const PlayerEvents *ev, int n)
{
    for (int i = 0; i < ev->nsfx; i++)
        if (ev->sfx[i] == n)
            return true;
    return false;
}

/* Clears all objects, then puts one with sprite `sprite` at (x, y). Returns its slot. */
static int only_object(int sprite, int x, int y)
{
    for (int i = 0; i < MAX_OBJECTS; i++)
        objs.obj[i].sprite = OBJ_NONE;
    objs.carry.sprite = OBJ_NONE;
    objs.obj[0] = (Obj){.x = x, .y = y, .prev_x = x, .prev_y = y, .sprite = (uint16_t)(sprite | OBJ_LIVE),
                        .rider = RIDER_NONE};
    objs.gate = 4;
    return 0;
}

/* Only enemy record 0 is active: `rec` as given, optionally with another key, standing still. */
static void only_enemy(const uint8_t *rec, int key)
{
    for (int i = 0; i < ENEMY_RECORDS; i++)
        ens.r[i][EN_KEY] = ens.r[i][EN_KEY + 1] = 0xFF;
    memcpy(ens.r[0], rec, ENEMY_RECORD_SIZE);
    if (key >= 0) {
        ens.r[0][EN_KEY] = (uint8_t)key;
        ens.r[0][EN_KEY + 1] = 0;
        ens.r[0][EN_SPEED] = ens.r[0][EN_SPEED + 1] = 0;
        ens.r[0][EN_KNOCK] = ens.r[0][EN_KNOCK + 1] = 0;    /* harmless, to test only the grab */
    }
    unsigned a = enemy_anim_start((ens.r[0][EN_KEY] | ens.r[0][EN_KEY + 1] << 8) & 0x1FFF);
    ens.r[0][EN_ANIM] = (uint8_t)a;
    ens.r[0][EN_ANIM + 1] = (uint8_t)(a >> 8);
}

/* Only elevator 0 is active: the long platform (sprite 92) at (x, y), moving `dir`. */
static void only_elevator(int x, int y, int dir, int speed)
{
    for (int i = 0; i < ELEVATOR_RECORDS; i++)
        els.e[i].sprite = ELEVATOR_NONE;
    els.e[0] = (Elevator){.x = x, .y = y, .prev_x = x, .prev_y = y, .sprite = 0x203E + ELEVATOR_SPRITE,
                          .dir = (uint8_t)dir, .speed = speed, .run = 200, .start_x = x, .start_y = y,
                          .start_dir = (uint8_t)dir};
}

static void place(Player *p, const Level *lv, int x, int y)
{
    player_spawn(p, lv);
    objects_reset(&objs, lv);
    enemies_reset(&ens, lv);
    for (int i = 0; i < ENEMY_RECORDS; i++)     /* physics tests run without enemies */
        ens.r[i][EN_KEY] = ens.r[i][EN_KEY + 1] = 0xFF;
    elevators_load(&els, lv);
    for (int i = 0; i < ELEVATOR_RECORDS; i++)  /* ... and elevators */
        els.e[i].sprite = ELEVATOR_NONE;
    p->x = p->prev_x = x;
    p->y = p->prev_y = y;
}

static bool empty(const Level *lv, int col, int row)
{
    return !level_floor(lv, col, row) && !level_wall(lv, col, row) && !level_ceiling(lv, col, row);
}

/* Solid floor on row r under columns c-half..c+half, with 4 free rows above. */
static bool find_open_floor(const Level *lv, int half, int *x, int *y)
{
    for (int r = lv->rows - 1; r >= 5; r--)
        for (int c = 3 + half; c < MAP_COLS - 3 - half; c++) {
            bool ok = true;
            for (int k = -half; k <= half && ok; k++) {
                ok = level_floor(lv, c + k, r) == FLOOR_SOLID;
                for (int u = 1; u <= 4 && ok; u++)
                    ok = empty(lv, c + k, r - u);
            }
            if (ok) {
                *x = c * 16 + 8;
                *y = r * 16;
                return true;
            }
        }
    return false;
}

/* A solid wall tile at the body row with 6 walkable, free tiles to its left. */
static bool find_wall(const Level *lv, int *wall_col, int *row)
{
    for (int r = 3; r < lv->rows; r++)
        for (int c = 8; c < MAP_COLS; c++) {
            if (level_wall(lv, c, r - 1) != WALL_SOLID)
                continue;
            bool ok = true;
            for (int k = c - 6; k < c && ok; k++)
                ok = level_floor(lv, k, r) == FLOOR_SOLID && empty(lv, k, r - 1) && empty(lv, k, r - 2);
            if (ok) {
                *wall_col = c;
                *row = r;
                return true;
            }
        }
    return false;
}

int selftest_run(void)
{
    char buf[160];
    PlayerTuning t;
    player_tuning_defaults(&t);
    if (!level_list_load() || !anim_load()) {
        printf("FAIL  could not load the game data\n");
        return 1;
    }
    Level lv;
    if (!level_load(&lv, 0)) {
        printf("FAIL  could not load level 1\n");
        return 1;
    }
    Player p;
    int gx, gy;
    if (!find_open_floor(&lv, 1, &gx, &gy)) {
        printf("FAIL  no open floor found in level 1\n");
        return 1;
    }
    const PlayerInput none = {0}, right = {.right = true}, jump = {.jump = true};

    /* 1. acceleration: 16 per frame up to 64 */
    place(&p, &lv, gx, gy);
    step(&p, &lv, &t, none);
    int v[5];
    for (int i = 0; i < 5; i++) {
        step(&p, &lv, &t, right);
        v[i] = p.vx;
    }
    SDL_snprintf(buf, sizeof buf, "vx %d %d %d %d %d", v[0], v[1], v[2], v[3], v[4]);
    check(v[0] == 16 && v[1] == 32 && v[2] == 48 && v[3] == 64 && v[4] == 64, "walk acceleration", buf);

    /* 2. friction: 12 per frame down to 0 */
    int f[6];
    for (int i = 0; i < 6; i++) {
        step(&p, &lv, &t, none);
        f[i] = p.vx;
    }
    SDL_snprintf(buf, sizeof buf, "vx %d %d %d %d %d %d", f[0], f[1], f[2], f[3], f[4], f[5]);
    check(f[0] == 52 && f[1] == 40 && f[2] == 28 && f[3] == 16 && f[4] == 4 && f[5] == 0, "friction", buf);

    /* 3. held jump: three impulse frames, peak 50 px */
    place(&p, &lv, gx, gy);
    step(&p, &lv, &t, none);
    int vy[4], top = p.y, base = p.y;
    for (int i = 0; i < 40; i++) {
        step(&p, &lv, &t, i < 4 ? jump : none);
        if (i < 4)
            vy[i] = p.vy;
        top = SDL_min(top, p.y);
    }
    SDL_snprintf(buf, sizeof buf, "vy %d %d %d %d, peak %d px, back on y %d", vy[0], vy[1], vy[2], vy[3],
                 base - top, p.y);
    check(vy[0] == -48 && vy[1] == -80 && vy[2] == -112 && base - top == 50 && p.y == base && p.vy == 0,
          "held jump", buf);

    /* 4. tapped jump: one impulse frame, peak 9 px */
    place(&p, &lv, gx, gy);
    step(&p, &lv, &t, none);
    base = top = p.y;
    for (int i = 0; i < 30; i++) {
        step(&p, &lv, &t, i == 0 ? jump : none);
        top = SDL_min(top, p.y);
    }
    SDL_snprintf(buf, sizeof buf, "peak %d px", base - top);
    check(base - top == 9, "tapped jump", buf);

    /* 5. falling lands on the tile top */
    place(&p, &lv, gx, gy - 48);
    for (int i = 0; i < 40; i++)
        step(&p, &lv, &t, none);
    SDL_snprintf(buf, sizeof buf, "y %d (floor %d), vy %d", p.y, gy, p.vy);
    check(p.y == gy && p.vy == 0, "fall and land", buf);

    /* 6. walking into a wall stops */
    int wc, wr;
    if (find_wall(&lv, &wc, &wr)) {
        place(&p, &lv, (wc - 4) * 16 + 8, wr * 16);
        int max_x = 0;
        for (int i = 0; i < 60; i++) {
            step(&p, &lv, &t, right);
            max_x = SDL_max(max_x, p.x);
        }
        SDL_snprintf(buf, sizeof buf, "wall at x %d, stopped at x %d, vx %d", wc * 16, max_x, p.vx);
        check(max_x < wc * 16 && p.vx == 0, "wall stops the player", buf);
    } else {
        check(false, "wall stops the player", "no suitable wall in level 1");
    }

    /* 8. pick up a crate in front, crouching */
    const PlayerInput down = {.down = true}, fire_down = {.down = true, .fire = true}, fire = {.fire = true};
    place(&p, &lv, gx, gy);
    step(&p, &lv, &t, none);
    only_object(SPR_CRATE, gx + 12, gy);
    step(&p, &lv, &t, down);
    PlayerEvents e8 = step(&p, &lv, &t, fire_down);
    SDL_snprintf(buf, sizeof buf, "carry %d, slot %04X, held %04X", p.carry, objs.obj[0].sprite, objs.carry.sprite);
    check(p.carry == 16 && objs.obj[0].sprite == OBJ_NONE && (objs.carry.sprite & 0x1FFF) == SPR_CRATE &&
          has_sfx(&e8, SFX_PICK_UP), "pick up", buf);

    /* 9. light object thrown sideways flies straight as a projectile */
    step(&p, &lv, &t, none);         /* stand up, release fire */
    step(&p, &lv, &t, none);
    PlayerEvents e9 = step(&p, &lv, &t, fire);
    int y9 = objs.carry.y, x9 = objs.carry.x;
    step(&p, &lv, &t, none);
    SDL_snprintf(buf, sizeof buf, "projectile vx %d, moved %d px, y %d -> %d", objs.carry.vx, objs.carry.x - x9, y9, objs.carry.y);
    check(p.throwing && objs.carry.vx == 0xE0 && objs.carry.x - x9 == 14 && objs.carry.y == y9 &&
          has_sfx(&e9, SFX_THROW), "throw crate (projectile)", buf);

    /* 10. heavy object (ball) thrown as a physics object at 3/4 speed */
    place(&p, &lv, gx, gy);
    step(&p, &lv, &t, none);
    only_object(SPR_BALL, gx + 12, gy);
    step(&p, &lv, &t, down);
    step(&p, &lv, &t, fire_down);
    step(&p, &lv, &t, none);
    step(&p, &lv, &t, none);
    step(&p, &lv, &t, fire);
    int slot = objs.last_drop;
    SDL_snprintf(buf, sizeof buf, "slot %d vx %d, projectile %d", slot, slot >= 0 ? objs.obj[slot].vx : 0, p.throwing);
    check(slot >= 0 && objs.obj[slot].vx == 0xA8 && !p.throwing, "throw ball (physics)", buf);

    /* 11. a dropped crate falls and lands on a tile top */
    place(&p, &lv, gx, gy);
    only_object(SPR_CRATE, gx, gy - 40);
    for (int i = 0; i < 40; i++)
        step(&p, &lv, &t, none);
    SDL_snprintf(buf, sizeof buf, "y %d (floor %d), vy %d", objs.obj[0].y, gy, objs.obj[0].vy);
    check(objs.obj[0].y == gy && objs.obj[0].vy == 0, "object falls and lands", buf);

    /* 12. a ball dropped from high up bounces */
    place(&p, &lv, gx - 64, gy);
    only_object(SPR_BALL, gx, gy - 64);
    int min_vy = 0, bounces = 0, last = 0;
    for (int i = 0; i < 60; i++) {
        step(&p, &lv, &t, none);
        if (objs.obj[0].vy < 0 && last >= 0)
            bounces++;
        last = objs.obj[0].vy;
        min_vy = SDL_min(min_vy, objs.obj[0].vy);
    }
    SDL_snprintf(buf, sizeof buf, "%d bounces, strongest vy %d", bounces, min_vy);
    check(bounces >= 1 && min_vy < 0, "ball bounces", buf);

    /* 13. Titus lands on top of a crate */
    place(&p, &lv, gx, gy - 64);
    only_object(SPR_CRATE, gx, gy);
    for (int i = 0; i < 40; i++)
        step(&p, &lv, &t, none);
    const SpriteInfo *crate = sprite_info(SPR_CRATE);
    SDL_snprintf(buf, sizeof buf, "y %d, crate top %d, vy %d", p.y, gy - crate->box_h, p.vy);
    check(p.y == gy - crate->box_h && p.vy == 0, "stand on a crate", buf);

    /* 14. Titus bounces off a spring */
    place(&p, &lv, gx, gy - 80);
    only_object(SPR_SPRING + 1, gx, gy);
    int best = 0;
    bool boing = false;
    for (int i = 0; i < 30; i++) {
        PlayerEvents e = step(&p, &lv, &t, none);
        boing |= has_sfx(&e, SFX_BOUNCE);
        best = SDL_min(best, p.vy);
    }
    SDL_snprintf(buf, sizeof buf, "strongest upward vy %d, sound %s", best, boing ? "yes" : "no");
    check(best < 0 && boing, "spring bounces Titus", buf);

    /* Enemy tests use level 1's first enemy: a patroller (type 0) at (433, 576), knock-back 50. */
    uint8_t first_enemy[ENEMY_RECORD_SIZE];
    memcpy(first_enemy, lv.enemies[0], sizeof first_enemy);
    int ex = first_enemy[0] | first_enemy[1] << 8, ey = first_enemy[2] | first_enemy[3] << 8;

    /* 15. touching an enemy hurts and knocks back */
    place(&p, &lv, ex - 4, ey);
    only_enemy(first_enemy, -1);
    PlayerEvents e15 = {0}, tmp;
    for (int i = 0; i < 3 && !e15.hurts; i++) {
        tmp = step(&p, &lv, &t, none);
        e15 = tmp;
    }
    SDL_snprintf(buf, sizeof buf, "hits %d, knock-back %d, vx %d vy %d", e15.hurts, p.knockback, p.vx, p.vy);
    check(e15.hurts == 1 && has_sfx(&e15, SFX_PLAYER_HURT) && p.knockback > 0 && abs(p.vx) == 50,
          "enemy contact hurts", buf);

    /* 15b. low difficulty: no hit while immune */
    place(&p, &lv, ex - 4, ey);
    only_enemy(first_enemy, -1);
    p.grace = 10;
    PlayerEvents e15b = step(&p, &lv, &t, none);
    SDL_snprintf(buf, sizeof buf, "hits %d, knock-back %d", e15b.hurts, p.knockback);
    check(e15b.hurts == 0 && p.knockback == 0, "immune to contact", buf);
    p.grace = 0;

    /* 16. a thrown crate defeats an enemy */
    place(&p, &lv, ex - 120, ey);
    only_enemy(first_enemy, -1);
    only_object(SPR_CRATE, ex - 120, ey);
    objs.obj[0].sprite = OBJ_NONE;
    objs.carry = (Obj){.x = ex - 40, .y = ey - 8, .prev_x = ex - 40, .prev_y = ey - 8, .vx = 0xE0,
                       .sprite = SPR_CRATE, .rider = RIDER_NONE};
    p.throwing = true;
    bool hit = false;
    for (int i = 0; i < 8; i++) {
        PlayerEvents e = step(&p, &lv, &t, none);
        hit |= has_sfx(&e, SFX_ENEMY_HIT);
    }
    SDL_snprintf(buf, sizeof buf, "hit sound %s, enemy flags %02X, star %d", hit ? "yes" : "no", ens.r[0][7], objs.star);
    check(hit && (ens.r[0][7] & 0xC0), "thrown crate defeats an enemy", buf);

    /* 20. a boss (key 0x93, level 3's) survives a hit and loses one hit point */
    place(&p, &lv, ex - 120, ey);
    only_enemy(first_enemy, 0x93);
    ens.r[0][EN_KNOCK] = 0;
    ens.boss_hp = 10;
    ens.boss_count = 1;
    objs.carry = (Obj){.x = ex - 40, .y = ey - 8, .prev_x = ex - 40, .prev_y = ey - 8, .vx = 0xE0,
                       .sprite = SPR_CRATE, .rider = RIDER_NONE};
    p.throwing = true;
    for (int i = 0; i < 8; i++)
        step(&p, &lv, &t, none);
    SDL_snprintf(buf, sizeof buf, "hit points %d, enemy flags %02X, bosses left %d", ens.boss_hp, ens.r[0][7],
                 ens.boss_count);
    check(ens.boss_hp == 9 && !(ens.r[0][7] & 0xC0) && ens.boss_count == 1, "a boss takes several hits", buf);

    /* 17. an enemy can be picked up from behind (a pickable kind, standing still) */
    place(&p, &lv, ex + 10, ey);
    only_enemy(first_enemy, 0x19);
    p.sprite = SPRITE_MIRROR;        /* facing left, towards its back */
    p.hdir = p.facing = 0xFF;
    const PlayerInput down_left = {.down = true}, grab = {.down = true, .fire = true};
    step(&p, &lv, &t, down_left);
    step(&p, &lv, &t, grab);
    SDL_snprintf(buf, sizeof buf, "carry %d, held sprite %d, enemy record %04X", p.carry, objs.carry.sprite & 0x1FFF,
                 ens.r[0][4] | ens.r[0][5] << 8);
    check(p.carry == 16 && (ens.r[0][4] | ens.r[0][5] << 8) == 0xFFFF, "pick up an enemy from behind", buf);

    /* 18. Titus lands on an elevator moving right and rides along with it */
    int ax, ay;
    if (find_open_floor(&lv, 4, &ax, &ay)) {
        place(&p, &lv, ax, ay - 60);
        only_elevator(ax - 16, ay - 32, ELEV_RIGHT, 1);
        int px0 = 0, ex0 = 0;
        for (int i = 0; i < 24; i++) {
            step(&p, &lv, &t, none);
            if (i == 13) {
                px0 = p.x;
                ex0 = els.e[0].x;
            }
        }
        SDL_snprintf(buf, sizeof buf, "y %d (platform %d), vy %d, moved %d px with it (%d)", p.y, els.e[0].y, p.vy,
                     p.x - px0, els.e[0].x - ex0);
        check(p.y == els.e[0].y && p.vy == 0 && p.x - px0 == els.e[0].x - ex0 && p.x > px0, "ride an elevator", buf);

        /* 19. an elevator going up carries Titus up */
        place(&p, &lv, ax, ay - 20);
        only_elevator(ax - 16, ay - 8, ELEV_UP, 1);
        int y0 = 0;
        for (int i = 0; i < 20; i++) {
            step(&p, &lv, &t, none);
            if (i == 9)
                y0 = p.y;
        }
        SDL_snprintf(buf, sizeof buf, "rose %d px, platform y %d, Titus y %d", y0 - p.y, els.e[0].y, p.y);
        check(y0 - p.y == 10 && p.y == els.e[0].y, "lifted by an elevator", buf);
    } else {
        check(false, "ride an elevator", "no open floor wide enough in level 1");
    }

    /* 7. falling out of the map kills */
    place(&p, &lv, gx, (lv.rows + 2) * 16);
    PlayerEvents ev = step(&p, &lv, &t, none);
    check(ev.died, "falling out of the map", ev.died ? "died" : "survived");

    level_free(&lv);
    anim_free();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
