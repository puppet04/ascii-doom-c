#include "map.h"
#include <stdio.h>
#include <string.h>

static const char *DEFAULT_MAP_LAYOUT[MAP_HEIGHT] = {
    "################",
    "#..............#",
    "#..##......##..#",
    "#..#........#..#",
    "#..............#",
    "#......##......#",
    "#......##......#",
    "#..............#",
    "#..............#",
    "#..#........#..#",
    "#..##......##..#",
    "#..............#",
    "#....#....#....#",
    "#....#....#....#",
    "#..............#",
    "################"
};

void map_init_default(GameMap *map) {
    if (!map) {
        return;
    }

    map->width = MAP_WIDTH;
    map->height = MAP_HEIGHT;

    for (int y = 0; y < MAP_HEIGHT; y++) {
        strncpy(map->tiles[y], DEFAULT_MAP_LAYOUT[y], MAP_WIDTH);
        map->tiles[y][MAP_WIDTH] = '\0';
    }
}

int map_load_from_file(GameMap *map, const char *filename) {
    if (!map || !filename) {
        return 0;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return 0;
    }

    map->width = MAP_WIDTH;
    map->height = MAP_HEIGHT;

    char line[128];
    int row = 0;

    while (row < MAP_HEIGHT && fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len--;
        }

        /* Ignora linhas completamente vazias no meio/fim do arquivo */
        if (len == 0) {
            continue;
        }

        for (int col = 0; col < MAP_WIDTH; col++) {
            if ((size_t)col < len) {
                map->tiles[row][col] = line[col];
            } else {
                map->tiles[row][col] = '#';
            }
        }
        map->tiles[row][MAP_WIDTH] = '\0';
        row++;
    }

    fclose(fp);

    if (row == 0) {
        return 0;
    }

    /* Se o arquivo tiver menos linhas que MAP_HEIGHT, fecha com paredes */
    while (row < MAP_HEIGHT) {
        for (int col = 0; col < MAP_WIDTH; col++) {
            map->tiles[row][col] = '#';
        }
        map->tiles[row][MAP_WIDTH] = '\0';
        row++;
    }

    return 1;
}

static int g_last_tile_passable = 0;

char map_get_wall_equivalent(char tile) {
    switch (tile) {
        case '$': return '#';
        case '&': return '@';
        case '/': return '%';
        default:  return tile;
    }
}

int map_is_passable_wall(char tile) {
    return (tile == '$' || tile == '&' || tile == '/');
}

int map_last_tile_is_passable(void) {
    return g_last_tile_passable;
}

unsigned char map_lighten_channel(unsigned char c) {
    int v = (int)c + ((255 - (int)c) * 35) / 100;
    return (unsigned char)((v > 255) ? 255 : v);
}

char map_get_raw_tile(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        return '#';
    }
    return map->tiles[y][x];
}

char map_get_tile(const GameMap *map, int x, int y) {
    char raw = map_get_raw_tile(map, x, y);
    g_last_tile_passable = map_is_passable_wall(raw);
    return map_get_wall_equivalent(raw);
}

int map_is_wall(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        return 1;
    }
    char tile = map_get_raw_tile(map, x, y);
    return (tile != '.' && tile != ' ' && !map_is_passable_wall(tile));
}

int map_is_visible_wall(const GameMap *map, int x, int y) {
    char tile = map_get_raw_tile(map, x, y);
    if (tile == '.' || tile == ' ') {
        g_last_tile_passable = 0;
        return 0;
    }
    return 1;
}
