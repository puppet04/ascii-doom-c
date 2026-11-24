#ifndef RENDERER_H
#define RENDERER_H

#include "config.h"
#include "map.h"
#include "player.h"
#include "entities.h"
#include "render_api.h"

/*
 * MODULO 3: MOTOR GRAFICO RAYCASTING ASCII COLORIDO (RAYLIB + GPU SHADER) + HUD
 * Biblioteca publica reutilizavel para projetar o mundo 3D, sprites, efeitos,
 * minimapa, arma e grades ASCII na GPU em 1 draw call a 165+ FPS.
 * Consulte docs/GUIA_LIB_RENDERIZACAO.md para exemplos de uso pelos demais modulos.
 */

/* Entidade simplificada usada para jogadores remotos multiplayer e marcadores do minimapa. */
typedef struct {
    float x;
    float y;
    int active;
    int shooting;
    char label;
    int visual_id;                  /* Preset visual ou skin customizada do jogador */
    CharacterAppearance appearance; /* Cores e pecas escolhidas no criador de skin */
    int team_accent;                /* >0: aliado Co-op (1..4); <0: rival PvP (-1..-4); 0: padrao */
} SpriteEntity;

/* === 1. Ciclo de Vida e Interface Publica === */

/* Abre a janela grafica da Raylib (1280x720), compila o shader GPU e monta o atlas mipmapped. */
void renderer_init(void);

/* Libera shader/texturas da GPU, restaura cursor se oculto e fecha a janela Raylib. */
void renderer_close(void);

/* Retorna a vtable publica RendererApi para entities.c e map.c desenharem sem editar renderer.c. */
const RendererApi *renderer_get_api(void);

/* Registra callbacks opcionais de customizacao de chao, paredes e minimapa (feitos em main.c). */
void renderer_set_floor_hook(RendererFloorHookFn hook);
void renderer_set_wall_hook(RendererWallStyleHookFn hook);
void renderer_set_minimap_hook(RendererMinimapHookFn hook);

/* Desenha qualquer grade AsciiCell 2D arbitaria em 1 draw call via GPU Shader (ex.: logos, fundo, HUD). */
void renderer_draw_ascii_buffer(const AsciiCell *cells, int width, int height,
                                int row_stride, float dst_x, float dst_y,
                                float dst_w, float dst_h);

/* === 2. Configuracao de Qualidade e Estilo Visual (F2 / F4) === */

typedef enum {
    ASCII_QUALITY_AUTO = 0,  /* Adapta celulas ao tamanho atual da janela */
    ASCII_QUALITY_LEVE = 1,  /* Preset BAIXO: caracteres maiores e carga minima */
    ASCII_QUALITY_MEDIA = 2, /* Preset MEDIO: equilibrio entre nitidez e custo */
    ASCII_QUALITY_ALTA = 3,  /* Preset ALTO: alta densidade ASCII */
    ASCII_QUALITY_ULTRA = 4  /* Preset ULTRA: micro-malha maxima */
} AsciiQualityMode;

typedef enum {
    ASCII_STYLE_CLASSIC = 0, /* Estilo 1: ASCII denso sobreposto tradicional */
    ASCII_STYLE_SOLID   = 1  /* Estilo 2: Micro-ASCII puro sem sobreposicao */
} AsciiVisualStyle;

/* Seleciona o estilo visual de renderizacao (0=Estilo 1 Sobreposto, 1=Estilo 2 Micro-ASCII). */
void renderer_set_visual_style(int style_mode);
int renderer_get_visual_style(void);

/* Ajusta dinamicamente a densidade da malha ASCII conforme qualidade (0..4) e resolucao da janela. */
void renderer_set_ascii_density(int quality_mode, int screen_w, int screen_h);

/* Ajusta suavemente a resolucao interna no modo AUTO em PCs mais antigos para estabilizar 60+ FPS. */
void renderer_update_auto_perf(float frame_dt);

/* === 3. Pipeline 3D do Frame (chamar nesta ordem entre frames) === */

/* Limpa a grade ASCII e reseta o z-buffer antes de desenhar um novo frame. */
void renderer_clear(void);

/* Configura o angulo vertical da camera (look up/down [-62..62]) e o modo 1a/3a pessoa. */
void renderer_set_camera_view(float pitch_offset, int third_person);

/* Passe 1 (DDA por coluna) + Passe 2 (teto/chao por linha) do mundo 3D no screen_grid e z_buffer. */
void renderer_draw_walls(const GameMap *map, const Player *player);

/* Desenha entidades simples (outros jogadores em LAN / silhueta 3a pessoa) respeitando o z_buffer. */
void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count);

/* Desenha a arma em 1a pessoa (espada Berserk ou rifle duplo com recoil/claroes). */
void renderer_draw_weapon(const Player *player);

/* Particulas da area da Kasane Teto e projetil Fogo Negro do Arauto, com oclusao por paredes. */
void renderer_draw_ability_effects(const Player *player);
void renderer_draw_burning_enemies(const Player *player, const EnemyManager *enemies);

/* Desenha o minimapa 2D colorido no canto superior esquerdo da grade ASCII. */
void renderer_draw_minimap(const GameMap *map, const Player *player,
                           const SpriteEntity *sprites, int count);

/* Desenha a mira central em modo fallback texto (no modo grafico a mira e vetorial/nitida no HUD). */
void renderer_draw_hud(const Player *player);

/* Envia o screen_grid para a GPU e desenha por cima a mira, cartoes do HUD, mugshot e status de habilidade. */
void renderer_draw_scene_and_hud(const Player *player, int is_multiplayer,
                                 int remote_connected, int mouse_locked, int fps_cap);

/* Encapsula BeginDrawing() -> renderer_draw_scene_and_hud() -> EndDrawing(). */
void renderer_present_window(const Player *player, int is_multiplayer,
                             int remote_connected, int mouse_locked, int fps_cap);

/* Imprime uma versao reduzida 80x26 no stdout (usado por --test-frame e CI sem janela). */
void renderer_present_stdout(const Player *player, int is_multiplayer,
                             int remote_connected);

#endif /* RENDERER_H */
