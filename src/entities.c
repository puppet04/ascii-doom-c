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
    mgr->enemies[3] = (Enemy){1, 4.5f, 7.5f, 60, 60, 10, 1.2f, 'Z', 0};
    mgr->enemies[4] = (Enemy){1, 12.5f, 12.5f, 80, 80, 12, 1.3f, 'I', 0};
    mgr->enemies[5] = (Enemy){1, 10.5f, 4.5f, 60, 60, 10, 1.4f, 'Z', 0};
    mgr->count = 6;
}

int enemies_alive(const EnemyManager *mgr) {
    int count = 0;
    for (int i = 0; i < mgr->count; i++) if (mgr->enemies[i].active) count++;
    return count;
}

void enemies_spawn_wave(EnemyManager *mgr, const GameMap *map, const Player *p) {
    enemies_init_defaults(mgr);
    for (int i = 0; i < mgr->count; i++) {
        Enemy *e = &mgr->enemies[i];
        float dx = e->x-p->x, dy = e->y-p->y;
        if (!map_is_wall(map, (int)e->x, (int)e->y) && dx*dx+dy*dy >= 12.25f) continue;
        e->active = 0;
        for (int y = 1; y < map->height-1 && !e->active; y++) {
            for (int x = 1; x < map->width-1 && !e->active; x++) {
                dx = x+0.5f-p->x; dy = y+0.5f-p->y;
                if (map_is_wall(map,x,y) || dx*dx+dy*dy < 12.25f) continue;
                int occupied = 0;
                for (int j = 0; j < mgr->count; j++)
                    if (j != i && mgr->enemies[j].active && (int)mgr->enemies[j].x == x && (int)mgr->enemies[j].y == y) occupied=1;
                if (!occupied) { e->x=x+0.5f; e->y=y+0.5f; e->active=1; }
            }
        }
    }
}

static void damage_enemy(Enemy *e, Player *p, int damage) {
    if (!e->active) return;
    e->health -= damage;
    if (e->health <= 0) {
        e->health = 0;
        e->active = 0;
        player_register_kill(p, e->x, e->y);
    }
}

void enemies_update_fireball(EnemyManager *mgr, const GameMap *map, Player *p, float dt) {
    AbilityState *s = &p->ability_state;
    if (!s->fireball_active || dt <= 0 || p->health <= 0) return;
    float travel = fminf(dt, s->fireball_time);
    int steps = (int)ceilf(travel * 7.0f / 0.06f);
    if (steps < 1) steps = 1;
    float step = travel * 7.0f / steps;
    for (int n = 0; n < steps && s->fireball_active; n++) {
        s->fireball_x += s->fireball_dx * step;
        s->fireball_y += s->fireball_dy * step;
        if (map_is_wall(map, (int)floorf(s->fireball_x), (int)floorf(s->fireball_y))) {
            s->fireball_active = 0;
            break;
        }
        for (int i = 0; i < mgr->count; i++) {
            Enemy *e = &mgr->enemies[i];
            float dx=e->x-s->fireball_x, dy=e->y-s->fireball_y;
            if (e->active && dx*dx+dy*dy < 0.28f*0.28f) {
                damage_enemy(e, p, 5);
                s->fireball_active = 0;
                break;
            }
        }
    }
    s->fireball_time = fmaxf(0, s->fireball_time-dt);
    if (s->fireball_time <= 0) s->fireball_active=0;
}

void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt) {
    if (!mgr || !map || !player || dt <= 0.0f || player->health <= 0) {
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
        } else if (dist <= 0.85f && e->attack_timer <= 0.0f &&
                   has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
            player_take_damage(player, e->damage);
            e->attack_timer = 1.0f;
        }
    }
}

int enemies_check_hit(EnemyManager *mgr, const GameMap *map,
                      Player *player, const Weapon *weapon) {
    if (!mgr || !map || !player || !weapon || player->health <= 0) {
        return 0;
    }

    int best_idx = -1;
    int multiplier = player->ability == ABILITY_GUERRILLA && player->ability_state.boosted_shot ? 2 : 1;
    player->ability_state.boosted_shot = 0; /* Consumido mesmo se o tiro errar. */
    int berserk = player->ability_state.berserk_time > 0;
    float best_dist = berserk ? 1.5f : weapon->range;

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
        if (aim_diff < (berserk ? 0.45f : 0.12f) && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
            best_dist = dist;
            best_idx = i;
        }
    }

    if (best_idx >= 0) {
        Enemy *target = &mgr->enemies[best_idx];
        damage_enemy(target, player, berserk ? target->health : player_attack_damage(player, weapon->damage) * multiplier);
        return 1;
    }

    return 0;
}
