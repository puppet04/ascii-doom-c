/* entities.c - armas, inimigos, projeteis, drops e renderizacao de entidades */

#include "entities.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mantem qualquer angulo no intervalo [-PI, PI] para diferencas de direcao. */
static float wrap_angle(float a) {
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

/* Raio de colisao do inimigo com paredes e distancia minima entre inimigos. */
#define ENEMY_WALL_RADIUS      0.22f
#define ENEMY_SEPARATION_DIST  0.80f

/* Verifica se os 4 cantos da caixa do inimigo ficam livres de parede. */
static int enemy_can_occupy(const GameMap *map, float x, float y) {
    const float r = ENEMY_WALL_RADIUS;
    return !map_is_wall(map, (int)floorf(x - r), (int)floorf(y - r)) &&
           !map_is_wall(map, (int)floorf(x + r), (int)floorf(y - r)) &&
           !map_is_wall(map, (int)floorf(x - r), (int)floorf(y + r)) &&
           !map_is_wall(map, (int)floorf(x + r), (int)floorf(y + r));
}

/* Checa colisao de projetil com parede, permitindo raspar rente a quinas. */
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

/* Testa linha de visada amostrando pontos ao longo do segmento entre dois pontos. */
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

/* Linha de tiro limpa (sem raspar em quinas): verifica raio central e margens laterais
 * com checagem estrita de parede para impedir inimigos de ficarem atirando atras de quinas. */
static int enemy_has_clear_shot(const GameMap *map, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.05f) {
        return 1;
    }

    float px = -dy / dist;
    float py = dx / dist;
    const float half_w = 0.18f;
    static const float offsets[3] = {0.0f, -1.0f, 1.0f};

    int steps = (int)(dist * 16.0f);
    if (steps < 2) steps = 2;

    for (int r = 0; r < 3; r++) {
        float ox = px * half_w * offsets[r];
        float oy = py * half_w * offsets[r];
        for (int i = 1; i < steps; i++) {
            float t = (float)i / (float)steps;
            float sx = x0 + ox + dx * t;
            float sy = y0 + oy + dy * t;
            if (map_is_wall(map, (int)floorf(sx), (int)floorf(sy))) {
                return 0;
            }
        }
    }
    return 1;
}

/* Tabela padrao das 9 armas: dano, cadencia, alcance, tamanho do pente e municao infinita. */
void weapons_init_defaults(Weapon out_weapons[MAX_WEAPONS]) {
    if (!out_weapons) {
        return;
    }

    Weapon list[MAX_WEAPONS] = {
        {WEAPON_PISTOL,       "Pistola",        25, 0.28f,  1,  5.8f, 10, 1},
        {WEAPON_SHOTGUN,      "Shotgun",        52, 0.72f,  1,  3.8f,  6, 0},
        {WEAPON_SNIPER,       "Sniper",        105, 1.50f,  1, 22.0f,  5, 0},
        {WEAPON_MACHINEGUN,   "Metralhadora",   11, 0.075f, 1,  6.8f, 30, 0},
        {WEAPON_FUTURISTIC,   "Arma Futurista", 64, 0.90f,  1,  7.5f,  4, 0},
        {WEAPON_MAQUITA,      "Maquita",         9, 0.045f, 1,  1.95f, 10, 1},
        {WEAPON_REVOLVER,     "Revolver",      118, 0.62f,  1, 16.0f,  6, 0},
        {WEAPON_TOMMYGUN,     "Tommy Gun",      15, 0.075f, 1, 10.5f, 50, 0},
        {WEAPON_FLAMETHROWER, "Lanca-Chamas",   19, 0.11f,  1,  6.2f, 40, 0}
    };

    for (int i = 0; i < MAX_WEAPONS; i++) {
        out_weapons[i] = list[i];
    }
}

/* Atributos base de cada tipo de inimigo (vida, movimento, ataque e projetil). */
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

/* Tabela principal de inimigos e bosses.
 * Projeteis: flecha 6, bola 3.1 e orbe 2.2; precisao usada nos ataques hitscan. */
static const EnemyStats ENEMY_STATS[ENEMY_TYPE_COUNT] = {
    /* Inimigos legado (Z, D, I) */
    {"Z legado",          'Z',  40, 1.00f,  3, ENEMY_ATTACK_MELEE,      0.85f, 1.90f,  7.5f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Metralhadora: hitscan com alta cadencia de tiro, dano leve por disparo (alcance 6.8m). */
    {"D legado",          'D',  80, 1.45f,  1, ENEMY_ATTACK_HITSCAN,    6.80f, 0.36f, 12.0f, 0.58f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    {"I legado",          'I',  55, 2.00f,  4, ENEMY_ATTACK_MELEE,      0.95f, 1.50f, 10.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Shotgun: aproxima ate o alcance da shotgun (3.8m) e dispara cone de 3 chumbinhos. */
    {"Imp",               'M',  60, 1.40f,  3, ENEMY_ATTACK_PROJECTILE, 3.80f, 2.15f, 11.0f, 0.90f, ENEMY_PROJECTILE_FIREBALL,   ENEMY_PROJECTILE_WAVERING,  3.1f, 0.9f},
    /* Ataque: investida veloz de curta distancia. */
    {"Pinky",             'Y',  75, 2.30f,  6, ENEMY_ATTACK_MELEE,      1.20f, 1.65f, 11.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Arma Futurista: orbe de energia teleguiado (alcance 7.5m). */
    {"Cacodemon",         'C', 175, 1.00f,  7, ENEMY_ATTACK_PROJECTILE, 7.50f, 3.90f, 13.0f, 0.75f, ENEMY_PROJECTILE_ENERGY_ORB, ENEMY_PROJECTILE_HOMING,    2.2f, 1.1f},
    /* Ataque: golpe brutal de maos nuas, muito lento e devastador. */
    {"Baron of Hell",     'B', 260, 0.75f, 13, ENEMY_ATTACK_MELEE,      1.60f, 4.50f, 10.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Sniper: longo alcance com tiro reto veloz (alcance 14.0m). */
    {"Soldado Kushan",    'K',  35, 1.10f,  6, ENEMY_ATTACK_PROJECTILE,14.00f, 2.80f, 18.0f, 0.90f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  6.0f, 0.0f},
    /* Arma: clava pesada; alcance estendido compensa a lentidao extrema. */
    {"Troll",             'T', 220, 0.70f, 10, ENEMY_ATTACK_MELEE,      2.05f, 4.00f,  8.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Ataque: sequencia frenetica de garras, dano baixo por golpe e recarga minima. */
    {"Apostolo",          'A',  95, 2.50f,  3, ENEMY_ATTACK_MELEE,      1.05f, 0.95f,  9.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Ataque: garra de mini-chefe implacavel. */
    {"Zodd",              'Q', 460, 1.45f, 16, ENEMY_ATTACK_MELEE,      1.90f, 2.20f, 12.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Boss 1 (Arena 1 - U): Obreiro Pesado com Maquita de corte continuo rapido. */
    {"Boss Maquita",      'U', 400, 2.05f,  1, ENEMY_ATTACK_MELEE,      1.65f, 0.10f, 11.0f, 1.00f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Boss 2 (Arena 2 - V): Pistoleiro com Revolver Grande de tiros pesados. */
    {"Boss Revolver",     'V', 520, 1.35f, 15, ENEMY_ATTACK_PROJECTILE,10.50f, 1.65f, 12.5f, 0.95f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  7.2f, 0.0f},
    /* Boss 3 (Arena 3 - W): Mafioso Blindado com Tommy Gun de alta cadencia. */
    {"Boss Tommy Gun",    'W', 650, 1.25f,  3, ENEMY_ATTACK_HITSCAN,    8.50f, 0.24f, 12.0f, 0.56f, ENEMY_PROJECTILE_ARROW,      ENEMY_PROJECTILE_STRAIGHT,  0.0f, 0.0f},
    /* Boss 4 (Arena 4 - X): Piromaniaco Hazmat com Lanca-Chamas em cone. */
    {"Boss Lanca-Chamas", 'X', 800, 1.30f,  7, ENEMY_ATTACK_PROJECTILE, 6.20f, 0.65f, 13.0f, 0.95f, ENEMY_PROJECTILE_FIREBALL,   ENEMY_PROJECTILE_STRAIGHT,  4.6f, 0.0f}
};

/* Pools globais de projeteis, drops de armas no chao e orbes do jogador */
static EnemyProjectile enemy_projectiles[MAX_ENEMY_PROJECTILES];
static WeaponDrop weapon_drops[MAX_WEAPON_DROPS];
static PlayerOrb player_orbs[MAX_PLAYER_ORBS];
static float g_fire_damage_acc = 0.0f; /* acumulador fracionario pro dano de fogo por segundo */
static float g_fire_burn_timer = 0.0f; /* tempo restante da queimadura no jogador */

/* Getters dos pools para HUD, minimapa e envio de estado em rede */
const EnemyProjectile *enemies_projectile_pool(void) {
    return enemy_projectiles;
}

const WeaponDrop *enemies_weapon_drop_pool(void) {
    return weapon_drops;
}

const PlayerOrb *enemies_player_orb_pool(void) {
    return player_orbs;
}

int enemies_player_is_burning(void) {
    return g_fire_burn_timer > 0.0f ? 1 : 0;
}

/* Busca as estatisticas do tipo informado (usa Z legado se vier indice invalido). */
static const EnemyStats *enemy_stats_for_type(EnemyType type) {
    if ((int)type < 0 || type >= ENEMY_TYPE_COUNT) {
        return &ENEMY_STATS[ENEMY_LEGACY_Z];
    }
    return &ENEMY_STATS[type];
}

/* Monta uma instancia de inimigo pronta no mapa a partir da tabela base. */
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

/* Zera o gerenciador de inimigos e limpa todos os pools de entidades. */
void enemies_init_defaults(EnemyManager *mgr) {
    if (!mgr) {
        return;
    }

    memset(mgr, 0, sizeof(*mgr));
    memset(enemy_projectiles, 0, sizeof(enemy_projectiles));
    memset(weapon_drops, 0, sizeof(weapon_drops));
    memset(player_orbs, 0, sizeof(player_orbs));
    g_fire_damage_acc = 0.0f;
    g_fire_burn_timer = 0.0f;
}

/* Bosses de arena (U, V, W, X) sao opcionais e nao travam a saida E do mapa. */
static int is_optional_enemy_symbol(char symbol) {
    return (symbol == 'U' || symbol == 'V' || symbol == 'W' || symbol == 'X');
}

/* Converte o caractere do mapa ASCII no EnemyType correspondente. */
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

/* Conta todos os inimigos ativos (inclusive bosses opcionais). */
int enemies_alive(const EnemyManager *mgr) {
    int count = 0;
    for (int i = 0; i < mgr->count; i++) if (mgr->enemies[i].active) count++;
    return count;
}

/* Conta apenas inimigos obrigatorios vivos que ainda seguram a saida fechada. */
int enemies_required_alive(const EnemyManager *mgr) {
    int count = 0;
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->enemies[i].active && !mgr->enemies[i].optional_for_exit) {
            count++;
        }
    }
    return count;
}

/* Varre o mapa inteiro e spawna todos os inimigos marcados nos tiles. */
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
            /* Se o centro do tile encostar numa parede vizinha, empurra um pouco pro lado livre. */
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

/* Mapeia inimigos comuns que derrubam uma arma especial ao morrer. */
static int enemy_weapon_drop_type(EnemyType type) {
    switch (type) {
        case ENEMY_DOOM_IMP:       return WEAPON_SHOTGUN;
        case ENEMY_BERSERK_KUSHAN: return WEAPON_SNIPER;
        case ENEMY_LEGACY_D:       return WEAPON_MACHINEGUN;
        case ENEMY_DOOM_CACODEMON: return WEAPON_FUTURISTIC;
        default:                   return -1;
    }
}

/* Coloca um drop de arma com municao fixa no chao (reaproveita slot 0 se lotar). */
static void spawn_weapon_drop_with_ammo(int weapon_type, int ammo, float x, float y) {
    if (weapon_type <= WEAPON_PISTOL || weapon_type >= MAX_WEAPONS || ammo <= 0) {
        return;
    }
    int slot = -1;
    for (int i = 0; i < MAX_WEAPON_DROPS; i++) {
        if (!weapon_drops[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
    }

    weapon_drops[slot].active = 1;
    weapon_drops[slot].weapon = (WeaponType)weapon_type;
    weapon_drops[slot].ammo = ammo;
    weapon_drops[slot].x = x;
    weapon_drops[slot].y = y;
}

/* Gera um drop com quantidade aleatoria entre ~1/3 e 1 carregador inteiro. */
static void spawn_weapon_drop(int weapon_type, float x, float y) {
    if (weapon_type <= WEAPON_PISTOL || weapon_type >= MAX_WEAPONS) {
        return;
    }
    int mag = player_weapon_mag_size(weapon_type);
    int min_ammo = (mag + 2) / 3; /* ~0.35x carregador (minimo 1) */
    if (min_ammo < 1) min_ammo = 1;
    int max_ammo = mag;           /* ate 1.0x carregador */
    int span = max_ammo - min_ammo + 1;
    int ammo = min_ammo + (span > 1 ? (rand() % span) : 0);
    spawn_weapon_drop_with_ammo(weapon_type, ammo, x, y);
}

/* Retorna a arma exclusiva derrubada por cada um dos 4 chefes de arena. */
static int enemy_boss_reward_weapon(EnemyType type) {
    switch (type) {
        case ENEMY_BOSS_MAQUITA:  return WEAPON_MAQUITA;
        case ENEMY_BOSS_REVOLVER: return WEAPON_REVOLVER;
        case ENEMY_BOSS_TOMMY:    return WEAPON_TOMMYGUN;
        case ENEMY_BOSS_FLAME:    return WEAPON_FLAMETHROWER;
        default:                  return -1;
    }
}

/* Aplica dano no inimigo; quando a vida zera, contabiliza kill e solta o drop. */
static void damage_enemy(Enemy *e, Player *p, int damage) {
    if (!e->active) return;
    e->health -= damage;
    if (e->health <= 0) {
        e->health = 0;
        e->active = 0;
        player_register_kill(p, e->x, e->y);
        e->burn_time = e->burn_tick = 0;
        int boss_w = enemy_boss_reward_weapon(e->type);
        if (boss_w >= 0) {
            /* Ao matar o boss, a arma cai no chao com 10x o carregador (igual as demais). */
            spawn_weapon_drop_with_ammo(boss_w, player_weapon_mag_size(boss_w) * 10, e->x, e->y);
        } else {
            int drop_w = enemy_weapon_drop_type(e->type);
            if (drop_w >= 0) {
                spawn_weapon_drop(drop_w, e->x, e->y);
            }
        }
    }
}

/* Dispara um projetil inimigo em direcao ao jogador com deslocamento angular opcional. */
static int enemy_spawn_projectile_angled(const Enemy *enemy, const Player *player,
                                         const EnemyStats *stats, float angle_offset) {
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

    float base_angle = atan2f(dy, dx) + angle_offset;
    float dir_x = cosf(base_angle);
    float dir_y = sinf(base_angle);

    EnemyProjectile *projectile = &enemy_projectiles[slot];
    projectile->active = 1;
    projectile->x = enemy->x + dir_x * 0.35f;
    projectile->y = enemy->y + dir_y * 0.35f;
    projectile->dx = dir_x;
    projectile->dy = dir_y;
    projectile->speed = stats->projectile_speed;
    projectile->turn_rate = stats->projectile_turn_rate;
    projectile->damage = enemy->damage;
    projectile->age = 0.0f;
    projectile->kind = stats->projectile_kind;
    projectile->path = stats->projectile_path;
    return 1;
}

/* Cria o padrao de disparo do inimigo (simples ou em cone de 3 tiros). */
static int enemy_spawn_projectile(const Enemy *enemy, const Player *player,
                                  const EnemyStats *stats) {
    if (enemy->type == ENEMY_DOOM_IMP) {
        /* Shotgun do Imp: tiro em cone (centro primeiro para manter compatibilidade). */
        int ok = enemy_spawn_projectile_angled(enemy, player, stats, 0.0f);
        EnemyStats side_stats = *stats;
        side_stats.projectile_path = ENEMY_PROJECTILE_STRAIGHT;
        side_stats.projectile_turn_rate = 0.0f;
        (void)enemy_spawn_projectile_angled(enemy, player, &side_stats, -0.30f);
        (void)enemy_spawn_projectile_angled(enemy, player, &side_stats, 0.30f);
        return ok;
    }
    if (enemy->type == ENEMY_BOSS_FLAME) {
        /* Lanca-Chamas do Boss 4: rajada em cone de 3 jatos de fogo. */
        int ok = enemy_spawn_projectile_angled(enemy, player, stats, 0.0f);
        (void)enemy_spawn_projectile_angled(enemy, player, stats, -0.17f);
        (void)enemy_spawn_projectile_angled(enemy, player, stats, 0.17f);
        return ok;
    }
    return enemy_spawn_projectile_angled(enemy, player, stats, 0.0f);
}

/* Avanca projeteis inimigos em subpassos curtos pra nao atravessar paredes finas. */
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
            /* Orbe teleguiado gira gradualmente na direcao do jogador. */
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
                /* Oscila levemente para os lados ao longo do trajeto. */
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
                /* Jato rapido de fogo do Boss 4 deixa o jogador queimando por 0.5s. */
                if (projectile->kind == ENEMY_PROJECTILE_FIREBALL &&
                    projectile->path == ENEMY_PROJECTILE_STRAIGHT &&
                    projectile->speed > 4.0f) {
                    g_fire_burn_timer = 0.5f;
                }
                projectile->active = 0;
            }
        }
    }
}

/* Atualiza a bola de fogo da habilidade do Arauto e aplica queimadura sombria ao acertar. */
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
                if (e->active) {
                    e->burn_time = SHADOW_BURN_DURATION;
                    e->burn_tick = 0;
                    e->burn_is_fire = 0;
                }
                s->fireball_active = 0;
                break;
            }
        }
    }
    s->fireball_time = fmaxf(0, s->fireball_time-dt);
    if (s->fireball_time <= 0) s->fireball_active=0;
}

/* Checa 5 pontos ao redor do jogador pra disparar armadilhas, fogo, medkits e armas do mapa. */
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
        int map_wtype = -1;
        if (map_try_collect_weapon_pickup(map, cx, cy, &map_wtype) && map_wtype >= 0) {
            int ammo = player_weapon_mag_size(map_wtype) * 10;
            player_pickup_weapon(player, map_wtype, ammo);
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

    /* Acumula 10 de dano por segundo enquanto o jogador estiver em chamas. */
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

/* Recolhe armas caidas no chao quando o jogador entra no raio de coleta. */
static void weapon_drops_update(Player *player) {
    if (!player || player->health <= 0) {
        return;
    }
    const float pickup_r2 = 0.65f * 0.65f;
    for (int i = 0; i < MAX_WEAPON_DROPS; i++) {
        WeaponDrop *d = &weapon_drops[i];
        if (!d->active) continue;
        float dx = player->x - d->x;
        float dy = player->y - d->y;
        if (dx * dx + dy * dy <= pickup_r2) {
            player_pickup_weapon(player, (int)d->weapon, d->ammo);
            d->active = 0;
        }
    }
}

/* Lanca o orbe teleguiado da Arma Futurista na direcao da mira do jogador. */
static int spawn_player_orb(const Player *player, int damage) {
    if (!player) return 0;
    int slot = -1;
    for (int i = 0; i < MAX_PLAYER_ORBS; i++) {
        if (!player_orbs[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;

    float dir_x = cosf(player->angle);
    float dir_y = sinf(player->angle);
    PlayerOrb *orb = &player_orbs[slot];
    orb->active = 1;
    orb->x = player->x + dir_x * 0.35f;
    orb->y = player->y + dir_y * 0.35f;
    orb->dx = dir_x;
    orb->dy = dir_y;
    orb->speed = 6.2f;
    orb->turn_rate = 5.0f;
    orb->damage = damage;
    orb->age = 0.0f;
    return 1;
}

/* Move os orbes da Arma Futurista e curva a trajetoria para o inimigo visivel mais perto. */
static void player_orbs_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt) {
    for (int i = 0; i < MAX_PLAYER_ORBS; i++) {
        PlayerOrb *orb = &player_orbs[i];
        if (!orb->active) continue;

        int steps = (int)ceilf(orb->speed * dt / 0.08f);
        if (steps < 1) steps = 1;
        if (steps > 128) steps = 128;
        float step_dt = dt / (float)steps;

        for (int step = 0; step < steps && orb->active; step++) {
            /* Busca o inimigo vivo mais proximo para teleguiar o orbe. */
            int target_idx = -1;
            float best_d2 = 7.5f * 7.5f;
            for (int e_idx = 0; e_idx < mgr->count; e_idx++) {
                const Enemy *e = &mgr->enemies[e_idx];
                if (!e->active) continue;
                float edx = e->x - orb->x;
                float edy = e->y - orb->y;
                float d2 = edx * edx + edy * edy;
                if (d2 < best_d2 && has_line_of_sight(map, orb->x, orb->y, e->x, e->y)) {
                    best_d2 = d2;
                    target_idx = e_idx;
                }
            }
            if (target_idx >= 0) {
                const Enemy *t = &mgr->enemies[target_idx];
                float target_angle = atan2f(t->y - orb->y, t->x - orb->x);
                float cur_angle = atan2f(orb->dy, orb->dx);
                float delta = wrap_angle(target_angle - cur_angle);
                float max_turn = orb->turn_rate * step_dt;
                if (delta > max_turn) delta = max_turn;
                if (delta < -max_turn) delta = -max_turn;
                float next_angle = cur_angle + delta;
                orb->dx = cosf(next_angle);
                orb->dy = sinf(next_angle);
            }

            orb->x += orb->dx * orb->speed * step_dt;
            orb->y += orb->dy * orb->speed * step_dt;
            orb->age += step_dt;

            if (projectile_hits_wall(map, orb->x, orb->y, orb->dx, orb->dy) || orb->age >= 1.25f) {
                orb->active = 0;
                break;
            }

            for (int e_idx = 0; e_idx < mgr->count; e_idx++) {
                Enemy *e = &mgr->enemies[e_idx];
                if (!e->active) continue;
                float edx = e->x - orb->x;
                float edy = e->y - orb->y;
                if (edx * edx + edy * edy <= 0.42f * 0.42f) {
                    damage_enemy(e, player, orb->damage);
                    orb->active = 0;
                    break;
                }
            }
        }
    }
}

/* Gera um campo de distancias via BFS partindo do tile do jogador para guiar todos os inimigos. */
static void enemy_build_flow_field(const GameMap *map, int goal_x, int goal_y,
                                   int dist_grid[MAP_HEIGHT][MAP_WIDTH]) {
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            dist_grid[y][x] = -1;
        }
    }
    if (!map || goal_x < 0 || goal_y < 0 || goal_x >= map->width || goal_y >= map->height) {
        return;
    }
    if (map_is_wall(map, goal_x, goal_y)) {
        return;
    }

    int qx[MAP_WIDTH * MAP_HEIGHT];
    int qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;
    dist_grid[goal_y][goal_x] = 0;
    qx[tail] = goal_x;
    qy[tail] = goal_y;
    tail++;

    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (head < tail) {
        int cx = qx[head];
        int cy = qy[head];
        head++;
        int nd = dist_grid[cy][cx] + 1;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
            if (dist_grid[ny][nx] >= 0) continue;
            if (map_is_wall(map, nx, ny)) continue;
            dist_grid[ny][nx] = nd;
            qx[tail] = nx;
            qy[tail] = ny;
            tail++;
        }
    }
}

/* Loop principal de IA: movimenta inimigos via flow-field BFS, evita empilhamento e executa ataques. */
void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt) {
    if (!mgr || !map || !player || dt <= 0.0f || player->health <= 0) {
        return;
    }

    enemy_projectiles_update(map, player, dt);
    player_orbs_update(mgr, map, player, dt);
    update_map_fire_hazards(map, player, dt);
    weapon_drops_update(player);

    int flow_dist[MAP_HEIGHT][MAP_WIDTH];
    enemy_build_flow_field(map, (int)floorf(player->x), (int)floorf(player->y), flow_dist);

    for (int i = 0; i < mgr->count; i++) {
        Enemy *e = &mgr->enemies[i];
        if (!e->active) {
            continue;
        }

        /* Processa dano continuo de queimadura (bola sombria do Arauto ou fogo). */
        if (e->burn_time > 0) {
            e->burn_tick += fminf(dt, e->burn_time);
            e->burn_time = fmaxf(0, e->burn_time - dt);
            while (e->active && e->burn_tick >= 0.9999f) {
                e->burn_tick = fmaxf(0, e->burn_tick - 1.0f);
                damage_enemy(e, player, SHADOW_BURN_DAMAGE);
            }
            if (e->burn_time == 0) e->burn_tick = 0;
            if (!e->active) continue;
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
        int is_ranged = (e->attack_kind == ENEMY_ATTACK_HITSCAN ||
                         e->attack_kind == ENEMY_ATTACK_PROJECTILE);
        int can_see = has_line_of_sight(map, player->x, player->y, e->x, e->y);
        int can_shoot = is_ranged
            ? (can_see && enemy_has_clear_shot(map, e->x, e->y, player->x, player->y))
            : can_see;
        float preferred_distance = is_ranged
            ? attack_range * 0.75f : attack_range * 0.90f;

        /* Forca de separacao para impedir que varios inimigos fiquem exatamente no mesmo ponto. */
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
                    /* Se dois inimigos estiverem sobrepostos, usa angulo aureo por indice pra desempatar. */
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
        float slide_dir_x = dx;
        float slide_dir_y = dy;
        if (dist < aggro_range && (dist > preferred_distance || !can_shoot)) {
            float move_speed = e->speed;
            /* Pinky e Apostolo aceleram quando chegam perto para dar investida. */
            if ((e->type == ENEMY_DOOM_PINKY || e->type == ENEMY_BERSERK_APOSTLE) &&
                dist <= 3.2f) {
                move_speed *= e->type == ENEMY_DOOM_PINKY ? 2.2f : 1.9f;
            }

            float steer_x = dx;
            float steer_y = dy;
            int direct_clear = can_shoot && dist > 0.001f &&
                enemy_can_occupy(map, e->x + (dx / dist) * 0.32f, e->y + (dy / dist) * 0.32f);

            /* Pathfinding BFS (flow-field): contorna paredes, pilares e portas sem enroscar. */
            if (!direct_clear) {
                int ex = (int)floorf(e->x);
                int ey = (int)floorf(e->y);
                int best_d = (ex >= 0 && ey >= 0 && ex < map->width && ey < map->height &&
                              flow_dist[ey][ex] >= 0) ? flow_dist[ey][ex] : 999999;
                int best_nx = ex;
                int best_ny = ey;
                static const int nbrs[8][2] = {
                    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
                    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
                };
                for (int k = 0; k < 8; k++) {
                    int ox = nbrs[k][0];
                    int oy = nbrs[k][1];
                    int nx = ex + ox;
                    int ny = ey + oy;
                    if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
                    int dval = flow_dist[ny][nx];
                    if (dval < 0) continue;
                    /* Diagonal so e permitida se ambos os lados cardinais estiverem livres (evita quinas). */
                    if (ox != 0 && oy != 0) {
                        if (map_is_wall(map, ex + ox, ey) || map_is_wall(map, ex, ey + oy)) {
                            continue;
                        }
                    }
                    if (dval < best_d) {
                        best_d = dval;
                        best_nx = nx;
                        best_ny = ny;
                    }
                }
                if (best_nx != ex || best_ny != ey) {
                    float wx = (float)best_nx + 0.5f;
                    float wy = (float)best_ny + 0.5f;
                    /* Se for virar uma quina, centraliza no tile atual antes de cruzar para nao raspar. */
                    if (best_nx != ex && best_ny == ey && fabsf(e->y - ((float)ey + 0.5f)) > 0.24f) {
                        wx = (float)ex + 0.5f;
                        wy = (float)ey + 0.5f;
                    } else if (best_ny != ey && best_nx == ex && fabsf(e->x - ((float)ex + 0.5f)) > 0.24f) {
                        wx = (float)ex + 0.5f;
                        wy = (float)ey + 0.5f;
                    }
                    steer_x = wx - e->x;
                    steer_y = wy - e->y;
                }
            }

            slide_dir_x = steer_x;
            slide_dir_y = steer_y;

            float steer_len = sqrtf(steer_x * steer_x + steer_y * steer_y);
            if (steer_len > 0.001f) {
                float target_gap = direct_clear ? fmaxf(0.0f, dist - preferred_distance) : steer_len;
                float step = fminf(move_speed * dt, target_gap);
                vx += (steer_x / steer_len) * step;
                vy += (steer_y / steer_len) * step;
            }

            /* Repulsao suave de paredes proximas para centralizar em corredores/portas. */
            int tx = (int)floorf(e->x);
            int ty = (int)floorf(e->y);
            float fx = e->x - (float)tx;
            float fy = e->y - (float)ty;
            const float margin = 0.36f;
            if (fx < margin && map_is_wall(map, tx - 1, ty)) vx += (margin - fx) * 2.2f * dt;
            if (fx > 1.0f - margin && map_is_wall(map, tx + 1, ty)) vx -= (fx - (1.0f - margin)) * 2.2f * dt;
            if (fy < margin && map_is_wall(map, tx, ty - 1)) vy += (margin - fy) * 2.2f * dt;
            if (fy > 1.0f - margin && map_is_wall(map, tx, ty + 1)) vy -= (fy - (1.0f - margin)) * 2.2f * dt;
        }

        /* Tenta aplicar movimento em X e Y separado para deslizar ao encostar em paredes. */
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
            /* Deslizamento em quinas: segue a direcao do pathfinding (nao a linha reta bloqueada). */
            float mag = sqrtf(vx * vx + vy * vy);
            if (!moved_x && fabsf(vx) > 0.0001f && fabsf(vy) < mag * 0.45f) {
                float dir_y = (slide_dir_y >= 0.0f) ? 1.0f : -1.0f;
                float slide = mag * 0.85f;
                if (enemy_can_occupy(map, e->x, e->y + dir_y * slide)) {
                    e->y += dir_y * slide;
                } else if (enemy_can_occupy(map, e->x, e->y - dir_y * slide)) {
                    e->y -= dir_y * slide;
                }
            } else if (!moved_y && fabsf(vy) > 0.0001f && fabsf(vx) < mag * 0.45f) {
                float dir_x = (slide_dir_x >= 0.0f) ? 1.0f : -1.0f;
                float slide = mag * 0.85f;
                if (enemy_can_occupy(map, e->x + dir_x * slide, e->y)) {
                    e->x += dir_x * slide;
                } else if (enemy_can_occupy(map, e->x - dir_x * slide, e->y)) {
                    e->x -= dir_x * slide;
                }
            }
        }

        /* Executa o ataque quando esta no alcance, com visada limpa e cooldown zerado. */
        if (dist <= attack_range && e->attack_timer <= 0.0f && can_shoot) {
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

/* Resolve o disparo ou golpe corpo a corpo do jogador contra os inimigos ativos. */
int enemies_check_hit(EnemyManager *mgr, const GameMap *map,
                      Player *player, const Weapon *weapon) {
    if (!mgr || !map || !player || !weapon || player->health <= 0) {
        return 0;
    }

    int multiplier = player->ability == ABILITY_GUERRILLA && player->ability_state.boosted_shot ? 2 : 1;
    player->ability_state.boosted_shot = 0; /* Consumido mesmo se o tiro errar. */
    int berserk = player->ability_state.berserk_time > 0;

    /* Arma Futurista lanca um orbe teleguiado em vez de acerto instantaneo. */
    if (!berserk && weapon->type == WEAPON_FUTURISTIC) {
        int orb_dmg = player_attack_damage(player, weapon->damage) * multiplier;
        return spawn_player_orb(player, orb_dmg);
    }

    if (!berserk && weapon->type == WEAPON_SHOTGUN) {
        /* Shotgun: tiro em cone que acerta todos os inimigos na area de dispersao. */
        int hits = 0;
        int dmg = player_attack_damage(player, weapon->damage) * multiplier;
        for (int i = 0; i < mgr->count; i++) {
            Enemy *e = &mgr->enemies[i];
            if (!e->active) continue;
            float dx = e->x - player->x;
            float dy = e->y - player->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > weapon->range) continue;
            float aim_diff = fabsf(wrap_angle(atan2f(dy, dx) - player->angle));
            if (aim_diff < 0.42f && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
                /* Cone da Shotgun: a queima-roupa supera a Sniper (ate 2.40x = 168), caindo ate 0.25x no limite e nas bordas. */
                float angle_factor = 1.0f - 0.65f * (aim_diff / 0.42f);
                float t_dist = fmaxf(0.0f, fminf(1.0f, (dist - 0.9f) / fmaxf(0.1f, weapon->range - 0.9f)));
                float dist_factor = 2.40f - 2.15f * t_dist;
                int shot_dmg = (int)roundf((float)dmg * angle_factor * dist_factor);
                if (shot_dmg < 5) shot_dmg = 5;
                damage_enemy(e, player, shot_dmg);
                hits++;
            }
        }
        return hits > 0 ? 1 : 0;
    }

    if (!berserk && weapon->type == WEAPON_MAQUITA) {
        /* Maquita (Boss 1): serra corpo a corpo continua em arco frontal (dano baixo, cadencia altissima). */
        int hits = 0;
        int dmg = player_attack_damage(player, weapon->damage) * multiplier;
        for (int i = 0; i < mgr->count; i++) {
            Enemy *e = &mgr->enemies[i];
            if (!e->active) continue;
            float dx = e->x - player->x;
            float dy = e->y - player->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > weapon->range) continue;
            float aim_diff = fabsf(wrap_angle(atan2f(dy, dx) - player->angle));
            if (aim_diff < 0.55f && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
                damage_enemy(e, player, dmg);
                hits++;
            }
        }
        return hits > 0 ? 1 : 0;
    }

    if (!berserk && weapon->type == WEAPON_REVOLVER) {
        /* Revolver Grande (Boss 2): disparo Magnum perfurante em linha reta. */
        int hits = 0;
        int dmg = player_attack_damage(player, weapon->damage) * multiplier;
        for (int i = 0; i < mgr->count; i++) {
            Enemy *e = &mgr->enemies[i];
            if (!e->active) continue;
            float dx = e->x - player->x;
            float dy = e->y - player->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > weapon->range) continue;
            float aim_diff = fabsf(wrap_angle(atan2f(dy, dx) - player->angle));
            if (aim_diff < 0.11f && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
                damage_enemy(e, player, dmg);
                hits++;
            }
        }
        return hits > 0 ? 1 : 0;
    }

    if (!berserk && weapon->type == WEAPON_FLAMETHROWER) {
        /* Lanca-Chamas (Boss 4): jato em cone que atinge e incendeia inimigos. */
        int hits = 0;
        int dmg = player_attack_damage(player, weapon->damage) * multiplier;
        for (int i = 0; i < mgr->count; i++) {
            Enemy *e = &mgr->enemies[i];
            if (!e->active) continue;
            float dx = e->x - player->x;
            float dy = e->y - player->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > weapon->range) continue;
            float aim_diff = fabsf(wrap_angle(atan2f(dy, dx) - player->angle));
            if (aim_diff < 0.44f && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
                damage_enemy(e, player, dmg);
                if (e->active) {
                    e->burn_time = SHADOW_BURN_DURATION;
                    e->burn_tick = 0.0f;
                    e->burn_is_fire = 1;
                }
                hits++;
            }
        }
        return hits > 0 ? 1 : 0;
    }

    /* Armas hitscan de alvo unico (Pistola, Sniper, Metralhadora, Tommy Gun) ou golpe Berserk. */
    int best_idx = -1;
    float best_dist = berserk ? 1.5f : weapon->range;
    float aim_limit = berserk ? 0.45f
                    : (weapon->type == WEAPON_SNIPER ? 0.07f
                    : (weapon->type == WEAPON_TOMMYGUN ? 0.16f
                    : (weapon->type == WEAPON_MACHINEGUN ? 0.14f : 0.12f)));

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
        if (aim_diff < aim_limit && has_line_of_sight(map, player->x, player->y, e->x, e->y)) {
            best_dist = dist;
            best_idx = i;
        }
    }

    if (best_idx >= 0) {
        Enemy *target = &mgr->enemies[best_idx];
        /* Berserk bate muito forte (4x), mas um golpe so nao pode apagar um boss inteiro. */
        damage_enemy(target, player, berserk
            ? player_attack_damage(player, weapon->damage) * 4
            : player_attack_damage(player, weapon->damage) * multiplier);
        return 1;
    }

    return 0;
}


/* --- Arte ASCII, mipmaps Scale2x e renderizacao dos inimigos --- */

/* Dimensoes dos sprites ASCII legados usados como fallback */
#define ENEMY_ART_WIDTH 9
#define ENEMY_ART_HEIGHT 12
#define ENEMY_ART_MAX_HEIGHT 13

/* Molde legado de sprite em grade pequena com paleta fixa de 5 cores */
typedef struct {
    char label;
    float width_ratio;
    float height_scale;
    const char *rows[ENEMY_ART_MAX_HEIGHT];
    AsciiColor palette[5];
} EnemyDesign;

/* Desenhos legados (9x12 / 9x13) mantidos como referencia de proporcao e fallback */
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

/* Sprite generico caso apareca algum simbolo desconhecido */
static const EnemyDesign ENEMY_DESIGN_FALLBACK = {
    '?', 0.62f, 1.0f, {
        "....^....", "...###...", "..#+++#..", "..#*@*#..",
        "...###...", "..#####..", ".###+###.", ".###+###.",
        "...#=#...", "..##=##..", "..#...#..", ".##...##."
    }, {{170,150,170}, {205,185,205}, {65,55,70}, {255,210,230}, {225,195,220}}
};

/* Procura o sprite legado pelo caractere do inimigo. */
static const EnemyDesign *enemy_design_for_label(char label) {
    int design_count = (int)(sizeof(ENEMY_DESIGNS) / sizeof(ENEMY_DESIGNS[0]));
    for (int i = 0; i < design_count; i++) {
        if (ENEMY_DESIGNS[i].label == label) return &ENEMY_DESIGNS[i];
    }
    return &ENEMY_DESIGN_FALLBACK;
}

/* Mapeia o caractere do sprite legado para um indice na paleta de 5 cores. */
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

/* Mapeia os tokens especificos do sprite legado do Imp ('M'). */
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

/* Converte o token interno do Imp no glifo ASCII exibido na tela. */
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

/* Estilo de um token na grade redesenhada (glifo, cor e se brilha ao atirar). */
typedef struct {
    char glyph;
    AsciiColor color;
    unsigned char brighten;
} EnemyArtStyle;

/* Descricao completa de um sprite redesenhado (grade 15x18 a 17x20 + proporcoes no mundo). */
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

/* Sprites limpos e definidos com formas solidas (2+ px) otimizadas para Scale2x 4x */
static const EnemyArtStyle LEGACY_Z_STYLE[128] = {
    ENEMY_STYLE('A', '#', 112, 122, 134, 1), /* armadura aco-cinza */
    ENEMY_STYLE('D', '%',  56,  62,  72, 1), /* sombra */
    ENEMY_STYLE('C', '#', 138,  52,  52, 1), /* peitoral/faixas vermelho-escuro */
    ENEMY_STYLE('S', '#', 162, 168, 158, 1), /* mascara/cranio */
    ENEMY_STYLE('E', 'o', 255,  55,  45, 0), /* olhos vermelhos */
    ENEMY_STYLE('R', '=',  42,  44,  50, 0)  /* botas */
};

static const EnemyArtStyle LEGACY_D_STYLE[128] = {
    ENEMY_STYLE('A', '#',  88,  96, 108, 1), /* exo-armadura pesada aco */
    ENEMY_STYLE('D', '%',  46,  52,  60, 1), /* sombra */
    ENEMY_STYLE('C', '#', 165,  56,  48, 1), /* ombreiras vermelhas */
    ENEMY_STYLE('E', '=', 255, 175,  45, 0), /* visor laranja */
    ENEMY_STYLE('G', '=', 172, 182, 196, 0), /* cano grosso da metralhadora */
    ENEMY_STYLE('Y', '#', 228, 188,  62, 0), /* caixa de municao dourada */
    ENEMY_STYLE('F', '*', 255, 235,  95, 0), /* boca da metralhadora */
    ENEMY_STYLE('R', '=',  38,  40,  46, 0)  /* botas pesadas */
};

static const EnemyArtStyle LEGACY_I_STYLE[128] = {
    ENEMY_STYLE('B', '#',  64,  62,  72, 1), /* corpo sombrio agil */
    ENEMY_STYLE('D', '%',  36,  34,  42, 1), /* sombra */
    ENEMY_STYLE('C', '#', 148,  46,  52, 1), /* dorso/marcas carmesim */
    ENEMY_STYLE('E', 'o', 255, 210,  55, 0), /* olhos amarelos */
    ENEMY_STYLE('K', '/', 215, 222, 232, 0)  /* garras longas */
};

static const EnemyArtStyle IMP_SHOTGUN_STYLE[128] = {
    ENEMY_STYLE('A', '#',  96, 110,  84, 1), /* colete tatico oliva */
    ENEMY_STYLE('D', '%',  50,  58,  46, 1), /* sombra */
    ENEMY_STYLE('H', '#',  72,  78,  86, 1), /* capacete de aco */
    ENEMY_STYLE('E', '=', 250,  60,  45, 0), /* visor vermelho */
    ENEMY_STYLE('W', '#', 165, 102,  52, 0), /* coronha de madeira da shotgun */
    ENEMY_STYLE('G', '=', 188, 198, 212, 0), /* cano duplo grosso da shotgun */
    ENEMY_STYLE('F', '*', 255, 225, 120, 0), /* boca da shotgun */
    ENEMY_STYLE('R', '=',  44,  44,  46, 0)  /* botas */
};

static const EnemyArtStyle PINKY_STYLE[128] = {
    ENEMY_STYLE('A', '#',  98,  76,  72, 1), /* blindagem ferro-ferrugem */
    ENEMY_STYLE('D', '%',  52,  38,  38, 1), /* sombra */
    ENEMY_STYLE('C', '#', 162,  58,  52, 1), /* placas carmesim */
    ENEMY_STYLE('E', 'o', 255, 165,  40, 0), /* olhos laranja */
    ENEMY_STYLE('T', 'v', 235, 230, 215, 0), /* mandibula de aco/dentes */
    ENEMY_STYLE('R', '=',  40,  38,  42, 0)  /* patas blindadas */
};

static const EnemyArtStyle CACODEMON_STYLE[128] = {
    ENEMY_STYLE('A', '#',  74,  92, 126, 1), /* casco blindado azul-aco */
    ENEMY_STYLE('D', '%',  38,  48,  70, 1), /* sombra */
    ENEMY_STYLE('S', '#', 138, 152, 174, 1), /* placas metalicas */
    ENEMY_STYLE('E', 'O',  95, 245, 255, 0), /* olho/lente ciano */
    ENEMY_STYLE('P', '@', 230, 255, 255, 0), /* centro da lente */
    ENEMY_STYLE('G', '=', 165, 198, 232, 0), /* canhoes de plasma laterais */
    ENEMY_STYLE('F', '*', 115, 248, 255, 0)  /* disparo plasma */
};

static const EnemyArtStyle BARON_STYLE[128] = {
    ENEMY_STYLE('A', '#',  58,  54,  66, 1), /* armadura negra de cavaleiro infernal */
    ENEMY_STYLE('D', '%',  32,  28,  38, 1), /* sombra */
    ENEMY_STYLE('C', '#', 168,  44,  46, 1), /* placas carmesim */
    ENEMY_STYLE('H', '^', 218, 210, 188, 0), /* chifres grossos */
    ENEMY_STYLE('E', 'O', 255, 155,  40, 0), /* olhos/nucleo em chamas */
    ENEMY_STYLE('N', '*', 255, 105,  30, 0), /* manoplas incandescentes */
    ENEMY_STYLE('R', '=',  38,  34,  42, 0)  /* grevas */
};

static const EnemyArtStyle KUSHAN_STYLE[128] = {
    ENEMY_STYLE('C', '#',  62,  68,  74, 1), /* capuz e capa tatica escura */
    ENEMY_STYLE('D', '%',  36,  40,  46, 1), /* sombra */
    ENEMY_STYLE('A', '#',  96, 106, 114, 1), /* colete cinza */
    ENEMY_STYLE('E', 'O', 105, 255, 130, 0), /* oculos de visao noturna verde */
    ENEMY_STYLE('S', '=',  82,  94,  88, 0), /* luneta da sniper */
    ENEMY_STYLE('G', '-', 196, 208, 216, 0), /* cano longo da sniper */
    ENEMY_STYLE('F', '*', 145, 255, 145, 0), /* laser verde */
    ENEMY_STYLE('R', '=',  40,  42,  46, 0)  /* botas */
};

static const EnemyArtStyle TROLL_STYLE[128] = {
    ENEMY_STYLE('A', '#', 118,  98,  72, 1), /* armadura bronze-ferro pesada */
    ENEMY_STYLE('D', '%',  62,  50,  38, 1), /* sombra */
    ENEMY_STYLE('S', '#',  84,  88,  96, 1), /* elmo e placas de ferro */
    ENEMY_STYLE('E', 'o', 255, 185,  45, 0), /* olhos brilhantes */
    ENEMY_STYLE('W', '|', 128,  86,  48, 0), /* cabo do martelo */
    ENEMY_STYLE('M', '#', 172, 180, 192, 0), /* cabeca pesada do martelo */
    ENEMY_STYLE('R', '=',  44,  42,  40, 0)  /* botas pesadas */
};

static const EnemyArtStyle APOSTLE_STYLE[128] = {
    ENEMY_STYLE('C', '#', 132,  38,  48, 1), /* manto/capuz carmesim escuro */
    ENEMY_STYLE('D', '%',  64,  20,  28, 1), /* sombra do manto */
    ENEMY_STYLE('A', '#',  62,  66,  78, 1), /* armadura negra */
    ENEMY_STYLE('E', 'O', 255, 220,  95, 0), /* olhos dourados */
    ENEMY_STYLE('K', '/', 215, 228, 242, 0)  /* laminas curvas duplas */
};

static const EnemyArtStyle ZODD_STYLE[128] = {
    ENEMY_STYLE('B', '#',  48,  38,  46, 1), /* corpo/armadura obsidiana */
    ENEMY_STYLE('D', '%', 112,  30,  38, 1), /* placas carmesim */
    ENEMY_STYLE('W', '*',  76,  24,  34, 1), /* asas grossas */
    ENEMY_STYLE('H', '^', 232, 224, 198, 0), /* chifres */
    ENEMY_STYLE('E', 'O', 255, 195,  45, 0), /* olhos dourados */
    ENEMY_STYLE('T', 'v', 240, 235, 222, 0), /* presas */
    ENEMY_STYLE('K', '/', 208, 218, 230, 0), /* garras pesadas */
    ENEMY_STYLE('R', '=',  36,  32,  38, 0)  /* grevas */
};

static const EnemyVisualDesign ENEMY_VISUAL_DESIGNS[] = {
    /* Z legado (15x18): Soldado corrompido com mascara/cranio e olhos vermelhos */
    {'Z', 15, 18, 0.70f, 0.98f, '\0', {0,0,0}, LEGACY_Z_STYLE, {
        "....DAAAAAD....",
        "...DAAAAAAAD...",
        "...DSSSDSSSD...",
        "...DSEDSESDD...",
        "....DSSSSSD....",
        "..DDCCCCCCCDD..",
        ".DCCCCCCCCCCCD.",
        ".DAA.CCCCC.AAD.",
        ".DAA.CCACC.AAD.",
        ".DAA.CCCCC.AAD.",
        "..DD.DDDDD.DD..",
        "....DAAAAAD....",
        "....DAA.AAD....",
        "....DAA.AAD....",
        "....DAA.AAD....",
        "....DDA.ADD....",
        "...DRRR.RRRD...",
        "...RRRR.RRRR..."
    }},
    /* D legado (17x19): Artilheiro Pesado com METRALHADORA rotativa grossa */
    {'D', 17, 19, 0.90f, 1.10f, 'F', {255,235,95}, LEGACY_D_STYLE, {
        ".....DAAAAAD.....",
        "....DAAAAAAAD....",
        "....DAEEEEEAD....",
        "....DAAAAAAAD....",
        ".CCCDDDDDDDDDCCC.",
        "CCCCAAAAAAAAACCCC",
        "CCCAADAAAAADAACCC",
        "CCAA.DAAAAAD.AACC",
        "DDAA.AAYYYAA.AADD",
        "DDAA.AAYYYAGGGGGG",
        "....DAAYYYAGGGGGF",
        "....DAAYYYAGGGGGF",
        ".....DAAAAAD.YY..",
        "....DDAA.AADD....",
        "....DAA...AAD....",
        "....DAA...AAD....",
        "...DRAA...AARD...",
        "...RRRR...RRRR...",
        "..RRRRR...RRRRR.."
    }},
    /* I legado (15x18): Fera Sombria agil com orelhas pontudas e garras longas */
    {'I', 15, 18, 0.78f, 0.96f, '\0', {0,0,0}, LEGACY_I_STYLE, {
        "...BB.....BB...",
        "...BBBDDDBBB...",
        "...DBBBBBBBDD..",
        "...DBEBBBEBD...",
        "....DBCCCBD....",
        "..DDBBBBBBBDD..",
        ".DBBBCCCCCBBBD.",
        "DBB..BBBBB..BBD",
        "DB...BBBBB...BD",
        "KK...DBBBD...KK",
        "KKK..DBBBD..KKK",
        ".KK..DB.BD..KK.",
        "....DBB.BBD....",
        "....DBB.BBD....",
        "...DBB...BBD...",
        "...DBB...BBD...",
        "...DDD...DDD...",
        "..DDDD...DDDD.."
    }},
    /* Imp (15x18): Comando tatico de capacete segurando SHOTGUN de cano duplo */
    {'M', 15, 18, 0.82f, 1.02f, 'F', {255,225,120}, IMP_SHOTGUN_STYLE, {
        "....DHHHHHD....",
        "...DHHHHHHHD...",
        "...DHEEEEEHD...",
        "....DHHHHHD....",
        "..DDAAAAAAADD..",
        ".DAAAAAAAAAAAD.",
        ".DAA.AAAAA.AAD.",
        ".DAA.AAAAA.AAD.",
        ".DA.WWWWGGGGGF.",
        ".DD.WWWWGGGGGF.",
        "....DWWAAAAD...",
        "....DAAAAAD....",
        "....DAA.AAD....",
        "....DAA.AAD....",
        "....DAA.AAD....",
        "....DDA.ADD....",
        "...DRRR.RRRD...",
        "...RRRR.RRRR..."
    }},
    /* Pinky (15x14): Cao-Blindado de ferro e carmesim de investida */
    {'Y', 15, 14, 1.24f, 0.80f, '\0', {0,0,0}, PINKY_STYLE, {
        "...DDAAAAADD...",
        "..DAAAAAAAAAD..",
        ".DACCCCCCCCAAD.",
        ".DAEEAAAAAEED..",
        "DDATTATATATTADD",
        "DAAACCCCCCCAAAD",
        "DAAADAAAAADAAAD",
        "DAAA.DAAAAD.AAD",
        "DAA..DAAAAD..AD",
        "DD...AA..AA..DD",
        "....AAA..AAA...",
        "...DAA....AAD..",
        "...RRR....RRR..",
        "..RRRR....RRRR."
    }},
    /* Cacodemon (17x18): Drone de Guerra blindado azul-aco com CANHOES DE PLASMA */
    {'C', 17, 18, 1.14f, 1.00f, 'F', {115,248,255}, CACODEMON_STYLE, {
        ".....DSSSSSD.....",
        "...DDAAAAAAADD...",
        "..DAAAAAAAAAAAD..",
        ".DSAAADDDDDAAASD.",
        "DSSAAEEEEEEEAASSD",
        "DSSAAEEPPPEEAASSD",
        "DSSAAEEEEEEEAASSD",
        ".DSAAADDDDDAAASD.",
        "..DAAAAAAAAAAAD..",
        ".GGDDAAAAAAADDGG.",
        "FGGGDAAAAAAADGGGF",
        "FGGG.DSSSSSD.GGGF",
        ".GG...DDDDD...GG.",
        ".................",
        ".................",
        ".................",
        ".................",
        "................."
    }},
    /* Baron of Hell (17x20): Cavaleiro Infernal negro/carmesim com chifres e nucleo de fogo */
    {'B', 17, 20, 1.24f, 1.32f, '\0', {0,0,0}, BARON_STYLE, {
        "HH.............HH",
        "HHH...DDDDD...HHH",
        ".HH..DAAAAAD..HH.",
        "..H..DAEAEAD..H..",
        ".....DAAAAAD.....",
        ".CCCDAAAAAAADCCC.",
        "CCCCAAAAAAAAACCCC",
        "CCAA.AACCCAA.AACC",
        "AA...AACEEAA...AA",
        "NN...AACCCAA...NN",
        "NNN..DAAAAAD..NNN",
        ".NN..DAAAAAD..NN.",
        "....DAA...AAD....",
        "....DAA...AAD....",
        "....DCC...CCD....",
        "...DAA.....AAD...",
        "...DAA.....AAD...",
        "...DRR.....RRD...",
        "..DRRR.....RRRD..",
        "..RRRR.....RRRR.."
    }},
    /* Soldado Kushan (15x18): Sniper encapuzado com oculos verde e SNIPER longa */
    {'K', 15, 18, 0.84f, 0.98f, 'F', {145,255,145}, KUSHAN_STYLE, {
        ".....DDDDD.....",
        "....DCCCCCD....",
        "...DCCEECCCD...",
        "...DCCCCCCCD...",
        "....DDAAADD....",
        "..DDCCAAACCDD..",
        ".DCCC.AAA.CCCD.",
        ".DCC..SSS..CCD.",
        ".DCC.GGGGGGGGGF",
        ".DCC.GGGGGGGGGF",
        "..DC.DAAAAD....",
        "....DCCCCCD....",
        "....DCC.CCD....",
        "....DCC.CCD....",
        "....DAA.AAD....",
        "....DAA.AAD....",
        "...DRRR.RRRD...",
        "...RRRR.RRRR..."
    }},
    /* Troll (17x19): Brutamontes de ferro e bronze com martelo de guerra pesado */
    {'T', 17, 19, 1.26f, 1.36f, '\0', {0,0,0}, TROLL_STYLE, {
        "............MMMMM",
        "....DSSSSSD.MMMMM",
        "...DSSEESSSDMMMMM",
        "...DSSSSSSSD..WW.",
        ".SSSDAAAAADSS.WW.",
        "SSSSAAAAAAAASSWW.",
        "SSSAAAAAAAAASSWW.",
        "SSA.AAAAAAAA.AWW.",
        "AA..AAAAAAAA..WW.",
        "DD..DAAAAAAD..WW.",
        "....DAAAAAAD..WW.",
        "....DSSSSSSD.....",
        "....DAA..AAD.....",
        "....DAA..AAD.....",
        "...DAA....AAD....",
        "...DAA....AAD....",
        "...DSS....SSD....",
        "..DRRR....RRRD...",
        "..RRRR....RRRR..."
    }},
    /* Apostolo (15x18): Assassino de manto carmesim com olhos dourados e laminas duplas */
    {'A', 15, 18, 0.86f, 1.08f, '\0', {0,0,0}, APOSTLE_STYLE, {
        ".....DDDDD.....",
        "....DCCCCCD....",
        "...DCECCCECD...",
        "....DCCCCCD....",
        "..DDCCCCCCCDD..",
        ".DCCCAAAAACCCD.",
        "DCC..AAAAA..CCD",
        "KKC..AAAAA..CKK",
        "KKK..DCCCCD.KKK",
        "KK...DCCCCD..KK",
        "....DCCCCCCD...",
        "....DCCCCCCCD..",
        "...DCCC..CCCD..",
        "...DCC....CCD..",
        "...DAA....AAD..",
        "...DAA....AAD..",
        "...DDD....DDD..",
        "..DDDD....DDDD."
    }},
    /* Zodd (17x20): Senhor Abissal colossal com chifres, asas grossas e garras */
    {'Q', 17, 20, 1.46f, 1.66f, 'E', {255,100,40}, ZODD_STYLE, {
        "HH.............HH",
        "HHH...BBBBB...HHH",
        ".HH..DBEBEBD..HH.",
        "WW...DBTTTBD...WW",
        "WWW.DDBBBBBDD.WWW",
        "WWWWDBBBBBBBBWWWW",
        "WWWWBBDDDDDBBWWWW",
        "WWWWBBBBBBBBBWWWW",
        ".WWWBBDDDDDBBWWW.",
        ".WW.BBBBBBBBB.WW.",
        "..KKBBBBBBBBBKK..",
        "..KKKBBBBBBBKKK..",
        ".....DBBBBBD.....",
        "....DBB...BBD....",
        "....DBB...BBD....",
        "...DBB.....BBD...",
        "...DDD.....DDD...",
        "...DRR.....RRD...",
        "..DRRR.....RRRD..",
        "..RRRR.....RRRR.."
    }}
};

#undef ENEMY_STYLE

/* Busca o sprite redesenhado correspondente ao simbolo do inimigo. */
static const EnemyVisualDesign *enemy_visual_design_for_label(char label) {
    int design_count = (int)(sizeof(ENEMY_VISUAL_DESIGNS) / sizeof(ENEMY_VISUAL_DESIGNS[0]));
    for (int i = 0; i < design_count; i++) {
        if (ENEMY_VISUAL_DESIGNS[i].label == label) return &ENEMY_VISUAL_DESIGNS[i];
    }
    return NULL;
}

/* Parametros de cache para ampliar sprites com Scale2x (4x) e gerar mipmaps 1/2 e 1/4. */
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

/* Celula ja resolvida de um sprite (glifo, token original e cor RGB). */
typedef struct {
    char glyph;
    char token;
    AsciiColor color;
} EnemyRenderCell;

/* Cache com o sprite ampliado em 4x e versoes reduzidas para distancias maiores. */
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

/* Ajusta o brilho de uma cor RGB mantendo dentro de um limite seguro. */
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

/* Converte uma posicao (x, y) da grade ASCII de origem em uma celula renderizavel. */
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

/* Le uma celula da grade com checagem de limites (retorna vazio fora da borda). */
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

/* Algoritmo Scale2x (EPX): dobra a resolucao suavizando degraus diagonais sem borrar. */
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

/* Faz media de cor num bloco [x0..x1, y0..y1] e escolhe o glifo mais perto do centro. */
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

/* Gera um nivel de mipmap reduzindo o sprite por um fator inteiro (2 ou 4). */
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

/* Adiciona iluminacao de borda, contorno escuro ao redor e gera os mipmaps 1/2 e 1/4. */
static void enemy_finish_sprite_cache(EnemySpriteCache *cache) {
    /* Destaca bordas de cima/esquerda e escurece bordas de baixo/direita pra dar volume. */
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

    /* Preenche celulas vazias adjacentes ao corpo com um contorno escuro. */
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

/* Monta o sprite cache aplicando Scale2x duas vezes (1x -> 2x -> 4x). */
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

/* Pre-calcula todos os caches de sprites uma unica vez na primeira renderizacao. */
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

/* Escolhe o nivel de mipmap (full, half ou quarter) conforme o tamanho projetado na tela. */
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

/* Amostra o mipmap nas coordenadas (u, v), aplicando box filter quando reduzindo. */
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

/* Contexto passado ao callback de amostragem de sprite do renderizador. */
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

/* Testa se o ponto (x, y) cai dentro de uma elipse centrada em (cx, cy). */
static int enemy_inside_oval(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx;
    float dy = (y - cy) / ry;
    return dx * dx + dy * dy < 1.0f;
}

/* Material usado na geracao procedural (cor base, glifo fixo e se brilha no escuro). */
typedef struct {
    AsciiColor color;
    char forced_glyph; /* '\0' = usa rampa ASCII densa com sombreamento */
    unsigned char emissive;
} EnemyProcMat;

/* Paleta de materiais procedurais no mesmo estilo nitido dos personagens jogaveis (player.c). */
static EnemyProcMat enemy_proc_palette(int mat) {
    static const EnemyProcMat pal[] = {
        {{  0,   0,   0}, '\0', 0}, /*  0: transparente */
        {{ 42,  46,  54}, '\0', 0}, /*  1: aco escuro / botas / contorno */
        {{ 84,  92, 106}, '\0', 0}, /*  2: armadura aco medio */
        {{148, 160, 178}, '\0', 0}, /*  3: aco claro / destaque */
        {{212, 222, 236}, '\0', 0}, /*  4: metal brilhante / laminas */
        {{ 68,  84,  62}, '\0', 0}, /*  5: traje tatico oliva escuro */
        {{104, 122,  92}, '\0', 0}, /*  6: traje tatico oliva claro */
        {{138,  42,  44}, '\0', 0}, /*  7: armadura/manto carmesim */
        {{192,  58,  54}, '\0', 0}, /*  8: carmesim vivo */
        {{ 74,  24,  30}, '\0', 0}, /*  9: sombra carmesim */
        {{154,  98,  52}, '\0', 0}, /* 10: madeira / couro */
        {{ 92,  58,  34}, '\0', 0}, /* 11: couro escuro */
        {{196, 162,  76}, '\0', 0}, /* 12: bronze / latão / municao */
        {{245, 210,  78}, '@',  1}, /* 13: brilho dourado / municao */
        {{255,  58,  48}, '@',  1}, /* 14: olho/visor vermelho */
        {{110, 255, 130}, '@',  1}, /* 15: visor/laser verde sniper */
        {{ 95, 245, 255}, '@',  1}, /* 16: plasma/lente ciano */
        {{255, 175,  48}, '@',  1}, /* 17: fogo/visor ambar */
        {{255, 248, 175}, '*',  1}, /* 18: flash de disparo */
        {{ 58,  76, 114}, '\0', 0}, /* 19: casco azul-aco */
        {{ 94, 118, 162}, '\0', 0}, /* 20: casco azul-aco claro */
        {{224, 218, 202}, '\0', 0}, /* 21: osso / chifre / mascara */
        {{ 30,  26,  34}, '\0', 0}, /* 22: obsidiana */
        {{ 66,  52,  64}, '\0', 0}, /* 23: obsidiana destaque */
        {{178, 146, 116}, '\0', 0}, /* 24: pele / rosto */
        {{ 34, 158, 178}, '\0', 0}, /* 25: azul-turquesa Makita */
        {{232, 196,  56}, '\0', 0}  /* 26: amarelo capacete/hazmat */
    };
    if (mat < 0 || mat >= (int)(sizeof(pal) / sizeof(pal[0]))) return pal[0];
    return pal[mat];
}

/* Silhueta anatomica base inspirada em Guts/Rambo/Arauto (player.c): nitida em resolucao nativa. */
static int enemy_humanoid_base(float x, float y, float torso_w, float leg_spread,
                               int body_mat, int trim_mat, int dark_mat, int plate_lines) {
    float ax = fabsf(x);
    int mat = 0;

    /* Pernas e botas de combate */
    float leg_w = torso_w * 0.28f;
    if (y > 0.62f && y < 0.96f && fabsf(ax - leg_spread) < leg_w) {
        mat = (y > 0.82f) ? dark_mat : body_mat;
        if (y > 0.72f && y < 0.755f) mat = trim_mat; /* joelheira */
        if (ax > leg_spread + leg_w * 0.65f) mat = dark_mat;
    }
    if (y >= 0.94f && y < 0.99f && ax > leg_spread - leg_w && ax < leg_spread + leg_w + 0.05f) {
        mat = dark_mat;
    }

    /* Tronco afunilado na cintura com placas e cinto */
    float waist = torso_w * (1.0f - 0.34f * fmaxf(0.0f, (y - 0.27f) / 0.36f));
    if (y > 0.27f && y < 0.64f && ax < waist) {
        mat = body_mat;
        if (plate_lines && fmodf((y + ax * 0.10f) * 28.0f, 1.0f) < 0.16f) mat = trim_mat;
        if (ax > waist - 0.025f) mat = dark_mat;
        if (ax < 0.025f && y < 0.54f) mat = dark_mat;
        if (y > 0.55f && y < 0.595f) mat = dark_mat;
        if (y > 0.555f && y < 0.59f && ax < 0.075f) mat = trim_mat;
    }

    /* Ombreiras e bracos */
    float arm_cx = torso_w + 0.08f + (y - 0.34f) * 0.04f;
    float arm_w = 0.068f + torso_w * 0.05f;
    if (y > 0.28f && y < 0.39f && ax > torso_w - 0.04f && ax < torso_w + 0.17f) {
        mat = (y < 0.31f || ax > torso_w + 0.13f) ? trim_mat : body_mat;
    }
    if (y > 0.38f && y < 0.65f && fabsf(ax - arm_cx) < arm_w) {
        mat = (y > 0.58f) ? dark_mat : body_mat;
        if (fabsf(ax - arm_cx) > arm_w * 0.72f) mat = dark_mat;
    }

    /* Pescoco */
    if (y > 0.21f && y < 0.29f && ax < 0.10f) {
        mat = dark_mat;
    }
    return mat;
}

/* Renderizador procedural nativo para todos os 11 inimigos: zero efeito "massinha". */
static int enemy_procedural_material(char label, float x, float y, int shooting, float time_now) {
    if (fabsf(x) > 1.0f || y < 0.0f || y > 1.0f) return 0;
    float ax = fabsf(x);
    int mat = 0;

    if (label == 'Z') {
        /* Z legado: Soldado tatico corrompido (estilo Fuzileiro/Rambo) com mascara e faca */
        mat = enemy_humanoid_base(x, y, 0.30f, 0.16f, 5, 6, 1, 1);
        /* Capacete + mascara de cranio */
        if (enemy_inside_oval(x, y, 0.0f, 0.155f, 0.22f, 0.115f)) {
            mat = (y < 0.145f) ? 2 : 21;
            if (y > 0.13f && y < 0.155f) mat = 1;
            if (y > 0.155f && y < 0.185f && ax > 0.04f && ax < 0.135f) mat = 14;
            if (y > 0.21f && y < 0.24f && ax < 0.09f && fmodf(x * 45.0f + 10.0f, 1.0f) < 0.45f) mat = 1;
        }
        /* Faca de combate na mao direita */
        if (x > 0.38f && x < 0.50f && y > 0.56f && y < 0.76f) {
            mat = (y > 0.63f) ? 4 : 1;
        }
        return mat;
    }

    if (label == 'I') {
        /* I legado: Stalker veloz em traje furtivo com laminas duplas nos antebracos */
        float tilt_x = x + (y - 0.5f) * 0.08f;
        mat = enemy_humanoid_base(tilt_x, y, 0.25f, 0.17f, 22, 7, 1, 0);
        /* Faixa carmesim diagonal no peito */
        if (y > 0.30f && y < 0.58f && fabsf(tilt_x - (0.35f - y * 0.8f)) < 0.045f) mat = 8;
        /* Cabeca angular com visor ambar */
        if (y > 0.07f && y < 0.24f && fabsf(tilt_x) < 0.19f - fmaxf(0.0f, y - 0.18f) * 0.9f) {
            mat = 22;
            if (y > 0.14f && y < 0.17f && fabsf(tilt_x) > 0.035f && fabsf(tilt_x) < 0.13f) mat = 17;
        }
        /* Laminas longas nos dois antebracos */
        if (y > 0.52f && y < 0.80f) {
            float blade_l = -0.38f - (y - 0.52f) * 0.42f;
            float blade_r =  0.38f + (y - 0.52f) * 0.42f;
            if (fabsf(x - blade_l) < 0.035f || fabsf(x - blade_r) < 0.035f) mat = 4;
        }
        return mat;
    }

    if (label == 'M') {
        /* Imp (M): Breacher de armadura tatica segurando SHOTGUN de cano duplo */
        mat = enemy_humanoid_base(x, y, 0.33f, 0.17f, 7, 3, 1, 1);
        /* Capacete de combate com visor vermelho brilhante */
        if (enemy_inside_oval(x, y, 0.0f, 0.15f, 0.24f, 0.115f)) {
            mat = (y < 0.12f) ? 3 : 2;
            if (y > 0.145f && y < 0.185f && ax < 0.17f) mat = 14;
            if (y > 0.205f && ax < 0.12f) mat = 1;
        }
        /* SHOTGUN (coronha de madeira + cano duplo de aco apontado para direita) */
        if (y > 0.44f && y < 0.56f && x > -0.34f && x < 0.04f) mat = 10; /* coronha madeira */
        if (y > 0.47f && y < 0.55f && x >= 0.04f && x < 0.42f) mat = 11; /* guarda-mao */
        if (y > 0.41f && y < 0.49f && x >= -0.08f && x < 0.86f) {
            mat = (y < 0.45f) ? 4 : 3; /* cano duplo superior/inferior */
            if (fabsf(y - 0.45f) < 0.008f) mat = 1;
        }
        if (shooting && enemy_inside_oval(x, y, 0.90f, 0.45f, 0.12f, 0.09f)) mat = 18;
        return mat;
    }

    if (label == 'K') {
        /* Soldado Kushan (K): Sniper encapuzado (estilo Arauto das Sombras) com SNIPER longa */
        /* Manto/capa fluida */
        if (y > 0.24f && y < 0.92f && ax < 0.26f + y * 0.22f) {
            float fold = sinf(x * 42.0f + y * 4.0f);
            mat = (fold > 0.45f) ? 6 : ((fold < -0.55f) ? 1 : 5);
            if (ax < 0.16f && y > 0.30f && y < 0.58f) mat = 2; /* colete */
        }
        if (y > 0.88f && y < 0.98f && fabsf(ax - 0.15f) < 0.08f) mat = 1;
        /* Capuz pontudo e visor verde de sniper */
        if (y > 0.03f && y < 0.29f) {
            float hood = (y < 0.12f) ? (0.06f + (y - 0.03f) * 2.0f) : (0.24f - (y - 0.12f) * 0.22f);
            if (ax < hood) {
                mat = (ax > hood - 0.035f) ? 6 : 5;
                if (y > 0.12f && ax < hood - 0.045f) {
                    mat = 1;
                    if (y > 0.155f && y < 0.188f && x > -0.02f && x < 0.12f) mat = 15; /* monoculo verde */
                }
            }
        }
        /* SNIPER LONGA com luneta alta e laser verde */
        if (y > 0.46f && y < 0.54f && x > -0.36f && x < 0.16f) mat = 1;  /* coronha/corpo */
        if (y > 0.40f && y < 0.455f && x > -0.06f && x < 0.34f) {
            mat = (x > 0.28f) ? 15 : 2; /* luneta + lente verde */
        }
        if (y > 0.45f && y < 0.495f && x >= 0.14f && x < 0.96f) mat = 4; /* cano longo fino */
        if (y > 0.435f && y < 0.51f && x > 0.88f && x < 0.96f) mat = 3;  /* freio de boca */
        if (shooting && enemy_inside_oval(x, y, 0.95f, 0.47f, 0.09f, 0.06f)) mat = 15;
        return mat;
    }

    if (label == 'D') {
        /* D legado (D): Artilheiro Pesado (estilo Guts/Exo) com METRALHADORA rotativa */
        mat = enemy_humanoid_base(x, y, 0.38f, 0.19f, 2, 3, 1, 1);
        /* Cinta de municao dourada cruzando o peito (estilo Rambo) */
        if (y > 0.29f && y < 0.58f && fabsf(x - (0.78f - y * 1.95f)) < 0.055f) {
            mat = (fmodf(y * 80.0f, 2.0f) < 1.2f) ? 13 : 1;
        }
        /* Capacete blindado largo com visor ambar */
        if (y > 0.06f && y < 0.25f && ax < 0.24f) {
            mat = (y < 0.11f) ? 3 : 2;
            if (y > 0.14f && y < 0.18f && ax < 0.17f) mat = 17;
        }
        /* METRALHADORA ROTATIVA (3 canos + tambor de municao dourado) */
        if (y > 0.48f && y < 0.66f && x > 0.02f && x < 0.34f) mat = (fmodf(x * 32.0f, 1.0f) < 0.3f) ? 1 : 12;
        if (y > 0.42f && y < 0.54f && x > -0.22f && x < 0.36f) mat = 2; /* chassi */
        if (x >= 0.34f && x < 0.94f) {
            int b1 = (y > 0.42f && y < 0.455f);
            int b2 = (y > 0.47f && y < 0.505f);
            int b3 = (y > 0.52f && y < 0.555f);
            if (b1 || b2 || b3) mat = 4;
            if ((x > 0.56f && x < 0.62f) || (x > 0.82f && x < 0.88f)) {
                if (y > 0.415f && y < 0.56f) mat = 1; /* abracadeiras */
            }
        }
        if (shooting && enemy_inside_oval(x, y, 0.94f, 0.485f, 0.11f, 0.10f)) mat = 18;
        return mat;
    }

    if (label == 'C') {
        /* Cacodemon (C): Drone-Ciborgue flutuante blindado + ARMA FUTURISTA de plasma */
        float hover = 0.03f * sinf(time_now * 4.0f);
        float cy = y + hover;
        /* Aletas laterais blindadas */
        if (cy > 0.22f && cy < 0.58f && ax > 0.46f && ax < 0.78f - fabsf(cy - 0.40f) * 1.1f) {
            mat = (ax > 0.64f) ? 3 : 19;
        }
        /* Esfera blindada principal */
        if (enemy_inside_oval(x, cy, 0.0f, 0.40f, 0.50f, 0.28f)) {
            mat = (x > 0.18f) ? 19 : 20;
            if (fmodf(cy * 26.0f, 1.0f) < 0.14f) mat = 1;
            /* Olho/lente otica ciano central */
            if (enemy_inside_oval(x, cy, 0.0f, 0.38f, 0.24f, 0.13f)) mat = 1;
            if (enemy_inside_oval(x, cy, 0.0f, 0.38f, 0.18f, 0.095f)) mat = 16;
            if (enemy_inside_oval(x, cy, 0.0f, 0.38f, 0.08f, 0.045f)) mat = 4;
        }
        /* ARMA FUTURISTA acoplada na base (ponteiras duplas + nucleo de plasma) */
        if (cy > 0.62f && cy < 0.84f && ax < 0.34f) {
            if (ax > 0.16f && ax < 0.32f) mat = 3; /* ponteiras */
            else if (cy < 0.74f) mat = 2;
            if (enemy_inside_oval(x, cy, 0.0f, 0.76f, 0.14f, 0.07f)) mat = shooting ? 18 : 16;
        }
        return mat;
    }

    if (label == 'Y') {
        /* Pinky (Y): Cao-Blindado de investida (baixo, largo, mandibula metalica) */
        if (y > 0.28f && y < 0.74f && ax < 0.62f - fabsf(y - 0.48f) * 0.75f) {
            mat = (y < 0.44f) ? 7 : 9;
            if (y < 0.38f && ax < 0.42f) mat = 2; /* placas de aco na testa */
        }
        /* Olhos predadores laranja */
        if (y > 0.40f && y < 0.47f && ax > 0.14f && ax < 0.32f) mat = 17;
        /* Mandibula com dentes de aco */
        if (y > 0.54f && y < 0.65f && ax < 0.32f) {
            mat = (fmodf(x * 26.0f + 10.0f, 1.0f) < 0.45f) ? 21 : 1;
        }
        /* Patas blindadas pesadas */
        if (y >= 0.66f && y < 0.96f && (fabsf(ax - 0.38f) < 0.12f || fabsf(ax - 0.16f) < 0.08f)) {
            mat = (y > 0.86f) ? 1 : 2;
        }
        return mat;
    }

    if (label == 'A') {
        /* Apostolo (A): Assassino sombrio encapuzado (estilo Arauto) com laminas curvas */
        float hem = 0.94f + 0.025f * sinf(x * 32.0f);
        if (y > 0.23f && y < hem && ax < 0.28f + y * 0.24f) {
            float fold = sinf(x * 44.0f + y * 3.0f);
            mat = (fold > 0.5f) ? 8 : ((fold < -0.55f) ? 9 : 7);
            if (ax < 0.13f) mat = 22;
        }
        /* Capuz e mascara palida com olhos dourados */
        if (y > 0.03f && y < 0.28f) {
            float hood = (y < 0.11f) ? (0.05f + (y - 0.03f) * 2.2f) : (0.23f - (y - 0.11f) * 0.22f);
            if (ax < hood) {
                mat = (ax > hood - 0.03f) ? 8 : 7;
                if (y > 0.11f && ax < hood - 0.045f) {
                    mat = 21; /* mascara ossea */
                    if (y > 0.155f && y < 0.185f && ax > 0.035f && ax < 0.105f) mat = 13;
                }
            }
        }
        /* Laminas curvas duplas nas laterais */
        if (y > 0.44f && y < 0.78f) {
            float curve = 0.44f + sinf((y - 0.44f) * 6.0f) * 0.18f;
            if (fabsf(ax - curve) < 0.045f) mat = 4;
        }
        return mat;
    }

    if (label == 'T') {
        /* Troll (T): Colosso de Ferro/Bronze com martelo de guerra sobre o ombro */
        /* Martelo gigante atras/ao lado do ombro direito (como a Dragonslayer do Guts!) */
        if (x > 0.42f && x < 0.52f && y > 0.20f && y < 0.74f) mat = 10; /* cabo */
        if (x > 0.28f && x < 0.76f && y > 0.04f && y < 0.22f) {
            mat = (y < 0.08f || x > 0.68f) ? 4 : 3; /* cabeca de ferro */
        }
        int body = enemy_humanoid_base(x, y, 0.44f, 0.22f, 11, 12, 1, 1);
        if (body) mat = body;
        /* Elmo pesado rebitado com visor ambar */
        if (enemy_inside_oval(x, y, 0.0f, 0.16f, 0.23f, 0.105f)) {
            mat = (y < 0.13f) ? 3 : 2;
            if (y > 0.155f && y < 0.185f && ax > 0.03f && ax < 0.14f) mat = 17;
        }
        return mat;
    }

    if (label == 'B') {
        /* Baron of Hell (B): Cavaleiro Infernal com chifres, peitoral em brasa e manoplas */
        /* Chifres curvos */
        if (y > 0.01f && y < 0.16f) {
            float horn_x = 0.20f + (0.16f - y) * 1.4f;
            if (fabsf(ax - horn_x) < 0.05f) mat = 21;
        }
        int body = enemy_humanoid_base(x, y, 0.42f, 0.21f, 22, 7, 1, 1);
        if (body) mat = body;
        /* Fenda/nucleo incandescente no peito e manoplas em brasa */
        if (enemy_inside_oval(x, y, 0.0f, 0.40f, 0.12f, 0.07f)) mat = 17;
        if (y > 0.56f && y < 0.67f && ax > 0.42f && ax < 0.62f) mat = 17;
        /* Elmo com olhos em chamas */
        if (y > 0.07f && y < 0.25f && ax < 0.22f - fmaxf(0.0f, y - 0.18f) * 0.8f) {
            mat = 22;
            if (ax > 0.16f) mat = 7;
            if (y > 0.145f && y < 0.178f && ax > 0.04f && ax < 0.15f) mat = 17;
        }
        return mat;
    }

    if (label == 'Q') {
        /* Zodd (Q): Senhor Abissal colossal (asas largas atras do corpo, chifres, armadura) */
        /* Asas abertas atras do corpo */
        if (y > 0.16f && y < 0.68f && ax > 0.30f) {
            float wing_top = 0.16f + (ax - 0.30f) * 0.32f;
            float wing_bot = 0.64f - (ax - 0.30f) * 0.28f + 0.04f * sinf(ax * 26.0f);
            if (y > wing_top && y < wing_bot && ax < 0.96f) {
                mat = (y < wing_top + 0.045f) ? 7 : 9;
            }
        }
        /* Chifres colossais */
        if (y > 0.005f && y < 0.15f) {
            float horn_x = 0.21f + (0.15f - y) * 1.6f;
            if (fabsf(ax - horn_x) < 0.055f) mat = 21;
        }
        int body = enemy_humanoid_base(x, y, 0.44f, 0.22f, 22, 23, 1, 1);
        if (body) mat = body;
        /* Garras prateadas */
        if (y > 0.57f && y < 0.69f && ax > 0.44f && ax < 0.64f) mat = 4;
        /* Rosto/elmo fera com olhos dourados */
        if (y > 0.07f && y < 0.25f && ax < 0.23f - fmaxf(0.0f, y - 0.18f) * 0.8f) {
            mat = 22;
            if (y > 0.14f && y < 0.175f && ax > 0.04f && ax < 0.16f) mat = shooting ? 14 : 13;
            if (y > 0.20f && y < 0.23f && ax < 0.12f && fmodf(x * 55.0f + 10.0f, 1.0f) < 0.45f) mat = 21;
        }
        return mat;
    }

    if (label == 'U') {
        /* Boss 1 (U): Obreiro Pesado Blindado com capacete amarelo e MAQUITA girando */
        mat = enemy_humanoid_base(x, y, 0.40f, 0.20f, 19, 2, 1, 1);
        /* Suspensorios industriais amarelos sobre o macacao */
        if (y > 0.28f && y < 0.56f && (fabsf(ax - 0.14f) < 0.035f)) mat = 26;
        /* Capacete de obra amarelo com viseira de solda escura */
        if (enemy_inside_oval(x, y, 0.0f, 0.15f, 0.26f, 0.115f)) {
            mat = (y < 0.155f) ? 26 : 24;
            if (y > 0.145f && y < 0.195f && ax < 0.19f) mat = 1; /* viseira */
            if (y > 0.160f && y < 0.182f && ax > 0.04f && ax < 0.13f) mat = 17; /* olhos */
        }
        if (y > 0.125f && y < 0.155f && ax < 0.30f) mat = 26; /* aba do capacete */
        /* MAQUITA (carcaca azul-turquesa + disco serrilhado prateado girando) */
        if (y > 0.44f && y < 0.57f && x > 0.10f && x < 0.56f) mat = 25; /* corpo Makita */
        if (y > 0.41f && y < 0.46f && x > 0.18f && x < 0.48f) mat = 1;  /* alca preta */
        float saw_cx = 0.68f, saw_cy = 0.50f;
        float sdx = (x - saw_cx) / 0.24f, sdy = (y - saw_cy) / 0.14f;
        float sr2 = sdx * sdx + sdy * sdy;
        if (sr2 < 1.0f) {
            float ang = atan2f(sdy, sdx) + time_now * (shooting ? 26.0f : 12.0f);
            float teeth = sinf(ang * 10.0f);
            mat = (sr2 < 0.18f) ? 1 : ((teeth > 0.15f) ? 4 : 3);
        }
        if (shooting && enemy_inside_oval(x, y, 0.88f, 0.44f, 0.12f, 0.09f)) mat = 18;
        return mat;
    }

    if (label == 'V') {
        /* Boss 2 (V): Pistoleiro Fora-da-Lei com chapeu cowboy, sobretudo e REVOLVER GRANDE */
        /* Sobretudo longo marrom escuro */
        if (y > 0.27f && y < 0.90f && ax < 0.34f + (y - 0.27f) * 0.24f) {
            float fold = sinf(x * 36.0f + y * 3.0f);
            mat = (fold > 0.45f) ? 10 : 11;
            if (ax < 0.16f && y < 0.64f) mat = 22; /* colete escuro */
            if (y > 0.54f && y < 0.59f && ax < 0.30f) mat = (ax < 0.07f) ? 12 : 1; /* fivela dourada */
        }
        if (y > 0.86f && y < 0.98f && fabsf(ax - 0.18f) < 0.09f) mat = 1;
        /* Lenco vermelho no pescoco */
        if (y > 0.24f && y < 0.33f && ax < 0.24f - (y - 0.24f) * 1.4f) mat = 8;
        /* Rosto sombreado + Chapeu cowboy de aba larga */
        if (enemy_inside_oval(x, y, 0.0f, 0.17f, 0.19f, 0.09f)) {
            mat = 24;
            if (y < 0.16f) mat = 1;
            if (y > 0.155f && y < 0.18f && ax > 0.04f && ax < 0.12f) mat = 17;
        }
        if (y > 0.05f && y < 0.145f && ax < 0.21f) mat = 11; /* copa do chapeu */
        if (y > 0.115f && y < 0.148f && ax < 0.44f) mat = (y < 0.128f) ? 12 : 11; /* aba larga */
        /* REVOLVER GRANDE cromado (tambor grande + cano longo + cabo de madeira) */
        if (y > 0.48f && y < 0.61f && x > 0.04f && x < 0.22f) mat = 10; /* cabo madeira */
        if (y > 0.41f && y < 0.53f && x >= 0.20f && x < 0.44f) {
            mat = (fmodf(y * 55.0f, 1.0f) < 0.28f) ? 2 : 4; /* tambor grande cromado */
        }
        if (y > 0.41f && y < 0.475f && x >= 0.44f && x < 0.94f) mat = 4; /* cano longo pesado */
        if (shooting && enemy_inside_oval(x, y, 0.94f, 0.44f, 0.13f, 0.09f)) mat = 18;
        return mat;
    }

    if (label == 'W') {
        /* Boss 3 (W): Mafioso Blindado de terno risca-de-giz, fedora e TOMMY GUN */
        mat = enemy_humanoid_base(x, y, 0.39f, 0.19f, 22, 3, 1, 0);
        /* Riscas de giz verticais no terno + gravata vermelha */
        if (y > 0.28f && y < 0.62f && ax < 0.34f && fmodf(x * 22.0f + 10.0f, 1.0f) < 0.14f) mat = 2;
        if (y > 0.27f && y < 0.52f && ax < 0.045f) mat = 8;
        /* Rosto + Chapeu Fedora preto com faixa dourada */
        if (enemy_inside_oval(x, y, 0.0f, 0.18f, 0.19f, 0.085f)) {
            mat = 24;
            if (y > 0.16f && y < 0.185f && ax > 0.04f && ax < 0.12f) mat = 14;
        }
        if (y > 0.05f && y < 0.145f && ax < 0.21f) mat = 22;
        if (y > 0.12f && y < 0.142f && ax < 0.22f) mat = 12; /* faixa dourada */
        if (y > 0.135f && y < 0.158f && ax < 0.36f) mat = 22; /* aba fedora */
        /* TOMMY GUN (coronha madeira + tambor redondo grande + cano longo) */
        if (y > 0.46f && y < 0.56f && x > -0.34f && x < 0.06f) mat = 10; /* coronha */
        if (y > 0.43f && y < 0.50f && x >= 0.04f && x < 0.92f) mat = (x > 0.42f) ? 3 : 2; /* corpo/cano */
        if (enemy_inside_oval(x, y, 0.34f, 0.56f, 0.16f, 0.10f)) {
            mat = (enemy_inside_oval(x, y, 0.34f, 0.56f, 0.07f, 0.045f)) ? 12 : 1; /* tambor 50 balas */
        }
        if (y > 0.49f && y < 0.58f && x > 0.56f && x < 0.65f) mat = 10; /* empunhadura frontal */
        if (shooting && enemy_inside_oval(x, y, 0.94f, 0.465f, 0.11f, 0.08f)) mat = 18;
        return mat;
    }

    if (label == 'X') {
        /* Boss 4 (X): Piromaniaco Hazmat com tanques de combustivel e LANCA-CHAMAS */
        /* Tanques de combustivel vermelhos/amarelos atras dos ombros */
        if (y > 0.16f && y < 0.52f && (fabsf(ax - 0.42f) < 0.12f)) {
            mat = (y < 0.24f) ? 26 : 8;
            if (fabsf(y - 0.34f) < 0.02f) mat = 1;
        }
        int body = enemy_humanoid_base(x, y, 0.41f, 0.20f, 7, 26, 1, 1);
        if (body) mat = body;
        /* Mascara de gas hazmat com lentes laranja incandescentes */
        if (enemy_inside_oval(x, y, 0.0f, 0.16f, 0.24f, 0.115f)) {
            mat = (y < 0.13f) ? 26 : 2;
            if (y > 0.145f && y < 0.185f && ax > 0.03f && ax < 0.15f) mat = 17;
            if (y > 0.195f && y < 0.25f && ax < 0.09f) mat = 1; /* filtro da mascara */
        }
        /* LANCA-CHAMAS pesado (tubo + tanque inferior + chama-piloto na ponta) */
        if (y > 0.42f && y < 0.52f && x > -0.14f && x < 0.84f) mat = (y < 0.46f) ? 3 : 2;
        if (y > 0.51f && y < 0.61f && x > 0.12f && x < 0.52f) mat = 8; /* cilindro combustivel */
        if (enemy_inside_oval(x, y, 0.90f, 0.47f, shooting ? 0.16f : 0.07f, shooting ? 0.11f : 0.05f)) {
            mat = shooting ? 18 : 17; /* chama-piloto / jato */
        }
        return mat;
    }

    return 0;
}

/* Callback de amostragem chamado pelo renderer para cada celula do sprite de um inimigo. */
static int enemy_sprite_sampler(float u, float v, int sx, int sy,
                                int sprite_w, int sprite_h,
                                float dist, float fog, float time_now,
                                const void *user_data,
                                char *out_ch, AsciiColor *out_col) {
    (void)dist;
    EnemyRenderCtx *ctx = (EnemyRenderCtx *)(uintptr_t)user_data;
    if (!ctx || !ctx->api) {
        return 0;
    }
    const RendererApi *r = ctx->api;

    /* Renderizacao procedural em resolucao nativa (mesma tecnica nitida dos personagens jogaveis). */
    float px = u * 2.0f - 1.0f;
    float py = v;
    int pmat = enemy_procedural_material(ctx->label, px, py, ctx->shooting, time_now);
    if (pmat > 0) {
        EnemyProcMat m = enemy_proc_palette(pmat);
        if (m.emissive) {
            *out_ch = m.forced_glyph ? m.forced_glyph : '@';
            *out_col = r->scale_rgb((float)m.color.r, (float)m.color.g, (float)m.color.b,
                                    fmaxf(0.75f, fog));
            return 1;
        }
        /* Sombreamento cilindrico suave usando a normal aproximada da superficie. */
        float nz = sqrtf(fmaxf(0.22f, 1.0f - px * px * 0.48f - (py - 0.45f) * (py - 0.45f) * 0.25f));
        float lum = (0.48f + 0.44f * nz) * fog;
        float boost = ctx->shooting ? 1.18f : 1.0f;
        *out_ch = m.forced_glyph ? m.forced_glyph : r->sample_ramp(lum, sx, sy);
        *out_col = r->scale_rgb((float)m.color.r, (float)m.color.g, (float)m.color.b,
                                (0.72f + 0.34f * nz) * fog * boost);
        return 1;
    }
    /* Apenas o sprite desconhecido '?' cai no fallback em cache Scale2x. */
    if (ctx->label != '?') {
        return 0;
    }

    if (!ctx->cache) {
        return 0;
    }

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

/* Chamas afiladas de queimadura nos inimigos (roxo para Arauto, laranja de fogo para Lanca-Chamas). */
static void entities_draw_shadow_flame(const Player *p, const RendererApi *r,
                                       float wx, float wy, float height,
                                       float size, float phase, int is_fire) {
    int sx, sy;
    float depth;
    if (!r->project_point(p, wx, wy, height, &sx, &sy, &depth)) {
        return;
    }
    int rw = 0, rh = 0;
    r->get_grid_size(&rw, &rh);
    int radius = (int)((float)rh * size / depth);
    if (radius < 2) radius = 2;
    if (radius > 32) radius = 32;
    AsciiColor black = {0, 0, 0};
    for (int y = -radius * 2; y <= radius; y++) {
        float v = (float)y / (float)radius;
        float width = v < 0.0f ? 1.0f + v * 0.46f : sqrtf(fmaxf(0.0f, 1.0f - v * v));
        float sway = sinf(phase * 7.0f + v * 5.0f) * 0.16f * (1.0f - v);
        for (int x = -radius * 2; x <= radius * 2; x++) {
            int px = sx + x;
            int py = sy + y;
            if (px < 0 || px >= rw || py < 0 || py >= rh) {
                continue;
            }
            float u = (float)x / (float)radius - sway;
            float edge = fabsf(u) / fmaxf(width, 0.01f);
            float ripple = 0.09f * sinf(v * 13.0f + phase * 11.0f + u * 4.0f);
            if (edge > 1.0f + ripple) {
                continue;
            }
            float vein = sinf(u * 9.0f + v * 8.0f - phase * 12.0f);
            AsciiColor color;
            char ch;
            if (edge < 0.48f && v > -0.6f) {
                color = is_fire ? (AsciiColor){255, 245, 165} : (AsciiColor){250, 205, 255};
                ch = '@';
            } else if (edge < 0.78f) {
                color = is_fire ? (AsciiColor){255, 130, 25} : (AsciiColor){195, 95, 255};
                ch = vein > 0.0f ? '#' : '*';
            } else {
                color = is_fire ? (AsciiColor){215, 42, 12} : (AsciiColor){110, 48, 185};
                ch = v < -0.6f ? '^' : '+';
            }
            r->set_cell_if_closer(px, py, depth, ch, color, black);
        }
    }
}

/* Dados passados ao sampler de sprite para desenhar uma arma caida no chao. */
typedef struct {
    int weapon_type;
    const RendererApi *api;
} DropRenderCtx;

static int weapon_drop_sampler(float u, float v, int sx, int sy,
                               int sprite_w, int sprite_h,
                               float dist, float fog, float time_now,
                               const void *user_data,
                               char *out_ch, AsciiColor *out_col) {
    (void)sprite_w;
    (void)sprite_h;
    (void)dist;
    const DropRenderCtx *ctx = (const DropRenderCtx *)user_data;
    if (!ctx || !ctx->api) return 0;
    return map_sample_weapon_pickup_sprite(ctx->weapon_type, u, v, sx, sy,
                                           fog, time_now, ctx->api, out_ch, out_col);
}

/* Renderiza os orbes teleguiados do jogador e os drops de armas no chao. */
static void entities_render_drops_and_orbs(const Player *player, const RendererApi *r) {
    /* Orbes teleguiados disparados pela Arma Futurista do jogador (com rastro de plasma) */
    for (int i = 0; i < MAX_PLAYER_ORBS; i++) {
        const PlayerOrb *orb = &player_orbs[i];
        if (!orb->active) continue;
        for (int t = 3; t >= 1; t--) {
            float lag = (float)t * 0.14f;
            r->draw_world_orb(player, orb->x - orb->dx * lag, orb->y - orb->dy * lag, 0.45f,
                              0.075f - (float)t * 0.015f, 2, 10,
                              '*', (AsciiColor){120, 215, 255},
                              '.', (AsciiColor){150, 95, 255},
                              0.52f);
        }
        r->draw_world_orb(player, orb->x, orb->y, 0.45f,
                          0.115f, 3, 18,
                          '@', (AsciiColor){210, 255, 255},
                          '*', (AsciiColor){95, 165, 255},
                          0.62f);
    }

    /* Armas derrubadas no chao por inimigos derrotados (armas de boss maiores e mais destacadas) */
    for (int i = 0; i < MAX_WEAPON_DROPS; i++) {
        const WeaponDrop *d = &weapon_drops[i];
        if (!d->active) continue;
        int is_boss_weapon = ((int)d->weapon >= (int)WEAPON_MAQUITA);
        RenderSpriteSpec spec;
        spec.world_x = d->x;
        spec.world_y = d->y;
        spec.height_scale = (is_boss_weapon ? 0.42f : 0.34f) / 0.78f;
        spec.width_ratio = is_boss_weapon ? 2.05f : 1.95f;
        spec.min_w = is_boss_weapon ? 12 : 10;
        spec.min_h = is_boss_weapon ? 6 : 5;
        spec.min_fog = is_boss_weapon ? 0.50f : 0.45f;

        DropRenderCtx dctx;
        dctx.weapon_type = (int)d->weapon;
        dctx.api = r;
        r->draw_world_sprite(player, &spec, weapon_drop_sampler, &dctx);
    }
}

/* Renderiza inimigos ordenados por profundidade, projeteis, drops e efeitos de queimadura. */
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

        /* Bosses de arena (U, V, W, X) usam escala maior para destacar na sala. */
        int is_arena_boss = is_optional_enemy_symbol(e->symbol);
        RenderSpriteSpec spec;
        spec.world_x = e->x;
        spec.world_y = e->y;
        spec.height_scale = is_arena_boss ? 1.48f
                          : (visual ? (REDESIGN_IMP_HEIGHT_SCALE * visual->world_height)
                                    : (legacy ? legacy->height_scale : 1.0f));
        spec.width_ratio = is_arena_boss ? 1.18f
                         : (visual ? visual->width_ratio
                                   : (legacy ? legacy->width_ratio : 0.625f));
        spec.min_w = 4;
        spec.min_h = 6;
        spec.min_fog = 0.65f;

        /* Mostra flash de disparo logo apos o inimigo atacar (primeiros ~28% do cooldown). */
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
            /* Orienta a ponteira da flecha conforme a direcao relativa a camera. */
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
        } else if (projectile->kind == ENEMY_PROJECTILE_FIREBALL) {
            if (projectile->path == ENEMY_PROJECTILE_STRAIGHT && projectile->speed > 4.0f) {
                /* Jato de fogo do Boss Lanca-Chamas */
                r->draw_world_orb(player, projectile->x, projectile->y, 0.46f,
                                  0.13f, 3, 18,
                                  '@', (AsciiColor){255, 225, 80},
                                  '*', (AsciiColor){255, 70, 15},
                                  0.62f);
            } else {
                /* Chumbinhos metalicos da Shotgun do Imp (sem aparencia de bola de fogo) */
                int sx, sy;
                float depth;
                if (r->project_point(player, projectile->x, projectile->y, 0.44f,
                                     &sx, &sy, &depth)) {
                    AsciiColor core = {255, 248, 195};
                    AsciiColor steel = {175, 185, 198};
                    AsciiColor smoke = {115, 122, 132};
                    r->set_cell_if_closer(sx, sy, depth, 'o', core, black);
                    r->set_cell_if_closer(sx - 1, sy, depth, '*', steel, black);
                    r->set_cell_if_closer(sx + 1, sy, depth, '*', steel, black);
                    r->set_cell_if_closer(sx, sy - 1, depth, '.', smoke, black);
                    r->set_cell_if_closer(sx, sy + 1, depth, '.', smoke, black);
                }
            }
        } else {
            /* Orbe de energia do Cacodemon */
            r->draw_world_orb(player, projectile->x, projectile->y, 0.46f,
                              0.095f, 3, 16,
                              'O', (AsciiColor){155, 255, 255},
                              '*', (AsciiColor){20, 150, 255},
                              0.55f);
        }
    }

    entities_render_drops_and_orbs(player, r);

    /* Chamas de queimadura (burn DOT do Arauto ou Lanca-Chamas) renderizadas via RendererApi */
    float burn_now = r->get_time();
    for (int i = 0; i < mgr->count; i++) {
        const Enemy *e = &mgr->enemies[i];
        if (!e->active || e->burn_time <= 0.0f) continue;
        float fade = fminf(1.0f, e->burn_time * 2.0f);
        for (int j = 0; j < 7; j++) {
            float phase = burn_now + (float)i * 1.7f + (float)j * 0.73f;
            float angle = (float)j * 2.4f + burn_now * 0.7f;
            float height = 0.12f + fmodf(burn_now * 0.65f + (float)j * 0.137f, 0.85f);
            entities_draw_shadow_flame(player, r,
                                       e->x + cosf(angle) * 0.22f,
                                       e->y + sinf(angle) * 0.22f,
                                       height,
                                       (0.045f + 0.025f * (1.0f - height)) * fade,
                                       phase, e->burn_is_fire);
        }
    }
}
