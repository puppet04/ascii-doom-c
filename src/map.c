/* map.c - carregamento de fases, colisao de tiles, armadilhas, pickups e minimapa */

#include "map.h"
#include "embedded_assets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Arena padrao usada caso nenhum arquivo de fase ou mapa embutido seja encontrado */
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

/* Estado global do nivel atual: spawn, camera do minimapa e objetos interativos */
static int g_enemies_alive = 0;
static int g_current_level = 1;
static int g_minimap_cam_x = 0;
static int g_minimap_cam_y = 0;
static float g_spawn_x = 2.5f;
static float g_spawn_y = 4.5f;
static int g_spawn_pending = 0;
static unsigned char g_trap_active[MAP_HEIGHT][MAP_WIDTH];
static float g_trap_timer[MAP_HEIGHT][MAP_WIDTH];
static int g_active_traps_count = 0;
static unsigned char g_medkit_used[MAP_HEIGHT][MAP_WIDTH];
static unsigned char g_weapon_pickup_used[MAP_HEIGHT][MAP_WIDTH];

/* Tempo em segundos que uma armadilha de piso fica acesa apos ser pisada */
#define TRAP_ACTIVE_DURATION 3.0f

/* Limpa o estado e os timers de todas as armadilhas de fogo */
void map_reset_traps(void) {
    memset(g_trap_active, 0, sizeof(g_trap_active));
    memset(g_trap_timer, 0, sizeof(g_trap_timer));
    g_active_traps_count = 0;
}

/* Marca todos os kits medicos do mapa como disponiveis novamente */
void map_reset_medkits(void) {
    memset(g_medkit_used, 0, sizeof(g_medkit_used));
}

/* Restaura todos os pickups de arma do mapa */
void map_reset_weapon_pickups(void) {
    memset(g_weapon_pickup_used, 0, sizeof(g_weapon_pickup_used));
}

/* Inicializa o mapa com a arena padrao e reseta o estado da fase */
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

/* Carrega um mapa .txt do disco (ou da copia embutida no binario se nao achar arquivo) */
int map_load_from_file(GameMap *map, const char *filename) {
    if (!map || !filename) {
        return 0;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* Tenta um nivel acima caso o executavel rode dentro de build/ */
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

    /* Extrai o numero da fase a partir do nome (ex: level3.txt) */
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
            /* Le linha por linha direto da string embutida quando nao ha FILE* */
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
                    /* Marca a posicao inicial do jogador e deixa o chao livre por baixo */
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

/* Total de fases registradas no catalogo */
int map_level_count(void) {
    return MAP_LEVEL_TOTAL;
}

/* Garante que o indice de fase fique dentro dos limites do catalogo */
int map_level_clamp(int index) {
    if (MAP_LEVEL_TOTAL <= 0) return 0;
    if (index < 0) return 0;
    if (index >= MAP_LEVEL_TOTAL) return MAP_LEVEL_TOTAL - 1;
    return index;
}

/* Devolve os metadados e spawns da fase escolhida */
const MapLevelInfo *map_level_get(int index) {
    return &MAP_LEVELS[map_level_clamp(index)];
}

/* Carrega a fase pelo indice (funcao customizada, arquivo .txt ou mapa padrao) */
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
        /* Se o .txt tinha um 'P', sobrescreve o spawn1 padrao do catalogo */
        if (g_spawn_pending) {
            MAP_LEVELS[clamped].spawn1_x = g_spawn_x;
            MAP_LEVELS[clamped].spawn1_y = g_spawn_y;
        }
        return;
    }
    map_init_default(map);
}

/* Flags auxiliares para o raycaster e para evitar spawn de inimigos em corredores secretos */
static int g_last_tile_passable = 0;
static int g_spawning_enemies = 0;
static int g_minimap_secret_floor = 0;
static int g_minimap_secret_x = -1;
static int g_minimap_secret_y = -1;

/* Pisos secretos usam simbolos especiais para parecerem parede no minimapa */
static int is_secret_floor(char tile) {
    return (tile == ',' || tile == ';' || tile == ':' || tile == '_' ||
            (tile >= '1' && tile <= '9'));
}

/* Retorna 1 se o caractere do mapa representa o ponto de spawn de algum inimigo ou boss */
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

/* Tiles de armadilha que disparam fogo quando o jogador passa por cima */
int map_is_trap_tile(char tile) {
    return (tile == '^' || tile == '5' || tile == '6' || tile == '7' || tile == '8');
}

/* Tiles de kit medico no chao (incluindo variantes escondidas em corredores secretos) */
int map_is_medkit_tile(char tile) {
    return (tile == '+' || tile == '1' || tile == '2' || tile == '3' || tile == '4');
}

/* Qualquer tile que pode queimar o jogador (armadilha ou fogo continuo) */
int map_is_hazard_tile(char tile) {
    return (map_is_trap_tile(tile) || tile == 'F' || tile == '9');
}

/* Verifica se a armadilha na celula (x, y) esta ativa no momento */
int map_is_trap_triggered(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 0;
    }
    return g_trap_active[y][x] != 0;
}

/* Dispara a armadilha da celula e reinicia o temporizador de chamas */
void map_trigger_trap(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return;
    }
    if (!g_trap_active[y][x]) {
        g_trap_active[y][x] = 1;
        g_active_traps_count++;
    }
    g_trap_timer[y][x] = TRAP_ACTIVE_DURATION;
}

/* Atualiza o tempo restante de todas as armadilhas acesas (pula o grid se nenhuma estiver acesa) */
void map_update_traps(float dt) {
    if (dt <= 0.0f || g_active_traps_count <= 0) {
        return;
    }
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (g_trap_active[y][x]) {
                g_trap_timer[y][x] -= dt;
                if (g_trap_timer[y][x] <= 0.0f) {
                    g_trap_active[y][x] = 0;
                    g_trap_timer[y][x] = 0.0f;
                    if (g_active_traps_count > 0) g_active_traps_count--;
                }
            }
        }
    }
}

/* Retorna 1 se ha fogo ativo na celula (seja chama fixa F/9 ou armadilha disparada) */
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

/* Retorna 1 se existe um kit medico ainda nao coletado na celula */
int map_is_medkit_active_at(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 0;
    }
    char tile = map_get_raw_tile(map, x, y);
    return map_is_medkit_tile(tile) && g_medkit_used[y][x] == 0;
}

/* Consome o kit medico da celula se estiver disponivel; retorna 1 em caso de coleta */
int map_try_collect_medkit(const GameMap *map, int x, int y) {
    if (!map_is_medkit_active_at(map, x, y)) {
        return 0;
    }
    g_medkit_used[y][x] = 1;
    return 1;
}

/* Converte o caractere do mapa para o id da arma correspondente (-1 se nao for arma) */
int map_weapon_tile_type(char tile) {
    switch (tile) {
        case 'G': return 1; /* WEAPON_SHOTGUN */
        case 'S': return 2; /* WEAPON_SNIPER */
        case 'R': return 3; /* WEAPON_MACHINEGUN */
        case 'O': return 4; /* WEAPON_FUTURISTIC */
        default:  return -1;
    }
}

/* Verifica se ha um pickup de arma ativo (nao coletado) na celula */
int map_is_weapon_pickup_active_at(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        return 0;
    }
    char tile = map_get_raw_tile(map, x, y);
    return map_weapon_tile_type(tile) >= 0 && g_weapon_pickup_used[y][x] == 0;
}

/* Coleta a arma da celula e devolve o tipo em out_weapon_type */
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

/* Mapeia tiles especiais (spawns, itens, passagens secretas) para o char visual equivalente */
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

/* Ativa o modo de spawn para impedir que inimigos nasçam dentro de salas secretas */
void map_begin_enemy_spawn(void) {
    g_spawning_enemies = 1;
}

/* Encerra o modo de spawn de inimigos */
void map_end_enemy_spawn(void) {
    g_spawning_enemies = 0;
}

/* Atualiza a flag global que controla se a saida E ainda esta trancada */
int map_set_enemies_alive(int count) {
    g_enemies_alive = (count > 0) ? 1 : 0;
    return count;
}

/* Informa se ainda existem inimigos obrigatorios vivos na fase */
int map_are_enemies_alive(void) {
    return g_enemies_alive;
}

/* Paredes falsas ($, &, /) sempre deixam passar; a saida E abre quando nao ha inimigos vivos */
int map_is_passable_wall(char tile) {
    if (tile == 'E') {
        return !g_enemies_alive;
    }
    return (tile == '$' || tile == '&' || tile == '/');
}

/* Retorna se o ultimo tile consultado por map_get_tile era parede falsa atravessavel */
int map_last_tile_is_passable(void) {
    return g_last_tile_passable;
}

/* Porcentagem de clareamento para dar uma pista sutil nas paredes atravessaveis */
static int passable_lighten_pct(char tile) {
    switch (tile) {
        case '#': return 9;
        case '@': return 25;
        case '%': return 25;
        default:  return 18;
    }
}

/* Clareia um canal de cor RGB de acordo com o tipo da parede falsa */
unsigned char map_lighten_channel_for_tile(char tile, unsigned char c) {
    int pct = passable_lighten_pct(tile);
    int v = (int)c + ((255 - (int)c) * pct) / 100;
    return (unsigned char)((v > 255) ? 255 : v);
}

/* Versao generica de clareamento sem especificar o caractere da parede */
unsigned char map_lighten_channel(unsigned char c) {
    return map_lighten_channel_for_tile('\0', c);
}

/* Le o caractere cru do mapa; fora dos limites trata como parede '#' */
char map_get_raw_tile(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        return '#';
    }
    return map->tiles[y][x];
}

/* Le o tile ja convertido para visualizacao e guarda flags de parede falsa e piso secreto */
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

/* Aplica no jogador a posicao 'P' lida do arquivo de mapa, se houver uma pendente */
void map_apply_pending_spawn(float *player_x, float *player_y) {
    if (!player_x || !player_y || !g_spawn_pending) {
        return;
    }
    *player_x = g_spawn_x;
    *player_y = g_spawn_y;
    g_spawn_pending = 0;
}

/* Tabela de flags pre-calculada para todos os 256 caracteres ASCII:
 * evita dezenas de comparacoes por passo no raycaster DDA e na linha de visao */
enum {
    TILE_F_SECRET   = 1 << 0,
    TILE_F_EXIT     = 1 << 1,
    TILE_F_SOLID    = 1 << 2,
    TILE_F_VIS_WALL = 1 << 3
};
static unsigned char g_tile_lut[256];
static int g_tile_lut_ready = 0;

static void ensure_tile_lut(void) {
    if (g_tile_lut_ready) return;
    for (int c = 0; c < 256; c++) {
        char t = (char)c;
        unsigned char fl = 0;
        if (is_secret_floor(t)) {
            fl |= TILE_F_SECRET;
        } else {
            int non_wall = (t == '.' || t == ' ' || t == 'P' ||
                            map_is_enemy_spawn_char(t) ||
                            map_is_hazard_tile(t) ||
                            map_is_medkit_tile(t) ||
                            map_weapon_tile_type(t) >= 0);
            if (!non_wall) {
                fl |= TILE_F_VIS_WALL;
                if (t == 'E') {
                    fl |= TILE_F_EXIT;
                } else if (t != '$' && t != '&' && t != '/') {
                    fl |= TILE_F_SOLID;
                }
            }
        }
        g_tile_lut[c] = fl;
    }
    g_tile_lut_ready = 1;
}

/* Verifica colisao fisica: retorna 1 se a celula bloqueia movimento e tiros */
int map_is_wall(const GameMap *map, int x, int y) {
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        return 1;
    }
    if (!g_tile_lut_ready) ensure_tile_lut();
    unsigned char fl = g_tile_lut[(unsigned char)map->tiles[y][x]];
    if (fl & TILE_F_SOLID) return 1;
    if (fl & TILE_F_EXIT) return g_enemies_alive;
    if (fl & TILE_F_SECRET) return g_spawning_enemies ? 1 : 0;
    return 0;
}

/* Verifica visibilidade no raycaster/minimapa (inclui paredes falsas e esconde pisos secretos) */
int map_is_visible_wall(const GameMap *map, int x, int y) {
    g_last_tile_passable = 0;
    if (!map || x < 0 || x >= map->width || y < 0 || y >= map->height) {
        g_minimap_secret_floor = 0;
        return 1;
    }
    if (!g_tile_lut_ready) ensure_tile_lut();
    unsigned char fl = g_tile_lut[(unsigned char)map->tiles[y][x]];
    if (fl & TILE_F_SECRET) {
        int on_minimap = (g_minimap_secret_floor &&
                          x == g_minimap_secret_x &&
                          y == g_minimap_secret_y);
        g_minimap_secret_floor = 0;
        return on_minimap ? 1 : 0;
    }
    g_minimap_secret_floor = 0;
    return (fl & TILE_F_VIS_WALL) ? 1 : 0;
}

/* Checa se o mapa possui pelo menos um portal de saida E */
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

/* Verifica se a coordenada continua (x, y) cai dentro de um tile de saida E */
static int is_exit_at(const GameMap *map, float x, float y) {
    return (map_get_raw_tile(map, (int)floorf(x), (int)floorf(y)) == 'E');
}

/* Avanca para a proxima fase se o jogador tocar na saida E sem inimigos vivos (retorna -1 no fim) */
int map_try_advance_level(GameMap *map, float *player_x, float *player_y, float *player_angle) {
    if (!map || !player_x || !player_y || !player_angle || g_enemies_alive) {
        return 0;
    }

    float px = *player_x;
    float py = *player_y;
    /* Testa o centro e 4 pontos ao redor para facilitar a entrada no portal */
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

    /* Fallback para fases extras alem do catalogo (ex: level6.txt) */
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

/* Limita a janela da camera do minimapa dentro das bordas do mapa */
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

/* Recorta um sub-mapa centrado ao redor do jogador para desenhar no minimapa */
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

/* Converte uma posicao do mundo para coordenadas locais do minimapa; retorna 1 se visivel */
int map_to_minimap_coords(float *x, float *y) {
    if (!x || !y) {
        return 0;
    }
    *x -= (float)g_minimap_cam_x;
    *y -= (float)g_minimap_cam_y;
    return (*x >= 0.0f && *x < (float)MINIMAP_VIEW_SIZE &&
            *y >= 0.0f && *y < (float)MINIMAP_VIEW_SIZE) ? 1 : 0;
}

/* Versao para minimapa da checagem de fogo ativo, somando o deslocamento da camera */
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

/* Versao para minimapa da checagem de kit medico disponivel */
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


/* --- Hooks de renderizacao de chao, paredes, kits medicos e chamas --- */

/* Escala uma cor RGB por factor mantendo cada canal dentro de [0, 255] */
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

/* Ajusta cor e brilho de paredes especiais (saida E liberada e paredes falsas) */
void map_wall_style_hook(const void *map_ptr, int mx, int my, char tile,
                         AsciiColor *io_color, float *io_fog_dist,
                         int *io_is_edge, int *io_side) {
    const GameMap *map = (const GameMap *)map_ptr;
    if (!map || !io_color || !io_fog_dist || !io_is_edge || !io_side) {
        return;
    }
    if (tile == 'E' && !map_are_enemies_alive()) {
        /* Quando todos os inimigos morreram, a saida brilha em branco ate de longe */
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

/* Desenha detalhes diretamente no piso (grelha das armadilhas de fogo e base dos kits) */
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

/* Define icone e cor de fogo, medkits e pickups de arma no minimapa */
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

/* Contexto passado aos callbacks de sprite de chamas, kits e armas no chao */
typedef struct {
    float seed;
    int weapon_type;
    const RendererApi *api;
} HazardRenderCtx;

/* Desenha proceduralmente em ASCII o sprite de cada arma coletavel no chao */
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

    if (weapon_type == 5) {
        /* MAQUITA (Boss 1): carcaca azul-turquesa + disco serrilhado prateado girando */
        float dx_s = (nx - 0.34f) / 0.44f;
        float dy_s = (ny + 0.04f) / 0.56f;
        float r2 = dx_s * dx_s + dy_s * dy_s;
        int blade = (r2 <= 1.0f);
        int body  = (ny > -0.26f && ny < 0.36f && nx > -0.82f && nx < 0.16f);
        int handle = (ny > -0.58f && ny <= -0.22f && nx > -0.66f && nx < -0.06f &&
                      (ny < -0.42f || nx < -0.50f || nx > -0.22f));
        int base  = (ny >= 0.30f && ny < 0.48f && nx > -0.58f && nx < 0.52f);
        if (!blade && !body && !handle && !base) return 0;

        if (blade && !body) {
            float ang = atan2f(dy_s, dx_s) + time_now * 14.0f;
            int tooth = (sinf(ang * 12.0f) > 0.15f);
            *out_ch = (r2 < 0.14f) ? '@' : (tooth ? '*' : '#');
            *out_col = (r2 < 0.14f) ? r->scale_rgb(255.0f, 210.0f, 70.0f, fog)
                                    : r->scale_rgb(215.0f, 228.0f, 240.0f, fog * pulse);
        } else if (handle || base) {
            *out_ch = '=';
            *out_col = r->scale_rgb(56.0f, 62.0f, 70.0f, fog);
        } else {
            *out_ch = r->sample_ramp(0.75f * fog, sx, sy);
            *out_col = r->scale_rgb(38.0f, 178.0f, 202.0f, fog * pulse);
        }
        return 1;
    }

    if (weapon_type == 6) {
        /* REVOLVER GRANDE (Boss 2): cano longo cromado + tambor largo + cabo madeira */
        int barrel = (ny > -0.34f && ny < -0.06f && nx > -0.04f && nx < 0.96f);
        int sight  = (ny > -0.44f && ny <= -0.32f && nx > 0.78f && nx < 0.94f);
        int cyl    = (ny > -0.30f && ny < 0.16f && nx > -0.42f && nx <= 0.04f);
        int frame  = (ny > -0.26f && ny < 0.24f && nx > -0.56f && nx <= -0.36f);
        int grip   = (nx >= -0.88f && nx <= -0.42f &&
                      ny > (0.04f + (-0.42f - nx) * 0.75f) &&
                      ny < (0.48f + (-0.42f - nx) * 0.85f));
        if (!barrel && !sight && !cyl && !frame && !grip) return 0;

        if (grip) {
            *out_ch = '#';
            *out_col = r->scale_rgb(165.0f, 96.0f, 48.0f, fog);
        } else if (cyl) {
            *out_ch = (fabsf(ny + 0.07f) < 0.08f) ? '=' : '#';
            *out_col = r->scale_rgb(235.0f, 220.0f, 165.0f, fog * pulse);
        } else {
            *out_ch = (sight || nx > 0.86f) ? ']' : '=';
            *out_col = r->scale_rgb(220.0f, 230.0f, 244.0f, fog * pulse);
        }
        return 1;
    }

    if (weapon_type == 7) {
        /* TOMMY GUN (Boss 3): cano aletado + tambor redondo 50 balas + madeira */
        int barrel   = (ny > -0.24f && ny < -0.04f && nx > -0.10f && nx < 0.96f);
        int receiver = (ny > -0.28f && ny < 0.06f && nx > -0.44f && nx <= 0.26f);
        int foregrip = (ny >= 0.00f && ny < 0.44f && nx > 0.34f && nx < 0.50f);
        int stock    = (nx >= -0.96f && nx <= -0.40f &&
                        ny > (-0.12f + (-0.40f - nx) * 0.35f) &&
                        ny < (0.28f + (-0.40f - nx) * 0.55f));
        float dx_d = (nx - 0.04f) / 0.26f;
        float dy_d = (ny - 0.30f) / 0.34f;
        int drum = (dx_d * dx_d + dy_d * dy_d <= 1.0f);
        if (!barrel && !receiver && !foregrip && !stock && !drum) return 0;

        if (drum) {
            int center = (dx_d * dx_d + dy_d * dy_d < 0.22f);
            *out_ch = center ? '@' : '#';
            *out_col = center ? r->scale_rgb(245.0f, 205.0f, 75.0f, fog * pulse)
                              : r->scale_rgb(68.0f, 74.0f, 84.0f, fog);
        } else if (stock || foregrip) {
            *out_ch = '#';
            *out_col = r->scale_rgb(158.0f, 96.0f, 50.0f, fog);
        } else {
            *out_ch = '=';
            *out_col = r->scale_rgb(185.0f, 195.0f, 208.0f, fog * pulse);
        }
        return 1;
    }

    if (weapon_type == 8) {
        /* LANCA-CHAMAS (Boss 4): tubo pesado + cilindro vermelho + chama-piloto na ponta */
        int tube   = (ny > -0.24f && ny < 0.06f && nx > -0.62f && nx < 0.78f);
        int tank   = (ny >= 0.04f && ny < 0.46f && nx > -0.42f && nx < 0.36f);
        int grip   = (ny >= 0.04f && ny < 0.50f && nx > -0.78f && nx < -0.52f);
        float dx_f = (nx - 0.88f) / 0.16f;
        float dy_f = (ny + 0.09f) / 0.24f;
        int flame  = (dx_f * dx_f + dy_f * dy_f <= 1.0f);
        if (!tube && !tank && !grip && !flame) return 0;

        if (flame) {
            *out_ch = (pulse > 0.94f) ? '@' : '*';
            *out_col = r->scale_rgb(255.0f, 180.0f, 45.0f, fog * (pulse + 0.15f));
        } else if (tank) {
            *out_ch = '#';
            *out_col = r->scale_rgb(218.0f, 52.0f, 38.0f, fog * pulse);
        } else if (grip) {
            *out_ch = '#';
            *out_col = r->scale_rgb(72.0f, 78.0f, 86.0f, fog);
        } else {
            *out_ch = '=';
            *out_col = r->scale_rgb(175.0f, 185.0f, 196.0f, fog);
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

/* Callback do renderer para amostrar o sprite da arma selecionada no contexto */
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

/* Desenha a maleta branca com cruz vermelha dos kits medicos no mundo 3D */
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

/* Desenha chama animada usando senoides e seed por celula para variar o tremor */
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

/* Varre o grid do mapa e envia sprites de fogo, kits medicos e armas para o renderer */
void map_render_hazards(const GameMap *map, const void *player_ptr,
                        const RendererApi *r) {
    if (!map || !player_ptr || !r) {
        return;
    }

    for (int my = 0; my < map->height; my++) {
        const char *row = map->tiles[my];
        for (int mx = 0; mx < map->width; mx++) {
            /* Pula direto pisos vazios e paredes comuns (mais de 95% das celulas do mapa) */
            char raw = row[mx];
            if (raw == '.' || raw == '#' || raw == '@' || raw == '%' || raw == ' ' || raw == 'E') {
                continue;
            }
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
