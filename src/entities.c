#include "entities.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

typedef struct {
    const char *name;
    char symbol;
    int health;
    float speed;
    int damage;
    EnemyAttackKind attack_kind;
    float attack_range;
    float attack_cooldown;
    float aggro_range;
    float precision;
    EnemyProjectileKind projectile_kind;
    EnemyProjectilePath projectile_path;
    float projectile_speed;
    float projectile_turn_rate;
} EnemyStats;

/* Projeteis: flecha 6, bola 3.1 e orbe 2.2; cerca de 40% mais lentos. */
/* Precision is retained for compatibility with manually configured hitscan enemies. */
static const EnemyStats ENEMY_STATS[ENEMY_TYPE_COUNT] = {
    {"Z legado",       'Z',  60, 1.4f,  7, ENEMY_ATTACK_MELEE,      0.85f, 2.20f,  9.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    {"D legado",       'D', 100, 1.8f, 10, ENEMY_ATTACK_MELEE,      0.85f, 3.20f,  9.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    {"I legado",       'I',  80, 1.6f,  8, ENEMY_ATTACK_MELEE,      0.85f, 2.50f,  9.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma: bola de fogo oscilante, lenta e de dano moderado. */
    {"Imp",            'M',  60, 1.2f,  5, ENEMY_ATTACK_PROJECTILE, 8.00f, 1.90f,  9.5f, 0.95f, ENEMY_PROJECTILE_FIREBALL,   ENEMY_PROJECTILE_WAVERING,  3.1f, 0.9f},
    /* Ataque: investida curta de ombro, com alcance e intervalo proprios. */
    {"Pinky",          'Y',  75, 2.0f,  8, ENEMY_ATTACK_MELEE,      1.20f, 2.50f,  9.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma: orbe de energia teleguiado, lento e devastador. */
    {"Cacodemon",      'C', 250, 1.0f, 11, ENEMY_ATTACK_PROJECTILE, 9.50f, 3.40f, 11.5f, 0.75f, ENEMY_PROJECTILE_ENERGY_ORB, ENEMY_PROJECTILE_HOMING,    2.2f, 1.1f},
    /* Ataque: golpe brutal de maos nuas, curto e devastador. */
    {"Baron of Hell",  'B', 275, 0.9f, 17, ENEMY_ATTACK_MELEE,      1.50f, 5.20f, 10.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma: flecha reta e veloz, de dano baixo e recarga lenta. */
    {"Soldado Kushan",'K',  50, 1.3f,  5, ENEMY_ATTACK_PROJECTILE, 7.00f, 1.90f,  9.5f, 0.90f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  6.0f, 0.0f},
    /* Arma: clava pesada; o alcance estendido compensa a lentidao. */
    {"Troll",          'T', 250, 0.8f, 15, ENEMY_ATTACK_MELEE,      1.90f, 4.60f,  8.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Ataque: sequencia de garras rapida, de baixo dano por golpe. */
    {"Apostolo",       'A', 100, 2.2f,  7, ENEMY_ATTACK_MELEE,      1.00f, 2.20f,  8.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Ataque: garra de mini-chefe; um Zodd por onda no maximo, raro. */
    {"Zodd",           'Q', 550, 1.4f, 20, ENEMY_ATTACK_MELEE,      1.80f, 6.10f, 11.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f}
};

static EnemyProjectile enemy_projectiles[MAX_ENEMY_PROJECTILES];

const EnemyProjectile *enemies_projectile_pool(void) {
    return enemy_projectiles;
}

static const EnemyStats *enemy_stats_for_type(EnemyType type) {
    if ((int)type < 0 || type >= ENEMY_TYPE_COUNT) {
        return &ENEMY_STATS[ENEMY_LEGACY_Z];
    }
    return &ENEMY_STATS[type];
}

static Enemy enemy_create(EnemyType type, float x, float y) {
    const EnemyStats *stats = enemy_stats_for_type(type);
    Enemy enemy = {0};
    enemy.active = 1;
    enemy.x = x;
    enemy.y = y;
    enemy.health = stats->health;
    enemy.max_health = stats->health;
    enemy.damage = stats->damage;
    enemy.speed = stats->speed;
    enemy.symbol = stats->symbol;
    enemy.type = type;
    enemy.attack_kind = stats->attack_kind;
    enemy.attack_range = stats->attack_range;
    enemy.attack_cooldown = stats->attack_cooldown;
    enemy.aggro_range = stats->aggro_range;
    return enemy;
}

void enemies_init_defaults(EnemyManager *mgr) {
    if (!mgr) {
        return;
    }

    memset(mgr, 0, sizeof(*mgr));
    memset(enemy_projectiles, 0, sizeof(enemy_projectiles));

    static const struct {
        EnemyType type;
        float x;
        float y;
        float speed_override;
    } initial_wave[] = {
        {ENEMY_LEGACY_Z, 7.5f, 4.5f, 1.4f},
        {ENEMY_LEGACY_D, 11.5f, 7.5f, 1.8f},
        {ENEMY_LEGACY_I, 8.5f, 12.5f, 1.6f},
        {ENEMY_LEGACY_Z, 4.5f, 7.5f, 1.2f},
        {ENEMY_LEGACY_I, 12.5f, 12.5f, 1.3f},
        {ENEMY_LEGACY_Z, 10.5f, 4.5f, 1.4f},
        {ENEMY_DOOM_IMP, 5.5f, 3.5f, 0.0f},
        {ENEMY_DOOM_IMP, 9.5f, 3.5f, 0.0f},
        {ENEMY_DOOM_PINKY, 2.5f, 5.5f, 0.0f},
        {ENEMY_DOOM_CACODEMON, 10.5f, 7.5f, 0.0f},
        {ENEMY_DOOM_BARON, 6.5f, 14.5f, 0.0f},
        {ENEMY_BERSERK_KUSHAN, 1.5f, 13.5f, 0.0f},
        {ENEMY_BERSERK_KUSHAN, 13.5f, 1.5f, 0.0f},
        {ENEMY_BERSERK_TROLL, 14.5f, 7.5f, 0.0f},
        {ENEMY_BERSERK_APOSTLE, 5.5f, 11.5f, 0.0f},
        {ENEMY_BERSERK_ZODD, 9.5f, 14.5f, 0.0f}
    };

    int wave_count = (int)(sizeof(initial_wave) / sizeof(initial_wave[0]));
    for (int i = 0; i < wave_count && mgr->count < MAX_ENEMIES; i++) {
        if (initial_wave[i].type == ENEMY_BERSERK_ZODD && rand() % 5 != 0) {
            continue;
        }
        Enemy enemy = enemy_create(initial_wave[i].type,
                                   initial_wave[i].x,
                                   initial_wave[i].y);
        if (initial_wave[i].speed_override > 0.0f) {
            enemy.speed = initial_wave[i].speed_override;
        }
        mgr->enemies[mgr->count++] = enemy;
    }
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

static int scale_enemy_damage(const Player *player, int damage) {
    /* Stats use 100 HP as baseline so each class takes the same damage percentage. */
    int max_health = player->max_health > 0 ? player->max_health : 100;
    return (int)roundf((float)damage * (float)max_health / 100.0f);
}

static int enemy_spawn_projectile(const Enemy *enemy, const Player *player,
                                  const EnemyStats *stats) {
    int slot = -1;
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        if (!enemy_projectiles[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0;

    float dx = player->x - enemy->x;
    float dy = player->y - enemy->y;
    float distance = sqrtf(dx * dx + dy * dy);
    if (distance < 0.001f) return 0;

    EnemyProjectile *projectile = &enemy_projectiles[slot];
    projectile->active = 1;
    projectile->x = enemy->x + dx / distance * 0.35f;
    projectile->y = enemy->y + dy / distance * 0.35f;
    projectile->dx = dx / distance;
    projectile->dy = dy / distance;
    projectile->speed = stats->projectile_speed;
    projectile->turn_rate = stats->projectile_turn_rate;
    projectile->damage = enemy->damage;
    projectile->kind = stats->projectile_kind;
    projectile->path = stats->projectile_path;
    return 1;
}

static void enemy_projectiles_update(const GameMap *map, Player *player, float dt) {
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        EnemyProjectile *projectile = &enemy_projectiles[i];
        if (!projectile->active) continue;

        int steps = (int)ceilf(projectile->speed * dt / 0.08f);
        if (steps < 1) steps = 1;
        if (steps > 128) steps = 128;
        float step_dt = dt / (float)steps;
        float hit_radius = projectile->kind == ENEMY_PROJECTILE_ARROW ? 0.12f : 0.30f;

        for (int step = 0; step < steps && projectile->active; step++) {
            if (projectile->path == ENEMY_PROJECTILE_HOMING) {
                float target_angle = atan2f(player->y - projectile->y,
                                            player->x - projectile->x);
                float current_angle = atan2f(projectile->dy, projectile->dx);
                float angle_delta = wrap_angle(target_angle - current_angle);
                float max_turn = projectile->turn_rate * step_dt;
                if (angle_delta > max_turn) angle_delta = max_turn;
                if (angle_delta < -max_turn) angle_delta = -max_turn;
                float angle = current_angle + angle_delta;
                projectile->dx = cosf(angle);
                projectile->dy = sinf(angle);
            } else if (projectile->path == ENEMY_PROJECTILE_WAVERING) {
                float angle = projectile->turn_rate * sinf(projectile->age * 6.0f) * step_dt;
                float dx = projectile->dx * cosf(angle) - projectile->dy * sinf(angle);
                projectile->dy = projectile->dx * sinf(angle) + projectile->dy * cosf(angle);
                projectile->dx = dx;
            }

            projectile->x += projectile->dx * projectile->speed * step_dt;
            projectile->y += projectile->dy * projectile->speed * step_dt;
            projectile->age += step_dt;

            if (map_is_wall(map, (int)floorf(projectile->x), (int)floorf(projectile->y)) ||
                projectile->age >= 12.0f) {
                projectile->active = 0;
                break;
            }

            float dx = player->x - projectile->x;
            float dy = player->y - projectile->y;
            float collision_radius = hit_radius + PLAYER_RADIUS;
            if (dx * dx + dy * dy <= collision_radius * collision_radius) {
                player_take_damage(player, scale_enemy_damage(player, projectile->damage));
                projectile->active = 0;
            }
        }
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

    enemy_projectiles_update(map, player, dt);

    for (int i = 0; i < mgr->count; i++) {
        Enemy *e = &mgr->enemies[i];
        if (!e->active) {
            continue;
        }

        const EnemyStats *stats = enemy_stats_for_type(e->type);
        float attack_range = e->attack_range > 0.0f ? e->attack_range : stats->attack_range;
        float attack_cooldown = e->attack_cooldown > 0.0f
            ? e->attack_cooldown : stats->attack_cooldown;
        float aggro_range = e->aggro_range > 0.0f ? e->aggro_range : stats->aggro_range;

        if (e->attack_timer > 0.0f) {
            e->attack_timer -= dt;
            if (e->attack_timer < 0.0f) e->attack_timer = 0.0f;
        }

        float dx = player->x - e->x;
        float dy = player->y - e->y;
        float dist = sqrtf(dx * dx + dy * dy);
        float preferred_distance = (e->attack_kind == ENEMY_ATTACK_HITSCAN ||
                                    e->attack_kind == ENEMY_ATTACK_PROJECTILE)
            ? attack_range * 0.8f : attack_range;

        if (dist > preferred_distance && dist < aggro_range) {
            float move_speed = e->speed;
            if ((e->type == ENEMY_DOOM_PINKY || e->type == ENEMY_BERSERK_APOSTLE) &&
                dist <= 3.2f) {
                move_speed *= e->type == ENEMY_DOOM_PINKY ? 2.2f : 1.9f;
            }
            float step = fminf(move_speed * dt, dist - preferred_distance);
            float nx = e->x + (dx / dist) * step;
            float ny = e->y + (dy / dist) * step;

            if (!map_is_wall(map, (int)floorf(nx), (int)floorf(e->y))) {
                e->x = nx;
            }
            if (!map_is_wall(map, (int)floorf(e->x), (int)floorf(ny))) {
                e->y = ny;
            }
        }

        if (dist <= attack_range && e->attack_timer <= 0.0f &&
            has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
            if (e->attack_kind == ENEMY_ATTACK_PROJECTILE) {
                (void)enemy_spawn_projectile(e, player, stats);
            } else if (e->attack_kind == ENEMY_ATTACK_HITSCAN) {
                float distance_factor = 1.0f - 0.6f * dist / attack_range;
                float hit_chance = stats->precision * fmaxf(0.0f, distance_factor);
                float roll = (float)rand() / ((float)RAND_MAX + 1.0f);
                if (roll < hit_chance) {
                    player_take_damage(player, scale_enemy_damage(player, e->damage));
                }
            } else {
                player_take_damage(player, scale_enemy_damage(player, e->damage));
            }
            e->attack_timer = attack_cooldown;
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
        /* Berserk hits hard, but a single swing must not erase a boss. */
        damage_enemy(target, player, berserk
            ? player_attack_damage(player, weapon->damage) * 4
            : player_attack_damage(player, weapon->damage) * multiplier);
        return 1;
    }

    return 0;
}
