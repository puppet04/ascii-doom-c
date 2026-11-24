#ifndef MAP_H
#define MAP_H

#include "config.h"
#include "render_api.h"

#undef MAP_WIDTH
#undef MAP_HEIGHT
#undef MAX_DEPTH
#define MAP_WIDTH  32
#define MAP_HEIGHT 32
#define MAX_DEPTH  36.0f
#define MINIMAP_VIEW_SIZE     16
#define MINIMAP_SCROLL_MARGIN 2

/*
 * MODULO 1: MAPA, ARQUIVOS E COLISAO (Responsavel: Lucas - branch lucas)
 * Responsavel por armazenar a matriz 2D da fase, catalogo de niveis/fases,
 * carregar mapas de arquivos .txt e fornecer consultas rapidas de colisao.
 */

typedef struct {
    int width;
    int height;
    char tiles[MAP_HEIGHT][MAP_WIDTH + 1];
} GameMap;

typedef int (*MapCustomLoaderFn)(GameMap *map);

typedef struct {
    const char *name;
    const char *subtitle;
    const char *file_path;
    MapCustomLoaderFn custom_fn;
    float spawn1_x;
    float spawn1_y;
    float spawn1_angle;
    float spawn2_x;
    float spawn2_y;
    float spawn2_angle;
} MapLevelInfo;

/* Carrega um mapa padrao embutido no codigo (fallback caso arquivo falhe). */
void map_init_default(GameMap *map);

/* Carrega o mapa a partir de um arquivo texto. Retorna 1 em sucesso, 0 em erro. */
int map_load_from_file(GameMap *map, const char *filename);

/* Catalogo de niveis disponiveis para o seletor de fases e multiplayer. */
int map_level_count(void);
int map_level_clamp(int index);
const MapLevelInfo *map_level_get(int index);
void map_load_level(int index, GameMap *map);

/* Retorna o caractere de textura equivalente na posicao (x, y) ('$'->'#', '&'->'@', '/'->'%', ','->'#', ';'->'@', ':'->'%', '_'->'*'). Fora dos limites retorna '#'. */
char map_get_tile(const GameMap *map, int x, int y);

/* Retorna o caractere bruto armazenado na posicao (x, y). Fora dos limites retorna '#'. */
char map_get_raw_tile(const GameMap *map, int x, int y);

/* Retorna 1 se a celula (x, y) for parede solida ou estiver fora dos limites, 0 se chao livre ('.', ' ') ou parede atravessavel ('$', '&', '/'). */
int map_is_wall(const GameMap *map, int x, int y);

/* Retorna 1 se o caractere for uma parede atravessavel ('$', '&', '/'), 0 caso contrario. */
int map_is_passable_wall(char tile);

/* Retorna 1 se a celula (x, y) deve ser desenhada como parede (solida ou atravessavel), 0 se chao livre. */
int map_is_visible_wall(const GameMap *map, int x, int y);

/* Retorna o caractere de parede base equivalente ('$'->'#', '&'->'@', '/'->'%', ','->'#', ';'->'@', ':'->'%', '_'->'*'). */
char map_get_wall_equivalent(char tile);

/* Retorna 1 se o ultimo tile consultado por map_get_tile era uma parede atravessavel ('$', '&', '/'). */
int map_last_tile_is_passable(void);

/* Clareia levemente um canal de cor RGB [0..255] para diferenciar paredes atravessaveis. */
unsigned char map_lighten_channel(unsigned char c);
unsigned char map_lighten_channel_for_tile(char tile, unsigned char c);

/* Atualiza e retorna a quantidade de inimigos vivos (controla bloqueio/cor do tile 'E'). */
int map_set_enemies_alive(int count);

/* Retorna 1 se houver inimigos vivos bloqueando o tile 'E', 0 caso contrario. */
int map_are_enemies_alive(void);

/* Retorna 1 se o mapa atual possuir algum tile de saida 'E', 0 caso contrario. */
int map_has_exit_tile(const GameMap *map);

/* Verifica se o jogador entrou em um tile 'E' desbloqueado (sem inimigos vivos).
 * Retorna 1 se carregou a proxima fase (level2.txt, etc.), -1 se nao houver proxima fase (voltar ao menu), 0 se ainda nao ativou. */
int map_try_advance_level(GameMap *map, float *player_x, float *player_y, float *player_angle);

/* Retorna 1 se o caractere representar um ponto de spawn de inimigo no mapa. */
int map_is_enemy_spawn_char(char tile);

/* Armadilhas ('^', '5'-'8'), labaredas ('F', '9') e kits medicos ('+', '1'-'4') no chao. */
int map_is_hazard_tile(char tile);
int map_is_trap_tile(char tile);
int map_is_trap_triggered(int x, int y);
void map_trigger_trap(int x, int y);
void map_update_traps(float dt);
int map_is_fire_active_at(const GameMap *map, int x, int y);
void map_reset_traps(void);

int map_is_medkit_tile(char tile);
int map_is_medkit_active_at(const GameMap *map, int x, int y);
int map_try_collect_medkit(const GameMap *map, int x, int y);
void map_reset_medkits(void);

int map_is_fire_active_minimap(const GameMap *mm_map, int mx, int my);
int map_is_medkit_active_minimap(const GameMap *mm_map, int mx, int my);

/* Ativa/desativa o modo de spawn de inimigos (bloqueia spawn em tiles ','). */
void map_begin_enemy_spawn(void);
void map_end_enemy_spawn(void);

/* Monta um recorte 16x16 do mapa 32x32 para o minimapa, movendo a janela apenas quando o jogador chega a 3 blocos da borda. */
void map_build_minimap_view(const GameMap *src, GameMap *dst, float *player_x, float *player_y);

/* Converte coordenadas do mundo 32x32 para a janela 16x16 atual do minimapa. Retorna 1 se visivel na janela, 0 se fora. */
int map_to_minimap_coords(float *x, float *y);

/* Aplica a posicao de spawn ('P') carregada do mapa atual nas coordenadas do jogador. */
void map_apply_pending_spawn(float *player_x, float *player_y);

/* Hooks e renderizador 3D do modulo de mapas (registrados na biblioteca renderer). */
int map_floor_decal_hook(const void *map_ptr, int mx, int my,
                         float fu, float fv, float row_dist,
                         char *out_ch, AsciiColor *out_col);
void map_wall_style_hook(const void *map_ptr, int mx, int my, char tile,
                         AsciiColor *io_color, float *io_fog_dist,
                         int *io_is_edge, int *io_side);
int map_minimap_tile_hook(const void *mm_map_ptr, int mx, int my,
                          char *out_ch, AsciiColor *out_col);
void map_render_hazards(const GameMap *map, const void *player_ptr,
                        const RendererApi *r);

#endif /* MAP_H */
