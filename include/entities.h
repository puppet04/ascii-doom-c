#ifndef ENTITIES_H
#define ENTITIES_H

#include "map.h"
#include "player.h"
#include "render_api.h"

/*
 * MODULO 5: ARMAS E INIMIGOS (Responsavel: Guilherme - branch guilherme)
 * Define os tipos de armas do arsenal, suas estatisticas e o gerenciador
 * de inimigos (spawn, movimentacao por Delta Time e deteccao de acertos).
 */

#define MAX_WEAPONS 9
#define MAX_ENEMIES 32
#define MAX_WEAPON_DROPS 32
#define MAX_PLAYER_ORBS 16
#define SHADOW_BURN_DURATION 5.0f
#define SHADOW_BURN_DAMAGE 2

typedef enum {
    WEAPON_PISTOL = 0,
    WEAPON_SHOTGUN = 1,
    WEAPON_SNIPER = 2,
    WEAPON_MACHINEGUN = 3,
    WEAPON_FUTURISTIC = 4,
    WEAPON_MAQUITA = 5,
    WEAPON_REVOLVER = 6,
    WEAPON_TOMMYGUN = 7,
    WEAPON_FLAMETHROWER = 8
} WeaponType;

#define WEAPON_PLASMA WEAPON_MACHINEGUN
#define WEAPON_BFG    WEAPON_FUTURISTIC

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
    ENEMY_BOSS_MAQUITA,
    ENEMY_BOSS_REVOLVER,
    ENEMY_BOSS_TOMMY,
    ENEMY_BOSS_FLAME,
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
    int mag_size;
    int infinite_reserve;
} Weapon;

typedef struct {
    int active;
    float x;
    float y;
    WeaponType weapon;
    int ammo;
} WeaponDrop;

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
} PlayerOrb;

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
    float burn_time;
    float burn_tick;
    int burn_is_fire;
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
const WeaponDrop *enemies_weapon_drop_pool(void);
const PlayerOrb *enemies_player_orb_pool(void);
int enemies_player_is_burning(void);

/* Atualiza movimentacao e ataque dos inimigos usando Delta Time (dt). */
void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt);

/*
 * Verifica se o tiro disparado pelo jogador acertou algum inimigo na mira.
 * Retorna 1 se acertou (aplicando dano), 0 caso contrario.
 */
int enemies_check_hit(EnemyManager *mgr, const GameMap *map,
                      Player *player, const Weapon *weapon);

/* Desenha todos os inimigos e projeteis ativos usando a biblioteca de renderizacao. */
void entities_render(const EnemyManager *mgr, const Player *player, const RendererApi *r);

#endif /* ENTITIES_H */
