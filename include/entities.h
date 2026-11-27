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

/* Limites fixos dos pools de armas, inimigos, drops e orbes na fase */
#define MAX_WEAPONS 9
#define MAX_ENEMIES 32
#define MAX_WEAPON_DROPS 32
#define MAX_PLAYER_ORBS 16

/* Duracao e dano por segundo do fogo negro aplicado pelo Arauto das Sombras */
#define SHADOW_BURN_DURATION 5.0f
#define SHADOW_BURN_DAMAGE 2

/* Tipos de armas disponiveis no arsenal do jogador */
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

/* Aliases mantidos para compatibilidade com nomes antigos de armas */
#define WEAPON_PLASMA WEAPON_MACHINEGUN
#define WEAPON_BFG    WEAPON_FUTURISTIC

/* Catalogo de tipos de inimigos (legado, Doom, Berserk e bosses de arena) */
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

/* Forma como o inimigo causa dano no jogador */
typedef enum {
    ENEMY_ATTACK_MELEE = 0,
    ENEMY_ATTACK_HITSCAN,
    ENEMY_ATTACK_PROJECTILE
} EnemyAttackKind;

/* Visual e comportamento base de cada projetil inimigo */
typedef enum {
    ENEMY_PROJECTILE_ARROW = 0,
    ENEMY_PROJECTILE_FIREBALL,
    ENEMY_PROJECTILE_ENERGY_ORB
} EnemyProjectileKind;

/* Padrao de movimento do projetil apos o disparo */
typedef enum {
    ENEMY_PROJECTILE_STRAIGHT = 0,
    ENEMY_PROJECTILE_WAVERING,
    ENEMY_PROJECTILE_HOMING
} EnemyProjectilePath;

#define MAX_ENEMY_PROJECTILES 32

/* Estatisticas de uma arma do arsenal */
typedef struct {
    WeaponType type;
    char name[32];
    int damage;
    float cooldown;
    int ammo_cost;
    float range;
    int mag_size;
    int infinite_reserve; /* 1 para a pistola padrao, que nunca fica sem reserva */
} Weapon;

/* Arma largada no chao quando um inimigo a distancia ou boss morre */
typedef struct {
    int active;
    float x;
    float y;
    WeaponType weapon;
    int ammo;
} WeaponDrop;

/* Orbe teleguiado disparado pela Arma Futurista do jogador */
typedef struct {
    int active;
    float x;
    float y;
    float dx;
    float dy;
    float speed;
    float age;       /* tempo de vida acumulado para expirar caso erre o alvo */
    float turn_rate; /* velocidade com que corrige a rota em direcao ao inimigo */
    int damage;
} PlayerOrb;

/* Estado completo de um inimigo ativo na fase */
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
    float aggro_range;       /* distancia maxima para comecar a perseguir o jogador */
    int optional_for_exit;   /* 1 para bosses de arena que nao travam a saida E */
    float burn_time;         /* tempo restante da queimadura do fogo negro */
    float burn_tick;         /* acumulador para aplicar dano a cada 1 segundo */
    int burn_is_fire;
} Enemy;

/* Projetil disparado por inimigos (Imp, Kushan, Cacodemon, etc.) */
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

/* Vetor fixo com todos os inimigos da fase atual */
typedef struct {
    Enemy enemies[MAX_ENEMIES];
    int count;
} EnemyManager;

/* Preenche um vetor com as 4 armas base do jogo. */
void weapons_init_defaults(Weapon out_weapons[MAX_WEAPONS]);

/* Inicializa os inimigos iniciais da fase. */
void enemies_init_defaults(EnemyManager *mgr);

/* Le o mapa atual e cria inimigos nas posicoes marcadas pelos caracteres de spawn. */
void enemies_spawn_wave(EnemyManager *mgr, const GameMap *map, const Player *player);

/* Conta quantos inimigos ativos ainda estao vivos no total. */
int enemies_alive(const EnemyManager *mgr);

/* Conta apenas inimigos obrigatorios vivos (ignora bosses opcionais da saida E). */
int enemies_required_alive(const EnemyManager *mgr);

/* Avanca a bola de fogo do Arauto das Sombras e aplica queimadura no impacto. */
void enemies_update_fireball(EnemyManager *mgr, const GameMap *map, Player *player, float dt);

/* Retornam ponteiros somente leitura para os pools usados na renderizacao e nos testes. */
const EnemyProjectile *enemies_projectile_pool(void);
const WeaponDrop *enemies_weapon_drop_pool(void);
const PlayerOrb *enemies_player_orb_pool(void);

/* Retorna 1 se o jogador ainda estiver com queimadura residual de armadilha ou fogo. */
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
