#include "map.h"
#include <math.h>
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

static int g_enemies_alive = 0;
static int g_current_level = 1;

void map_init_default(GameMap *map) {
    if (!map) {
        return;
    }

    g_enemies_alive = 0;
    g_current_level = 1;
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
        char parent_path[256];
        snprintf(parent_path, sizeof(parent_path), "../%s", filename);
        fp = fopen(parent_path, "r");
        if (!fp) {
            return 0;
        }
    }

    g_enemies_alive = 0;
    const char *lvl_ptr = strstr(filename, "level");
    if (lvl_ptr) {
        int lvl = 0;
        if (sscanf(lvl_ptr + 5, "%d", &lvl) == 1 && lvl >= 1) {
            g_current_level = lvl;
        }
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
static int g_spawning_enemies = 0;
static int g_minimap_secret_floor = 0;
static int g_minimap_secret_x = -1;
static int g_minimap_secret_y = -1;

char map_get_wall_equivalent(char tile) {
    switch (tile) {
        case '$': return '#';
        case '&': return '@';
        case '/': return '%';
        case ',': return '#';
        default:  return tile;
    }
}

void map_begin_enemy_spawn(void) {
    g_spawning_enemies = 1;
}

void map_end_enemy_spawn(void) {
    g_spawning_enemies = 0;
}

int map_set_enemies_alive(int count) {
    g_enemies_alive = (count > 0) ? 1 : 0;
    return count;
}

int map_are_enemies_alive(void) {
    return g_enemies_alive;
}

int map_is_passable_wall(char tile) {
    if (tile == 'E') {
        return !g_enemies_alive;
    }
    return (tile == '$' || tile == '&' || tile == '/');
}

int map_last_tile_is_passable(void) {
    return g_last_tile_passable;
}

unsigned char map_lighten_channel(unsigned char c) {
    int v = (int)c + ((255 - (int)c) * 18) / 100;
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
    g_last_tile_passable = (raw == '$' || raw == '&' || raw == '/');
    if (raw == ',') {
        g_minimap_secret_floor = 1;
        g_minimap_secret_x = x;
        g_minimap_secret_y = y;
    } else {
        g_minimap_secret_floor = 0;
    }
    return map_get_wall_equivalent(raw);
}

int map_is_wall(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        return 1;
    }
    char tile = map_get_raw_tile(map, x, y);
    if (tile == ',') {
        return g_spawning_enemies ? 1 : 0;
    }
    return (tile != '.' && tile != ' ' && !map_is_passable_wall(tile));
}

int map_is_visible_wall(const GameMap *map, int x, int y) {
    char tile = map_get_raw_tile(map, x, y);
    g_last_tile_passable = 0;
    if (tile == ',') {
        int on_minimap = (g_minimap_secret_floor &&
                          x == g_minimap_secret_x &&
                          y == g_minimap_secret_y);
        g_minimap_secret_floor = 0;
        return on_minimap ? 1 : 0;
    }
    g_minimap_secret_floor = 0;
    return (tile != '.' && tile != ' ');
}

int map_has_exit_tile(const GameMap *map) {
    if (!map) {
        return 0;
    }
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            if (map->tiles[y][x] == 'E') {
                return 1;
            }
        }
    }
    return 0;
}

static int is_exit_at(const GameMap *map, float x, float y) {
    return (map_get_raw_tile(map, (int)floorf(x), (int)floorf(y)) == 'E');
}

int map_try_advance_level(GameMap *map, float *player_x, float *player_y, float *player_angle) {
    if (!map || !player_x || !player_y || !player_angle || g_enemies_alive) {
        return 0;
    }

    float px = *player_x;
    float py = *player_y;
    if (!is_exit_at(map, px, py) &&
        !is_exit_at(map, px + 0.15f, py) &&
        !is_exit_at(map, px - 0.15f, py) &&
        !is_exit_at(map, px, py + 0.15f) &&
        !is_exit_at(map, px, py - 0.15f)) {
        return 0;
    }

    char next_path[128];
    snprintf(next_path, sizeof(next_path), "assets/maps/level%d.txt", g_current_level + 1);
    if (map_load_from_file(map, next_path)) {
        *player_x = 2.5f;
        *player_y = 4.5f;
        *player_angle = 0.0f;
        return 1;
    }

    return -1;
}
