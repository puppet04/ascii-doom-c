#ifndef PLAYER_H
#define PLAYER_H

#include "config.h"
#include "map.h"

/*
 * MODULO 2: JOGADOR, FISICA E INPUT
 * Responsavel pelo estado do jogador, movimentacao com deteccao de colisao
 * contra o mapa, rotacao da camera e leitura do teclado.
 */

typedef struct {
    int id;
    float x;
    float y;
    float angle;
    float fov;
    int health;
    int ammo;
    int score;
} Player;

/* Inicializa o jogador com posicao, angulo e status padrao. */
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
 * Le as teclas pressionadas no terminal sem bloquear o game loop.
 * Atualiza *running = 0 caso o usuario pressione ESC.
 * Define *did_shoot = 1 caso o usuario atire (Espaco) e tenha municao.
 */
void player_handle_input(Player *p, const GameMap *map, int *running, int *did_shoot);

#endif /* PLAYER_H */
