#ifndef RENDERER_H
#define RENDERER_H

#include "config.h"
#include "map.h"
#include "player.h"

/*
 * MODULO 3: MOTOR GRAFICO RAYCASTING ASCII + HUD
 * Responsavel por projetar o mapa 2D em uma visao 3D em primeira pessoa no
 * terminal utilizando o algoritmo DDA, z-buffer para sprites e minimapa.
 */

typedef struct {
    float x;
    float y;
    int active;
    int shooting;
    char label;
} SpriteEntity;

/* Configura o terminal do Windows (oculta cursor e habilita sequencias ANSI). */
void renderer_init(void);

/* Limpa o buffer de tela e reseta o z-buffer antes de desenhar um novo frame. */
void renderer_clear(void);

/* Calcula e desenha todas as colunas de parede usando Raycasting DDA. */
void renderer_draw_walls(const GameMap *map, const Player *player);

/* Desenha entidades (outros jogadores/inimigos) respeitando o z-buffer. */
void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count);

/* Desenha o minimapa 2D no canto superior esquerdo do frame. */
void renderer_draw_minimap(const GameMap *map, const Player *player,
                           const SpriteEntity *sprites, int count);

/* Desenha mira central e cabecalho/rodape de status (HUD). */
void renderer_draw_hud(const Player *player, int is_multiplayer,
                       int shoot_flash, int remote_connected);

/* Envia o frame pronto para o console sem piscar a tela. */
void renderer_present(int show_hud_footer, const Player *player,
                      int is_multiplayer, int remote_connected);

#endif /* RENDERER_H */
