#include "entities.h"
#include <math.h>
#include <stdint.h>
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
    {"Z legado",       'Z',  45, 1.00f,  6, ENEMY_ATTACK_MELEE,      0.85f, 1.80f,  7.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Hitscan de medio alcance para diferenciar dos atacantes corpo a corpo. */
    {"D legado",       'D',  95, 1.50f,  9, ENEMY_ATTACK_HITSCAN,    6.50f, 2.10f, 10.0f, 0.78f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    {"I legado",       'I',  65, 2.10f,  7, ENEMY_ATTACK_MELEE,      0.95f, 1.40f,  9.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma: bola de fogo oscilante de medio alcance e dano moderado. */
    {"Imp",            'M',  70, 1.20f,  8, ENEMY_ATTACK_PROJECTILE, 8.00f, 1.75f, 10.0f, 0.95f, ENEMY_PROJECTILE_FIREBALL,   ENEMY_PROJECTILE_WAVERING,  3.1f, 0.9f},
    /* Ataque: investida veloz de curta distancia. */
    {"Pinky",          'Y',  85, 2.40f, 10, ENEMY_ATTACK_MELEE,      1.20f, 1.50f, 10.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma: orbe de energia teleguiado, tanque lento de longo alcance. */
    {"Cacodemon",      'C', 220, 0.90f, 14, ENEMY_ATTACK_PROJECTILE,10.50f, 3.60f, 12.5f, 0.75f, ENEMY_PROJECTILE_ENERGY_ORB, ENEMY_PROJECTILE_HOMING,    2.2f, 1.1f},
    /* Ataque: golpe brutal de maos nuas, muito lento e devastador. */
    {"Baron of Hell",  'B', 340, 0.75f, 24, ENEMY_ATTACK_MELEE,      1.60f, 4.50f,  9.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma: sniper fragil com flecha reta veloz e dano alto. */
    {"Soldado Kushan", 'K',  40, 1.10f, 11, ENEMY_ATTACK_PROJECTILE, 9.50f, 2.40f, 11.5f, 0.90f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  6.0f, 0.0f},
    /* Arma: clava pesada; alcance estendido compensa a lentidao extrema. */
    {"Troll",          'T', 290, 0.70f, 18, ENEMY_ATTACK_MELEE,      2.05f, 4.00f,  8.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Ataque: sequencia frenetica de garras, dano baixo por golpe e recarga minima. */
    {"Apostolo",       'A', 120, 2.60f,  5, ENEMY_ATTACK_MELEE,      1.05f, 0.85f,  9.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Ataque: garra de mini-chefe implacavel. */
    {"Zodd",           'Q', 600, 1.50f, 28, ENEMY_ATTACK_MELEE,      1.90f, 4.80f, 12.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f}
};

static EnemyProjectile enemy_projectiles[MAX_ENEMY_PROJECTILES];
static float g_fire_damage_acc = 0.0f;
static float g_fire_burn_timer = 0.0f;

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
    g_fire_damage_acc = 0.0f;
    g_fire_burn_timer = 0.0f;
}

static int enemy_type_from_symbol(char symbol, EnemyType *out_type) {
    for (int i = 0; i < ENEMY_TYPE_COUNT; i++) {
        if (ENEMY_STATS[i].symbol == symbol) {
            if (out_type) {
                *out_type = (EnemyType)i;
            }
            return 1;
        }
    }
    return 0;
}

int enemies_alive(const EnemyManager *mgr) {
    int count = 0;
    for (int i = 0; i < mgr->count; i++) if (mgr->enemies[i].active) count++;
    return count;
}

void enemies_spawn_wave(EnemyManager *mgr, const GameMap *map, const Player *p) {
    if (!mgr) {
        return;
    }

    enemies_init_defaults(mgr);
    if (!map) {
        return;
    }
    (void)p;

    for (int y = 0; y < map->height && mgr->count < MAX_ENEMIES; y++) {
        for (int x = 0; x < map->width && mgr->count < MAX_ENEMIES; x++) {
            char tile = map_get_raw_tile(map, x, y);
            EnemyType type;
            if (!enemy_type_from_symbol(tile, &type)) {
                continue;
            }
            mgr->enemies[mgr->count++] = enemy_create(type, (float)x + 0.5f, (float)y + 0.5f);
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
                player_take_damage(player, projectile->damage);
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

static void update_map_fire_hazards(const GameMap *map, Player *player, float dt) {
    map_update_traps(dt);

    const float r = 0.18f;
    const float offsets[5][2] = {
        {0.0f, 0.0f}, {r, 0.0f}, {-r, 0.0f}, {0.0f, r}, {0.0f, -r}
    };
    int in_fire = 0;

    for (int k = 0; k < 5; k++) {
        int cx = (int)floorf(player->x + offsets[k][0]);
        int cy = (int)floorf(player->y + offsets[k][1]);
        char raw = map_get_raw_tile(map, cx, cy);
        if (map_is_trap_tile(raw)) {
            map_trigger_trap(cx, cy);
        }
        if (map_is_fire_active_at(map, cx, cy)) {
            in_fire = 1;
        }
        if (map_try_collect_medkit(map, cx, cy)) {
            int max_hp = player->max_health > 0 ? player->max_health : 100;
            player->health += 20;
            if (player->health > max_hp) {
                player->health = max_hp;
            }
        }
    }

    if (in_fire) {
        g_fire_burn_timer = 1.0f;
    } else if (g_fire_burn_timer > 0.0f) {
        g_fire_burn_timer -= dt;
        if (g_fire_burn_timer < 0.0f) {
            g_fire_burn_timer = 0.0f;
        }
    }

    if (in_fire || g_fire_burn_timer > 0.0f) {
        g_fire_damage_acc += 10.0f * dt;
        int dmg = (int)g_fire_damage_acc;
        if (dmg > 0) {
            g_fire_damage_acc -= (float)dmg;
            player_take_damage(player, dmg);
        }
    } else {
        g_fire_damage_acc = 0.0f;
    }
}

void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt) {
    if (!mgr || !map || !player || dt <= 0.0f || player->health <= 0) {
        return;
    }

    enemy_projectiles_update(map, player, dt);
    update_map_fire_hazards(map, player, dt);

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
                    player_take_damage(player, e->damage);
                }
            } else {
                player_take_damage(player, e->damage);
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


/* ============================================================================
 * ARTE ASCII, SCALE2X MIPMAPS E RENDERIZACAO DOS INIMIGOS (MODULO ENTITIES)
 * ============================================================================ */

#define ENEMY_ART_WIDTH 9
#define ENEMY_ART_HEIGHT 12
#define ENEMY_ART_MAX_HEIGHT 13

typedef struct {
    char label;
    float width_ratio;
    float height_scale;
    const char *rows[ENEMY_ART_MAX_HEIGHT];
    AsciiColor palette[5];
} EnemyDesign;

static const EnemyDesign ENEMY_DESIGNS[] = {
    {'Z', 0.60f, 0.92f, {
        "....^....", "...###...", "..#+++#..", "..#*@*#..",
        "...###...", "..#####..", ".###+###.", ".###+###.",
        "...#=#...", "..##=##..", "..#...#..", ".##...##."
    }, {{120,150,120}, {155,180,145}, {45,65,52}, {220,235,170}, {200,205,185}}},
    {'D', 0.72f, 1.05f, {
        "...^^^...", "..#####..", "..#+++#..", "..#*@*#..",
        ".#######.", "#########", "###+#####", "###+#####",
        "..##=##..", ".###=###.", ".##...##.", "##.....##"
    }, {{110,130,160}, {155,175,205}, {35,50,75}, {210,230,255}, {180,195,220}}},
    {'I', 0.62f, 0.98f, {
        "....^....", "...###...", "..##+##..", "..#*@*#..",
        "..#####..", ".#######.", "..##+##..", "...##=##.",
        "...#.#...", "..##.##..", "..#...#..", ".##...##."
    }, {{160,150,100}, {195,185,125}, {65,58,35}, {245,225,120}, {215,205,165}}},
    {'M', 0.62f, 1.10f, {
        "..DBBBD..", "..BEBEB..", "..BCCCB..", ".SBBBBBS.",
        ".BBBDBBB.", "SB.BDB.BS", "B..BBB..B", "C..DBD..C",
        "..BBDBB..", "..BB.BB..", ".SBB.BBS.", "..BB.BB..",
        ".CCC.CCC."
    }, {{140,85,45}, {100,60,30}, {65,35,25}, {235,30,30}, {235,230,215}}},
    {'Y', 1.12f, 0.76f, {
        ".........", ".........", "..####...", ".#######.",
        ".##+++##.", ".#*@#*@#.", "#########", "##===####",
        "#########", ".##...##.", ".........", "........."
    }, {{220,120,140}, {240,155,165}, {90,35,48}, {255,215,205}, {245,185,175}}},
    {'C', 1.02f, 0.94f, {
        ".........", "...###...", "..#####..", ".#######.",
        "##@@@@@##", "##@***@##", "##@@@@@##", ".#######.",
        "..##!##..", "...!!!...", ".........", "........."
    }, {{210,40,40}, {170,28,30}, {65,10,20}, {220,235,90}, {245,210,160}}},
    {'B', 1.08f, 1.20f, {
        "^.......^", "##^...^##", ".#######.", "..#+++#..",
        "..#*@*#..", ".#######.", "#########", "###+#####",
        "..##=##..", "..##=##..", ".##...##.", "##.....##"
    }, {{140,30,30}, {180,65,58}, {48,8,12}, {255,125,70}, {225,150,115}}},
    {'K', 0.48f, 0.68f, {
        "....+....", "...###...", "...#@#...", "...###...",
        "....#....", "..#####..", "..##+##..", "....#....",
        "...##=##.", "..#...#..", "..#...#..", "........."
    }, {{150,155,165}, {215,225,235}, {52,60,70}, {250,255,255}, {190,210,220}}},
    {'T', 1.14f, 1.02f, {
        "..#####..", ".#######.", "#########", "#.####.##",
        "##@###@##", ".########", "#########", "###++++##",
        "=##=###=.", "=##=###=.", "..##..#..", ".##...##."
    }, {{80,110,50}, {110,140,68}, {34,48,25}, {180,195,80}, {130,150,85}}},
    {'A', 0.68f, 1.00f, {
        "....^^...", "...####..", "...#++#..", "..##*@##.",
        "..######.", "..#######", "..##+##..", "..##=##..",
        "..#..##..", ".##...#..", "##.......", "..#......"
    }, {{100,40,130}, {155,70,185}, {38,12,52}, {235,170,255}, {200,100,235}}},
    {'Q', 1.12f, 1.28f, {
        "^..###..^", "##^###^##", ".###+###.", "..#####..",
        ".##@*@##.", "#########", "###+#####", "##=###=##",
        "##=###=##", ".##===##.", "##.....##", "##.....##"
    }, {{40,10,15}, {90,20,28}, {14,5,8}, {210,170,60}, {255,220,110}}}
};

static const EnemyDesign ENEMY_DESIGN_FALLBACK = {
    '?', 0.62f, 1.0f, {
        "....^....", "...###...", "..#+++#..", "..#*@*#..",
        "...###...", "..#####..", ".###+###.", ".###+###.",
        "...#=#...", "..##=##..", "..#...#..", ".##...##."
    }, {{170,150,170}, {205,185,205}, {65,55,70}, {255,210,230}, {225,195,220}}
};

static const EnemyDesign *enemy_design_for_label(char label) {
    int design_count = (int)(sizeof(ENEMY_DESIGNS) / sizeof(ENEMY_DESIGNS[0]));
    for (int i = 0; i < design_count; i++) {
        if (ENEMY_DESIGNS[i].label == label) return &ENEMY_DESIGNS[i];
    }
    return &ENEMY_DESIGN_FALLBACK;
}

static int enemy_art_color_index(char glyph) {
    switch (glyph) {
        case '#': return 0;
        case '+': return 1;
        case '=': return 2;
        case '@':
        case '*': return 3;
        case '^':
        case '!':
        case '|': return 4;
        default: return -1;
    }
}

static int imp_art_color_index(char token) {
    switch (token) {
        case 'B': return 0;
        case 'D': return 1;
        case 'E': return 3;
        case 'S':
        case 'C': return 4;
        default: return -1;
    }
}

static char imp_art_glyph(char token) {
    switch (token) {
        case 'B': return '#';
        case 'D': return '%';
        case 'E': return 'O';
        case 'S': return '^';
        case 'C': return 'v';
        default: return token;
    }
}

#define REDESIGN_ART_MAX_WIDTH 17
#define REDESIGN_ART_MAX_HEIGHT 20
#define REDESIGN_IMP_HEIGHT_SCALE 1.10f

typedef struct {
    char glyph;
    AsciiColor color;
    unsigned char brighten;
} EnemyArtStyle;

typedef struct {
    char label;
    int width;
    int height;
    float width_ratio;
    float world_height;
    char effect_token;
    AsciiColor effect_color;
    const EnemyArtStyle *styles;
    const char *rows[REDESIGN_ART_MAX_HEIGHT];
} EnemyVisualDesign;

#define ENEMY_STYLE(token, glyph, red, green, blue, body) \
    [token] = {glyph, {red, green, blue}, body}

static const EnemyArtStyle LEGACY_Z_STYLE[128] = {
    ENEMY_STYLE('S', '#', 120, 150, 120, 1),
    ENEMY_STYLE('D', '%', 85, 110, 85, 1),
    ENEMY_STYLE('E', 'o', 200, 40, 40, 0),
    ENEMY_STYLE('R', '=', 70, 80, 60, 0)
};

static const EnemyArtStyle LEGACY_D_STYLE[128] = {
    ENEMY_STYLE('S', '#', 110, 130, 160, 1),
    ENEMY_STYLE('D', '%', 80, 95, 120, 1),
    ENEMY_STYLE('E', 'o', 200, 40, 40, 0),
    ENEMY_STYLE('R', '=', 50, 60, 90, 0)
};

static const EnemyArtStyle LEGACY_I_STYLE[128] = {
    ENEMY_STYLE('S', '#', 160, 150, 100, 1),
    ENEMY_STYLE('D', '%', 120, 110, 70, 1),
    ENEMY_STYLE('E', 'o', 200, 40, 40, 0),
    ENEMY_STYLE('R', '=', 95, 85, 55, 0)
};

static const EnemyArtStyle PINKY_STYLE[128] = {
    ENEMY_STYLE('P', '#', 225, 125, 145, 1),
    ENEMY_STYLE('R', '%', 170, 85, 105, 1),
    ENEMY_STYLE('G', 'o', 230, 190, 40, 0),
    ENEMY_STYLE('M', '=', 120, 20, 30, 0),
    ENEMY_STYLE('T', 'v', 240, 235, 225, 0),
    ENEMY_STYLE('H', '^', 235, 230, 205, 0),
    ENEMY_STYLE('C', 'v', 240, 235, 225, 0)
};

static const EnemyArtStyle CACODEMON_STYLE[128] = {
    ENEMY_STYLE('R', '#', 210, 40, 40, 1),
    ENEMY_STYLE('D', '%', 150, 25, 30, 1),
    ENEMY_STYLE('E', 'O', 235, 225, 90, 0),
    ENEMY_STYLE('P', '@', 25, 90, 35, 0),
    ENEMY_STYLE('H', '^', 235, 230, 205, 0),
    ENEMY_STYLE('M', '=', 60, 10, 15, 0),
    ENEMY_STYLE('T', 'v', 240, 235, 225, 0)
};

static const EnemyArtStyle BARON_STYLE[128] = {
    ENEMY_STYLE('P', '#', 215, 120, 130, 1),
    ENEMY_STYLE('D', '%', 160, 80, 95, 1),
    ENEMY_STYLE('F', '@', 225, 220, 200, 0),
    ENEMY_STYLE('E', 'O', 235, 40, 30, 0),
    ENEMY_STYLE('H', '^', 190, 185, 165, 0),
    ENEMY_STYLE('L', '#', 120, 75, 40, 0),
    ENEMY_STYLE('K', '%', 85, 50, 25, 0),
    ENEMY_STYLE('N', 'v', 240, 140, 40, 0),
    ENEMY_STYLE('X', '=', 35, 28, 25, 0)
};

static const EnemyArtStyle KUSHAN_STYLE[128] = {
    ENEMY_STYLE('M', '#', 150, 155, 165, 1),
    ENEMY_STYLE('D', '%', 100, 105, 115, 1),
    ENEMY_STYLE('H', '^', 210, 215, 225, 0),
    ENEMY_STYLE('F', 'o', 150, 100, 70, 0),
    ENEMY_STYLE('W', ')', 140, 95, 50, 0),
    ENEMY_STYLE('R', '=', 170, 40, 40, 0)
};

static const EnemyArtStyle TROLL_STYLE[128] = {
    ENEMY_STYLE('G', '#', 80, 110, 50, 1),
    ENEMY_STYLE('D', '%', 55, 80, 35, 1),
    ENEMY_STYLE('E', 'o', 230, 200, 60, 0),
    ENEMY_STYLE('N', '*', 210, 120, 120, 0),
    ENEMY_STYLE('T', 'v', 235, 230, 210, 0),
    ENEMY_STYLE('W', '|', 110, 75, 40, 0)
};

static const EnemyArtStyle APOSTLE_STYLE[128] = {
    ENEMY_STYLE('V', '#', 100, 40, 130, 1),
    ENEMY_STYLE('D', '%', 60, 20, 85, 1),
    ENEMY_STYLE('E', 'O', 200, 230, 70, 0),
    ENEMY_STYLE('T', 'v', 235, 230, 210, 0),
    ENEMY_STYLE('C', 'v', 225, 215, 190, 0)
};

static const EnemyArtStyle ZODD_STYLE[128] = {
    ENEMY_STYLE('B', '#', 35, 25, 30, 1),
    ENEMY_STYLE('D', '%', 80, 15, 25, 1),
    ENEMY_STYLE('E', 'O', 230, 170, 50, 0),
    ENEMY_STYLE('H', '^', 225, 215, 190, 0),
    ENEMY_STYLE('W', '*', 90, 20, 35, 0),
    ENEMY_STYLE('T', 'v', 240, 235, 225, 0),
    ENEMY_STYLE('C', 'v', 225, 215, 190, 0),
    ENEMY_STYLE('X', '=', 50, 45, 45, 0)
};

static const EnemyVisualDesign ENEMY_VISUAL_DESIGNS[] = {
    {'Z', 9, 12, 0.60f, 0.95f, '\0', {0,0,0}, LEGACY_Z_STYLE, {
        "..DSSSD..", "..SESES..", "..SSDSS..", "...SSS...",
        ".RRRRRRR.", "SRRRDRRRS", "S.RRRRR.S", "S.RRDRR.S",
        "..RRRRR..", "..RR.RR..", "..SS.SS..", "..DD.DD.."
    }},
    {'D', 9, 12, 0.72f, 1.05f, '\0', {0,0,0}, LEGACY_D_STYLE, {
        "..DSSSD..", "..SESES..", "..SSDSS..", "...SSS...",
        ".RRRRRRR.", "SRRRDRRRS", "S.RRRRR.S", "S.RRDRR.S",
        "..RRRRR..", "..RR.RR..", "..SS.SS..", "..DD.DD.."
    }},
    {'I', 9, 12, 0.62f, 1.00f, '\0', {0,0,0}, LEGACY_I_STYLE, {
        "..DSSSD..", "..SESES..", "..SSDSS..", "...SSS...",
        ".RRRRRRR.", "SRRRDRRRS", "S.RRRRR.S", "S.RRDRR.S",
        "..RRRRR..", "..RR.RR..", "..SS.SS..", "..DD.DD.."
    }},
    {'Y', 13, 10, 1.25f, 0.85f, '\0', {0,0,0}, PINKY_STYLE, {
        "...H.....H...", "..RPPPPPPPR..", "..PGPPPPPGP..",
        "..PMTMTMTMP..", ".PPRPPPPPRPP.", "PPPPRPPPRPPPP",
        "RPP.RPPPR.PPR", "CP..RPPPR..PC", "..PPP...PPP..",
        ".CCCC...CCCC."
    }},
    {'C', 13, 11, 1.15f, 0.95f, 'M', {255,160,40}, CACODEMON_STYLE, {
        "...H.....H...", "...DRRRRRD...", "..DRRRRRRRD..",
        ".DRRREEERRRD.", ".RRREEPEERRR.", ".DRRREEERRRD.",
        "RRRRRRRRRRRRR", "RDMTMTMTMTMDR", "RRDMMMMMMMDRR",
        ".DRDDDDDDDRD.", "..DDDDDDDDD.."
    }},
    {'B', 13, 17, 1.20f, 1.30f, '\0', {0,0,0}, BARON_STYLE, {
        "H...........H", "HH..FFFFF..HH", ".H..FEFEF..H.",
        "...FFFFFFF...", "..PPPPPPPPP..", ".PPDPPPPPDPP.",
        "PPPDPPPPPDPPP", "PPDPPPPPPPDPP", "PP.DPPPPPD.PP",
        "NN.PPDPDPP.NN", "NN..PPPPP..NN", "....DPPPD....",
        "...LLL.LLL...", "...LKL.LKL...", "....LL.LL....",
        "...LL...LL...", "..XXX...XXX.."
    }},
    {'K', 9, 11, 0.66f, 0.85f, 'W', {255,230,120}, KUSHAN_STYLE, {
        "....H....", "..DHHHD..", "..DFFFD..", "..DMMMD..",
        ".MMMMMMM.", "WMMMDMMM.", "W.MRRRM..", "W.MMDMM..",
        "W..MMM...", "...M.M...", "..DD.DD.."
    }},
    {'T', 15, 16, 1.20f, 1.40f, '\0', {0,0,0}, TROLL_STYLE, {
        "....DGGGGGD.WW.", "...DGEGGGEGD.WW", "...GGGNNNGGG.WW",
        "...DGTGGGTGD..W", "..GGGGGGGGGGG.W", ".GGDGGGGGGGDGGW",
        "GGGGDGGGGGDGGGW", "GGDG.GGGGG.GGGW", "GG.G.DGGGD.G.GW",
        "DD....GGGGG..DD", "....DGGGGGD....", "....GGDGDGG....",
        "....GG...GG....", "...GGG...GGG...", "...GGG...GGG...",
        "..DDDD...DDDD.."
    }},
    {'A', 11, 14, 0.95f, 1.10f, '\0', {0,0,0}, APOSTLE_STYLE, {
        "..DVVD.....", ".DEVEVD....", ".VVVTTVV...", "..DVVVVVD..",
        ".VVVDVVVVV.", "VVV.VVVV.VV", "VV..DVVV..V",
        "C...VVVD..V", "....DVVV..V", "....VVDV..V",
        "....VV.VV.V", "...VV..VV.C", "...VV..VV..",
        "..CCC.CCC.."
    }},
    {'Q', 17, 20, 1.45f, 1.70f, 'E', {255,60,40}, ZODD_STYLE, {
        "H...............H", "HH....BBBBB....HH", ".H...DBEBEBD...H.",
        "..W..DBTTTBD..W..", ".WW..DBBBBBD..WW.", "WWW.DBBBBBBBD.WWW",
        "WWWWDBBDBDBBDWWWW", "WWWWBBBBBBBBBWWWW",
        ".WWWBBDBBBDBBWWW.", ".WWBBBBBBBBBBBWW.",
        "..WBBBBDBDBBBBW..", "...BBBBBBBBBBB...",
        "..CBBB.BBB.BBBC..", "..CBB..BDB..BBC..",
        ".....BBBBBBB.....", ".....BBDBDBB.....",
        "....BBB...BBB....", "....BBB...BBB....",
        "....DDD...DDD....", "...XXXX...XXXX..."
    }}
};

#undef ENEMY_STYLE

static const EnemyVisualDesign *enemy_visual_design_for_label(char label) {
    int design_count = (int)(sizeof(ENEMY_VISUAL_DESIGNS) / sizeof(ENEMY_VISUAL_DESIGNS[0]));
    for (int i = 0; i < design_count; i++) {
        if (ENEMY_VISUAL_DESIGNS[i].label == label) return &ENEMY_VISUAL_DESIGNS[i];
    }
    return NULL;
}

#define SPRITE_UPSCALE 4
#define SPRITE_SOURCE_MAX_WIDTH 17
#define SPRITE_SOURCE_MAX_HEIGHT 20
#define SPRITE_UPSCALED_MAX_WIDTH (SPRITE_SOURCE_MAX_WIDTH * SPRITE_UPSCALE + 2)
#define SPRITE_UPSCALED_MAX_HEIGHT (SPRITE_SOURCE_MAX_HEIGHT * SPRITE_UPSCALE + 2)
#define SPRITE_MIP_HALF_MAX_WIDTH ((SPRITE_UPSCALED_MAX_WIDTH + 1) / 2)
#define SPRITE_MIP_HALF_MAX_HEIGHT ((SPRITE_UPSCALED_MAX_HEIGHT + 1) / 2)
#define SPRITE_MIP_QUARTER_MAX_WIDTH ((SPRITE_UPSCALED_MAX_WIDTH + 3) / 4)
#define SPRITE_MIP_QUARTER_MAX_HEIGHT ((SPRITE_UPSCALED_MAX_HEIGHT + 3) / 4)

enum {
    ENEMY_VISUAL_DESIGN_COUNT = sizeof(ENEMY_VISUAL_DESIGNS) / sizeof(ENEMY_VISUAL_DESIGNS[0]),
    ENEMY_IMP_CACHE_INDEX = ENEMY_VISUAL_DESIGN_COUNT,
    ENEMY_FALLBACK_CACHE_INDEX = ENEMY_VISUAL_DESIGN_COUNT + 1,
    ENEMY_SPRITE_CACHE_COUNT = ENEMY_VISUAL_DESIGN_COUNT + 2
};

typedef struct {
    char glyph;
    char token;
    AsciiColor color;
} EnemyRenderCell;

typedef struct {
    int source_width;
    int source_height;
    int full_width;
    int full_height;
    int half_width;
    int half_height;
    int quarter_width;
    int quarter_height;
    EnemyRenderCell full[SPRITE_UPSCALED_MAX_HEIGHT][SPRITE_UPSCALED_MAX_WIDTH];
    EnemyRenderCell half[SPRITE_MIP_HALF_MAX_HEIGHT][SPRITE_MIP_HALF_MAX_WIDTH];
    EnemyRenderCell quarter[SPRITE_MIP_QUARTER_MAX_HEIGHT][SPRITE_MIP_QUARTER_MAX_WIDTH];
} EnemySpriteCache;

static EnemySpriteCache enemy_sprite_caches[ENEMY_SPRITE_CACHE_COUNT];
static EnemyRenderCell enemy_sprite_source[SPRITE_SOURCE_MAX_HEIGHT][SPRITE_SOURCE_MAX_WIDTH];
static EnemyRenderCell enemy_sprite_scale2[SPRITE_SOURCE_MAX_HEIGHT * 2][SPRITE_SOURCE_MAX_WIDTH * 2];
static int enemy_sprite_pipeline_ready = 0;

static unsigned char clamp_u8_local(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (unsigned char)v;
}

static AsciiColor scale_rgb_local(unsigned char r, unsigned char g, unsigned char b, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.35f) factor = 1.35f;
    AsciiColor c = {
        clamp_u8_local((float)r * factor),
        clamp_u8_local((float)g * factor),
        clamp_u8_local((float)b * factor)
    };
    return c;
}

static EnemyRenderCell enemy_source_cell(char label, const EnemyVisualDesign *visual,
                                         const EnemyDesign *source, int x, int y) {
    EnemyRenderCell cell = {0};
    char token = visual ? visual->rows[y][x] : source->rows[y][x];
    if (token == '.') return cell;

    if (visual) {
        EnemyArtStyle style = visual->styles[(unsigned char)token];
        if (style.glyph) {
            cell.glyph = style.glyph;
            cell.token = token;
            cell.color = style.color;
        }
    } else if (label == 'M') {
        int color_index = imp_art_color_index(token);
        if (color_index >= 0) {
            cell.glyph = imp_art_glyph(token);
            cell.token = token;
            cell.color = source->palette[color_index];
        }
    } else {
        int color_index = enemy_art_color_index(token);
        if (color_index >= 0) {
            cell.glyph = token;
            cell.token = token;
            cell.color = source->palette[color_index];
        }
    }
    return cell;
}

static EnemyRenderCell enemy_pixel_at(const EnemyRenderCell *pixels, int width,
                                      int height, int stride, int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        EnemyRenderCell empty = {0};
        return empty;
    }
    return pixels[y * stride + x];
}

static int enemy_pixels_equal(EnemyRenderCell a, EnemyRenderCell b) {
    if (!a.glyph || !b.glyph) return !a.glyph && !b.glyph;
    return a.glyph == b.glyph && a.token == b.token &&
           a.color.r == b.color.r && a.color.g == b.color.g &&
           a.color.b == b.color.b;
}

static void enemy_scale2x(const EnemyRenderCell *source, int width, int height,
                          int source_stride, EnemyRenderCell *dest, int dest_stride) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            EnemyRenderCell e = enemy_pixel_at(source, width, height, source_stride, x, y);
            EnemyRenderCell b = enemy_pixel_at(source, width, height, source_stride, x, y - 1);
            EnemyRenderCell d = enemy_pixel_at(source, width, height, source_stride, x - 1, y);
            EnemyRenderCell f = enemy_pixel_at(source, width, height, source_stride, x + 1, y);
            EnemyRenderCell h = enemy_pixel_at(source, width, height, source_stride, x, y + 1);
            EnemyRenderCell e0 = e, e1 = e, e2 = e, e3 = e;

            if (!enemy_pixels_equal(b, h) && !enemy_pixels_equal(d, f)) {
                if (enemy_pixels_equal(d, b)) e0 = d;
                if (enemy_pixels_equal(b, f)) e1 = f;
                if (enemy_pixels_equal(d, h)) e2 = d;
                if (enemy_pixels_equal(h, f)) e3 = h;
            }

            int dx = x * 2;
            int dy = y * 2;
            dest[dy * dest_stride + dx] = e0;
            dest[dy * dest_stride + dx + 1] = e1;
            dest[(dy + 1) * dest_stride + dx] = e2;
            dest[(dy + 1) * dest_stride + dx + 1] = e3;
        }
    }
}

static EnemyRenderCell enemy_box_filter(const EnemyRenderCell *source, int source_width,
                                        int source_height, int source_stride,
                                        int x0, int y0, int x1, int y1) {
    unsigned int red = 0, green = 0, blue = 0;
    int total = (x1 - x0) * (y1 - y0);
    int covered = 0;
    float center_x = ((float)x0 + (float)x1) * 0.5f;
    float center_y = ((float)y0 + (float)y1) * 0.5f;
    float closest = INFINITY;
    EnemyRenderCell selected = {0};

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            EnemyRenderCell cell = enemy_pixel_at(source, source_width, source_height,
                                                  source_stride, x, y);
            if (!cell.glyph) continue;

            red += cell.color.r;
            green += cell.color.g;
            blue += cell.color.b;
            covered++;

            float dx = ((float)x + 0.5f) - center_x;
            float dy = ((float)y + 0.5f) - center_y;
            float distance = dx * dx + dy * dy;
            if (distance < closest) {
                closest = distance;
                selected = cell;
            }
        }
    }

    if (total <= 0 || covered * 4 < total) {
        EnemyRenderCell empty = {0};
        return empty;
    }
    selected.color.r = (unsigned char)((red + (unsigned int)(covered / 2)) / (unsigned int)covered);
    selected.color.g = (unsigned char)((green + (unsigned int)(covered / 2)) / (unsigned int)covered);
    selected.color.b = (unsigned char)((blue + (unsigned int)(covered / 2)) / (unsigned int)covered);
    return selected;
}

static void enemy_generate_mip(const EnemyRenderCell *source, int source_width,
                               int source_height, int source_stride, int factor,
                               EnemyRenderCell *dest, int dest_stride,
                               int *dest_width, int *dest_height) {
    *dest_width = (source_width + factor - 1) / factor;
    *dest_height = (source_height + factor - 1) / factor;
    for (int y = 0; y < *dest_height; y++) {
        for (int x = 0; x < *dest_width; x++) {
            int x0 = x * factor;
            int y0 = y * factor;
            int x1 = x0 + factor < source_width ? x0 + factor : source_width;
            int y1 = y0 + factor < source_height ? y0 + factor : source_height;
            dest[y * dest_stride + x] = enemy_box_filter(source, source_width,
                source_height, source_stride, x0, y0, x1, y1);
        }
    }
}

static void enemy_finish_sprite_cache(EnemySpriteCache *cache) {
    for (int y = 1; y < cache->full_height - 1; y++) {
        for (int x = 1; x < cache->full_width - 1; x++) {
            EnemyRenderCell *cell = &cache->full[y][x];
            if (!cell->token) continue;

            int lit = !cache->full[y][x - 1].glyph || !cache->full[y - 1][x].glyph;
            int shadow = !cache->full[y][x + 1].glyph || !cache->full[y + 1][x].glyph;
            float factor = lit ? 1.15f : (shadow ? 0.80f : 1.0f);
            cell->color = scale_rgb_local(cell->color.r, cell->color.g, cell->color.b, factor);
        }
    }

    for (int y = 0; y < cache->full_height; y++) {
        for (int x = 0; x < cache->full_width; x++) {
            EnemyRenderCell *cell = &cache->full[y][x];
            if (cell->glyph) continue;

            EnemyRenderCell *neighbor = NULL;
            for (int dy = -1; dy <= 1 && !neighbor; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < cache->full_width &&
                        ny >= 0 && ny < cache->full_height &&
                        cache->full[ny][nx].token) {
                        neighbor = &cache->full[ny][nx];
                        break;
                    }
                }
            }
            if (neighbor) {
                cell->glyph = neighbor->glyph;
                cell->color = scale_rgb_local(neighbor->color.r, neighbor->color.g,
                                              neighbor->color.b, 0.65f);
            }
        }
    }

    enemy_generate_mip(&cache->full[0][0], cache->full_width, cache->full_height,
        SPRITE_UPSCALED_MAX_WIDTH, 2, &cache->half[0][0],
        SPRITE_MIP_HALF_MAX_WIDTH, &cache->half_width, &cache->half_height);
    enemy_generate_mip(&cache->full[0][0], cache->full_width, cache->full_height,
        SPRITE_UPSCALED_MAX_WIDTH, 4, &cache->quarter[0][0],
        SPRITE_MIP_QUARTER_MAX_WIDTH, &cache->quarter_width, &cache->quarter_height);
}

static void enemy_build_sprite_cache(EnemySpriteCache *cache, char label,
                                     int width, int height,
                                     const EnemyVisualDesign *visual,
                                     const EnemyDesign *source) {
    memset(enemy_sprite_source, 0, sizeof(enemy_sprite_source));
    memset(enemy_sprite_scale2, 0, sizeof(enemy_sprite_scale2));
    memset(cache, 0, sizeof(*cache));

    cache->source_width = width;
    cache->source_height = height;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            enemy_sprite_source[y][x] = enemy_source_cell(label, visual, source, x, y);
        }
    }

    int scale2_width = width * 2;
    int scale2_height = height * 2;
    enemy_scale2x(&enemy_sprite_source[0][0], width, height,
                  SPRITE_SOURCE_MAX_WIDTH, &enemy_sprite_scale2[0][0],
                  SPRITE_SOURCE_MAX_WIDTH * 2);

    cache->full_width = width * SPRITE_UPSCALE + 2;
    cache->full_height = height * SPRITE_UPSCALE + 2;
    enemy_scale2x(&enemy_sprite_scale2[0][0], scale2_width, scale2_height,
                  SPRITE_SOURCE_MAX_WIDTH * 2, &cache->full[1][1],
                  SPRITE_UPSCALED_MAX_WIDTH);
    enemy_finish_sprite_cache(cache);
}

static void enemy_sprite_pipeline_init(void) {
    if (enemy_sprite_pipeline_ready) return;

    memset(enemy_sprite_caches, 0, sizeof(enemy_sprite_caches));
    for (int i = 0; i < ENEMY_VISUAL_DESIGN_COUNT; i++) {
        const EnemyVisualDesign *design = &ENEMY_VISUAL_DESIGNS[i];
        enemy_build_sprite_cache(&enemy_sprite_caches[i], design->label,
            design->width, design->height, design, NULL);
    }

    const EnemyDesign *imp = enemy_design_for_label('M');
    enemy_build_sprite_cache(&enemy_sprite_caches[ENEMY_IMP_CACHE_INDEX], 'M',
        ENEMY_ART_WIDTH, ENEMY_ART_MAX_HEIGHT, NULL, imp);
    enemy_build_sprite_cache(&enemy_sprite_caches[ENEMY_FALLBACK_CACHE_INDEX], '?',
        ENEMY_ART_WIDTH, ENEMY_ART_HEIGHT, NULL, &ENEMY_DESIGN_FALLBACK);
    enemy_sprite_pipeline_ready = 1;
}

static const EnemySpriteCache *enemy_sprite_cache_for_label(char label) {
    const EnemyVisualDesign *design = enemy_visual_design_for_label(label);
    if (design) return &enemy_sprite_caches[design - ENEMY_VISUAL_DESIGNS];
    if (label == 'M') return &enemy_sprite_caches[ENEMY_IMP_CACHE_INDEX];
    return &enemy_sprite_caches[ENEMY_FALLBACK_CACHE_INDEX];
}

static const EnemyRenderCell *enemy_select_sprite_mip(const EnemySpriteCache *cache,
                                                      int target_width, int target_height,
                                                      int *width, int *height, int *stride) {
    const EnemyRenderCell *pixels = &cache->full[0][0];
    *width = cache->full_width;
    *height = cache->full_height;
    *stride = SPRITE_UPSCALED_MAX_WIDTH;

    float reduce_x = (float)*width / (float)target_width;
    float reduce_y = (float)*height / (float)target_height;
    float reduction = fmaxf(reduce_x, reduce_y);
    if (reduction >= 4.0f) {
        pixels = &cache->quarter[0][0];
        *width = cache->quarter_width;
        *height = cache->quarter_height;
        *stride = SPRITE_MIP_QUARTER_MAX_WIDTH;
    } else if (reduction >= 2.0f) {
        pixels = &cache->half[0][0];
        *width = cache->half_width;
        *height = cache->half_height;
        *stride = SPRITE_MIP_HALF_MAX_WIDTH;
    }
    return pixels;
}

static EnemyRenderCell enemy_sample_sprite_mip(const EnemyRenderCell *pixels,
                                               int texture_width, int texture_height,
                                               int texture_stride, float u, float v,
                                               int target_width, int target_height) {
    if (texture_width <= target_width && texture_height <= target_height) {
        int x = (int)(u * (float)texture_width);
        int y = (int)(v * (float)texture_height);
        if (x >= texture_width) x = texture_width - 1;
        if (y >= texture_height) y = texture_height - 1;
        return enemy_pixel_at(pixels, texture_width, texture_height, texture_stride, x, y);
    }

    float x0f = u * (float)texture_width;
    float y0f = v * (float)texture_height;
    float x1f = (u + 1.0f / (float)target_width) * (float)texture_width;
    float y1f = (v + 1.0f / (float)target_height) * (float)texture_height;
    int x0 = (int)floorf(x0f);
    int y0 = (int)floorf(y0f);
    int x1 = (int)ceilf(x1f);
    int y1 = (int)ceilf(y1f);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 >= texture_width) x0 = texture_width - 1;
    if (y0 >= texture_height) y0 = texture_height - 1;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > texture_width) x1 = texture_width;
    if (y1 > texture_height) y1 = texture_height;

    return enemy_box_filter(pixels, texture_width, texture_height, texture_stride,
                            x0, y0, x1, y1);
}

typedef struct {
    char label;
    int shooting;
    const EnemyVisualDesign *visual;
    const EnemySpriteCache *cache;
    const EnemyRenderCell *mip_pixels;
    int mip_w;
    int mip_h;
    int mip_stride;
    int last_target_w;
    int last_target_h;
    const RendererApi *api;
} EnemyRenderCtx;

static int enemy_sprite_sampler(float u, float v, int sx, int sy,
                                int sprite_w, int sprite_h,
                                float dist, float fog, float time_now,
                                const void *user_data,
                                char *out_ch, AsciiColor *out_col) {
    (void)dist;
    (void)time_now;
    EnemyRenderCtx *ctx = (EnemyRenderCtx *)(uintptr_t)user_data;
    if (!ctx || !ctx->cache || !ctx->api) {
        return 0;
    }
    const RendererApi *r = ctx->api;

    if (!ctx->mip_pixels || ctx->last_target_w != sprite_w || ctx->last_target_h != sprite_h) {
        ctx->mip_pixels = enemy_select_sprite_mip(ctx->cache, sprite_w, sprite_h,
                                                  &ctx->mip_w, &ctx->mip_h, &ctx->mip_stride);
        ctx->last_target_w = sprite_w;
        ctx->last_target_h = sprite_h;
    }

    EnemyRenderCell pixel = enemy_sample_sprite_mip(ctx->mip_pixels,
        ctx->mip_w, ctx->mip_h, ctx->mip_stride, u, v, sprite_w, sprite_h);
    if (!pixel.glyph) {
        return 0;
    }

    int source_x = (int)(u * (float)ctx->cache->source_width);
    int source_y = (int)(v * (float)ctx->cache->source_height);
    if (source_x >= ctx->cache->source_width) source_x = ctx->cache->source_width - 1;
    if (source_y >= ctx->cache->source_height) source_y = ctx->cache->source_height - 1;

    int is_imp = (ctx->label == 'M');
    if (is_imp && ctx->shooting && source_y == 7 && source_x == 8) {
        *out_ch = '@';
        *out_col = r->scale_rgb(255.0f, 160.0f, 40.0f, fog);
        return 1;
    }

    char glyph = pixel.glyph;
    AsciiColor color = pixel.color;
    int effect = ctx->shooting && ctx->visual &&
                 pixel.token == ctx->visual->effect_token;
    int muzzle_flash = ctx->shooting && !is_imp &&
        source_y == ctx->cache->source_height / 2 &&
        source_x >= ctx->cache->source_width - 2;

    if (effect) {
        color = ctx->visual->effect_color;
    } else if (muzzle_flash) {
        *out_ch = '*';
        *out_col = r->scale_rgb(255.0f, 245.0f, 80.0f, fog);
        return 1;
    }

    int brighten = 0;
    if (ctx->visual && pixel.token) {
        brighten = ctx->visual->styles[(unsigned char)pixel.token].brighten;
    } else if (is_imp && (pixel.token == 'B' || pixel.token == 'D')) {
        brighten = 1;
    }

    if (ctx->shooting && !effect && brighten) {
        color = r->scale_rgb((float)color.r, (float)color.g, (float)color.b, 1.25f);
    }

    /* Aplica densidade e sombreamento cilindrico dinamico sobre o corpo de TODOS os inimigos,
     * preservando detalhes anatomicos finos (olhos, chifres, presas, arcos). */
    if (!effect && (glyph == '#' || glyph == '%' || glyph == '=')) {
        float nx = (u - 0.5f) * 2.0f;
        float ny = (v - 0.5f) * 2.0f;
        float nz = sqrtf(fmaxf(0.18f, 1.0f - nx * nx * 0.56f - ny * ny * 0.24f));
        float base_lum = (glyph == '=') ? 0.34f : ((glyph == '%') ? 0.46f : 0.60f);
        float lum = (base_lum + 0.36f * nz) * fog;
        glyph = r->sample_ramp(lum, sx, sy);
        color = r->scale_rgb((float)color.r, (float)color.g, (float)color.b,
                             (0.76f + 0.30f * nz) * fog);
    } else {
        color = r->scale_rgb((float)color.r, (float)color.g, (float)color.b, fog);
    }

    *out_ch = glyph;
    *out_col = color;
    return 1;
}

void entities_render(const EnemyManager *mgr, const Player *player, const RendererApi *r) {
    if (!mgr || !player || !r) {
        return;
    }
    if (!enemy_sprite_pipeline_ready) {
        enemy_sprite_pipeline_init();
    }

    int indices[MAX_ENEMIES];
    float dists[MAX_ENEMIES];
    int active_count = 0;

    for (int i = 0; i < mgr->count; i++) {
        const Enemy *e = &mgr->enemies[i];
        if (!e->active) continue;
        float dx = e->x - player->x;
        float dy = e->y - player->y;
        indices[active_count] = i;
        dists[active_count] = dx * dx + dy * dy;
        active_count++;
    }

    /* Ordena de tras para frente para consistencia com z-buffer */
    for (int i = 0; i < active_count - 1; i++) {
        for (int j = i + 1; j < active_count; j++) {
            if (dists[i] < dists[j]) {
                int ti = indices[i];
                float td = dists[i];
                indices[i] = indices[j];
                dists[i] = dists[j];
                indices[j] = ti;
                dists[j] = td;
            }
        }
    }

    for (int n = 0; n < active_count; n++) {
        const Enemy *e = &mgr->enemies[indices[n]];
        const EnemyVisualDesign *visual = enemy_visual_design_for_label(e->symbol);
        const EnemyDesign *legacy = visual ? NULL : enemy_design_for_label(e->symbol);
        const EnemySpriteCache *cache = enemy_sprite_cache_for_label(e->symbol);

        RenderSpriteSpec spec;
        spec.world_x = e->x;
        spec.world_y = e->y;
        spec.height_scale = visual ? (REDESIGN_IMP_HEIGHT_SCALE * visual->world_height)
                                   : (legacy ? legacy->height_scale : 1.0f);
        spec.width_ratio = visual ? visual->width_ratio
                                  : (legacy ? legacy->width_ratio : 0.625f);
        spec.min_w = 4;
        spec.min_h = 6;
        spec.min_fog = 0.65f;

        int shooting = (e->attack_timer > 0.0f && e->attack_cooldown > 0.0f &&
                        e->attack_timer > e->attack_cooldown * 0.72f);

        EnemyRenderCtx ctx;
        ctx.label = e->symbol;
        ctx.shooting = shooting;
        ctx.visual = visual;
        ctx.cache = cache;
        ctx.mip_pixels = NULL;
        ctx.mip_w = 0;
        ctx.mip_h = 0;
        ctx.mip_stride = 0;
        ctx.last_target_w = 0;
        ctx.last_target_h = 0;
        ctx.api = r;

        r->draw_world_sprite(player, &spec, enemy_sprite_sampler, &ctx);
    }

    /* Renderiza projeteis ativos dos inimigos via biblioteca grafica */
    AsciiColor black = {0, 0, 0};
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        const EnemyProjectile *projectile = &enemy_projectiles[i];
        if (!projectile->active) continue;

        if (projectile->kind == ENEMY_PROJECTILE_ARROW) {
            int sx, sy;
            float depth;
            if (!r->project_point(player, projectile->x, projectile->y, 0.32f,
                                  &sx, &sy, &depth)) {
                continue;
            }
            float angle = atan2f(projectile->dy, projectile->dx);
            float view_angle = r->get_view_angle(player);
            float horizontal = sinf(wrap_angle(angle - view_angle));
            char arrow = horizontal > 0.18f ? '>' : (horizontal < -0.18f ? '<' : '^');
            int tail_x = arrow == '>' ? -1 : (arrow == '<' ? 1 : 0);
            int tail_y = arrow == '^' ? 1 : 0;
            r->set_cell_if_closer(sx, sy, depth, arrow, (AsciiColor){255, 245, 115}, black);
            for (int trail = 1; trail <= 2; trail++) {
                int px = sx + tail_x * trail;
                int py = sy + tail_y * trail;
                r->set_cell_if_closer(px, py, depth, trail == 1 ? '-' : '.',
                                      trail == 1 ? (AsciiColor){255, 185, 55}
                                                 : (AsciiColor){220, 115, 30},
                                      black);
            }
        } else {
            int is_fb = (projectile->kind == ENEMY_PROJECTILE_FIREBALL);
            r->draw_world_orb(player, projectile->x, projectile->y, 0.46f,
                              is_fb ? 0.10f : 0.09f, 3, 16,
                              is_fb ? '@' : 'O',
                              is_fb ? (AsciiColor){255, 180, 45} : (AsciiColor){155, 255, 255},
                              '*',
                              is_fb ? (AsciiColor){255, 55, 10} : (AsciiColor){20, 150, 255},
                              0.55f);
        }
    }
}
