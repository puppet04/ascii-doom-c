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

#define ENEMY_WALL_RADIUS      0.22f
#define ENEMY_SEPARATION_DIST  0.80f

static int enemy_can_occupy(const GameMap *map, float x, float y) {
    const float r = ENEMY_WALL_RADIUS;
    return !map_is_wall(map, (int)floorf(x - r), (int)floorf(y - r)) &&
           !map_is_wall(map, (int)floorf(x + r), (int)floorf(y - r)) &&
           !map_is_wall(map, (int)floorf(x - r), (int)floorf(y + r)) &&
           !map_is_wall(map, (int)floorf(x + r), (int)floorf(y + r));
}

static int projectile_hits_wall(const GameMap *map, float x, float y, float dir_x, float dir_y) {
    int mx = (int)floorf(x);
    int my = (int)floorf(y);
    if (!map_is_wall(map, mx, my)) {
        return 0;
    }
    /* Margem apenas nas bordas paralelas ao trajeto: quem bate de frente colide imediatamente,
     * quem passa rente a uma parede/quina nao some no ar. */
    float len = sqrtf(dir_x * dir_x + dir_y * dir_y);
    float ndx = (len > 0.0001f) ? fabsf(dir_x / len) : 1.0f;
    float ndy = (len > 0.0001f) ? fabsf(dir_y / len) : 1.0f;
    float fx = x - (float)mx;
    float fy = y - (float)my;
    const float inset = 0.09f;
    int graze_x = (ndx < 0.35f) &&
                  (((fx < inset) && !map_is_wall(map, mx - 1, my)) ||
                   ((fx > 1.0f - inset) && !map_is_wall(map, mx + 1, my)));
    int graze_y = (ndy < 0.35f) &&
                  (((fy < inset) && !map_is_wall(map, mx, my - 1)) ||
                   ((fy > 1.0f - inset) && !map_is_wall(map, mx, my + 1)));
    if (graze_x || graze_y) {
        return 0;
    }
    return 1;
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
        if (projectile_hits_wall(map, x0 + dx * t, y0 + dy * t, dx, dy)) {
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

int enemies_player_is_burning(void) {
    return g_fire_burn_timer > 0.0f ? 1 : 0;
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

static int is_optional_enemy_symbol(char symbol) {
    return (symbol == 'U' || symbol == 'V' || symbol == 'W' || symbol == 'X');
}

static int enemy_type_from_symbol(char symbol, EnemyType *out_type) {
    if (is_optional_enemy_symbol(symbol)) {
        if (out_type) {
            *out_type = ENEMY_BERSERK_ZODD;
        }
        return 1;
    }
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

int enemies_required_alive(const EnemyManager *mgr) {
    int count = 0;
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->enemies[i].active && !mgr->enemies[i].optional_for_exit) {
            count++;
        }
    }
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
            float sx = (float)x + 0.5f;
            float sy = (float)y + 0.5f;
            if (!enemy_can_occupy(map, sx, sy)) {
                static const float nudges[8][2] = {
                    {0.25f, 0.0f}, {-0.25f, 0.0f}, {0.0f, 0.25f}, {0.0f, -0.25f},
                    {0.25f, 0.25f}, {-0.25f, 0.25f}, {0.25f, -0.25f}, {-0.25f, -0.25f}
                };
                for (int n = 0; n < 8; n++) {
                    if (enemy_can_occupy(map, sx + nudges[n][0], sy + nudges[n][1])) {
                        sx += nudges[n][0];
                        sy += nudges[n][1];
                        break;
                    }
                }
            }
            Enemy spawned = enemy_create(type, sx, sy);
            spawned.optional_for_exit = is_optional_enemy_symbol(tile);
            mgr->enemies[mgr->count++] = spawned;
        }
    }
    map_set_enemies_alive(enemies_required_alive(mgr));
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

            if (projectile_hits_wall(map, projectile->x, projectile->y, projectile->dx, projectile->dy) ||
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
        if (projectile_hits_wall(map, s->fireball_x, s->fireball_y, s->fireball_dx, s->fireball_dy)) {
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
        g_fire_burn_timer = 0.5f;
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

        float sep_x = 0.0f;
        float sep_y = 0.0f;
        for (int j = 0; j < mgr->count; j++) {
            if (j == i || !mgr->enemies[j].active) {
                continue;
            }
            float odx = e->x - mgr->enemies[j].x;
            float ody = e->y - mgr->enemies[j].y;
            float d2 = odx * odx + ody * ody;
            if (d2 < ENEMY_SEPARATION_DIST * ENEMY_SEPARATION_DIST) {
                if (d2 < 0.0001f) {
                    float ang = (float)(i * 2.39996f);
                    sep_x += cosf(ang) * 0.6f;
                    sep_y += sinf(ang) * 0.6f;
                } else {
                    float d = sqrtf(d2);
                    float push = (ENEMY_SEPARATION_DIST - d) / ENEMY_SEPARATION_DIST;
                    sep_x += (odx / d) * push;
                    sep_y += (ody / d) * push;
                }
            }
        }

        float vx = sep_x * 1.6f * dt;
        float vy = sep_y * 1.6f * dt;
        if (dist > preferred_distance && dist < aggro_range) {
            float move_speed = e->speed;
            if ((e->type == ENEMY_DOOM_PINKY || e->type == ENEMY_BERSERK_APOSTLE) &&
                dist <= 3.2f) {
                move_speed *= e->type == ENEMY_DOOM_PINKY ? 2.2f : 1.9f;
            }
            float step = fminf(move_speed * dt, dist - preferred_distance);
            vx += (dx / dist) * step;
            vy += (dy / dist) * step;
        }

        if (vx != 0.0f || vy != 0.0f) {
            float nx = e->x + vx;
            float ny = e->y + vy;
            int moved_x = 0;
            int moved_y = 0;
            if (enemy_can_occupy(map, nx, e->y)) {
                e->x = nx;
                moved_x = 1;
            }
            if (enemy_can_occupy(map, e->x, ny)) {
                e->y = ny;
                moved_y = 1;
            }
            /* Deslizamento em quinas: se travar em um eixo, contorna pela direcao livre. */
            float mag = sqrtf(vx * vx + vy * vy);
            if (!moved_x && fabsf(vx) > 0.0001f && fabsf(vy) < mag * 0.45f) {
                float dir_y = (dy >= 0.0f) ? 1.0f : -1.0f;
                float slide = mag * 0.85f;
                if (enemy_can_occupy(map, e->x, e->y + dir_y * slide)) {
                    e->y += dir_y * slide;
                } else if (enemy_can_occupy(map, e->x, e->y - dir_y * slide)) {
                    e->y -= dir_y * slide;
                }
            } else if (!moved_y && fabsf(vy) > 0.0001f && fabsf(vx) < mag * 0.45f) {
                float dir_x = (dx >= 0.0f) ? 1.0f : -1.0f;
                float slide = mag * 0.85f;
                if (enemy_can_occupy(map, e->x + dir_x * slide, e->y)) {
                    e->x += dir_x * slide;
                } else if (enemy_can_occupy(map, e->x - dir_x * slide, e->y)) {
                    e->x -= dir_x * slide;
                }
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
    map_set_enemies_alive(enemies_required_alive(mgr));
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
