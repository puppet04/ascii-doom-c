#include "map.h"
#include "embedded_assets.h"
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
static unsigned char g_weapon_pickup_used[MAP_HEIGHT][MAP_WIDTH];

#define TRAP_ACTIVE_DURATION 3.0f

void map_reset_traps(void) {
    memset(g_trap_active, 0, sizeof(g_trap_active));
    memset(g_trap_timer, 0, sizeof(g_trap_timer));
}

void map_reset_medkits(void) {
    memset(g_medkit_used, 0, sizeof(g_medkit_used));
}

void map_reset_weapon_pickups(void) {
    memset(g_weapon_pickup_used, 0, sizeof(g_weapon_pickup_used));
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
    map_reset_weapon_pickups();
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
    }
    const char *emb = (!fp) ? embedded_map_lookup(filename) : NULL;
    if (!fp && !emb) {
        return 0;
    }

    g_enemies_alive = 0;
    g_minimap_cam_x = 0;
    g_minimap_cam_y = 0;
    g_spawn_x = 2.5f;
    g_spawn_y = 4.5f;
    g_spawn_pending = 0;
    map_reset_traps();
    map_reset_medkits();
    map_reset_weapon_pickups();
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

    while (row < MAP_HEIGHT) {
        if (fp) {
            if (fgets(line, sizeof(line), fp) == NULL) {
                break;
            }
        } else {
            if (!emb || *emb == '\0') {
                break;
            }
            const char *nl = strchr(emb, '\n');
            size_t seg = nl ? (size_t)(nl - emb) : strlen(emb);
            size_t copy_n = (seg < sizeof(line) - 1) ? seg : (sizeof(line) - 1);
            memcpy(line, emb, copy_n);
            line[copy_n] = '\0';
            emb = nl ? (nl + 1) : (emb + seg);
        }

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

    if (fp) {
        fclose(fp);
    }

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
    },
    {
        "FASE 3 - CORREDORES DE PLASMA",
        "Armadilhas de fogo, kits medicos e patrulhas",
        "assets/maps/level3.txt",
        NULL,
        2.5f, 4.5f, PI_F * 0.5f,
        13.5f, 10.5f, PI_F
    },
    {
        "FASE 4 - FORNALHA SUBTERRANEA",
        "Labirinto inflamavel com passagens secretas",
        "assets/maps/level4.txt",
        NULL,
        2.5f, 4.5f, PI_F * 0.5f,
        13.5f, 10.5f, PI_F
    },
    {
        "FASE 5 - CITADELA FINAL",
        "Desafio completo com chefes e corredores ocultos",
        "assets/maps/level5.txt",
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

int map_weapon_tile_type(char tile) {
    switch (tile) {
        case 'G': return 1; /* WEAPON_SHOTGUN */
        case 'S': return 2; /* WEAPON_SNIPER */
        case 'R': return 3; /* WEAPON_MACHINEGUN */
        case 'O': return 4; /* WEAPON_FUTURISTIC */
        default:  return -1;
    }
}

int map_is_weapon_pickup_active_at(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 0;
    }
    char tile = map_get_raw_tile(map, x, y);
    return map_weapon_tile_type(tile) >= 0 && g_weapon_pickup_used[y][x] == 0;
}

int map_try_collect_weapon_pickup(const GameMap *map, int x, int y, int *out_weapon_type) {
    if (!map_is_weapon_pickup_active_at(map, x, y)) {
        return 0;
    }
    int wtype = map_weapon_tile_type(map_get_raw_tile(map, x, y));
    g_weapon_pickup_used[y][x] = 1;
    if (out_weapon_type) {
        *out_weapon_type = wtype;
    }
    return 1;
}

char map_get_wall_equivalent(char tile) {
    if (tile == 'P' || map_is_enemy_spawn_char(tile) ||
        tile == '^' || tile == 'F' || tile == '+' ||
        map_weapon_tile_type(tile) >= 0) {
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
        map_is_hazard_tile(tile) || map_is_medkit_tile(tile) ||
        map_weapon_tile_type(tile) >= 0) {
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
            !map_is_medkit_tile(tile) &&
            map_weapon_tile_type(tile) < 0);
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


/* ============================================================================
 * HOOKS E RENDERIZACAO DE CHAO, PAREDES, KITS MEDICOS E CHAMAS (MODULO MAPA)
 * ============================================================================ */

static AsciiColor map_scale_rgb_local(float r, float g, float b, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.35f) factor = 1.35f;
    float rv = r * factor;
    float gv = g * factor;
    float bv = b * factor;
    if (rv < 0.0f) rv = 0.0f; else if (rv > 255.0f) rv = 255.0f;
    if (gv < 0.0f) gv = 0.0f; else if (gv > 255.0f) gv = 255.0f;
    if (bv < 0.0f) bv = 0.0f; else if (bv > 255.0f) bv = 255.0f;
    AsciiColor c = {(unsigned char)rv, (unsigned char)gv, (unsigned char)bv};
    return c;
}

void map_wall_style_hook(const void *map_ptr, int mx, int my, char tile,
                         AsciiColor *io_color, float *io_fog_dist,
                         int *io_is_edge, int *io_side) {
    const GameMap *map = (const GameMap *)map_ptr;
    if (!map || !io_color || !io_fog_dist || !io_is_edge || !io_side) {
        return;
    }
    if (tile == 'E' && !map_are_enemies_alive()) {
        *io_color = (AsciiColor){255, 255, 255};
        *io_fog_dist = -50.0f;
        *io_is_edge = 0;
        *io_side = 0;
        return;
    }
    char raw = map_get_raw_tile(map, mx, my);
    if (map_is_passable_wall(raw)) {
        io_color->r = map_lighten_channel_for_tile(tile, io_color->r);
        io_color->g = map_lighten_channel_for_tile(tile, io_color->g);
        io_color->b = map_lighten_channel_for_tile(tile, io_color->b);
    }
}

int map_floor_decal_hook(const void *map_ptr, int mx, int my,
                         float fu, float fv, float row_dist,
                         char *out_ch, AsciiColor *out_col) {
    const GameMap *map = (const GameMap *)map_ptr;
    if (!map || !out_ch || !out_col) {
        return 0;
    }

    char floor_raw = map_get_raw_tile(map, mx, my);
    int trap_unlit = map_is_trap_tile(floor_raw) && !map_is_trap_triggered(mx, my);
    int fire_lit = map_is_fire_active_at(map, mx, my);
    if (trap_unlit || fire_lit) {
        float dx_m = fu - 0.5f;
        float dy_m = fv - 0.5f;
        float rad = sqrtf(dx_m * dx_m + dy_m * dy_m);
        float adx = fabsf(dx_m);
        float ady = fabsf(dy_m);
        int outer_ring = (rad >= 0.29f && rad <= 0.37f);
        int inner_ring = (rad >= 0.14f && rad <= 0.21f);
        int core_jet = (rad < 0.09f);
        int vane = (rad >= 0.09f && rad <= 0.37f) &&
                   (adx < 0.035f || ady < 0.035f || fabsf(adx - ady) < 0.04f);
        if (outer_ring || inner_ring || core_jet || vane) {
            float tfog = fmaxf(0.28f, 1.0f - (row_dist / 13.5f));
            *out_ch = core_jet ? (fire_lit ? '@' : 'o')
                    : (inner_ring ? (fire_lit ? '*' : 'O')
                    : (vane ? '+' : '#'));
            if (fire_lit) {
                *out_col = core_jet ? map_scale_rgb_local(255.0f, 245.0f, 160.0f, tfog)
                         : (inner_ring ? map_scale_rgb_local(255.0f, 125.0f, 25.0f, tfog)
                         : (vane ? map_scale_rgb_local(245.0f, 80.0f, 20.0f, tfog)
                                 : map_scale_rgb_local(175.0f, 55.0f, 25.0f, tfog)));
            } else {
                *out_col = core_jet ? map_scale_rgb_local(225.0f, 105.0f, 35.0f, tfog)
                         : (inner_ring ? map_scale_rgb_local(170.0f, 85.0f, 40.0f, tfog)
                         : (vane ? map_scale_rgb_local(135.0f, 145.0f, 155.0f, tfog)
                                 : map_scale_rgb_local(95.0f, 105.0f, 115.0f, tfog)));
            }
            return 1;
        }
    } else if (map_is_medkit_tile(floor_raw)) {
        float dx_m = fabsf(fu - 0.5f);
        float dy_m = fabsf(fv - 0.5f);
        if (dx_m < 0.28f && dy_m < 0.28f) {
            float mf = fmaxf(0.32f, 1.0f - (row_dist / 13.5f));
            int active_kit = map_is_medkit_active_at(map, mx, my);
            int is_cross = (dx_m < 0.07f && dy_m < 0.20f) || (dy_m < 0.07f && dx_m < 0.20f);
            if (active_kit) {
                *out_ch = is_cross ? '+' : '#';
                *out_col = is_cross ? map_scale_rgb_local(235.0f, 55.0f, 55.0f, mf)
                                    : map_scale_rgb_local(240.0f, 245.0f, 250.0f, mf);
            } else {
                int on_edge = (dx_m > 0.21f || dy_m > 0.21f);
                *out_ch = is_cross ? '+' : (on_edge ? '#' : '=');
                *out_col = is_cross ? map_scale_rgb_local(105.0f, 55.0f, 55.0f, mf)
                                    : map_scale_rgb_local(95.0f, 105.0f, 118.0f, mf);
            }
            return 1;
        }
    }
    return 0;
}

int map_minimap_tile_hook(const void *mm_map_ptr, int mx, int my,
                          char *out_ch, AsciiColor *out_col) {
    const GameMap *mm_map = (const GameMap *)mm_map_ptr;
    if (!mm_map || !out_ch || !out_col) {
        return 0;
    }
    if (map_is_fire_active_minimap(mm_map, mx, my)) {
        *out_ch = '^';
        *out_col = (AsciiColor){255, 160, 20};
        return 1;
    }
    if (map_is_medkit_active_minimap(mm_map, mx, my)) {
        *out_ch = '+';
        *out_col = (AsciiColor){185, 255, 205};
        return 1;
    }
    if (map_is_weapon_pickup_active_at(mm_map, mx, my)) {
        char raw = map_get_raw_tile(mm_map, mx, my);
        *out_ch = raw;
        *out_col = (AsciiColor){115, 235, 255};
        return 1;
    }
    return 0;
}

typedef struct {
    float seed;
    int weapon_type;
    const RendererApi *api;
} HazardRenderCtx;

int map_sample_weapon_pickup_sprite(int weapon_type, float u, float v, int sx, int sy,
                                    float fog, float time_now, const RendererApi *r,
                                    char *out_ch, AsciiColor *out_col) {
    if (!r || !out_ch || !out_col) return 0;

    /* Coordenadas normalizadas [-1, 1] com leve flutuacao vertical */
    float bob = 0.06f * sinf(time_now * 4.5f);
    float nx = u * 2.0f - 1.0f;
    float ny = v * 2.0f - 1.0f + bob;
    float pulse = 0.88f + 0.12f * sinf(time_now * 6.5f);

    if (weapon_type == 1) {
        /* SHOTGUN: cano duplo largo + guarda-mao de madeira + coronha curva */
        int top_barrel = (ny > -0.34f && ny < -0.14f && nx > -0.32f && nx < 0.94f);
        int bot_barrel = (ny >= -0.14f && ny < 0.04f && nx > -0.24f && nx < 0.88f);
        int pump       = (ny >= 0.00f && ny < 0.22f && nx > 0.02f && nx < 0.56f);
        int receiver   = (ny > -0.30f && ny < 0.22f && nx > -0.42f && nx <= 0.04f);
        int stock      = (nx >= -0.94f && nx <= -0.36f &&
                          ny > (-0.12f + (-0.36f - nx) * 0.45f) &&
                          ny < (0.28f + (-0.36f - nx) * 0.65f));
        if (!top_barrel && !bot_barrel && !pump && !receiver && !stock) return 0;

        if (pump || stock) {
            *out_ch = '#';
            *out_col = r->scale_rgb(175.0f, 102.0f, 52.0f, fog);
        } else if (top_barrel) {
            *out_ch = (nx > 0.82f) ? ']' : '=';
            *out_col = r->scale_rgb(225.0f, 232.0f, 240.0f, fog * pulse);
        } else if (bot_barrel) {
            *out_ch = '=';
            *out_col = r->scale_rgb(145.0f, 155.0f, 170.0f, fog);
        } else {
            *out_ch = r->sample_ramp(0.65f * fog, sx, sy);
            *out_col = r->scale_rgb(235.0f, 150.0f, 65.0f, fog * pulse);
        }
        return 1;
    }

    if (weapon_type == 2) {
        /* SNIPER: cano longo e fino + luneta alta com lente verde + bipe */
        int barrel = (ny > -0.12f && ny < 0.04f && nx > -0.32f && nx < 0.98f);
        int muzzle = (ny > -0.18f && ny < 0.10f && nx >= 0.86f && nx < 0.98f);
        int scope  = (ny > -0.48f && ny < -0.22f && nx > -0.30f && nx < 0.28f);
        int mount  = (ny >= -0.24f && ny <= -0.10f &&
                      ((nx > -0.18f && nx < -0.06f) || (nx > 0.08f && nx < 0.20f)));
        int body   = (ny >= 0.02f && ny < 0.26f && nx > -0.48f && nx < 0.36f);
        int stock  = (ny > -0.04f && ny < 0.34f && nx >= -0.96f && nx <= -0.46f);
        int mag    = (ny >= 0.24f && ny < 0.56f && nx > -0.12f && nx < 0.12f);
        int bipod  = (ny >= 0.04f && ny < 0.52f && fabsf((nx - 0.52f) - (ny - 0.04f) * 0.40f) < 0.06f);
        if (!barrel && !muzzle && !scope && !mount && !body && !stock && !mag && !bipod) return 0;

        if (scope) {
            int lens = (nx > 0.16f || nx < -0.20f);
            *out_ch = lens ? '@' : '=';
            *out_col = lens ? r->scale_rgb(120.0f, 255.0f, 130.0f, fog * pulse)
                            : r->scale_rgb(80.0f, 92.0f, 102.0f, fog);
        } else if (muzzle || barrel) {
            *out_ch = muzzle ? '#' : '-';
            *out_col = r->scale_rgb(215.0f, 230.0f, 210.0f, fog * pulse);
        } else if (bipod || mag || mount) {
            *out_ch = '|';
            *out_col = r->scale_rgb(105.0f, 118.0f, 108.0f, fog);
        } else {
            *out_ch = r->sample_ramp(0.72f * fog, sx, sy);
            *out_col = r->scale_rgb(165.0f, 185.0f, 130.0f, fog);
        }
        return 1;
    }

    if (weapon_type == 3) {
        /* METRALHADORA: canos rotativos pesados + tambor de municao + alca superior */
        int handle  = ((ny > -0.56f && ny < -0.40f && nx > -0.34f && nx < 0.22f) ||
                       (ny >= -0.42f && ny < -0.22f &&
                        ((nx > -0.34f && nx < -0.20f) || (nx > 0.08f && nx < 0.22f))));
        int body    = (ny > -0.26f && ny < 0.24f && nx > -0.68f && nx < 0.26f);
        int barrels = (nx >= 0.24f && nx < 0.96f &&
                       ((ny > -0.22f && ny < -0.08f) ||
                        (ny > -0.04f && ny < 0.10f) ||
                        (ny > 0.14f && ny < 0.26f)));
        int clamp   = (ny > -0.24f && ny < 0.28f &&
                       ((nx > 0.50f && nx < 0.60f) || (nx > 0.84f && nx < 0.94f)));
        int drum    = (ny >= 0.22f && ny < 0.68f && nx > -0.24f && nx < 0.22f);
        int grip    = (ny >= 0.22f && ny < 0.64f && nx > -0.64f && nx < -0.42f);
        if (!handle && !body && !barrels && !clamp && !drum && !grip) return 0;

        if (drum) {
            *out_ch = (fabsf(nx) < 0.08f) ? ':' : '#';
            *out_col = r->scale_rgb(225.0f, 195.0f, 75.0f, fog * pulse);
        } else if (clamp || barrels) {
            *out_ch = clamp ? '#' : '=';
            *out_col = r->scale_rgb(195.0f, 210.0f, 185.0f, fog * pulse);
        } else if (handle || grip) {
            *out_ch = '#';
            *out_col = r->scale_rgb(82.0f, 96.0f, 78.0f, fog);
        } else {
            *out_ch = r->sample_ramp(0.70f * fog, sx, sy);
            *out_col = r->scale_rgb(120.0f, 165.0f, 92.0f, fog);
        }
        return 1;
    }

    /* ARMA FUTURISTA (4): ponteiras duplas abertas + nucleo de plasma brilhante */
    int top_prong = (ny > -0.42f && ny < -0.18f && nx > 0.10f && nx < 0.95f);
    int bot_prong = (ny > 0.14f && ny < 0.38f && nx > 0.10f && nx < 0.95f);
    int core      = ((nx - 0.22f) * (nx - 0.22f) + (ny + 0.02f) * (ny + 0.02f) < 0.09f);
    int chassis   = (ny > -0.34f && ny < 0.30f && nx > -0.62f && nx <= 0.18f);
    int fin       = (ny >= 0.28f && ny < 0.66f && nx > -0.48f && nx < -0.18f);
    if (!top_prong && !bot_prong && !core && !chassis && !fin) return 0;

    if (core) {
        *out_ch = (pulse > 0.94f) ? '@' : '*';
        *out_col = r->scale_rgb(110.0f, 250.0f, 255.0f, fog * (pulse + 0.12f));
    } else if (top_prong || bot_prong) {
        *out_ch = (nx > 0.78f) ? '>' : '=';
        *out_col = r->scale_rgb(165.0f, 225.0f, 255.0f, fog * pulse);
    } else if (fin) {
        *out_ch = '/';
        *out_col = r->scale_rgb(125.0f, 95.0f, 210.0f, fog);
    } else {
        *out_ch = r->sample_ramp(0.78f * fog, sx, sy);
        *out_col = r->scale_rgb(110.0f, 145.0f, 225.0f, fog);
    }
    return 1;
}

static int map_weapon_pickup_sampler(float u, float v, int sx, int sy,
                                     int sprite_w, int sprite_h,
                                     float dist, float fog, float time_now,
                                     const void *user_data,
                                     char *out_ch, AsciiColor *out_col) {
    (void)sprite_w;
    (void)sprite_h;
    (void)dist;
    const HazardRenderCtx *ctx = (const HazardRenderCtx *)user_data;
    if (!ctx || !ctx->api) return 0;
    return map_sample_weapon_pickup_sprite(ctx->weapon_type, u, v, sx, sy,
                                           fog, time_now, ctx->api, out_ch, out_col);
}

static int map_medkit_sampler(float u, float v, int sx, int sy,
                              int sprite_w, int sprite_h,
                              float dist, float fog, float time_now,
                              const void *user_data,
                              char *out_ch, AsciiColor *out_col) {
    (void)sprite_w;
    (void)sprite_h;
    (void)dist;
    (void)time_now;
    const HazardRenderCtx *ctx = (const HazardRenderCtx *)user_data;
    const RendererApi *r = ctx ? ctx->api : NULL;
    if (!r) return 0;

    float nx = u * 2.0f - 1.0f;
    float ny = v * 2.0f - 1.0f;
    int is_border = (fabsf(nx) > 0.80f || fabsf(ny) > 0.78f);
    int is_cross = (fabsf(nx) < 0.20f && fabsf(ny) < 0.58f) ||
                   (fabsf(ny) < 0.22f && fabsf(nx) < 0.52f);

    if (is_cross) {
        *out_ch = '+';
        *out_col = r->scale_rgb(235.0f, 52.0f, 52.0f, fog);
    } else if (is_border) {
        *out_ch = '#';
        *out_col = r->scale_rgb(130.0f, 150.0f, 170.0f, fog);
    } else {
        float lum = (0.72f + 0.18f * (1.0f - fabsf(nx) * 0.5f)) * fog;
        *out_ch = r->sample_ramp(lum, sx, sy);
        *out_col = r->scale_rgb(240.0f, 246.0f, 252.0f, fog);
    }
    return 1;
}

static int map_flame_sampler(float u, float v, int sx, int sy,
                             int sprite_w, int sprite_h,
                             float dist, float fog, float time_now,
                             const void *user_data,
                             char *out_ch, AsciiColor *out_col) {
    (void)sprite_h;
    (void)dist;
    (void)fog;
    const HazardRenderCtx *ctx = (const HazardRenderCtx *)user_data;
    const RendererApi *r = ctx ? ctx->api : NULL;
    if (!r) return 0;

    float nx = u * 2.0f - 1.0f;
    float seed = ctx->seed;
    float tongue = 0.64f + 0.24f * sinf(u * 13.0f + time_now * 11.5f + seed)
                         + 0.12f * cosf(u * 23.0f - time_now * 16.0f + seed * 0.5f);
    float max_h = (1.0f - powf(fabsf(nx), 1.45f) * 0.85f) * tongue;
    float h_norm = 1.0f - v;
    if (h_norm > max_h) {
        return 0;
    }

    float flicker = 0.5f + 0.5f * sinf(time_now * 17.0f + nx * 7.5f - h_norm * 10.0f + seed);
    if (h_norm < 0.26f && fabsf(nx) < 0.42f) {
        *out_col = (AsciiColor){255, 250, 185};
        *out_ch = '@';
    } else if (h_norm < 0.62f) {
        *out_col = (AsciiColor){255, (unsigned char)(95 + (int)(flicker * 85.0f)), 15};
        *out_ch = (sprite_w >= 8) ? r->sample_ramp(0.55f + 0.35f * flicker, sx, sy)
                                  : (flicker > 0.48f ? '#' : '*');
    } else {
        *out_col = (AsciiColor){(unsigned char)(200 + (int)(flicker * 45.0f)), 32, 10};
        *out_ch = (flicker > 0.55f ? '^' : '!');
    }
    return 1;
}

void map_render_hazards(const GameMap *map, const void *player_ptr,
                        const RendererApi *r) {
    if (!map || !player_ptr || !r) {
        return;
    }

    for (int my = 0; my < map->height; my++) {
        for (int mx = 0; mx < map->width; mx++) {
            int is_fire = map_is_fire_active_at(map, mx, my);
            int is_medkit = !is_fire && map_is_medkit_active_at(map, mx, my);
            int is_weapon = !is_fire && !is_medkit && map_is_weapon_pickup_active_at(map, mx, my);
            if (!is_fire && !is_medkit && !is_weapon) {
                continue;
            }

            RenderSpriteSpec spec;
            spec.world_x = (float)mx + 0.5f;
            spec.world_y = (float)my + 0.5f;

            HazardRenderCtx ctx;
            ctx.seed = (float)(mx * 17 + my * 31);
            ctx.weapon_type = is_weapon ? map_weapon_tile_type(map_get_raw_tile(map, mx, my)) : -1;
            ctx.api = r;

            if (is_medkit) {
                spec.height_scale = 0.28f / 0.78f;
                spec.width_ratio = 1.35f;
                spec.min_w = 5;
                spec.min_h = 4;
                spec.min_fog = 0.32f;
                r->draw_world_sprite(player_ptr, &spec, map_medkit_sampler, &ctx);
            } else if (is_weapon) {
                spec.height_scale = 0.34f / 0.78f;
                spec.width_ratio = 1.95f;
                spec.min_w = 10;
                spec.min_h = 5;
                spec.min_fog = 0.45f;
                r->draw_world_sprite(player_ptr, &spec, map_weapon_pickup_sampler, &ctx);
            } else {
                spec.height_scale = 1.20f / 0.78f;
                spec.width_ratio = 0.58f;
                spec.min_w = 5;
                spec.min_h = 7;
                spec.min_fog = 0.25f;
                r->draw_world_sprite(player_ptr, &spec, map_flame_sampler, &ctx);
            }
        }
    }
}
