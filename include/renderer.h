#ifndef RENDERER_H
#define RENDERER_H

#include "config.h"
#include "map.h"
#include "player.h"

/*
 * MODULO 3: MOTOR GRAFICO RAYCASTING ASCII COLORIDO (RAYLIB) + HUD
 * Projeta o mapa 2D em perspectiva 3D usando uma grade de alta resolucao
 * (160x80 caracteres ASCII com cores RGB), arma em primeira pessoa,
 * minimapa colorido e janela a 165 FPS.
 */

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} AsciiColor;

typedef struct {
    char ch;
    AsciiColor fg;
    AsciiColor bg;
} AsciiCell;

typedef struct {
    float x;
    float y;
    int active;
    int shooting;
    char label;
} SpriteEntity;

/* Abre a janela grafica da Raylib (1280x720) e configura FPS e cursor. */
void renderer_init(void);

/* Libera o cursor e fecha a janela grafica da Raylib. */
void renderer_close(void);

/* Limpa a grade ASCII e reseta o z-buffer antes de desenhar um novo frame. */
void renderer_clear(void);

/* Calcula e desenha as 160 colunas de parede/teto/chao usando Raycasting DDA. */
void renderer_draw_walls(const GameMap *map, const Player *player);

/* Desenha entidades (outros jogadores/inimigos) respeitando o z-buffer. */
void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count);

/* Desenha a arma em ASCII Art colorido com animacao de caminhada e disparo. */
void renderer_draw_weapon(const Player *player);

/* Desenha o minimapa 2D colorido no canto superior esquerdo da tela. */
void renderer_draw_minimap(const GameMap *map, const Player *player,
                           const SpriteEntity *sprites, int count);

/* Desenha a mira central no buffer ASCII. */
void renderer_draw_hud(const Player *player);

/* Renderiza a grade ASCII colorida e o painel de status na janela Raylib. */
void renderer_present_window(const Player *player, int is_multiplayer,
                             int remote_connected, int mouse_locked, int fps_cap);

/* Imprime uma versao reduzida do frame no stdout (usado por --test-frame). */
void renderer_present_stdout(const Player *player, int is_multiplayer,
                             int remote_connected);

#endif /* RENDERER_H */
