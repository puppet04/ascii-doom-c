#ifndef MAP_H
#define MAP_H

#include "config.h"

/*
 * MODULO 1: MAPA, ARQUIVOS E COLISAO
 * Responsavel por armazenar a matriz 2D da fase, carregar mapas de arquivos .txt
 * e fornecer consultas rapidas de colisao para a fisica e o raycasting.
 */

typedef struct {
    int width;
    int height;
    char tiles[MAP_HEIGHT][MAP_WIDTH + 1];
} GameMap;

/* Carrega um mapa padrao embutido no codigo (fallback caso arquivo falhe). */
void map_init_default(GameMap *map);

/* Carrega o mapa a partir de um arquivo texto. Retorna 1 em sucesso, 0 em erro. */
int map_load_from_file(GameMap *map, const char *filename);

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

#ifdef RENDERER_H
#define map_is_wall(map, x, y) map_is_visible_wall((map), (x), (y))
#define MAP_GTBC_char get_tile_base_color(char
#define MAP_GTBC_tile (map_last_tile_is_passable() \
    ? make_color(map_lighten_channel(get_tile_base_color(tile).r), \
                 map_lighten_channel(get_tile_base_color(tile).g), \
                 map_lighten_channel(get_tile_base_color(tile).b)) \
    : get_tile_base_color(tile)
#define get_tile_base_color(x) MAP_GTBC_##x)
#endif

#endif /* MAP_H */
