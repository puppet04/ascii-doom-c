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

/* Retorna o caractere na posicao (x, y). Fora dos limites retorna '#'. */
char map_get_tile(const GameMap *map, int x, int y);

/* Retorna 1 se a celula (x, y) for parede ou estiver fora dos limites, 0 se livre. */
int map_is_wall(const GameMap *map, int x, int y);

#endif /* MAP_H */
