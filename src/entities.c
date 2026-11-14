#include "entities.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float wrap_angle(float a) {
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static int has_line_of_sight(const GameMap *map, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.05f) {
        return 1;
    }

    int steps = (int)(dist * 8.0f);
    if (steps < 1) steps = 1;

    for (int i = 1; i < steps; i++) {
        float t = (float)i / (float)steps;
        int mx = (int)floorf(x0 + dx * t);
        int my = (int)floorf(y0 + dy * t);
        if (map_is_wall(map, mx, my)) {
            return 0;
        }
    }
    return 1;
}

void weapons_init_defaults(Weapon out_weapons[MAX_WEAPONS]) {
    if (!out_weapons) {
        return;
    }

    Weapon list[MAX_WEAPONS] = {
        {WEAPON_PISTOL,  "Pistola",      25,  0.25f, 1, 10.0f},
        {WEAPON_SHOTGUN, "Escopeta",     60,  0.65f, 2, 6.5f},
        {WEAPON_PLASMA,  "Rifle Plasma", 35,  0.14f, 1, 12.0f},
        {WEAPON_BFG,     "Canhao DOOM",  120, 1.10f, 5, 14.0f}
    };

    for (int i = 0; i < MAX_WEAPONS; i++) {
        out_weapons[i] = list[i];
    }
}

void enemies_init_defaults(EnemyManager *mgr) {
    if (!mgr) {
        return;
    }

    memset(mgr, 0, sizeof(*mgr));

    /* Inimigos base prontos para serem expandidos no modulo */
    mgr->enemies[0] = (Enemy){1, 7.5f,  4.5f, 60,  60,  10, 1.4f, 'Z', 0.0f};
    mgr->enemies[1] = (Enemy){1, 11.5f, 7.5f, 100, 100, 15, 1.8f, 'D', 0.0f};
    mgr->enemies[2] = (Enemy){1, 8.5f, 12.5f, 80,  80,  12, 1.6f, 'I', 0.0f};
    mgr->count = 3;
}

void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt) {
    if (!mgr || !map || !player || dt <= 0.0f) {
        return;
    }

    for (int i = 0; i < mgr->count; i++) {
        Enemy *e = &mgr->enemies[i];
        if (!e->active) {
            continue;
        }

        if (e->attack_timer > 0.0f) {
            e->attack_timer -= dt;
            if (e->attack_timer < 0.0f) e->attack_timer = 0.0f;
        }

        float dx = player->x - e->x;
        float dy = player->y - e->y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist > 0.85f && dist < 9.0f) {
            float step = e->speed * dt;
            float nx = e->x + (dx / dist) * step;
            float ny = e->y + (dy / dist) * step;

            if (!map_is_wall(map, (int)floorf(nx), (int)floorf(e->y))) {
                e->x = nx;
            }
            if (!map_is_wall(map, (int)floorf(e->x), (int)floorf(ny))) {
                e->y = ny;
            }
        } else if (dist <= 0.85f && e->attack_timer <= 0.0f) {
            player->health -= e->damage;
            if (player->health < 0) {
                player->health = 0;
            }
            e->attack_timer = 1.0f;
        }
    }
}

int enemies_check_hit(EnemyManager *mgr, const GameMap *map,
                      const Player *player, const Weapon *weapon) {
    if (!mgr || !map || !player || !weapon) {
        return 0;
    }

    int best_idx = -1;
    float best_dist = weapon->range;

    for (int i = 0; i < mgr->count; i++) {
        Enemy *e = &mgr->enemies[i];
        if (!e->active) {
            continue;
        }

        float dx = e->x - player->x;
        float dy = e->y - player->y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > best_dist) {
            continue;
        }

        float aim_diff = fabsf(wrap_angle(atan2f(dy, dx) - player->angle));
        if (aim_diff < 0.12f && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
            best_dist = dist;
            best_idx = i;
        }
    }

    if (best_idx >= 0) {
        Enemy *target = &mgr->enemies[best_idx];
        target->health -= weapon->damage;
        if (target->health <= 0) {
            target->health = 0;
            target->active = 0;
        }
        return 1;
    }

    return 0;
}
