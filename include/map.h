#ifndef MAP_H
#define MAP_H

#include "config.h"

#undef MAP_WIDTH
#undef MAP_HEIGHT
#undef MAX_DEPTH
#define MAP_WIDTH  32
#define MAP_HEIGHT 32
#define MAX_DEPTH  36.0f
#define MINIMAP_VIEW_SIZE     16
#define MINIMAP_SCROLL_MARGIN 3

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

/* Retorna o caractere de textura equivalente na posicao (x, y) ('$'->'#', '&'->'@', '/'->'%'). Fora dos limites retorna '#'. */
char map_get_tile(const GameMap *map, int x, int y);

/* Retorna o caractere bruto armazenado na posicao (x, y). Fora dos limites retorna '#'. */
char map_get_raw_tile(const GameMap *map, int x, int y);

/* Retorna 1 se a celula (x, y) for parede solida ou estiver fora dos limites, 0 se chao livre ('.', ' ') ou parede atravessavel ('$', '&', '/'). */
int map_is_wall(const GameMap *map, int x, int y);

/* Retorna 1 se o caractere for uma parede atravessavel ('$', '&', '/'), 0 caso contrario. */
int map_is_passable_wall(char tile);

/* Retorna 1 se a celula (x, y) deve ser desenhada como parede (solida ou atravessavel), 0 se chao livre. */
int map_is_visible_wall(const GameMap *map, int x, int y);

/* Retorna o caractere de parede base equivalente ('$'->'#', '&'->'@', '/'->'%'). */
char map_get_wall_equivalent(char tile);

/* Retorna 1 se o ultimo tile consultado por map_get_tile era uma parede atravessavel ('$', '&', '/'). */
int map_last_tile_is_passable(void);

/* Clareia levemente um canal de cor RGB [0..255] para diferenciar paredes atravessaveis. */
unsigned char map_lighten_channel(unsigned char c);

/* Atualiza e retorna a quantidade de inimigos vivos (controla bloqueio/cor do tile 'E'). */
int map_set_enemies_alive(int count);

/* Retorna 1 se houver inimigos vivos bloqueando o tile 'E', 0 caso contrario. */
int map_are_enemies_alive(void);

/* Retorna 1 se o mapa atual possuir algum tile de saida 'E', 0 caso contrario. */
int map_has_exit_tile(const GameMap *map);

/* Verifica se o jogador entrou em um tile 'E' desbloqueado (sem inimigos vivos).
 * Retorna 1 se carregou a proxima fase (level2.txt, etc.), -1 se nao houver proxima fase (voltar ao menu), 0 se ainda nao ativou. */
int map_try_advance_level(GameMap *map, float *player_x, float *player_y, float *player_angle);

/* Ativa/desativa o modo de spawn de inimigos (bloqueia spawn em tiles ','). */
void map_begin_enemy_spawn(void);
void map_end_enemy_spawn(void);

/* Monta um recorte 16x16 do mapa 32x32 para o minimapa, movendo a janela apenas quando o jogador chega a 3 blocos da borda. */
void map_build_minimap_view(const GameMap *src, GameMap *dst, float *player_x, float *player_y);

/* Converte coordenadas do mundo 32x32 para a janela 16x16 atual do minimapa. Retorna 1 se visivel na janela, 0 se fora. */
int map_to_minimap_coords(float *x, float *y);

#ifdef RENDERER_H
static float perp_dist __attribute__((unused));
static int is_block_edge __attribute__((unused));
static int side __attribute__((unused));
#define map_is_wall(map, x, y) map_is_visible_wall((map), (x), (y))
#define MAP_GTBC_char get_tile_base_color(char
#define MAP_GTBC_tile (((tile) == 'E' && !map_are_enemies_alive()) \
    ? (perp_dist = -50.0f, is_block_edge = 0, side = 0, make_color(255, 255, 255)) \
    : (map_last_tile_is_passable() \
        ? make_color(map_lighten_channel(get_tile_base_color(tile).r), \
                     map_lighten_channel(get_tile_base_color(tile).g), \
                     map_lighten_channel(get_tile_base_color(tile).b)) \
        : get_tile_base_color(tile))
#define get_tile_base_color(x) MAP_GTBC_##x)
#endif

#endif /* MAP_H */

#ifdef MAX_ENEMIES
#ifndef MAP_ENTITIES_HOOKED
#define MAP_ENTITIES_HOOKED
#define enemies_alive(mgr) map_set_enemies_alive(enemies_alive(mgr))
#define enemies_spawn_wave(mgr, map, player) \
    (map_set_enemies_alive(1), map_begin_enemy_spawn(), enemies_spawn_wave((mgr), (map), (player)), map_end_enemy_spawn())
#define enemies_update(mgr, map_ptr, player_ptr, dt) \
    do { \
        enemies_update((mgr), (map_ptr), (player_ptr), (dt)); \
        if (map_has_exit_tile(map_ptr) && !map_are_enemies_alive()) { \
            wave_timer = 3.0f; \
        } \
        int _nav = map_try_advance_level((map_ptr), &(player_ptr)->x, &(player_ptr)->y, &(player_ptr)->angle); \
        if (_nav > 0) { \
            enemies_spawn_wave((mgr), (map_ptr), (player_ptr)); \
            wave_timer = 3.0f; \
        } else if (_nav < 0) { \
            running = 0; \
        } \
    } while (0)
#endif
#endif

#ifdef RENDERER_H
#ifndef MAP_MINIMAP_HOOKED
#define MAP_MINIMAP_HOOKED
#define MAP_RDM_CALL(map_arg, player_arg, sprites_arg, count_arg) \
    do { \
        GameMap _mm_map; \
        Player _mm_player = *(player_arg); \
        SpriteEntity _mm_sprites[MAX_SPRITES]; \
        int _mm_cnt = (count_arg); \
        if (_mm_cnt > MAX_SPRITES) _mm_cnt = MAX_SPRITES; \
        map_build_minimap_view((map_arg), &_mm_map, &_mm_player.x, &_mm_player.y); \
        for (int _i = 0; _i < _mm_cnt; _i++) { \
            _mm_sprites[_i] = (sprites_arg)[_i]; \
            if (!map_to_minimap_coords(&_mm_sprites[_i].x, &_mm_sprites[_i].y)) { \
                _mm_sprites[_i].active = 0; \
            } \
        } \
        renderer_draw_minimap(&_mm_map, &_mm_player, _mm_sprites, _mm_cnt); \
    } while (0
#define MAP_RDM_int \
    renderer_draw_minimap(const GameMap *map, const Player *player, const SpriteEntity *sprites, int
#define MAP_RDM_1 \
    MAP_RDM_CALL(&map, &player, &demo_sprite, 1)
#define MAP_RDM_sprite_count \
    MAP_RDM_CALL(&map, &player, sprites, sprite_count)
#define renderer_draw_minimap(a, b, c, d) MAP_RDM_##d)
#endif
#endif
