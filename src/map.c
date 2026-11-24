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
static float g_spawn_x = 2.5f;
static float g_spawn_y = 4.5f;
static int g_spawn_pending = 0;
static unsigned char g_trap_active[MAP_HEIGHT][MAP_WIDTH];
static float g_trap_timer[MAP_HEIGHT][MAP_WIDTH];
static unsigned char g_medkit_used[MAP_HEIGHT][MAP_WIDTH];

#define TRAP_ACTIVE_DURATION 3.0f

void map_reset_traps(void) {
    memset(g_trap_active, 0, sizeof(g_trap_active));
    memset(g_trap_timer, 0, sizeof(g_trap_timer));
}

void map_reset_medkits(void) {
    memset(g_medkit_used, 0, sizeof(g_medkit_used));
}

void map_init_default(GameMap *map) {
    if (!map) {
        return;
    }

    g_enemies_alive = 0;
    g_current_level = 1;
    g_minimap_cam_x = 0;
    g_minimap_cam_y = 0;
    g_spawn_x = 2.5f;
    g_spawn_y = 4.5f;
    g_spawn_pending = 1;
    map_reset_traps();
    map_reset_medkits();
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
    g_spawn_x = 2.5f;
    g_spawn_y = 4.5f;
    g_spawn_pending = 0;
    map_reset_traps();
    map_reset_medkits();
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
                if (line[col] == 'P') {
                    g_spawn_x = (float)col + 0.5f;
                    g_spawn_y = (float)row + 0.5f;
                    g_spawn_pending = 1;
                    map->tiles[row][col] = '.';
                } else {
                    map->tiles[row][col] = line[col];
                }
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
static MapLevelInfo MAP_LEVELS[] = {
    {
        "FASE 1 - COMPLEXO BASE",
        "Arena inicial com saida E ao sul",
        "assets/maps/level1.txt",
        NULL,
        2.5f, 4.5f, PI_F * 0.5f,
        13.5f, 11.5f, PI_F
    },
    {
        "FASE 2 - SETOR SECRETO",
        "Corredores com passagens ocultas e saida E",
        "assets/maps/level2.txt",
        NULL,
        2.5f, 4.5f, PI_F * 0.5f,
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
    MAP_LEVELS[clamped].spawn1_angle = PI_F * 0.5f;
    const MapLevelInfo *entry = &MAP_LEVELS[clamped];
    if (entry->custom_fn && entry->custom_fn(map)) {
        g_current_level = clamped + 1;
        return;
    }
    if (entry->file_path && map_load_from_file(map, entry->file_path)) {
        g_current_level = clamped + 1;
        if (g_spawn_pending) {
            MAP_LEVELS[clamped].spawn1_x = g_spawn_x;
            MAP_LEVELS[clamped].spawn1_y = g_spawn_y;
        }
        return;
    }
    map_init_default(map);
}

static int g_last_tile_passable = 0;
static int g_spawning_enemies = 0;
static int g_minimap_secret_floor = 0;
static int g_minimap_secret_x = -1;
static int g_minimap_secret_y = -1;

static int is_secret_floor(char tile) {
    return (tile == ',' || tile == ';' || tile == ':' || tile == '_' ||
            (tile >= '1' && tile <= '9'));
}

int map_is_enemy_spawn_char(char tile) {
    switch (tile) {
        case 'Z':
        case 'D':
        case 'I':
        case 'M':
        case 'Y':
        case 'C':
        case 'B':
        case 'K':
        case 'T':
        case 'A':
        case 'Q':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
            return 1;
        default:
            return 0;
    }
}

int map_is_trap_tile(char tile) {
    return (tile == '^' || tile == '5' || tile == '6' || tile == '7' || tile == '8');
}

int map_is_medkit_tile(char tile) {
    return (tile == '+' || tile == '1' || tile == '2' || tile == '3' || tile == '4');
}

int map_is_hazard_tile(char tile) {
    return (map_is_trap_tile(tile) || tile == 'F' || tile == '9');
}

int map_is_trap_triggered(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 0;
    }
    return g_trap_active[y][x] != 0;
}

void map_trigger_trap(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return;
    }
    g_trap_active[y][x] = 1;
    g_trap_timer[y][x] = TRAP_ACTIVE_DURATION;
}

void map_update_traps(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (g_trap_active[y][x]) {
                g_trap_timer[y][x] -= dt;
                if (g_trap_timer[y][x] <= 0.0f) {
                    g_trap_active[y][x] = 0;
                    g_trap_timer[y][x] = 0.0f;
                }
            }
        }
    }
}

int map_is_fire_active_at(const GameMap *map, int x, int y) {
    char tile = map_get_raw_tile(map, x, y);
    if (tile == 'F' || tile == '9') {
        return 1;
    }
    if (map_is_trap_tile(tile)) {
        return map_is_trap_triggered(x, y);
    }
    return 0;
}

int map_is_medkit_active_at(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 0;
    }
    char tile = map_get_raw_tile(map, x, y);
    return map_is_medkit_tile(tile) && g_medkit_used[y][x] == 0;
}

int map_try_collect_medkit(const GameMap *map, int x, int y) {
    if (!map_is_medkit_active_at(map, x, y)) {
        return 0;
    }
    g_medkit_used[y][x] = 1;
    return 1;
}

char map_get_wall_equivalent(char tile) {
    if (tile == 'P' || map_is_enemy_spawn_char(tile) ||
        tile == '^' || tile == 'F' || tile == '+') {
        return '.';
    }
    switch (tile) {
        case '$': case ',': case '1': case '5': case '9': return '#';
        case '&': case ';': case '2': case '6':           return '@';
        case '/': case ':': case '3': case '7':           return '%';
        case '_': case '4': case '8':                     return '*';
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

static int passable_lighten_pct(char tile) {
    switch (tile) {
        case '#': return 9;
        case '@': return 25;
        case '%': return 25;
        default:  return 18;
    }
}

unsigned char map_lighten_channel_for_tile(char tile, unsigned char c) {
    int pct = passable_lighten_pct(tile);
    int v = (int)c + ((255 - (int)c) * pct) / 100;
    return (unsigned char)((v > 255) ? 255 : v);
}

unsigned char map_lighten_channel(unsigned char c) {
    return map_lighten_channel_for_tile('\0', c);
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
    if (is_secret_floor(raw)) {
        g_minimap_secret_floor = 1;
        g_minimap_secret_x = x;
        g_minimap_secret_y = y;
    } else {
        g_minimap_secret_floor = 0;
    }
    return map_get_wall_equivalent(raw);
}

void map_apply_pending_spawn(float *player_x, float *player_y) {
    if (!player_x || !player_y || !g_spawn_pending) {
        return;
    }
    *player_x = g_spawn_x;
    *player_y = g_spawn_y;
    g_spawn_pending = 0;
}

int map_is_wall(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        return 1;
    }
    char tile = map_get_raw_tile(map, x, y);
    if (is_secret_floor(tile)) {
        return g_spawning_enemies ? 1 : 0;
    }
    if (tile == 'P' || map_is_enemy_spawn_char(tile) ||
        map_is_hazard_tile(tile) || map_is_medkit_tile(tile)) {
        return 0;
    }
    return (tile != '.' && tile != ' ' && !map_is_passable_wall(tile));
}

int map_is_visible_wall(const GameMap *map, int x, int y) {
    char tile = map_get_raw_tile(map, x, y);
    g_last_tile_passable = 0;
    if (is_secret_floor(tile)) {
        int on_minimap = (g_minimap_secret_floor &&
                          x == g_minimap_secret_x &&
                          y == g_minimap_secret_y);
        g_minimap_secret_floor = 0;
        return on_minimap ? 1 : 0;
    }
    g_minimap_secret_floor = 0;
    return (tile != '.' && tile != ' ' && tile != 'P' &&
            !map_is_enemy_spawn_char(tile) &&
            !map_is_hazard_tile(tile) &&
            !map_is_medkit_tile(tile));
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
        *player_angle = PI_F * 0.5f;
        map_apply_pending_spawn(player_x, player_y);
        return 1;
    }

    char next_path[128];
    snprintf(next_path, sizeof(next_path), "assets/maps/level%d.txt", g_current_level + 1);
    if (map_load_from_file(map, next_path)) {
        *player_x = 2.5f;
        *player_y = 4.5f;
        *player_angle = PI_F * 0.5f;
        map_apply_pending_spawn(player_x, player_y);
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

int map_is_fire_active_minimap(const GameMap *mm_map, int mx, int my) {
    char tile = map_get_raw_tile(mm_map, mx, my);
    if (tile == 'F') {
        return 1;
    }
    if (tile == '^') {
        return map_is_trap_triggered(g_minimap_cam_x + mx, g_minimap_cam_y + my);
    }
    return 0;
}

int map_is_medkit_active_minimap(const GameMap *mm_map, int mx, int my) {
    char tile = map_get_raw_tile(mm_map, mx, my);
    if (tile != '+') {
        return 0;
    }
    int wx = g_minimap_cam_x + mx;
    int wy = g_minimap_cam_y + my;
    if (wx < 0 || wx >= MAP_WIDTH || wy < 0 || wy >= MAP_HEIGHT) {
        return 0;
    }
    return g_medkit_used[wy][wx] == 0;
}
