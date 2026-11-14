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
#define MAX_ENEMIES 16

typedef enum {
    WEAPON_PISTOL = 0,
    WEAPON_SHOTGUN = 1,
    WEAPON_PLASMA = 2,
    WEAPON_BFG = 3
} WeaponType;

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
} Enemy;

typedef struct {
    Enemy enemies[MAX_ENEMIES];
    int count;
} EnemyManager;

/* Preenche um vetor com as 4 armas base do jogo. */
void weapons_init_defaults(Weapon out_weapons[MAX_WEAPONS]);

/* Inicializa os inimigos iniciais da fase. */
void enemies_init_defaults(EnemyManager *mgr);

/* Atualiza movimentacao e ataque dos inimigos usando Delta Time (dt). */
void enemies_update(EnemyManager *mgr, const GameMap *map, Player *player, float dt);

/*
 * Verifica se o tiro disparado pelo jogador acertou algum inimigo na mira.
 * Retorna 1 se acertou (aplicando dano), 0 caso contrario.
 */
int enemies_check_hit(EnemyManager *mgr, const GameMap *map,
                      const Player *player, const Weapon *weapon);

#endif /* ENTITIES_H */
