#ifndef PLAYER_H
#define PLAYER_H

#include "config.h"
#include "map.h"

/*
 * MODULO 2: PERSONAGENS, CRIADOR DE PERSONAGEM E FISICA
 * (Responsavel: Strazzer - branch strazzer)
 * Responsavel pelos 4 personagens base, criador de personagem customizado,
 * movimentacao com colisao por Delta Time, mouse look e disparos.
 */

#define PRESET_CLASS_COUNT 4

typedef struct {
    char name[32];
    int max_health;
    int start_ammo;
    float move_speed;
    int start_weapon;
} CharacterClass;

typedef struct {
    int id;
    char class_name[32];
    float x;
    float y;
    float angle;
    float fov;
    float move_speed;
    int health;
    int max_health;
    int ammo;
    int active_weapon;
    int score;
    float bob_phase;
    float shoot_timer;
} Player;

/* Retorna um dos 4 personagens base pre-definidos (indice de 0 a 3). */
CharacterClass player_get_preset_class(int index);

/* Aplica os atributos de uma classe (base ou customizada) ao jogador. */
void player_apply_class(Player *p, const CharacterClass *cls);

/* Inicializa o jogador com posicao, angulo e atributos da classe padrao. */
void player_init(Player *p, float x, float y, float angle);

/*
 * Move o jogador considerando colisao por eixo (permite deslizar nas paredes).
 * forward: deslocamento para frente (>0) ou tras (<0)
 * strafe:  deslocamento lateral para direita (>0) ou esquerda (<0)
 * Retorna 1 se houve movimento em algum eixo, 0 se bloqueado.
 */
int player_move(Player *p, const GameMap *map, float forward, float strafe);

/* Gira o angulo de visao do jogador e normaliza entre 0 e 2*PI. */
void player_rotate(Player *p, float delta_angle);

/*
 * Processa teclado (WASD/Shift/R/ESC) e mouse (Mouse Look + Clique Esquerdo)
 * multiplicando todas as velocidades por dt (Delta Time).
 */
void player_handle_input(Player *p, const GameMap *map, float dt,
                         int *running, int *did_shoot);

#endif /* PLAYER_H */
