#ifndef ENTITIES_H
#define ENTITIES_H

#include "map.h"
#include "player.h"

/*
 * MODULO 5: ARMAS E INIMIGOS (Responsavel: Guilherme - branch guilherme)
 * Define os tipos de armas do arsenal, suas estatisticas e o gerenciador
 * de inimigos (spawn, movimentacao por Delta Time e deteccao de acertos).
 */

#define MAX_WEAPONS 4
#define MAX_ENEMIES 32

typedef enum {
    WEAPON_PISTOL = 0,
    WEAPON_SHOTGUN = 1,
    WEAPON_PLASMA = 2,
    WEAPON_BFG = 3
} WeaponType;

typedef enum {
    ENEMY_LEGACY_Z = 0,
    ENEMY_LEGACY_D,
    ENEMY_LEGACY_I,
    ENEMY_DOOM_IMP,
    ENEMY_DOOM_PINKY,
    ENEMY_DOOM_CACODEMON,
    ENEMY_DOOM_BARON,
    ENEMY_BERSERK_KUSHAN,
    ENEMY_BERSERK_TROLL,
    ENEMY_BERSERK_APOSTLE,
    ENEMY_BERSERK_ZODD,
    ENEMY_TYPE_COUNT
} EnemyType;

typedef enum {
    ENEMY_ATTACK_MELEE = 0,
    ENEMY_ATTACK_HITSCAN,
    ENEMY_ATTACK_PROJECTILE
} EnemyAttackKind;

typedef enum {
    ENEMY_PROJECTILE_ARROW = 0,
    ENEMY_PROJECTILE_FIREBALL,
    ENEMY_PROJECTILE_ENERGY_ORB
} EnemyProjectileKind;

typedef enum {
    ENEMY_PROJECTILE_STRAIGHT = 0,
    ENEMY_PROJECTILE_WAVERING,
    ENEMY_PROJECTILE_HOMING
} EnemyProjectilePath;

#define MAX_ENEMY_PROJECTILES 32

typedef struct {
    WeaponType type;
    char name[32];
    int damage;
    float cooldown;
    int ammo_cost;
    float range;
} Weapon;

typedef struct {
    int active;
    float x;
    float y;
    int health;
    int max_health;
    int damage;
    float speed;
    char symbol;
    float attack_timer;
    EnemyType type;
    EnemyAttackKind attack_kind;
    float attack_range;
    float attack_cooldown;
    float aggro_range;
    int optional_for_exit;
} Enemy;

typedef struct {
    int active;
    float x;
    float y;
    float dx;
    float dy;
    float speed;
    float age;
    float turn_rate;
    int damage;
    EnemyProjectileKind kind;
    EnemyProjectilePath path;
} EnemyProjectile;

typedef struct {
    Enemy enemies[MAX_ENEMIES];
    int count;
} EnemyManager;

/* Preenche um vetor com as 4 armas base do jogo. */
void weapons_init_defaults(Weapon out_weapons[MAX_WEAPONS]);

/* Inicializa os inimigos iniciais da fase. */
void enemies_init_defaults(EnemyManager *mgr);
void enemies_spawn_wave(EnemyManager *mgr, const GameMap *map, const Player *player);
int enemies_alive(const EnemyManager *mgr);
int enemies_required_alive(const EnemyManager *mgr);
void enemies_update_fireball(EnemyManager *mgr, const GameMap *map, Player *player, float dt);
const EnemyProjectile *enemies_projectile_pool(void);
int enemies_player_is_burning(void);

/* Atualiza movimentacao e ataque dos inimigos usando Delta Time (dt). */
void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt);

/*
 * Verifica se o tiro disparado pelo jogador acertou algum inimigo na mira.
 * Retorna 1 se acertou (aplicando dano), 0 caso contrario.
 */
int enemies_check_hit(EnemyManager *mgr, const GameMap *map,
                      Player *player, const Weapon *weapon);

#endif /* ENTITIES_H */
