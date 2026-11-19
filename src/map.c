#include "map.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *DEFAULT_MAP_LAYOUT[MAP_HEIGHT] = {
    "################################",
    "#..............................#",
    "#..##......##......##......##..#",
    "#..#........#......#........#..#",
    "#..............................#",
    "#......##..............##......#",
    "#......##..............##......#",
    "#..............................#",
    "#..............................#",
    "#..#........#......#........#..#",
    "#..##......##......##......##..#",
    "#..............................#",
    "#....#....#..........#....#....#",
    "#....#....#..........#....#....#",
    "#..............##..............#",
    "#..............##..............#",
    "#..............................#",
    "#..............................#",
    "#....#....#..........#....#....#",
    "#....#....#..........#....#....#",
    "#..............................#",
    "#..##......##......##......##..#",
    "#..#........#......#........#..#",
    "#..............................#",
    "#..............................#",
    "#......##..............##......#",
    "#......##..............##......#",
    "#..............................#",
    "#..#........#......#........#..#",
    "#..##......##......##......##..#",
    "#..............................#",
    "################################"
};

static int g_enemies_alive = 0;
static int g_current_level = 1;
static int g_minimap_cam_x = 0;
static int g_minimap_cam_y = 0;

void map_init_default(GameMap *map) {
    if (!map) {
        return;
    }

    g_enemies_alive = 0;
    g_current_level = 1;
    g_minimap_cam_x = 0;
    g_minimap_cam_y = 0;
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
    g_minimap_cam_x = 0;
    g_minimap_cam_y = 0;
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

/*
 * CATALOGO OFICIAL DE NIVEIS (Modulo Mapas - Lucas)
 * Para adicionar novas fases sem mexer em main.c:
 *   1) Via .txt:    { "FASE 3 - ...", "Descricao", "assets/maps/level3.txt", NULL, 2.5f, 4.5f, 0.0f, 13.5f, 11.5f, PI_F },
 *   2) Via funcao C: { "FASE 3 - ...", "Descricao", NULL, map_load_level3, 2.5f, 4.5f, 0.0f, 13.5f, 11.5f, PI_F },
 */
static const MapLevelInfo MAP_LEVELS[] = {
    {
        "FASE 1 - COMPLEXO BASE",
        "Arena inicial com saida E ao sul",
        "assets/maps/level1.txt",
        NULL,
        2.5f, 4.5f, 0.0f,
        13.5f, 11.5f, PI_F
    },
    {
        "FASE 2 - SETOR SECRETO",
        "Corredores com passagens ocultas e saida E",
        "assets/maps/level2.txt",
        NULL,
        2.5f, 4.5f, 0.0f,
        13.5f, 10.5f, PI_F
    }
};
static const int MAP_LEVEL_TOTAL = (int)(sizeof(MAP_LEVELS) / sizeof(MAP_LEVELS[0]));

int map_level_count(void) {
    return MAP_LEVEL_TOTAL;
}

int map_level_clamp(int index) {
    if (MAP_LEVEL_TOTAL <= 0) return 0;
    if (index < 0) return 0;
    if (index >= MAP_LEVEL_TOTAL) return MAP_LEVEL_TOTAL - 1;
    return index;
}

const MapLevelInfo *map_level_get(int index) {
    return &MAP_LEVELS[map_level_clamp(index)];
}

void map_load_level(int index, GameMap *map) {
    if (!map) {
        return;
    }
    int clamped = map_level_clamp(index);
    const MapLevelInfo *entry = &MAP_LEVELS[clamped];
    if (entry->custom_fn && entry->custom_fn(map)) {
        g_current_level = clamped + 1;
        return;
    }
    if (entry->file_path && map_load_from_file(map, entry->file_path)) {
        g_current_level = clamped + 1;
        return;
    }
    map_init_default(map);
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

    if (g_current_level >= 1 && g_current_level < MAP_LEVEL_TOTAL) {
        const MapLevelInfo *next_lvl = &MAP_LEVELS[g_current_level];
        map_load_level(g_current_level, map);
        *player_x = next_lvl->spawn1_x;
        *player_y = next_lvl->spawn1_y;
        *player_angle = next_lvl->spawn1_angle;
        return 1;
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

static int clamp_minimap_cam(int val, int map_dim) {
    int max_cam = map_dim - MINIMAP_VIEW_SIZE;
    if (max_cam < 0) {
        max_cam = 0;
    }
    if (val < 0) {
        return 0;
    }
    if (val > max_cam) {
        return max_cam;
    }
    return val;
}

void map_build_minimap_view(const GameMap *src, GameMap *dst, float *player_x, float *player_y) {
    if (!src || !dst || !player_x || !player_y) {
        return;
    }

    int px = (int)floorf(*player_x);
    int py = (int)floorf(*player_y);

    int min_x = g_minimap_cam_x + MINIMAP_SCROLL_MARGIN;
    int max_x = g_minimap_cam_x + MINIMAP_VIEW_SIZE - 1 - MINIMAP_SCROLL_MARGIN;
    if (px < min_x) {
        g_minimap_cam_x = px - MINIMAP_SCROLL_MARGIN;
    } else if (px > max_x) {
        g_minimap_cam_x = px - (MINIMAP_VIEW_SIZE - 1 - MINIMAP_SCROLL_MARGIN);
    }
    g_minimap_cam_x = clamp_minimap_cam(g_minimap_cam_x, src->width);

    int min_y = g_minimap_cam_y + MINIMAP_SCROLL_MARGIN;
    int max_y = g_minimap_cam_y + MINIMAP_VIEW_SIZE - 1 - MINIMAP_SCROLL_MARGIN;
    if (py < min_y) {
        g_minimap_cam_y = py - MINIMAP_SCROLL_MARGIN;
    } else if (py > max_y) {
        g_minimap_cam_y = py - (MINIMAP_VIEW_SIZE - 1 - MINIMAP_SCROLL_MARGIN);
    }
    g_minimap_cam_y = clamp_minimap_cam(g_minimap_cam_y, src->height);

    int view_w = (src->width < MINIMAP_VIEW_SIZE) ? src->width : MINIMAP_VIEW_SIZE;
    int view_h = (src->height < MINIMAP_VIEW_SIZE) ? src->height : MINIMAP_VIEW_SIZE;
    dst->width = view_w;
    dst->height = view_h;

    for (int y = 0; y < view_h; y++) {
        for (int x = 0; x < view_w; x++) {
            dst->tiles[y][x] = src->tiles[g_minimap_cam_y + y][g_minimap_cam_x + x];
        }
        dst->tiles[y][view_w] = '\0';
    }

    *player_x -= (float)g_minimap_cam_x;
    *player_y -= (float)g_minimap_cam_y;
}

int map_to_minimap_coords(float *x, float *y) {
    if (!x || !y) {
        return 0;
    }
    *x -= (float)g_minimap_cam_x;
    *y -= (float)g_minimap_cam_y;
    return (*x >= 0.0f && *x < (float)MINIMAP_VIEW_SIZE &&
            *y >= 0.0f && *y < (float)MINIMAP_VIEW_SIZE) ? 1 : 0;
}
