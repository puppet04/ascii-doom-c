/* main.c - ponto de entrada, menus, lobby multiplayer (LAN/Online) e loop da partida */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "entities.h"
#include "map.h"
#include "network.h"
#include "player.h"
#include "raylib.h"
#include "renderer.h"

#ifdef _WIN32
/* Solicita automaticamente a GPU dedicada (NVIDIA Optimus / AMD PowerXpress) em notebooks hibridos */
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

/* --- Configuracoes locais, resolucao e keybinds (exclusivo de main.c) --- */

/* Paginas possiveis na navegacao do menu principal */
typedef enum {
    MENU_PAGE_MAIN = 0,
    MENU_PAGE_SINGLEPLAYER = 1,
    MENU_PAGE_MULTIPLAYER = 2,
    MENU_PAGE_LOBBY = 3,
    MENU_PAGE_CUSTOMIZE = 4,
    MENU_PAGE_SETTINGS = 5,
    MENU_PAGE_CREATE = 6
} MenuPage;

/* Mapeamento de teclas configuraveis e sensibilidade do mouse */
typedef struct {
    int move_forward;
    int move_backward;
    int move_left;
    int move_right;
    int turn_left;
    int turn_right;
    int sprint;
    int reload;
    int shoot_key;
    float mouse_sens;
} MenuKeyBinds;

typedef struct {
    int w;
    int h;
    const char *label;
} ResPreset;

static const ResPreset RES_PRESETS[] = {
    {1024, 576,  "1024 x 576"},
    {1280, 720,  "1280 x 720"},
    {1600, 900,  "1600 x 900"},
    {1920, 1080, "1920 x 1080"},
    {2560, 1440, "2560 x 1440"}
};
static const int RES_PRESET_COUNT = (int)(sizeof(RES_PRESETS) / sizeof(RES_PRESETS[0]));

/* Estado persistido em memoria enquanto o jogo esta aberto (video, binds, roster e fase) */
typedef struct {
    MenuKeyBinds binds;
    int fps_cap;
    int show_fps;
    int res_idx;
    int ascii_quality;
    int ascii_style;
    int selected_character;
    int selected_level;
    int mp_game_mode; /* NET_GAME_COOP (0) ou NET_GAME_PVP (1) */
    CharacterRoster roster;
} AppConfig;

/* Preenche os controles padrao (WASD + Q/setas + Shift + R + Espaco) */
static void binds_init_default(MenuKeyBinds *b) {
    if (!b) return;
    b->move_forward  = KEY_W;
    b->move_backward = KEY_S;
    b->move_left     = KEY_A;
    b->move_right    = KEY_D;
    b->turn_left     = KEY_Q;
    b->turn_right    = KEY_RIGHT;
    b->sprint        = KEY_LEFT_SHIFT;
    b->reload        = KEY_R;
    b->shoot_key     = KEY_SPACE;
    b->mouse_sens    = MOUSE_SENSITIVITY;
}

/* Inicializa configuracoes de video, roster de personagens e opcoes padrao */
static void app_config_init(AppConfig *cfg) {
    if (!cfg) return;
    binds_init_default(&cfg->binds);
    cfg->fps_cap = TARGET_FPS;
    cfg->selected_character = PRESET_FUZILEIRO;
    cfg->selected_level = 0;
    cfg->mp_game_mode = NET_GAME_COOP;
    player_roster_init(&cfg->roster);
    cfg->show_fps = 0; /* Desativado por padrao */
    cfg->res_idx = 1;  /* 1280 x 720 padrao */
    cfg->ascii_quality = ASCII_QUALITY_AUTO;
    cfg->ascii_style = ASCII_STYLE_CLASSIC;
}

static const char *quality_to_label(int q) {
    switch (q) {
        case ASCII_QUALITY_LEVE:  return "BAIXO";
        case ASCII_QUALITY_MEDIA: return "MEDIO";
        case ASCII_QUALITY_ALTA:  return "ALTO";
        case ASCII_QUALITY_ULTRA: return "ULTRA";
        default:                  return "AUTO (JANELA)";
    }
}

static const char *style_to_label(int style) {
    return (style == ASCII_STYLE_SOLID) ? "ESTILO 2: MICRO-ASCII" : "ESTILO 1: SOBREPOSTO";
}

/* Calcula o fator de densidade ASCII para o menu/showcase conforme a qualidade e tamanho da janela */
static float calc_effective_density_scale(int quality, int sw, int sh) {
    float s;
    float wf = ((float)sw / 1280.0f < (float)sh / 720.0f)
             ? ((float)sw / 1280.0f) : ((float)sh / 720.0f);
    if (wf < 0.0f) wf = 0.0f;
    if (wf > 1.0f) wf = 1.0f;

    switch (quality) {
        case ASCII_QUALITY_LEVE:  s = 0.54f; break;
        case ASCII_QUALITY_MEDIA: s = 0.72f; break;
        case ASCII_QUALITY_ALTA:  s = 0.90f; break;
        case ASCII_QUALITY_ULTRA: s = 1.08f; break;
        case ASCII_QUALITY_AUTO:
        default:                  s = 0.52f + wf * 0.42f; break;
    }

    /* Em janelas menores, evita celulas apertadas e suaviza carga automaticamente */
    float max_s = 0.50f + wf * 0.58f;
    if (s > max_s) s = max_s;
    if (s < 0.52f) s = 0.52f;
    if (s > 1.08f) s = 1.08f;
    return s;
}

/* Controle de janela borderless e limpeza de screenshots automaticos do Raylib (F12) */
static int g_borderless_mode = 0;
static int g_borderless_unfocused_shrunk = 0;
static int g_f12_cleanup_frames = 0;
static int g_saved_win_x = 100;
static int g_saved_win_y = 80;
static int g_saved_win_w = 1280;
static int g_saved_win_h = 720;

/* Remove arquivos screenshotXXX.png que o Raylib grava sozinho quando aperta F12 */
static void cleanup_raylib_screenshots(void) {
    char path[32];
    for (int i = 0; i <= 15; i++) {
        snprintf(path, sizeof(path), "screenshot%03d.png", i);
        if (FileExists(path)) {
            (void)remove(path);
        }
    }
}

/* Alterna janela sem bordas ocupando o monitor inteiro.
 * Usa (mh + 1) px para impedir promocao do Windows WGL para Exclusive Fullscreen. */
static void toggle_borderless_window(void) {
    if (!g_borderless_mode) {
        Vector2 wp = GetWindowPosition();
        int cw = GetScreenWidth();
        int ch = GetScreenHeight();
        if (cw > 100 && ch > 100) {
            g_saved_win_x = (int)wp.x;
            g_saved_win_y = (int)wp.y;
            g_saved_win_w = cw;
            g_saved_win_h = ch;
        }
        int mon = GetCurrentMonitor();
        Vector2 mp = GetMonitorPosition(mon);
        int mw = GetMonitorWidth(mon);
        int mh = GetMonitorHeight(mon);
        if (mw <= 0) mw = WINDOW_WIDTH;
        if (mh <= 0) mh = WINDOW_HEIGHT;
        if (IsWindowState(FLAG_WINDOW_MAXIMIZED)) {
            RestoreWindow();
        }
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        /* +1px de altura evita promocao WGL Exclusive Fullscreen que trava o Alt+Tab no Windows */
        SetWindowSize(mw, mh + 1);
        SetWindowPosition((int)mp.x, (int)mp.y);
        g_borderless_mode = 1;
        g_borderless_unfocused_shrunk = 0;
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        int py = (g_saved_win_y < 24) ? 24 : g_saved_win_y;
        SetWindowSize(g_saved_win_w, g_saved_win_h);
        SetWindowPosition(g_saved_win_x, py);
        g_borderless_mode = 0;
        g_borderless_unfocused_shrunk = 0;
    }
}

/* Mantem o cursor solto ao perder foco/Alt+Tab e ajusta a janela sem bordas quando desfocada */
static int sync_window_focus_and_borderless(void) {
    int focused = IsWindowFocused();
    int sys_mod = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT) ||
                  IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    int win_active = focused && !sys_mod;

    if (!win_active && IsCursorHidden()) {
        EnableCursor();
    }

    if (g_borderless_mode) {
        int mon = GetCurrentMonitor();
        Vector2 mp = GetMonitorPosition(mon);
        int mw = GetMonitorWidth(mon);
        int mh = GetMonitorHeight(mon);
        if (mw > 0 && mh > 0) {
            if (!focused && !g_borderless_unfocused_shrunk) {
                /* Ao perder foco no Alt+Tab, libera barra de tarefas e permite outras janelas a frente */
                SetWindowSize(mw, mh - 1);
                SetWindowPosition((int)mp.x, (int)mp.y);
                g_borderless_unfocused_shrunk = 1;
            } else if (focused && g_borderless_unfocused_shrunk) {
                SetWindowSize(mw, mh + 1);
                SetWindowPosition((int)mp.x, (int)mp.y);
                g_borderless_unfocused_shrunk = 0;
            }
        }
    }

    if (IsKeyPressed(KEY_F12)) {
        g_f12_cleanup_frames = 2;
    }
    if (g_f12_cleanup_frames > 0) {
        cleanup_raylib_screenshots();
        g_f12_cleanup_frames--;
    }

    return win_active;
}

/* Aplica uma resolucao da lista, saindo do modo sem bordas/maximizado e centralizando no monitor */
static void apply_window_resolution(int res_idx) {
    if (res_idx < 0 || res_idx >= RES_PRESET_COUNT) return;
    if (g_borderless_mode) {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        g_borderless_mode = 0;
        g_borderless_unfocused_shrunk = 0;
    }
    if (IsWindowState(FLAG_WINDOW_MAXIMIZED)) {
        RestoreWindow();
    }

    int w = RES_PRESETS[res_idx].w;
    int h = RES_PRESETS[res_idx].h;
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon);
    int mh = GetMonitorHeight(mon);

    if (mw > 0 && w > mw) w = mw;
    if (mh > 0 && h > mh) h = mh;

    SetWindowSize(w, h);
    if (mw > 0 && mh > 0) {
        int px = (mw - w) / 2;
        int py = (mh - h) / 2;
        if (px < 0) px = 0;
        if (py < 24) py = 24;
        SetWindowPosition(px, py);
    }
}

/* Calcula largura minima de um botao para o texto caber com folga */
static int calc_fit_w(const char *txt, int font_sz, int min_w, float sc) {
    int w = MeasureText(txt, font_sz) + (int)(26.0f * sc);
    return (w > min_w) ? w : min_w;
}

/* Converte o codigo de tecla do Raylib para texto legivel na tela de configuracoes */
static const char *key_to_label(int key) {
    static const char *LETTERS[26] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
    };
    static const char *DIGITS[10] = {
        "0","1","2","3","4","5","6","7","8","9"
    };

    if (key >= KEY_A && key <= KEY_Z) return LETTERS[key - KEY_A];
    if (key >= KEY_ZERO && key <= KEY_NINE) return DIGITS[key - KEY_ZERO];

    switch (key) {
        case KEY_SPACE:         return "ESPACO";
        case KEY_LEFT_SHIFT:    return "L-SHIFT";
        case KEY_RIGHT_SHIFT:   return "R-SHIFT";
        case KEY_LEFT_CONTROL:  return "L-CTRL";
        case KEY_RIGHT_CONTROL: return "R-CTRL";
        case KEY_LEFT_ALT:      return "L-ALT";
        case KEY_RIGHT_ALT:     return "R-ALT";
        case KEY_TAB:           return "TAB";
        case KEY_ENTER:         return "ENTER";
        case KEY_BACKSPACE:     return "BACKSPACE";
        case KEY_UP:            return "SETA CIMA";
        case KEY_DOWN:          return "SETA BAIXO";
        case KEY_LEFT:          return "SETA ESQ";
        case KEY_RIGHT:         return "SETA DIR";
        case KEY_MINUS:         return "-";
        case KEY_EQUAL:         return "=";
        case KEY_F1:            return "F1";
        case KEY_F2:            return "F2";
        case KEY_F4:            return "F4";
        case KEY_F5:            return "F5";
        case KEY_F6:            return "F6";
        case KEY_F7:            return "F7";
        case KEY_F8:            return "F8";
        case KEY_F9:            return "F9";
        case KEY_F10:           return "F10";
        case KEY_F12:           return "F12";
        default:                return "TECLA";
    }
}

/* --- Hooks do renderer e controle de jogo com keybinds customizaveis --- */

/* Conecta os hooks do modulo map.c (chao, paredes especiais 'E'/'$' e minimapa)
 * dentro da biblioteca renderer, sem criar dependencia entre renderer e map.c. */
static void register_renderer_hooks(void) {
    renderer_set_floor_hook(map_floor_decal_hook);
    renderer_set_wall_hook(map_wall_style_hook);
    renderer_set_minimap_hook(map_minimap_tile_hook);
}

/* Atualiza a camera deslizante 16x16 do minimapa em funcao da posicao do jogador
 * e converte as coordenadas de inimigos/remoto antes de desenhar o minimapa. */
static void draw_sliding_minimap(const GameMap *map, const Player *player,
                                 const SpriteEntity *sprites, int count) {
    if (!map || !player) {
        return;
    }
    GameMap mm_map;
    Player mm_player = *player;
    SpriteEntity mm_sprites[MAX_SPRITES];
    int mm_cnt = count;
    if (mm_cnt < 0) mm_cnt = 0;
    if (mm_cnt > MAX_SPRITES) mm_cnt = MAX_SPRITES;

    map_build_minimap_view(map, &mm_map, &mm_player.x, &mm_player.y);
    for (int i = 0; i < mm_cnt; i++) {
        mm_sprites[i] = sprites[i];
        if (!map_to_minimap_coords(&mm_sprites[i].x, &mm_sprites[i].y)) {
            mm_sprites[i].active = 0;
        }
    }
    renderer_draw_minimap(&mm_map, &mm_player, mm_sprites, mm_cnt);
}

/* Roda 1 frame em modo texto/headless para validar o renderer em testes automatizados */
static int run_single_test_frame(void) {
    GameMap map;
    Player player;
    SpriteEntity demo_sprite;

    register_renderer_hooks();
    map_load_level(0, &map);
    player_init(&player, 2.5f, 4.5f, 0.15f);
    map_apply_pending_spawn(&player.x, &player.y);

    demo_sprite.x = 5.5f;
    demo_sprite.y = 4.5f;
    demo_sprite.active = 1;
    demo_sprite.shooting = 0;
    demo_sprite.label = 'O';
    demo_sprite.visual_id = PRESET_FUZILEIRO;
    memset(&demo_sprite.appearance, 0, sizeof(demo_sprite.appearance));
    demo_sprite.team_accent = 2;

    const RendererApi *r_api = renderer_get_api();
    renderer_clear();
    renderer_draw_walls(&map, &player);
    renderer_draw_sprites(&player, &demo_sprite, 1);
    map_render_hazards(&map, &player, r_api);
    player_render_abilities(&player, r_api);
    draw_sliding_minimap(&map, &player, &demo_sprite, 1);
    if (!player_render_weapon(&player, r_api)) {
        renderer_draw_weapon(&player);
    }
    renderer_draw_hud(&player);
    renderer_present_stdout(&player, 0, 0);

    return 0;
}

/* Processa mouse look, movimentacao, sprint, recarga, troca de arma e tiro usando as binds atuais */
static void handle_custom_input(Player *p, const GameMap *map, const MenuKeyBinds *b,
                                float dt, int *running, int *did_shoot,
                                float *pitch_offset) {
    if (!p || !map || !b || !running || !did_shoot) {
        return;
    }

    *did_shoot = 0;

    if (p->shoot_timer > 0.0f) {
        p->shoot_timer -= dt;
        if (p->shoot_timer < 0.0f) {
            p->shoot_timer = 0.0f;
        }
    }

    /* So aplica giro por mouse quando o cursor estiver travado na janela */
    if (IsCursorHidden()) {
        Vector2 md = GetMouseDelta();
        if (md.x != 0.0f) {
            player_rotate(p, md.x * b->mouse_sens);
        }
        if (md.y != 0.0f && pitch_offset) {
            *pitch_offset -= md.y * b->mouse_sens * 220.0f;
            if (*pitch_offset < -62.0f) *pitch_offset = -62.0f;
            if (*pitch_offset > 62.0f) *pitch_offset = 62.0f;
        }
    }

    float rot_dir = 0.0f;
    if (IsKeyDown(b->turn_left)  || IsKeyDown(KEY_LEFT))  rot_dir -= 1.0f;
    if (IsKeyDown(b->turn_right) || IsKeyDown(KEY_RIGHT)) rot_dir += 1.0f;
    if (rot_dir != 0.0f) {
        player_rotate(p, rot_dir * ROT_SPEED * dt);
    }

    float fwd = 0.0f;
    float str = 0.0f;
    if (IsKeyDown(b->move_forward)  || IsKeyDown(KEY_UP))   fwd += 1.0f;
    if (IsKeyDown(b->move_backward) || IsKeyDown(KEY_DOWN)) fwd -= 1.0f;
    if (IsKeyDown(b->move_left))  str -= 1.0f;
    if (IsKeyDown(b->move_right)) str += 1.0f;

    if (fwd != 0.0f || str != 0.0f) {
        /* Normaliza o vetor pra andar na diagonal na mesma velocidade */
        float mag = sqrtf(fwd * fwd + str * str);
        fwd /= mag;
        str /= mag;

        float speed = player_effective_speed(p);
        int sprinting = IsKeyDown(b->sprint) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (sprinting) {
            speed *= SPRINT_MULT;
        }

        if (player_move(p, map, fwd * speed * dt, str * speed * dt)) {
            p->bob_phase += dt * (sprinting ? 14.0f : 10.0f);
        }
    }

    if (IsKeyPressed(b->reload)) {
        player_reload(p);
    }

    if (IsKeyPressed(KEY_F)) {
        player_cycle_weapon(p, 1);
    }

    int want_shoot = (IsCursorHidden() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
                     IsKeyDown(b->shoot_key);
    if (want_shoot) *did_shoot = player_begin_attack(p);

    (void)running;
}

/* --- Helpers matematicos e desenho de UI/showcase em ASCII --- */

static unsigned char clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (unsigned char)v;
}

static float clamp_f01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float smoothstep_f(float e0, float e1, float x) {
    float t = clamp_f01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static Color shade_rgb(float r, float g, float b, float mul, unsigned char a) {
    return (Color){clamp_u8(r * mul), clamp_u8(g * mul), clamp_u8(b * mul), a};
}

/* Matriz Bayer 4x4 pre-escalada retornando limiar em [-0.18, +0.18] para dithering ASCII */
static inline float bayer4x4(int x, int y) {
    static const float BAYER_OFF[4][4] = {
        { -0.180000f,  0.012000f, -0.132000f,  0.060000f },
        {  0.108000f, -0.084000f,  0.156000f, -0.036000f },
        { -0.108000f,  0.084000f, -0.156000f,  0.036000f },
        {  0.180000f, -0.012000f,  0.132000f, -0.060000f }
    };
    return BAYER_OFF[y & 3][x & 3];
}

/* Botao tatico com chanfro duplo e realce energetico */
static int draw_chamfer_button(int x, int y, int w, int h, int bevel,
                               const char *label, int font_size,
                               int selected, int *hovered_out) {
    Vector2 m = GetMousePosition();
    Rectangle rect = {(float)x, (float)y, (float)w, (float)h};
    int hover = CheckCollisionPointRec(m, rect);
    if (hovered_out) {
        *hovered_out = hover;
    }

    int active = selected || hover;
    Color fill = active ? (Color){196, 62, 22, 238} : (Color){12, 16, 24, 215};
    Color border = active ? (Color){255, 185, 70, 255} : (Color){62, 74, 92, 225};
    Color accent = active ? (Color){255, 225, 120, 255} : (Color){175, 65, 30, 210};
    Color text_col = active ? (Color){255, 252, 240, 255} : (Color){210, 218, 228, 255};

    DrawRectangle(x, y, w, h - bevel, fill);
    for (int row = 0; row < bevel; row++) {
        int cur_w = w - row - 1;
        if (cur_w > 0) {
            DrawRectangle(x, y + h - bevel + row, cur_w, 1, fill);
        }
    }

    if (active) {
        DrawRectangle(x + 8, y + 2, w - 16, 2, (Color){255, 195, 95, 175});
    }

    DrawRectangle(x, y, 6, h, accent);

    DrawLine(x, y, x + w, y, border);
    DrawLine(x, y, x, y + h, border);
    DrawLine(x + w, y, x + w, y + h - bevel, border);
    DrawLine(x + w, y + h - bevel, x + w - bevel, y + h, border);
    DrawLine(x, y + h, x + w - bevel, y + h, border);

    int text_y = y + (h - font_size) / 2;
    if (active) {
        DrawText(">>", x + 16, text_y, font_size, (Color){255, 225, 125, 255});
        DrawText(label, x + 46, text_y, font_size, text_col);
    } else {
        DrawText(label, x + 22, text_y, font_size, text_col);
    }

    return (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
}

/* Botao retangular simples para submenus, setas e acoes rapidas */
static int draw_small_button(int x, int y, int w, int h, const char *label,
                             int font_size, Color base_col, Color active_col) {
    Vector2 m = GetMousePosition();
    Rectangle r = {(float)x, (float)y, (float)w, (float)h};
    int hover = CheckCollisionPointRec(m, r);

    Color fill = hover ? active_col : base_col;
    Color border = hover ? (Color){255, 200, 80, 255} : (Color){82, 98, 115, 255};
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 1.5f, border);

    int tw = MeasureText(label, font_size);
    DrawText(label, x + (w - tw) / 2, y + (h - font_size) / 2, font_size,
             hover ? WHITE : (Color){225, 230, 235, 255});

    return (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
}

enum { SHOWCASE_GW = 280, SHOWCASE_GH = 340 };

typedef struct {
    int gw;
    int gh;
    float cw;
    float ch;
    float total_w;
    float total_h;
    int cx_px;
    int x0;
    int y0;
    float glyph_sz;
} ShowcaseLayout;

/* Calcula grade e posicao do boneco showcase a direita da tela */
static ShowcaseLayout calc_showcase_layout(int sw, int sh, float sc, int page, int quality) {
    ShowcaseLayout L;
    float dscale = calc_effective_density_scale(quality, sw, sh);
    float grid_f = (dscale > 1.0f) ? 1.0f : dscale;
    int solid = (renderer_get_visual_style() == ASCII_STYLE_SOLID);
    int max_gw = solid ? SHOWCASE_GW : 210;
    int max_gh = solid ? SHOWCASE_GH : 260;

    L.gw = (int)roundf((float)max_gw * grid_f);
    L.gh = (int)roundf((float)max_gh * grid_f);
    if (L.gw < (max_gw / 2)) L.gw = max_gw / 2;
    if (L.gh < (max_gh / 2)) L.gh = max_gh / 2;
    if (L.gw > max_gw) L.gw = max_gw;
    if (L.gh > max_gh) L.gh = max_gh;

    L.total_h = fminf((float)sh * 0.76f, 540.0f * sc);
    if (L.total_h < 220.0f) L.total_h = 220.0f;
    L.ch = L.total_h / (float)L.gh;
    L.cw = L.ch * 0.92f;
    L.total_w = (float)L.gw * L.cw;

    float center_frac = (page == MENU_PAGE_MAIN) ? 0.70f : 0.75f;
    L.cx_px = (int)((float)sw * center_frac);
    L.x0 = L.cx_px - (int)(L.total_w * 0.5f);
    if (L.x0 + (int)L.total_w > sw - 12) {
        L.x0 = sw - 12 - (int)L.total_w;
        L.cx_px = L.x0 + (int)(L.total_w * 0.5f);
    }
    L.y0 = (int)((float)sh * 0.44f - L.total_h * 0.5f);
    if (L.y0 < 14) L.y0 = 14;
    L.glyph_sz = L.ch * 2.15f;
    return L;
}

/* Avalia geometria 2.5D continua de qualquer personagem ou skin no espaco canonico gx in [-34..34], gy in [0..86].
 * Se for skin customizada, delega o corpo para player_appearance_material; senao desenha a armadura do Fuzileiro. */
static int showcase_character = PRESET_FUZILEIRO;
static CharacterAppearance showcase_appearance;
static char showcase_name[32] = "Fuzileiro";

static void eval_showcase_point_for(int visual_id, const CharacterAppearance *app,
                                    float gx, float gy, float breath,
                                    int *mat_out, float *nx_out, float *ny_out) {
    if (player_appearance_visual(visual_id, app).custom_sprite) {
        if (gy < 74.0f) {
            *mat_out = player_appearance_material(visual_id, app, gx / 28.0f, (gy - breath) / 74.0f, 0);
            *nx_out = gx / 40.0f;
            *ny_out = 0.0f;
            return;
        }
        /* Na base (>= 74.0), desenha apenas o pedestal holografico para nao vazar botas do Fuzileiro */
        int pmat = 0;
        float pnx = 0.0f, pny = 0.0f;
        float ped_dx = gx / 27.5f;
        float ped_dy = (gy - 79.2f) / 4.3f;
        float ped_d = sqrtf(ped_dx * ped_dx + ped_dy * ped_dy);
        float ped_dy_b = (gy - 80.7f) / 4.2f;
        float ped_d_b = sqrtf(ped_dx * ped_dx + ped_dy_b * ped_dy_b);
        if (ped_d <= 1.0f) {
            int neon_ring = (ped_d > 0.86f) || (fabsf(ped_d - 0.58f) < 0.045f);
            pmat = neon_ring ? 3 : 5;
            pnx = ped_dx * 0.48f;
            pny = ped_dy * 0.58f - 0.45f;
        } else if (ped_d_b <= 1.0f && gy >= 79.2f) {
            pmat = 5;
            pnx = ped_dx * 0.52f;
            pny = 0.35f;
        }
        *mat_out = pmat;
        *nx_out = pnx;
        *ny_out = pny;
        return;
    }
    float uy = gy - breath;
    float ax = fabsf(gx);

    int mat = 0; /* 0=vazio, 1=armadura, 2=titanio, 3=visor/neon, 4=escuro/arma, 5=plataforma */
    float nx = 0.0f;
    float ny = 0.0f;

    /* 1. Plataforma Holografica na base (gy: 74..85) */
    float ped_dx = gx / 27.5f;
    float ped_dy = (gy - 79.2f) / 4.3f;
    float ped_d = sqrtf(ped_dx * ped_dx + ped_dy * ped_dy);
    float ped_dy_b = (gy - 80.7f) / 4.2f;
    float ped_d_b = sqrtf(ped_dx * ped_dx + ped_dy_b * ped_dy_b);
    if (ped_d <= 1.0f) {
        int neon_ring = (ped_d > 0.86f) || (fabsf(ped_d - 0.58f) < 0.045f);
        mat = neon_ring ? 3 : 5;
        nx = ped_dx * 0.48f;
        ny = ped_dy * 0.58f - 0.45f;
    } else if (ped_d_b <= 1.0f && gy >= 79.2f) {
        mat = 5;
        nx = ped_dx * 0.52f;
        ny = 0.35f;
    }

    /* 2. Pernas e Botas Pesadas (gy: 44..78.2) */
    if (gy >= 44.0f && gy <= 78.2f) {
        float leg_sep = 5.2f + (gy - 44.0f) * 0.056f;
        float leg_hw;
        if (gy >= 71.0f) {
            leg_hw = 4.85f;
        } else if (gy < 56.5f) {
            leg_hw = 4.35f - (gy - 44.0f) * 0.03f;
        } else if (gy < 62.5f) {
            leg_hw = 4.15f;
        } else {
            leg_hw = 3.95f + (gy - 62.5f) * 0.04f;
        }

        float dl = gx + leg_sep;
        float dr = gx - leg_sep;
        float off_x = 0.0f;
        int hit_leg = 0;

        if (fabsf(dl) <= leg_hw) {
            off_x = dl / leg_hw;
            hit_leg = 1;
        } else if (fabsf(dr) <= leg_hw) {
            off_x = dr / leg_hw;
            hit_leg = 1;
        }

        if (hit_leg) {
            mat = 1;
            nx = off_x * 0.80f;
            ny = -0.15f;
            if (gy >= 56.8f && gy <= 62.5f && fabsf(off_x) < 0.76f) {
                mat = 2; /* Joelheiras blindadas */
                ny = (gy - 59.6f) * 0.14f;
            } else if (gy >= 74.2f) {
                mat = 4; /* Sola pesada das botas */
            } else if (gy >= 70.8f && fabsf(off_x) < 0.78f) {
                mat = 2; /* Biqueira metalica */
            } else if (gy > 62.8f && gy < 70.5f && fabsf(off_x) < 0.44f) {
                mat = 2; /* Placa frontal da caneleira */
            }
        }
    }

    /* 3. Torso, Cintura e Peitoral Blindado (uy: 17..46.8) */
    if (uy >= 17.0f && uy <= 46.8f) {
        float tw;
        if (uy < 30.0f) {
            tw = 13.4f - (uy - 17.0f) * 0.16f;
        } else if (uy < 40.0f) {
            tw = 11.32f - (uy - 30.0f) * 0.24f;
        } else if (uy <= 44.0f) {
            tw = 8.92f + (uy - 40.0f) * 0.22f;
        } else {
            tw = 9.8f - (uy - 44.0f) * 1.45f;
        }

        if (tw > 0.5f && ax <= tw) {
            float ox = gx / tw;
            mat = 1;
            nx = ox * 0.78f;
            ny = (uy - 26.5f) * 0.03f - 0.12f;

            /* Placas chanfradas do peitoral em titanio */
            if (uy >= 18.2f && uy <= 29.2f && fabsf(ox) > 0.09f && fabsf(ox) < 0.89f) {
                if (uy < 29.0f - (1.0f - fabsf(ox)) * 2.4f) {
                    mat = 2;
                    ny -= 0.10f;
                }
            }

            /* Reator / Nucleo energetico no peito */
            float core_dx = gx;
            float core_dy = uy - 24.8f;
            float core_d = sqrtf(core_dx * core_dx + core_dy * core_dy);
            if (core_d <= 2.65f) {
                mat = 3;
                nx = core_dx / 2.65f * 0.5f;
                ny = core_dy / 2.65f * 0.5f;
            } else if (core_d <= 3.55f) {
                mat = 2;
            }

            /* Abdomen segmentado continuo */
            if (uy >= 30.6f && uy <= 39.2f && fmodf(uy - 30.6f, 2.6f) < 0.72f) {
                mat = 4;
            }

            /* Cinto tatico e fivela iluminada */
            if (uy >= 39.6f && uy <= 43.2f) {
                if (ax <= 2.35f) {
                    mat = 3;
                } else if (ax <= 3.55f) {
                    mat = 2;
                } else {
                    mat = 4;
                }
            }
        }
    }

    /* 4. Ombreiras Unicas Abobadadas (peca unica 3D) e Bracos (uy: 17.0..49.0) */
    if (ax >= 10.5f && ax <= 20.8f && uy >= 16.8f && uy <= 27.6f) {
        float sx = (ax - 15.5f) / 5.1f;
        float sy = (uy - 22.8f) / 5.8f;
        if (sx * sx + sy * sy <= 1.0f) {
            mat = 1;
            nx = (gx >= 0.0f ? 1.0f : -1.0f) * sx * 0.86f;
            ny = sy * 0.72f - 0.16f;
        }
    }
    if (uy >= 26.5f && uy <= 49.0f) {
        float arm_cx = 15.8f - (uy - 28.5f) * 0.055f;
        float arm_hw = (uy < 40.5f) ? 3.25f : ((uy < 45.5f) ? 3.05f : 2.55f);
        if (fabsf(ax - arm_cx) <= arm_hw) {
            float dir = (gx >= 0.0f) ? 1.0f : -1.0f;
            if (uy >= 45.8f) {
                mat = 4;
            } else if (uy >= 39.8f) {
                mat = 2;
            } else if (uy >= 38.0f && uy < 39.8f) {
                mat = 4;
            } else {
                mat = 1;
            }
            nx = dir * ((ax - arm_cx) / arm_hw) * 0.85f;
            ny = -0.10f;
        }
    }

    /* 5. Rifle de Plasma na mao direita (gx: +14..+24.8, uy: 31.5..58.0) */
    if (gx >= 14.0f && gx <= 24.8f && uy >= 31.5f && uy <= 58.0f) {
        float gun_cx = 17.6f + (uy - 38.0f) * 0.16f;
        float gun_hw = (uy < 38.0f) ? 1.65f : ((uy < 45.0f) ? 2.35f : 1.55f);
        if (fabsf(gx - gun_cx) <= gun_hw) {
            if ((uy >= 46.5f && uy <= 50.2f) || uy >= 55.2f) {
                mat = 3;
            } else if (uy >= 34.5f && uy <= 41.2f && gx < gun_cx) {
                mat = 2;
            } else {
                mat = 4;
            }
            nx = (gx - gun_cx) / gun_hw * 0.80f;
            ny = 0.0f;
        }
    }

    /* 6. Pescoco e Capacete Tatico Abobadado (uy: 3.6..18.2) */
    if (uy >= 14.5f && uy <= 18.2f && ax <= 4.2f) {
        mat = 4;
        nx = gx / 4.2f * 0.72f;
        ny = 0.0f;
    }
    if (uy >= 3.6f && uy <= 16.0f) {
        float h_hw;
        if (uy < 7.6f) {
            float dome_t = (7.6f - uy) / 4.0f;
            h_hw = 7.7f * sqrtf(fmaxf(0.08f, 1.0f - dome_t * dome_t * 0.72f));
        } else if (uy < 12.2f) {
            h_hw = 7.7f;
        } else {
            h_hw = 7.7f - (uy - 12.2f) * 0.58f;
        }

        if (ax <= h_hw) {
            float hx = gx / h_hw;
            mat = 1;
            nx = hx * 0.82f;
            ny = (uy - 9.2f) / 6.2f * 0.65f;
            if (uy < 8.4f && ax <= 1.75f) {
                mat = 2; /* Crista central */
            } else if (uy >= 8.3f && uy <= 11.7f && ax <= 5.9f &&
                       uy >= 8.1f + ax * 0.24f) {
                mat = 3; /* Visor angular neon */
            } else if (uy > 12.2f && ax <= 3.9f) {
                mat = 4; /* Respirador */
            }
        }
    }

    *mat_out = mat;
    *nx_out = nx;
    *ny_out = ny;
}

static void eval_showcase_point(float gx, float gy, float breath,
                                int *mat_out, float *nx_out, float *ny_out) {
    eval_showcase_point_for(showcase_character, &showcase_appearance,
                            gx, gy, breath, mat_out, nx_out, ny_out);
}

/* Cenario cyber-industrial (pilares, brilho no horizonte, piso 3D e brasas) renderizado em 1 draw call na GPU */
static void draw_cyber_arena_backdrop(Font font, int sw, int sh, float sc, float t,
                                      int page, int quality) {
    (void)font;
    float dscale = calc_effective_density_scale(quality, sw, sh);
    int solid = (renderer_get_visual_style() == ASCII_STYLE_SOLID);
    int bg_gw = (int)roundf((solid ? 480.0f : 320.0f) * dscale);
    int bg_gh = (int)roundf((solid ? 240.0f : 160.0f) * dscale);
    if (solid) {
        if (bg_gw < 220) bg_gw = 220;
        if (bg_gh < 110) bg_gh = 110;
        if (bg_gw > 560) bg_gw = 560;
        if (bg_gh > 280) bg_gh = 280;
    } else {
        if (bg_gw < 140) bg_gw = 140;
        if (bg_gh < 70)  bg_gh = 70;
        if (bg_gw > 384) bg_gw = 384;
        if (bg_gh > 192) bg_gh = 192;
    }

    static AsciiCell bg_buf[288 * 576];
    static int bg_c_gw = -1, bg_c_gh = -1, bg_c_page = -1, bg_c_sw = -1, bg_c_sh = -1;
    static int bg_c_solid = -1, bg_c_char = -1;
    static CharacterAppearance bg_c_app = {0};
    static float bg_c_t = -10.0f;

    ClearBackground((Color){5, 7, 11, 255});

    /* Reaproveita o buffer em cache (~30 Hz) se janela, pagina, estilo e boneco nao mudaram */
    int bg_dirty = (bg_c_gw != bg_gw || bg_c_gh != bg_gh || bg_c_page != page ||
                    bg_c_sw != sw || bg_c_sh != sh || bg_c_solid != solid ||
                    fabsf(t - bg_c_t) >= 0.033f || bg_c_t < 0.0f);
    if (!bg_dirty && page != MENU_PAGE_LOBBY) {
        if (bg_c_char != showcase_character ||
            memcmp(&bg_c_app, &showcase_appearance, sizeof(bg_c_app)) != 0) {
            bg_dirty = 1;
        }
    }
    if (!bg_dirty) {
        renderer_draw_ascii_buffer(bg_buf, bg_gw, bg_gh, bg_gw, 0.0f, 0.0f, (float)sw, (float)sh);
        return;
    }

    static const char *SOLID_BG_RAMP = "=+*xX#%8&BWM@"; /* 13 caracteres de alta cobertura */
    static const char *GLOW_RAMP     = ":-~=+*#%8&WM@"; /* 13 niveis para nevoa do horizonte */
    static const float PILLAR_X[7] = {0.08f, 0.22f, 0.36f, 0.50f, 0.64f, 0.78f, 0.92f};

    ShowcaseLayout L = calc_showcase_layout(sw, sh, sc, page, quality);
    float breath = sinf(t * 2.3f) * 0.60f;

    float cw = (float)sw / (float)bg_gw;
    float ch = (float)sh / (float)bg_gh;
    const float horizon = 0.62f;

    /* Arrays pre-calculados por coluna (eliminam buscas em 7 pilares, expf e sqrtf por celula) */
    float col_xn[576];
    float col_px_c[576];
    float col_left_atten[576];
    float col_h_focus[576];
    float col_glow_x_mod[576];
    float col_ped_glow_x[576];
    int   col_pil_idx[576];
    float col_pil_u[576];
    float col_pil_cyl[576];

    for (int c = 0; c < bg_gw; c++) {
        float px_c = ((float)c + 0.5f) * cw;
        float xn = ((float)c + 0.5f) / (float)bg_gw;
        float left_atten = 0.46f + 0.54f * smoothstep_f(0.06f, 0.45f, xn);
        float ped_dx = (xn - 0.70f) / 0.21f;

        col_px_c[c] = px_c;
        col_xn[c] = xn;
        col_left_atten[c] = left_atten;
        col_h_focus[c] = 0.65f + 0.45f * expf(-fabsf(xn - 0.70f) * 3.8f);
        col_glow_x_mod[c] = 0.64f + 0.14f * sinf(xn * 6.5f + t * 0.45f) +
                            0.32f * expf(-fabsf(xn - 0.70f) * 3.0f);
        col_ped_glow_x[c] = expf(-(ped_dx * ped_dx) * 1.35f) * 0.62f;

        int pil_idx = -1;
        float pil_u = 0.0f;
        float pil_cyl = 0.0f;
        for (int i = 0; i < 7; i++) {
            float pw = 0.015f + 0.004f * fabsf((float)i - 3.0f);
            float dist_p = fabsf(xn - PILLAR_X[i]);
            if (dist_p <= pw) {
                pil_idx = i;
                pil_u = (xn - PILLAR_X[i]) / pw;
                pil_cyl = sqrtf(fmaxf(0.10f, 1.0f - pil_u * pil_u));
                break;
            }
        }
        col_pil_idx[c] = pil_idx;
        col_pil_u[c] = pil_u;
        col_pil_cyl[c] = pil_cyl;
    }

    const float L_x1 = (float)L.x0 + L.total_w;
    const float L_y1 = (float)L.y0 + L.total_h;
    const float inv_L_w = 1.0f / L.total_w;
    const float inv_L_h = 1.0f / L.total_h;
    const int t_scroll = (int)(t * 17.0f);
    const AsciiColor bg_zero = {0, 0, 0};

    for (int r = 0; r < bg_gh; r++) {
        float py_c = ((float)r + 0.5f) * ch;
        float yn = ((float)r + 0.5f) / (float)bg_gh;
        int in_showcase_y = (page != MENU_PAGE_LOBBY && py_c >= (float)L.y0 && py_c <= L_y1);
        float sy = in_showcase_y ? ((py_c - (float)L.y0) * inv_L_h) * 86.0f : 0.0f;

        /* Linha brilhante do horizonte, pulando celulas cobertas pelo personagem showcase */
        if (fabsf(yn - horizon) < 0.0080f) {
            for (int c = 0; c < bg_gw; c++) {
                int cell_idx = r * bg_gw + c;
                bg_buf[cell_idx].ch = ' ';
                if (in_showcase_y && col_px_c[c] >= (float)L.x0 && col_px_c[c] <= L_x1) {
                    float sx = ((col_px_c[c] - (float)L.x0) * inv_L_w - 0.5f) * 68.0f;
                    int s_mat = 0; float s_nx = 0.0f, s_ny = 0.0f;
                    eval_showcase_point(sx, sy, breath, &s_mat, &s_nx, &s_ny);
                    if (s_mat != 0) continue;
                }
                float h_lum = clamp_f01(col_h_focus[c] * (0.72f + 0.28f * col_left_atten[c]));
                static const char *H_CH = "=#@W";
                bg_buf[cell_idx] = (AsciiCell){
                    H_CH[(c + r) & 3],
                    {clamp_u8(255.0f * h_lum), clamp_u8(146.0f * h_lum), clamp_u8(54.0f * h_lum)},
                    bg_zero
                };
            }
            continue;
        }

        /* Metade superior: pilares industriais com faixa neon pulsante, brasas subindo e nevoa */
        if (yn < horizon) {
            float dy_h = horizon - yn;
            float row_glow_y = expf(-dy_h * 11.2f);
            float rib_mod = fmodf(yn + 0.02f, 0.09f);
            int is_rib = (rib_mod < 0.012f);
            int neon_y = (yn >= 0.12f && yn <= 0.60f);

            for (int c = 0; c < bg_gw; c++) {
                int cell_idx = r * bg_gw + c;
                bg_buf[cell_idx].ch = ' ';

                if (in_showcase_y && col_px_c[c] >= (float)L.x0 && col_px_c[c] <= L_x1) {
                    float sx = ((col_px_c[c] - (float)L.x0) * inv_L_w - 0.5f) * 68.0f;
                    int s_mat = 0; float s_nx = 0.0f, s_ny = 0.0f;
                    eval_showcase_point(sx, sy, breath, &s_mat, &s_nx, &s_ny);
                    if (s_mat != 0) continue;
                }

                float left_atten = col_left_atten[c];
                float dither = bayer4x4(c, r);
                float glow = row_glow_y * col_glow_x_mod[c];

                if (col_pil_idx[c] >= 0) {
                    float pil_u = col_pil_u[c];
                    int is_neon = (neon_y && fabsf(pil_u) < 0.16f);
                    if (is_neon) {
                        float pulse = 0.70f + 0.30f * sinf(t * 2.4f - yn * 11.0f + (float)col_pil_idx[c] * 1.15f);
                        float nlum = clamp_f01(pulse * (0.48f + 0.52f * left_atten));
                        static const char *N_CH = "@W#%=";
                        int ni = (int)(clamp_f01(1.0f - pulse + fabsf(pil_u) * 2.5f) * 4.5f);
                        bg_buf[cell_idx] = (AsciiCell){
                            N_CH[ni],
                            {clamp_u8(255.0f * nlum), clamp_u8(138.0f * nlum), clamp_u8(44.0f * nlum)},
                            bg_zero
                        };
                    } else {
                        int edge = (fabsf(pil_u) > 0.78f);
                        float illum = (0.20f + 0.28f * col_pil_cyl[c] + glow * 0.42f + (is_rib ? 0.18f : 0.0f)) * left_atten;
                        char gch;
                        if (edge) {
                            gch = (pil_u < 0.0f) ? '[' : ']';
                        } else if (is_rib) {
                            gch = '=';
                        } else {
                            int idx = (int)(clamp_f01(illum + dither * 0.55f) * 12.9f);
                            if (idx < 0) idx = 0;
                            if (idx > 12) idx = 12;
                            gch = SOLID_BG_RAMP[idx];
                        }
                        bg_buf[cell_idx] = (AsciiCell){
                            gch,
                            {clamp_u8((44.0f + glow * 160.0f) * illum * 1.40f),
                             clamp_u8((50.0f + glow * 76.0f) * illum * 1.35f),
                             clamp_u8((66.0f + glow * 28.0f) * illum * 1.35f)},
                            bg_zero
                        };
                    }
                    continue;
                }

                int scroll_r = r + t_scroll + ((c * 19) % 7);
                unsigned h = (unsigned)(c * 73856093u ^ scroll_r * 19349663u);
                if ((h % 263u) == 0u && yn > 0.08f && left_atten > 0.55f) {
                    static const char *SPARK_CH = "*+#%";
                    float sp_lum = (0.68f + 0.32f * sinf(t * 4.0f + (float)c)) * left_atten;
                    bg_buf[cell_idx] = (AsciiCell){
                        SPARK_CH[h & 3u],
                        {clamp_u8(255.0f * sp_lum), clamp_u8(168.0f * sp_lum), clamp_u8(68.0f * sp_lum)},
                        bg_zero
                    };
                    continue;
                }

                float g_lum = glow * left_atten;
                if (g_lum + dither * 0.35f > 0.11f) {
                    int idx = (int)(clamp_f01(g_lum + dither * 0.7f) * 12.9f);
                    if (idx < 0) idx = 0;
                    if (idx > 12) idx = 12;
                    bg_buf[cell_idx] = (AsciiCell){
                        GLOW_RAMP[idx],
                        {clamp_u8(232.0f * g_lum), clamp_u8(98.0f * g_lum), clamp_u8(40.0f * g_lum)},
                        bg_zero
                    };
                }
            }
            continue;
        }

        /* Metade inferior (chao perspectiva): termos de Z pre-calculados para toda a linha */
        float dz = yn - horizon + 0.008f;
        float z_world = 0.32f / dz;
        float dist_fade = expf(-z_world * 0.14f);
        float z065 = z_world * 0.65f + 0.5f;
        float fz = fabsf((z065 - (float)(int)z065) - 0.5f);
        float radial_w = 0.0088f * z_world * 1.15f;
        if (radial_w < 0.018f) radial_w = 0.018f;
        float radial_lim = radial_w * 0.90f;
        float trans_w = 0.018f * z_world * z_world * 0.65f;
        if (trans_w < 0.025f) trans_w = 0.025f;
        int on_trans = (fz < trans_w * 0.78f);

        float ped_dy = (yn - 0.80f) / 0.13f;
        float row_ped_y = expf(-(ped_dy * ped_dy) * 1.35f);
        float hor_glow = expf(-(yn - horizon) * 16.0f) * 0.45f;
        float pulse_ring = 0.86f + 0.14f * sinf(z_world * 1.8f - t * 2.6f);
        float cos_zw = cosf(z_world * 4.2f);
        float xw_scale = z_world * 3.1f;

        for (int c = 0; c < bg_gw; c++) {
            int cell_idx = r * bg_gw + c;
            bg_buf[cell_idx].ch = ' ';

            if (in_showcase_y && col_px_c[c] >= (float)L.x0 && col_px_c[c] <= L_x1) {
                float sx = ((col_px_c[c] - (float)L.x0) * inv_L_w - 0.5f) * 68.0f;
                int s_mat = 0; float s_nx = 0.0f, s_ny = 0.0f;
                eval_showcase_point(sx, sy, breath, &s_mat, &s_nx, &s_ny);
                if (s_mat != 0) continue;
            }

            float xn = col_xn[c];
            float left_atten = col_left_atten[c];
            float dither = bayer4x4(c, r);
            float x_world = (xn - 0.56f) * xw_scale;
            float axw = fabsf(x_world * 1.15f) + 0.5f;
            float fx = fabsf((axw - (float)(int)axw) - 0.5f);
            int on_radial = (fx < radial_lim);
            float ped_glow = col_ped_glow_x[c] * row_ped_y;

            if (on_radial || on_trans) {
                float line_lum = clamp_f01((0.48f + 0.52f * dist_fade + ped_glow * 0.45f) * pulse_ring * left_atten);
                if (line_lum < 0.06f) continue;

                char lch;
                if (on_radial && on_trans) {
                    lch = (line_lum > 0.72f) ? '@' : '#';
                } else if (on_radial) {
                    if (xn < 0.545f) lch = '/';
                    else if (xn > 0.575f) lch = '\\';
                    else lch = '|';
                } else {
                    lch = (line_lum > 0.68f) ? '#' : '=';
                }

                bg_buf[cell_idx] = (AsciiCell){
                    lch,
                    {clamp_u8(250.0f * line_lum), clamp_u8(126.0f * line_lum), clamp_u8(48.0f * line_lum)},
                    bg_zero
                };
            } else {
                float tile_n = 0.22f + 0.09f * sinf(x_world * 4.2f) * cos_zw;
                float plate_lum = (dist_fade * tile_n + ped_glow + hor_glow) * left_atten;
                if (plate_lum < 0.025f) continue;

                int idx = (int)(clamp_f01(plate_lum * 1.15f + dither * 0.6f) * 12.9f);
                if (idx < 0) idx = 0;
                if (idx > 12) idx = 12;

                float col_scale = 0.24f + plate_lum * 1.05f;
                bg_buf[cell_idx] = (AsciiCell){
                    SOLID_BG_RAMP[idx],
                    {clamp_u8((115.0f + ped_glow * 140.0f) * col_scale),
                     clamp_u8((66.0f + ped_glow * 74.0f) * col_scale),
                     clamp_u8((44.0f + dist_fade * 36.0f) * col_scale)},
                    bg_zero
                };
            }
        }
    }

    bg_c_gw = bg_gw; bg_c_gh = bg_gh; bg_c_page = page;
    bg_c_sw = sw; bg_c_sh = sh; bg_c_solid = solid;
    bg_c_char = showcase_character;
    bg_c_app = showcase_appearance;
    bg_c_t = t;
    renderer_draw_ascii_buffer(bg_buf, bg_gw, bg_gh, bg_gw, 0.0f, 0.0f, (float)sw, (float)sh);
}

/* Mapa de bits 5x7 usado para montar as letras do logo SECTOR 404 */
static const char *glyph_rows_5x7(char ch, int row) {
    static const char *A[7] = {".###.","##.##","##.##","#####","##.##","##.##","##.##"};
    static const char *S[7] = {".####","##...","##...",".###.","...##","...##","####."};
    static const char *C[7] = {".####","##...","##...","##...","##...","##...",".####"};
    static const char *E[7] = {"#####","##...","##...","####.","##...","##...","#####"};
    static const char *I[7] = {"#####","..#..","..#..","..#..","..#..","..#..","#####"};
    static const char *D[7] = {"####.","##.##","##.##","##.##","##.##","##.##","####."};
    static const char *O[7] = {".###.","##.##","##.##","##.##","##.##","##.##",".###."};
    static const char *M[7] = {"##.##","#####","#####","##.##","##.##","##.##","##.##"};
    static const char *R[7] = {"####.","##.##","##.##","####.","##.#.","##.##","##.##"};
    static const char *T[7] = {"#####","..#..","..#..","..#..","..#..","..#..","..#.."};
    static const char *N0[7]= {".###.","##.##","##.##","##.##","##.##","##.##",".###."};
    static const char *N4[7]= {"##.##","##.##","##.##","#####","...##","...##","...##"};
    if (row < 0 || row >= 7) return ".....";
    switch (ch) {
        case 'A': return A[row];
        case 'S': return S[row];
        case 'C': return C[row];
        case 'E': return E[row];
        case 'I': return I[row];
        case 'D': return D[row];
        case 'O': return O[row];
        case 'M': return M[row];
        case 'R': return R[row];
        case 'T': return T[row];
        case '0': return N0[row];
        case '4': return N4[row];
        default:  return ".....";
    }
}

/* Desenha uma palavra em micro-malha ASCII continua em 1 draw call na GPU (ZERO DrawRectangle) */
static void draw_dense_ascii_word(Font font, const char *word, int x, int y,
                                  float cell, int sub_div, Color top_col, Color bot_col,
                                  float t) {
    static const char *LOGO_RAMP = "=+*x#%8&WM@"; /* 11 niveis de alta cobertura */
    enum { LOGO_BUF_W = 256, LOGO_BUF_H = 64 };

    int len = (int)strlen(word);
    if (len <= 0 || sub_div <= 0) return;

    int char_cols = 5 * sub_div;
    int gap_cols = sub_div;
    int total_rows = 7 * sub_div;
    int grid_w = len * (char_cols + gap_cols) - gap_cols + 2;
    int grid_h = total_rows + 2;

    if (grid_w <= LOGO_BUF_W && grid_h <= LOGO_BUF_H) {
        (void)font;
        /* Cache separado para as duas palavras do logo (SECTOR no slot 0, 404 no slot 1) */
        static AsciiCell word_cache[2][LOGO_BUF_H * LOGO_BUF_W];
        static char wc_word[2][16] = {"", ""};
        static int wc_sub[2] = {-1, -1};
        static int wc_gw[2] = {-1, -1};
        static int wc_gh[2] = {-1, -1};
        static int wc_style[2] = {-1, -1};
        static float wc_t[2] = {-10.0f, -10.0f};
        int slot = (word[0] == '4' || word[0] == 'D') ? 1 : 0;
        int cur_style = renderer_get_visual_style();
        AsciiCell *wbuf = word_cache[slot];

        int w_dirty = (wc_sub[slot] != sub_div || wc_gw[slot] != grid_w ||
                       wc_gh[slot] != grid_h || wc_style[slot] != cur_style ||
                       strncmp(wc_word[slot], word, 15) != 0 ||
                       fabsf(t - wc_t[slot]) >= 0.033f || wc_t[slot] < 0.0f);
        if (!w_dirty) {
            renderer_draw_ascii_buffer(wbuf, grid_w, grid_h, grid_w,
                                       (float)x, (float)y,
                                       (float)grid_w * cell, (float)grid_h * cell);
            return;
        }

        const AsciiColor bg_zero = {0, 0, 0};
        const AsciiColor sh_col = {8, 10, 16};
        int total_cells = grid_w * grid_h;
        for (int i = 0; i < total_cells; i++) {
            wbuf[i].ch = ' ';
            wbuf[i].fg = bg_zero;
            wbuf[i].bg = bg_zero;
        }

        /* Passada 1: sombra discreta apenas em ASCII puro no buffer GPU */
        for (int idx = 0; idx < len; idx++) {
            char ch = word[idx];
            int mc_base = idx * (char_cols + gap_cols);
            for (int br = 0; br < 7; br++) {
                const char *pattern = glyph_rows_5x7(ch, br);
                for (int bc = 0; bc < 5; bc++) {
                    if (pattern[bc] != '#') continue;
                    int empty_right = (bc == 4 || pattern[bc + 1] != '#');
                    int empty_bot   = (br == 6 || glyph_rows_5x7(ch, br + 1)[bc] != '#');
                    if (!empty_right && !empty_bot) continue;

                    for (int sr = 0; sr < sub_div; sr++) {
                        int mr = br * sub_div + sr;
                        for (int sc_i = 0; sc_i < sub_div; sc_i++) {
                            int is_r = (empty_right && sc_i == sub_div - 1);
                            int is_b = (empty_bot && sr == sub_div - 1);
                            if (!is_r && !is_b) continue;
                            int mc = mc_base + bc * sub_div + sc_i;
                            wbuf[(mr + 1) * grid_w + (mc + 1)] = (AsciiCell){':', sh_col, bg_zero};
                        }
                    }
                }
            }
        }

        /* Passada 2: preenchimento ultra-denso continuo e degrade vertical no buffer GPU */
        for (int idx = 0; idx < len; idx++) {
            char ch = word[idx];
            int mc_base = idx * (char_cols + gap_cols);
            for (int br = 0; br < 7; br++) {
                const char *pattern = glyph_rows_5x7(ch, br);
                for (int bc = 0; bc < 5; bc++) {
                    if (pattern[bc] != '#') continue;

                    int empty_left  = (bc == 0 || pattern[bc - 1] != '#');
                    int empty_right = (bc == 4 || pattern[bc + 1] != '#');
                    int empty_top   = (br == 0 || glyph_rows_5x7(ch, br - 1)[bc] != '#');
                    int empty_bot   = (br == 6 || glyph_rows_5x7(ch, br + 1)[bc] != '#');

                    for (int sr = 0; sr < sub_div; sr++) {
                        int mr = br * sub_div + sr;
                        float v = (total_rows > 1) ? ((float)mr / (float)(total_rows - 1)) : 0.0f;
                        float wave = 0.14f * sinf(t * 2.8f - v * 4.5f + (float)idx * 0.7f);
                        float cr = (float)top_col.r * (1.0f - v) + (float)bot_col.r * v;
                        float cg = (float)top_col.g * (1.0f - v) + (float)bot_col.g * v;
                        float cb = (float)top_col.b * (1.0f - v) + (float)bot_col.b * v;

                        for (int sc_i = 0; sc_i < sub_div; sc_i++) {
                            int mc = mc_base + bc * sub_div + sc_i;
                            int is_l = (empty_left && sc_i == 0);
                            int is_r = (empty_right && sc_i == sub_div - 1);
                            int is_t = (empty_top && sr == 0);
                            int is_b = (empty_bot && sr == sub_div - 1);

                            char gch;
                            float boost = 1.0f + wave;
                            if (is_l && !is_t && !is_b) {
                                gch = (sub_div > 1) ? '[' : '|';
                                boost = 1.18f;
                            } else if (is_r && !is_t && !is_b) {
                                gch = (sub_div > 1) ? ']' : '|';
                                boost = 1.12f;
                            } else if (is_t) {
                                gch = '=';
                                boost = 1.24f;
                            } else if (is_b) {
                                gch = '_';
                                boost = 0.92f;
                            } else {
                                float dens = (0.85f - v * 0.35f) + wave + bayer4x4(mc + idx * 3, mr) * 0.75f;
                                int li = (int)(clamp_f01(dens) * 10.9f);
                                if (li < 0) li = 0;
                                if (li > 10) li = 10;
                                gch = LOGO_RAMP[li];
                            }

                            wbuf[mr * grid_w + mc] = (AsciiCell){
                                gch,
                                {clamp_u8(cr * boost), clamp_u8(cg * boost), clamp_u8(cb * boost)},
                                bg_zero
                            };
                        }
                    }
                }
            }
        }

        snprintf(wc_word[slot], sizeof(wc_word[slot]), "%.15s", word);
        wc_sub[slot] = sub_div; wc_gw[slot] = grid_w; wc_gh[slot] = grid_h;
        wc_style[slot] = cur_style; wc_t[slot] = t;
        renderer_draw_ascii_buffer(wbuf, grid_w, grid_h, grid_w,
                                   (float)x, (float)y,
                                   (float)grid_w * cell, (float)grid_h * cell);
        return;
    }

    /* Fallback CPU se a palavra exceder os limites estaticos */
    float glyph_sz = cell * 2.65f;
    if (glyph_sz < 3.5f) glyph_sz = 3.5f;
    float sh_off = cell * 1.35f;
    for (int idx = 0; idx < len; idx++) {
        char ch = word[idx];
        float gx0 = (float)x + (float)(idx * (char_cols + gap_cols)) * cell;
        for (int br = 0; br < 7; br++) {
            const char *pattern = glyph_rows_5x7(ch, br);
            for (int bc = 0; bc < 5; bc++) {
                if (pattern[bc] != '#') continue;
                int empty_right = (bc == 4 || pattern[bc + 1] != '#');
                int empty_bot   = (br == 6 || glyph_rows_5x7(ch, br + 1)[bc] != '#');
                for (int sr = 0; sr < sub_div; sr++) {
                    int mr = br * sub_div + sr;
                    float v = (total_rows > 1) ? ((float)mr / (float)(total_rows - 1)) : 0.0f;
                    float cr = (float)top_col.r * (1.0f - v) + (float)bot_col.r * v;
                    float cg = (float)top_col.g * (1.0f - v) + (float)bot_col.g * v;
                    float cb = (float)top_col.b * (1.0f - v) + (float)bot_col.b * v;
                    for (int sc_i = 0; sc_i < sub_div; sc_i++) {
                        int mc = bc * sub_div + sc_i;
                        float px = gx0 + (float)mc * cell;
                        float py = (float)y + (float)mr * cell;
                        if ((empty_right && sc_i == sub_div - 1) || (empty_bot && sr == sub_div - 1)) {
                            DrawTextCodepoint(font, ':', (Vector2){px + sh_off, py + sh_off}, glyph_sz,
                                              (Color){8, 10, 16, 225});
                        }
                        DrawTextCodepoint(font, '#', (Vector2){px, py}, glyph_sz, shade_rgb(cr, cg, cb, 1.0f, 255));
                    }
                }
            }
        }
    }
}

/* Logo em duas camadas em micro-malha ASCII dinamica (sub_div adaptado a densidade/janela) */
static int draw_dense_logo(Font font, int left_x, int top_y, float sc, float t,
                           int quality, int sw, int sh) {
    int eff_q = quality;
    if (eff_q == ASCII_QUALITY_AUTO) {
        eff_q = (sw < 1000 || sh < 560) ? ASCII_QUALITY_MEDIA : ASCII_QUALITY_ALTA;
    }
    int sub_sm = (eff_q <= ASCII_QUALITY_MEDIA) ? 2 : 3;
    int sub_bg = (eff_q == ASCII_QUALITY_LEVE)  ? 4 :
                 (eff_q == ASCII_QUALITY_MEDIA) ? 5 : 7;

    float sm_cell = (1.42f * (3.0f / (float)sub_sm)) * sc;
    draw_dense_ascii_word(font, "SECTOR", left_x, top_y, sm_cell, sub_sm,
                          (Color){232, 240, 252, 255}, (Color){140, 162, 195, 255}, t);

    int tag_x = left_x + (int)((35.0f * (float)sub_sm) * sm_cell) + (int)(12.0f * sc);
    int tag_sz = (int)(14.0f * sc);
    if (tag_sz < 10) tag_sz = 10;
    DrawText("404 NF", tag_x, top_y + (int)(2.0f * sc), tag_sz, (Color){255, 185, 80, 245});
    DrawText("ASCII ENGINE", tag_x, top_y + (int)(17.0f * sc), tag_sz - 1, (Color){175, 190, 212, 235});

    float big_cell = (1.92f * (7.0f / (float)sub_bg)) * sc;
    int title_y = top_y + (int)((7.0f * (float)sub_sm) * sm_cell) + (int)(10.0f * sc);
    draw_dense_ascii_word(font, "404", left_x, title_y, big_cell, sub_bg,
                          (Color){255, 220, 95, 255}, (Color){232, 54, 36, 255}, t);

    int sub_y = title_y + (int)((7.0f * (float)sub_bg) * big_cell) + (int)(10.0f * sc);
    int sub_sz = (int)(15.0f * sc);
    if (sub_sz < 11) sub_sz = 11;
    DrawText("SHOOTER 3D EM C  //  LAN & SINGLEPLAYER", left_x, sub_y,
             sub_sz, (Color){205, 218, 235, 245});

    return sub_y + sub_sz + (int)(24.0f * sc);
}

/* Helper compartilhado: amostra qualquer personagem/skin em uma grade ASCII fornecida */
static void build_character_ascii_quad(int visual_id, const CharacterAppearance *app,
                                       int gw, int gh, float t,
                                       float max_gy, float lum_floor,
                                       AsciiCell *out_buf) {
    static const char *SOLID_RAMP = "=+*xX#%8&BWM@"; /* 13 niveis densos de alta cobertura */
    static int mat_grid[SHOWCASE_GH][SHOWCASE_GW];
    static float nx_grid[SHOWCASE_GH][SHOWCASE_GW];
    static float ny_grid[SHOWCASE_GH][SHOWCASE_GW];

    if (gw < 8) gw = 8;
    if (gh < 8) gh = 8;
    if (gw > SHOWCASE_GW) gw = SHOWCASE_GW;
    if (gh > SHOWCASE_GH) gh = SHOWCASE_GH;

    float breath = sinf(t * 2.3f) * 0.60f;
    float scan_row = fmodf(t * 22.0f, 112.0f) - 12.0f;
    int is_custom_skin = player_appearance_visual(visual_id, app).custom_sprite;

    /* Passada 1: amostra geometria continua na grade para detectar contornos e juntas */
    for (int r = 0; r < gh; r++) {
        float gy = (((float)r + 0.5f) / (float)gh) * max_gy;
        for (int c = 0; c < gw; c++) {
            float gx = (((float)c + 0.5f) / (float)gw - 0.5f) * 68.0f;
            eval_showcase_point_for(visual_id, app, gx, gy, breath,
                                    &mat_grid[r][c], &nx_grid[r][c], &ny_grid[r][c]);
        }
    }

    /* Passada 2: preenche grade ASCII densa no buffer de saida */
    for (int r = 0; r < gh; r++) {
        float gy = (((float)r + 0.5f) / (float)gh) * max_gy;
        float scan = expf(-fabsf(gy - scan_row) * 0.45f) * 0.24f;

        for (int c = 0; c < gw; c++) {
            int buf_idx = r * gw + c;
            int mat = mat_grid[r][c];
            if (mat == 0) {
                out_buf[buf_idx].ch = ' ';
                continue;
            }

            /* Iluminacao direcional + especular + brilho de borda (rim) a partir da normal 2.5D */
            float nx = nx_grid[r][c];
            float ny = ny_grid[r][c];
            float nz = sqrtf(fmaxf(0.08f, 1.0f - nx * nx * 0.62f - ny * ny * 0.45f));
            float ndotl = fmaxf(0.26f, (-0.44f * nx - 0.50f * ny + 0.74f * nz));
            float s = (mat == 2 || mat == 3)
                ? fmaxf(0.0f, (-0.30f * nx - 0.42f * ny + 0.86f * nz))
                : 0.0f;
            float s2 = s * s;
            float s4 = s2 * s2;
            float spec = s4 * s4 * s2 * 0.36f;
            float inv_nz = fmaxf(0.0f, 1.0f - nz);
            float rim = inv_nz * inv_nz * 0.60f;

            float cr, cg, cb;
            if (mat == 1) {
                cr = 158.0f; cg = 184.0f; cb = 214.0f;
            } else if (mat == 2) {
                cr = 224.0f; cg = 236.0f; cb = 252.0f;
            } else if (mat == 3) {
                cr = 255.0f; cg = 192.0f; cb = 74.0f;
                ndotl = fmaxf(ndotl, 0.90f);
            } else if (mat == 5) {
                cr = 118.0f; cg = 142.0f; cb = 172.0f;
            } else {
                cr = 118.0f; cg = 136.0f; cb = 158.0f;
            }

            /* Skins customizadas usam a paleta do modulo player e saem direto sem sombreado metalico */
            if (is_custom_skin && mat != 5) {
                static const char *SKIN_RAMP = "#%8&BWM@";
                CharacterColor color = player_get_material_color(mat);
                cr = (float)color.r; cg = (float)color.g; cb = (float)color.b;
                float mat_lum = (0.299f * cr + 0.587f * cg + 0.114f * cb) / 255.0f;
                float ch_lum = clamp_f01(0.82f + 0.22f * nz);
                float gl_val = clamp_f01(mat_lum * 0.65f + 0.35f * nz + bayer4x4(c, r) * 0.22f);
                int r_idx = (int)(gl_val * 7.9f);
                if (r_idx < 0) r_idx = 0;
                if (r_idx > 7) r_idx = 7;
                out_buf[buf_idx] = (AsciiCell){
                    SKIN_RAMP[r_idx],
                    {clamp_u8(cr * ch_lum), clamp_u8(cg * ch_lum), clamp_u8(cb * ch_lum)},
                    {0, 0, 0}
                };
                continue;
            }
            float lum = ndotl * 1.08f + spec + rim * 0.44f + scan;

            int m_l = (c > 0) ? mat_grid[r][c - 1] : 0;
            int m_r = (c + 1 < gw) ? mat_grid[r][c + 1] : 0;
            int m_u = (r > 0) ? mat_grid[r - 1][c] : 0;
            int m_d = (r + 1 < gh) ? mat_grid[r + 1][c] : 0;

            int is_outer = (m_l == 0 || m_r == 0 || m_u == 0 || m_d == 0);
            int is_seam = (!is_outer && (m_l != mat || m_r != mat || m_u != mat || m_d != mat));

            char glyph;
            float ch_lum = fmaxf(lum, lum_floor);

            if (is_outer) {
                if (m_u == 0 || m_d == 0) {
                    glyph = (mat == 5) ? '~' : '_';
                } else if (m_l == 0) {
                    glyph = (ny < -0.25f) ? '/' : (ny > 0.25f ? '\\' : '[');
                } else {
                    glyph = (ny < -0.25f) ? '\\' : (ny > 0.25f ? '/' : ']');
                }
                ch_lum = fmaxf(ch_lum * 1.18f, 0.80f);
            } else if (mat == 5) {
                static const char *PAD_CH = "=#*+WM";
                glyph = PAD_CH[(c + r * 2) % 6];
            } else {
                float d_lum = lum + bayer4x4(c, r) * 0.65f;
                if (is_seam) {
                    d_lum = fmaxf(d_lum, 0.68f);
                }
                int r_idx = (int)(clamp_f01(d_lum) * 12.9f);
                if (r_idx < 0) r_idx = 0;
                if (r_idx > 12) r_idx = 12;
                glyph = is_seam ? ((m_u != mat || m_d != mat) ? '=' : '#') : SOLID_RAMP[r_idx];
            }

            float edge_tint = rim * ((mat == 3) ? 0.25f : 0.78f);
            out_buf[buf_idx] = (AsciiCell){
                glyph,
                {clamp_u8(cr * ch_lum + edge_tint * 92.0f),
                 clamp_u8(cg * ch_lum + edge_tint * 55.0f),
                 clamp_u8(cb * ch_lum + edge_tint * 25.0f)},
                {0, 0, 0}
            };
        }
    }
}

/* Preview compacto animado em ASCII (cacheado a 15 Hz por slot) usado no Lobby */
static void draw_lobby_character_preview(int box_x, int box_y, int box_w, int box_h,
                                         int visual_id, const CharacterAppearance *app,
                                         float t) {
    if (box_w < 24 || box_h < 24) {
        return;
    }
    enum { LB_GW = 148, LB_GH = 184, LB_SLOTS = 4 };
    static AsciiCell lb_bufs[LB_SLOTS][LB_GH * LB_GW];
    static int lb_box_x[LB_SLOTS] = {-1, -1, -1, -1};
    static int lb_vid[LB_SLOTS] = {-1, -1, -1, -1};
    static CharacterAppearance lb_app[LB_SLOTS] = {0};
    static int lb_style[LB_SLOTS] = {-1, -1, -1, -1};
    static float lb_t[LB_SLOTS] = {-10.0f, -10.0f, -10.0f, -10.0f};
    static int lb_rr = 0;

    int slot = -1;
    for (int i = 0; i < LB_SLOTS; i++) {
        if (lb_box_x[i] == box_x) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < LB_SLOTS; i++) {
            if (lb_box_x[i] < 0) { slot = i; break; }
        }
        if (slot < 0) { slot = lb_rr; lb_rr = (lb_rr + 1) % LB_SLOTS; }
    }

    int cur_style = renderer_get_visual_style();
    CharacterAppearance zero_app = {0};
    const CharacterAppearance *eff_app = app ? app : &zero_app;
    int dirty = (lb_box_x[slot] != box_x || lb_vid[slot] != visual_id ||
                 lb_style[slot] != cur_style ||
                 memcmp(&lb_app[slot], eff_app, sizeof(*eff_app)) != 0 ||
                 fabsf(t - lb_t[slot]) >= 0.066f || lb_t[slot] < 0.0f);

    if (dirty) {
        build_character_ascii_quad(visual_id, eff_app, LB_GW, LB_GH,
                                   t, 74.0f, 0.38f, lb_bufs[slot]);
        lb_box_x[slot] = box_x;
        lb_vid[slot] = visual_id;
        lb_app[slot] = *eff_app;
        lb_style[slot] = cur_style;
        lb_t[slot] = t;
    }

    float target_ratio = (float)LB_GW / (float)LB_GH;
    float draw_h = (float)box_h;
    float draw_w = draw_h * target_ratio;
    if (draw_w > (float)box_w) {
        draw_w = (float)box_w;
        draw_h = draw_w / target_ratio;
    }
    float draw_x = (float)box_x + ((float)box_w - draw_w) * 0.5f;
    float draw_y = (float)box_y + ((float)box_h - draw_h) * 0.5f;

    renderer_draw_ascii_buffer(lb_bufs[slot], LB_GW, LB_GH, LB_GW,
                               draw_x, draw_y, draw_w, draw_h);
}

/* Showcase do personagem em micro-ASCII (cacheado a 30 Hz) em 1 draw call na GPU */
static void draw_dense_character_showcase(Font font, int sw, int sh, float sc, float t,
                                          int page, int quality) {
    (void)font;
    static AsciiCell sc_buf[SHOWCASE_GH * SHOWCASE_GW];
    static int sc_gw = -1, sc_gh = -1, sc_vid = -1, sc_style = -1;
    static CharacterAppearance sc_app = {0};
    static float sc_t = -10.0f;

    ShowcaseLayout L = calc_showcase_layout(sw, sh, sc, page, quality);
    int cur_style = renderer_get_visual_style();
    int dirty = (sc_gw != L.gw || sc_gh != L.gh || sc_vid != showcase_character ||
                 sc_style != cur_style ||
                 memcmp(&sc_app, &showcase_appearance, sizeof(sc_app)) != 0 ||
                 fabsf(t - sc_t) >= 0.033f || sc_t < 0.0f);
    if (dirty) {
        build_character_ascii_quad(showcase_character, &showcase_appearance,
                                   L.gw, L.gh, t, 86.0f, 0.34f, sc_buf);
        sc_gw = L.gw; sc_gh = L.gh;
        sc_vid = showcase_character;
        sc_app = showcase_appearance;
        sc_style = cur_style;
        sc_t = t;
    }

    renderer_draw_ascii_buffer(sc_buf, L.gw, L.gh, L.gw,
                               (float)L.x0, (float)L.y0, L.total_w, L.total_h);

    /* Etiqueta compacta acoplada logo abaixo da plataforma ASCII */
    int tag_y = L.y0 + (int)L.total_h - (int)(6.0f * sc);
    int tag_sz = (int)(15.0f * sc);
    if (tag_sz < 10) tag_sz = 10;
    const char *tag = TextFormat("[ %s ]", showcase_name[0] ? showcase_name : "NOVO PERSONAGEM");
    while (tag_sz > 9 && MeasureText(tag, tag_sz) + 24 > (sw / 2)) {
        tag_sz--;
    }
    int tw = MeasureText(tag, tag_sz);
    int pill_w = tw + (int)(22.0f * sc);
    int pill_h = tag_sz + (int)(10.0f * sc);
    int pill_x = L.cx_px - pill_w / 2;
    if (pill_x + pill_w > sw - 8) pill_x = sw - 8 - pill_w;
    if (pill_x < 8) pill_x = 8;

    DrawRectangle(pill_x, tag_y, pill_w, pill_h, (Color){10, 14, 22, 232});
    DrawRectangleLinesEx((Rectangle){(float)pill_x, (float)tag_y, (float)pill_w, (float)pill_h},
                         1.5f, (Color){230, 115, 45, 235});
    DrawText(tag, pill_x + (pill_w - tw) / 2, tag_y + (pill_h - tag_sz) / 2, tag_sz,
             (Color){255, 205, 105, 255});
}

/* --- Telas de menu, configuracoes e criador de personagem --- */

/* Diminui a fonte ate o texto caber dentro de max_w sem cortar */
static int fit_menu_font(const char *text, int base_sz, int min_sz, int max_w) {
    int sz = base_sz;
    if (!text || max_w <= 0) return min_sz;
    while (sz > min_sz && MeasureText(text, sz) > max_w) {
        sz--;
    }
    return sz;
}

/* Quebra o texto em ate max_lines linhas respeitando max_w e retorna a altura usada */
static int draw_wrapped_text(const char *text, int x, int y, int max_w,
                             int font_sz, int line_gap, int max_lines, Color col) {
    if (!text || text[0] == '\0' || max_lines <= 0 || max_w <= 0) {
        return 0;
    }
    const char *p = text;
    int row = 0;
    while (*p && row < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;

        char line[192];
        line[0] = '\0';
        size_t line_len = 0;
        const char *cursor = p;

        while (*cursor) {
            while (*cursor == ' ') cursor++;
            if (!*cursor) break;

            const char *word_end = cursor;
            while (*word_end && *word_end != ' ') word_end++;
            size_t wlen = (size_t)(word_end - cursor);

            char cand[192];
            if (line_len == 0) {
                size_t copy_len = (wlen < sizeof(cand) - 1) ? wlen : (sizeof(cand) - 1);
                memcpy(cand, cursor, copy_len);
                cand[copy_len] = '\0';
            } else {
                if (line_len + 1 + wlen >= sizeof(cand)) break;
                memcpy(cand, line, line_len);
                cand[line_len] = ' ';
                memcpy(cand + line_len + 1, cursor, wlen);
                cand[line_len + 1 + wlen] = '\0';
            }

            if (line_len > 0 && MeasureText(cand, font_sz) > max_w) {
                break;
            }
            snprintf(line, sizeof(line), "%s", cand);
            line_len = strlen(line);
            cursor = word_end;
            if (MeasureText(line, font_sz) > max_w) {
                break;
            }
        }

        /* Se e a ultima linha permitida e ainda restou texto, anexa ou garante caber */
        if (row == max_lines - 1 && *cursor) {
            char full[192];
            snprintf(full, sizeof(full), "%s", p);
            int fit_sz = fit_menu_font(full, font_sz, 9, max_w);
            if (MeasureText(full, fit_sz) <= max_w) {
                DrawText(full, x, y + row * (font_sz + line_gap), fit_sz, col);
                row++;
                break;
            }
        }

        int line_fs = fit_menu_font(line, font_sz, 9, max_w);
        DrawText(line, x, y + row * (font_sz + line_gap), line_fs, col);
        row++;
        p = cursor;
    }

    if (row <= 0) return 0;
    return row * font_sz + (row - 1) * line_gap;
}

/* Captura a proxima tecla apertada para rebind, bloqueando ESC e atalhos globais (E, F2..F12) */
static void update_bind_capture(AppConfig *cfg, int *listening_bind) {
    if (!cfg || !listening_bind || *listening_bind < 0) {
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        *listening_bind = -1;
        return;
    }
    int pressed = GetKeyPressed();
    if (pressed > 0 && pressed != KEY_E && pressed != KEY_F11 &&
        pressed != KEY_F12 && pressed != KEY_F4 && pressed != KEY_F3 && pressed != KEY_F2) {
        int *bind_ptrs[9] = {
            &cfg->binds.move_forward,
            &cfg->binds.move_backward,
            &cfg->binds.move_left,
            &cfg->binds.move_right,
            &cfg->binds.turn_left,
            &cfg->binds.turn_right,
            &cfg->binds.sprint,
            &cfg->binds.reload,
            &cfg->binds.shoot_key
        };
        if (*listening_bind < 9) {
            *bind_ptrs[*listening_bind] = pressed;
        }
        *listening_bind = -1;
    }
}

/* Painel compartilhado entre menu principal (alinhado a esquerda) e pause (centralizado).
 * Retorna 1 quando o usuario clica no botao de voltar. */
static int draw_settings_panel(AppConfig *cfg, int panel_x, int panel_y,
                               int max_h, int sw, float sc, int pad_x,
                               int center_x, int *settings_tab,
                               int *listening_bind, const char *back_label) {
    int clicked_back = 0;
    int lbl_sz = (int)(15.5f * sc);
    if (lbl_sz < 11) lbl_sz = 11;
    int b_font = (int)(13.5f * sc);
    if (b_font < 10) b_font = 10;

    int sq_w_est = (int)(28.0f * sc);
    if (sq_w_est < 22) sq_w_est = 22;
    int val_w1 = calc_fit_w("ESTILO 1: SOBREPOSTO", b_font, (int)(160.0f * sc), sc);
    int val_w2 = calc_fit_w("ESTILO 2: MICRO-ASCII", b_font, (int)(160.0f * sc), sc);
    int val_w_est = (val_w1 > val_w2) ? val_w1 : val_w2;

    int pw = (int)(555.0f * sc);
    int max_pw = center_x ? (sw - 32) : (int)((float)sw * 0.56f);
    int min_sw_mode = MeasureText("Modo de Exibicao (F11):", lbl_sz) +
                      calc_fit_w("MODO: SEM BORDAS", b_font, (int)(145.0f * sc), sc) +
                      pad_x * 2 + (int)(20.0f * sc);
    int min_sw_sty = MeasureText("Qualidade ASCII (F2):", lbl_sz) +
                     val_w_est + sq_w_est * 2 + 10 +
                     pad_x * 2 + (int)(20.0f * sc);
    int min_sw = (min_sw_mode > min_sw_sty) ? min_sw_mode : min_sw_sty;
    if (pw < min_sw) pw = min_sw;
    if (pw > max_pw && max_pw >= 330) pw = max_pw;

    int ph = (int)(445.0f * sc);
    if (ph > max_h && max_h > 220) ph = max_h;

    if (center_x) {
        int sh_cur = GetScreenHeight();
        panel_x = (sw - pw) / 2;
        panel_y = (sh_cur - ph) / 2;
        if (panel_y < 10) panel_y = 10;
    }

    DrawRectangle(panel_x, panel_y, pw, ph, (Color){14, 18, 25, 240});
    DrawRectangleLinesEx((Rectangle){(float)panel_x, (float)panel_y, (float)pw, (float)ph},
                         2.0f, (Color){95, 175, 245, 255});

    int right_edge = panel_x + pw - pad_x;
    int tab_gap = (int)(10.0f * sc);
    int tab_w = (pw - pad_x * 2 - tab_gap) / 2;
    int tab_h = (int)(34.0f * sc);
    if (tab_h < 24) tab_h = 24;
    int tab_y = panel_y + (int)(12.0f * sc);

    if (draw_small_button(panel_x + pad_x, tab_y, tab_w, tab_h,
                          "GERAL / VIDEO", b_font + 1,
                          *settings_tab == 0 ? (Color){196, 62, 22, 255} : (Color){32, 38, 48, 255},
                          (Color){225, 105, 35, 255})) {
        *settings_tab = 0;
        *listening_bind = -1;
    }
    if (draw_small_button(panel_x + pad_x + tab_w + tab_gap, tab_y, tab_w, tab_h,
                          "CONTROLES / BINDS", b_font + 1,
                          *settings_tab == 1 ? (Color){196, 62, 22, 255} : (Color){32, 38, 48, 255},
                          (Color){225, 105, 35, 255})) {
        *settings_tab = 1;
    }

    int sy = tab_y + tab_h + (int)(14.0f * sc);
    int foot_h = (int)(32.0f * sc);
    if (foot_h < 24) foot_h = 24;
    int foot_btn_y = panel_y + ph - foot_h - (int)(12.0f * sc);
    int avail_h = foot_btn_y - (int)(10.0f * sc) - sy;

    if (*settings_tab == 0) {
        int row_step = avail_h / 7;
        if (row_step > (int)(44.0f * sc)) row_step = (int)(44.0f * sc);
        if (row_step < 22) row_step = 22;
        int row_bh = (int)(26.0f * sc);
        if (row_bh > row_step - 4) row_bh = row_step - 4;
        if (row_bh < 18) row_bh = 18;

        int sq_w = sq_w_est;
        int val_w = val_w_est;
        int gt_x = right_edge - sq_w;
        int val_x = gt_x - 5 - val_w;
        int lt_x = val_x - 5 - sq_w;

        /* Linha 1: Resolucao da janela */
        int r0y = sy;
        DrawText("Resolucao:", panel_x + pad_x, r0y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        if (draw_small_button(lt_x, r0y, sq_w, row_bh, "<", b_font,
                              (Color){40, 48, 60, 255}, (Color){205, 85, 35, 255})) {
            cfg->res_idx = (cfg->res_idx + RES_PRESET_COUNT - 1) % RES_PRESET_COUNT;
            apply_window_resolution(cfg->res_idx);
        }
        DrawRectangle(val_x, r0y, val_w, row_bh, (Color){24, 30, 40, 255});
        DrawRectangleLinesEx((Rectangle){(float)val_x, (float)r0y, (float)val_w, (float)row_bh},
                             1.5f, (Color){82, 98, 115, 255});
        const char *res_lbl = RES_PRESETS[cfg->res_idx].label;
        int rw_txt = MeasureText(res_lbl, b_font);
        DrawText(res_lbl, val_x + (val_w - rw_txt) / 2, r0y + (row_bh - b_font) / 2,
                 b_font, (Color){255, 215, 115, 255});
        if (draw_small_button(gt_x, r0y, sq_w, row_bh, ">", b_font,
                              (Color){40, 48, 60, 255}, (Color){205, 85, 35, 255})) {
            cfg->res_idx = (cfg->res_idx + 1) % RES_PRESET_COUNT;
            apply_window_resolution(cfg->res_idx);
        }

        /* Linha 2: Qualidade Dinamica ASCII */
        int r1y = sy + row_step;
        DrawText("Qualidade ASCII (F2):", panel_x + pad_x, r1y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        if (draw_small_button(lt_x, r1y, sq_w, row_bh, "<", b_font,
                              (Color){40, 48, 60, 255}, (Color){205, 85, 35, 255})) {
            cfg->ascii_quality = (cfg->ascii_quality + 4) % 5;
        }
        DrawRectangle(val_x, r1y, val_w, row_bh, (Color){24, 30, 40, 255});
        DrawRectangleLinesEx((Rectangle){(float)val_x, (float)r1y, (float)val_w, (float)row_bh},
                             1.5f, (Color){82, 98, 115, 255});
        const char *qual_lbl = quality_to_label(cfg->ascii_quality);
        int qw_txt = MeasureText(qual_lbl, b_font);
        DrawText(qual_lbl, val_x + (val_w - qw_txt) / 2, r1y + (row_bh - b_font) / 2,
                 b_font, (Color){115, 225, 255, 255});
        if (draw_small_button(gt_x, r1y, sq_w, row_bh, ">", b_font,
                              (Color){40, 48, 60, 255}, (Color){205, 85, 35, 255})) {
            cfg->ascii_quality = (cfg->ascii_quality + 1) % 5;
        }

        /* Linha 3: Estilo Visual (Estilo 1 Sobreposto vs Estilo 2 Micro-ASCII) */
        int r_sty = sy + row_step * 2;
        DrawText("Estilo Visual (F4):", panel_x + pad_x, r_sty + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        if (draw_small_button(lt_x, r_sty, sq_w, row_bh, "<", b_font,
                              (Color){40, 48, 60, 255}, (Color){205, 85, 35, 255})) {
            cfg->ascii_style = (cfg->ascii_style + 1) % 2;
        }
        DrawRectangle(val_x, r_sty, val_w, row_bh, (Color){24, 30, 40, 255});
        DrawRectangleLinesEx((Rectangle){(float)val_x, (float)r_sty, (float)val_w, (float)row_bh},
                             1.5f, (Color){82, 98, 115, 255});
        const char *sty_lbl = style_to_label(cfg->ascii_style);
        int sw_txt = MeasureText(sty_lbl, b_font);
        DrawText(sty_lbl, val_x + (val_w - sw_txt) / 2, r_sty + (row_bh - b_font) / 2,
                 b_font, (cfg->ascii_style == ASCII_STYLE_SOLID)
                             ? (Color){130, 245, 170, 255}
                             : (Color){255, 195, 105, 255});
        if (draw_small_button(gt_x, r_sty, sq_w, row_bh, ">", b_font,
                              (Color){40, 48, 60, 255}, (Color){205, 85, 35, 255})) {
            cfg->ascii_style = (cfg->ascii_style + 1) % 2;
        }

        /* Linha 4: Modo de Exibicao (Janela / Janela Sem Bordas) */
        int r2y = sy + row_step * 3;
        DrawText("Modo de Exibicao (F11):", panel_x + pad_x, r2y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        int is_bl = g_borderless_mode;
        const char *mode_txt = is_bl ? "MODO: SEM BORDAS" : "MODO: JANELA";
        int mode_bw = calc_fit_w("MODO: SEM BORDAS", b_font, (int)(152.0f * sc), sc);
        if (draw_small_button(right_edge - mode_bw, r2y, mode_bw, row_bh,
                              mode_txt, b_font,
                              is_bl ? (Color){185, 80, 28, 255} : (Color){38, 52, 74, 255},
                              (Color){75, 135, 220, 255})) {
            toggle_borderless_window();
        }

        /* Linha 5: Mostrar FPS na Tela */
        int r3y = sy + row_step * 4;
        DrawText("Mostrar FPS na Tela (F3):", panel_x + pad_x, r3y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        const char *sfps_txt = cfg->show_fps ? "FPS: ATIVADO" : "FPS: DESATIVADO";
        int sfps_bw = calc_fit_w("FPS: DESATIVADO", b_font, (int)(152.0f * sc), sc);
        if (draw_small_button(right_edge - sfps_bw, r3y, sfps_bw, row_bh,
                              sfps_txt, b_font,
                              cfg->show_fps ? (Color){52, 130, 68, 255} : (Color){44, 48, 58, 255},
                              (Color){205, 95, 35, 255})) {
            cfg->show_fps = !cfg->show_fps;
        }

        /* Linha 6: Limite de FPS */
        int r4y = sy + row_step * 5;
        const char *fps_lbl = (cfg->fps_cap == 165) ? "165 FPS"
                            : (cfg->fps_cap == 60)  ? "60 FPS"
                                                    : "ILIMITADO";
        DrawText(TextFormat("Limite de FPS: %s", fps_lbl),
                 panel_x + pad_x, r4y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        int cap_bw = calc_fit_w("ALTERNAR LIMITE", b_font, (int)(152.0f * sc), sc);
        if (draw_small_button(right_edge - cap_bw, r4y, cap_bw, row_bh,
                              "ALTERNAR LIMITE", b_font,
                              (Color){38, 52, 74, 255}, (Color){75, 135, 220, 255})) {
            if (cfg->fps_cap == 165) cfg->fps_cap = 0;
            else if (cfg->fps_cap == 0) cfg->fps_cap = 60;
            else cfg->fps_cap = 165;
            SetTargetFPS(cfg->fps_cap);
        }

        /* Linha 7: Sensibilidade do Mouse */
        int r5y = sy + row_step * 6;
        DrawText(TextFormat("Sensibilidade Mouse: %.4f", cfg->binds.mouse_sens),
                 panel_x + pad_x, r5y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        int pm_w = (int)(36.0f * sc);
        if (pm_w < 26) pm_w = 26;
        if (draw_small_button(right_edge - pm_w * 2 - 6, r5y, pm_w, row_bh, "-", lbl_sz,
                              (Color){44, 48, 58, 255}, (Color){205, 80, 30, 255})) {
            if (cfg->binds.mouse_sens > 0.0006f) cfg->binds.mouse_sens -= 0.0003f;
        }
        if (draw_small_button(right_edge - pm_w, r5y, pm_w, row_bh, "+", lbl_sz,
                              (Color){44, 48, 58, 255}, (Color){70, 165, 80, 255})) {
            if (cfg->binds.mouse_sens < 0.0100f) cfg->binds.mouse_sens += 0.0003f;
        }
    } else {
        /* Aba 1: lista dos 9 keybinds remapeaveis (clique na tecla para aguardar nova tecla) */
        const char *bind_labels[9] = {
            "Mover Frente", "Mover Tras", "Mover Esquerda", "Mover Direita",
            "Girar Esquerda", "Girar Direita", "Correr (Sprint)",
            "Recarregar", "Atirar (Tecla)"
        };
        int bind_vals[9] = {
            cfg->binds.move_forward, cfg->binds.move_backward,
            cfg->binds.move_left, cfg->binds.move_right,
            cfg->binds.turn_left, cfg->binds.turn_right,
            cfg->binds.sprint, cfg->binds.reload, cfg->binds.shoot_key
        };

        int row_step = avail_h / 9;
        if (row_step < 22) row_step = 22;
        int bind_bw = calc_fit_w("PRESSIONE TECLA...", b_font, (int)(168.0f * sc), sc);

        for (int i = 0; i < 9; i++) {
            int ry = sy + i * row_step;
            DrawText(bind_labels[i], panel_x + pad_x, ry + (row_step - 4 - b_font) / 2,
                     b_font, (Color){220, 225, 235, 255});

            const char *ktext = (*listening_bind == i)
                ? "PRESSIONE TECLA..."
                : key_to_label(bind_vals[i]);
            Color btn_c = (*listening_bind == i)
                ? (Color){210, 135, 25, 255}
                : (Color){34, 42, 52, 255};
            if (draw_small_button(right_edge - bind_bw, ry, bind_bw, row_step - 4,
                                  ktext, b_font, btn_c, (Color){225, 105, 30, 255})) {
                *listening_bind = i;
            }
        }
    }

    int vw = calc_fit_w(back_label, b_font + 1, (int)(110.0f * sc), sc);
    if (draw_small_button(panel_x + pad_x, foot_btn_y, vw, foot_h,
                          back_label, b_font + 1, (Color){38, 42, 52, 255},
                          (Color){195, 75, 30, 255})) {
        *listening_bind = -1;
        clicked_back = 1;
    }
    if (*settings_tab == 1) {
        int rest_w = calc_fit_w("RESTAURAR PADRAO", b_font, (int)(168.0f * sc), sc);
        if (draw_small_button(right_edge - rest_w, foot_btn_y, rest_w, foot_h,
                              "RESTAURAR PADRAO", b_font,
                              (Color){60, 40, 42, 255}, (Color){185, 65, 55, 255})) {
            binds_init_default(&cfg->binds);
            *listening_bind = -1;
        }
    }

    return clicked_back;
}

/* Criador usa apenas dados do modulo player; nenhuma regra visual no renderer.
 * Retorna 1 para salvar, -1 para cancelar e 0 enquanto o usuario ainda edita. */
static int draw_character_creator(CharacterClass *draft, int x, int y, int w, int h,
                                   float sc, int *editing, const char *error) {
    int pad = (int)(18 * sc), row = h / 11;
    int font = (int)(16 * sc); if (font < 11) font = 11;
    int bh = row - 6;
    Color bg = {35, 40, 52, 255}, hover = {145, 55, 90, 255};
    DrawRectangle(x, y, w, h, (Color){14, 18, 25, 245});
    DrawText("+ CRIAR SKIN", x + pad, y + 10, font + 2, (Color){255, 150, 180, 255});
    Rectangle name_box = {(float)(x + pad), (float)(y + row), (float)(w - 2 * pad), (float)bh};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) *editing = CheckCollisionPointRec(GetMousePosition(), name_box);
    if (*editing) {
        /* Aceita texto UTF-8 ate 24 bytes e trata backspace por codepoint para acentos */
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            size_t len = strlen(draft->name);
            int bytes = 0;
            const char *encoded = CodepointToUTF8(ch, &bytes);
            if (ch >= 32 && ch != 127 && bytes > 0 && len + bytes <= 24) {
                memcpy(draft->name + len, encoded, bytes);
                draft->name[len + bytes] = '\0';
            }
        }
        size_t len = strlen(draft->name);
        if (IsKeyPressed(KEY_BACKSPACE) && len) {
            do { len--; } while (len && ((unsigned char)draft->name[len] & 0xc0) == 0x80);
            draft->name[len] = '\0';
        }
    }
    DrawRectangleRec(name_box, bg);
    DrawRectangleLinesEx(name_box, 1, *editing ? (Color){255, 150, 180, 255} : GRAY);
    DrawText(TextFormat("Nome: %s%s", draft->name, *editing ? "_" : ""), x + pad + 8, y + row + 8, font, WHITE);
    int half = (w - 2 * pad - 8) / 2;
    for (int part = 0; part < CHARACTER_PART_COUNT; part++) {
        int ry = y + (2 + part) * row;
        int value = draft->appearance.choice[part];
        DrawText(TextFormat("%s: %s", player_part_label(part), player_part_option(part, value)),
                 x + pad, ry + (bh - font) / 2, font, WHITE);
        int delta = 0;
        if (draw_small_button(x + w - pad - 2 * bh - 6, ry, bh, bh, "<", font, bg, hover)) delta = -1;
        if (draw_small_button(x + w - pad - bh, ry, bh, bh, ">", font, bg, hover)) delta = 1;
        int count = player_part_count(part);
        draft->appearance.choice[part] = (value + delta + count) % count;
    }
    DrawText(error[0] ? error : "Disponivel enquanto o jogo estiver aberto.", x + pad, y + 9 * row + 4,
             font - 1, error[0] ? (Color){255, 100, 100, 255} : LIGHTGRAY);
    if (draw_small_button(x + pad, y + 10 * row, half, bh, "CANCELAR", font, bg, hover)) return -1;
    if (draw_small_button(x + pad + half + 8, y + 10 * row, half, bh, "SALVAR", font,
                          (Color){40, 105, 75, 255}, (Color){65, 150, 100, 255})) return 1;
    return 0;
}

/* Preview tatico 16x16 do mapa no menu: centraliza a entrada (P / **) no topo e oculta salas secretas */
static void draw_level_minimap_preview(int box_x, int box_y, int box_size,
                                       const GameMap *m,
                                       float spawn_x, float spawn_y) {
    DrawRectangle(box_x, box_y, box_size, box_size, (Color){10, 14, 20, 255});
    DrawRectangleLinesEx((Rectangle){(float)box_x, (float)box_y, (float)box_size, (float)box_size},
                         1.5f, (Color){82, 108, 138, 255});
    if (!m || m->width <= 0 || m->height <= 0) {
        return;
    }

    int pad = 6;
    int inner = box_size - pad * 2;
    if (inner < 16) return;

    /* Fixa a janela de previa em 16x16 para nunca revelar a planta completa 32x32 nem salas secretas */
    int view_w = (m->width < MINIMAP_VIEW_SIZE) ? m->width : MINIMAP_VIEW_SIZE;
    int view_h = (m->height < MINIMAP_VIEW_SIZE) ? m->height : MINIMAP_VIEW_SIZE;
    int sp_x = (int)floorf(spawn_x);
    int sp_y = (int)floorf(spawn_y);

    /* Centraliza o spawn ('P') e o arco superior ('**' em sp_x..sp_x+1) no topo central da grade 16x16 */
    int start_x = sp_x - (view_w / 2 - 1);
    int start_y = sp_y - 1;
    if (start_x < 0) start_x = 0;
    if (start_x > m->width - view_w) start_x = m->width - view_w;
    if (start_y < 0) start_y = 0;
    if (start_y > m->height - view_h) start_y = m->height - view_h;

    float cw = (float)inner / (float)view_w;
    float ch = (float)inner / (float)view_h;
    int fsz = (int)fmaxf(6.0f, ch * 0.95f);

    for (int dy = 0; dy < view_h; dy++) {
        int my = start_y + dy;
        for (int dx = 0; dx < view_w; dx++) {
            int mx = start_x + dx;
            char raw = map_get_raw_tile(m, mx, my);
            char glyph = '.';
            Color col = (Color){44, 54, 72, 255};

            if (mx == sp_x && my == sp_y) {
                /* Posicao inicial do jogador logo abaixo da entrada superior */
                glyph = 'P';
                col = (Color){255, 240, 75, 255};
            } else if (raw == 'E') {
                glyph = 'E';
                col = (Color){255, 240, 110, 255};
            } else {
                /* Converte paredes falsas ('$', '&', '/') e blocos secretos (',', ';', ':', '1'..'9') para solido equivalente */
                char t = map_get_wall_equivalent(raw);
                if (t == '#') {
                    glyph = '#';
                    col = (Color){220, 74, 58, 255};
                } else if (t == '@') {
                    glyph = '@';
                    col = (Color){74, 158, 245, 255};
                } else if (t == '%') {
                    glyph = '%';
                    col = (Color){72, 212, 120, 255};
                } else if (t != '.' && t != ' ') {
                    glyph = t;
                    col = (Color){225, 172, 70, 255};
                }
            }

            char s[2] = {glyph, '\0'};
            int px = box_x + pad + (int)((float)dx * cw + cw * 0.15f);
            int py = box_y + pad + (int)((float)dy * ch + ch * 0.05f);
            DrawText(s, px, py, fsz, col);
        }
    }
}

/* Copia string de IP garantindo terminador nulo mesmo se truncar */
static void copy_ip_str(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    size_t j = 0;
    if (src) {
        while (j + 1 < dst_size && src[j] != '\0') {
            dst[j] = src[j];
            j++;
        }
    }
    dst[j] = '\0';
}

/* Fecha o socket de broadcast/descoberta LAN e limpa a lista de salas */
static void stop_lan_discovery(int *disc_active, unsigned long long *disc_sock, int *disc_count) {
    if (disc_active && *disc_active) {
        net_discovery_close(disc_sock);
        *disc_active = 0;
    }
    if (disc_count) {
        *disc_count = 0;
    }
}

/* Envia pacote de saida e fecha o socket do lobby antes de voltar ao menu */
static void leave_mp_lobby(int *lobby_active, NetContext *lobby_net) {
    if (lobby_active && *lobby_active) {
        net_send_lobby_leave(lobby_net);
        net_close(lobby_net);
        *lobby_active = 0;
    }
}

/* Loop das telas de menu (principal, singleplayer, multiplayer LAN/Online, lobby, skins e config).
 * Retorna: 1=Singleplayer, 2=Host MP, 3=Client MP, 4=Sair. */
static int run_menu_screen(AppConfig *cfg, char *ip_buf, size_t ip_size,
                           int *port_out, NetContext *net_out, const char *status_msg) {
    const char *main_items[5] = {
        "JOGAR SINGLEPLAYER",
        "JOGAR MULTIPLAYER",
        "SELECIONAR PERSONAGEM",
        "CONFIGURACOES",
        "SAIR"
    };

    MenuPage page = MENU_PAGE_MAIN;
    CharacterClass draft = player_get_preset_class(PRESET_FUZILEIRO);
    int list_page = cfg->selected_character / 4;
    int name_editing = 1;

    const char *creator_error = "";
    int main_sel = 0;
    int mp_editing_ip = 0;
    int mp_editing_code = 0;
    int settings_tab = 0; /* 0 = Geral/Video, 1 = Controles/Binds */
    int listening_bind = -1;
    *port_out = DEFAULT_PORT;

    unsigned long long disc_sock = 0;
    int disc_active = 0;
    NetDiscoveredRoom disc_rooms[4];
    memset(disc_rooms, 0, sizeof(disc_rooms));
    int disc_count = 0;
    double last_probe_t = 0.0;

    NetContext lobby_net;
    memset(&lobby_net, 0, sizeof(lobby_net));
    int lobby_active = 0;
    int lobby_ready = 0;
    int lobby_started_burst = 0;
    double last_lobby_tx_t = 0.0;
    double last_lobby_rx_t = 0.0;
    double lobby_join_start_t = 0.0;
    char local_ip_str[128] = "127.0.0.1";
    char room_code_buf[16] = "";
    char mp_err_msg[128] = "";

    GameMap level_preview_map;
    int preview_loaded_idx = -1;

    if (IsCursorHidden()) {
        EnableCursor();
    }

    Font font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);

    while (!WindowShouldClose()) {
        MenuPage page_at_frame_start = page;
        sync_window_focus_and_borderless();
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        float sc = fminf((float)sw / 1280.0f, (float)sh / 720.0f);
        if (sc < 0.48f) sc = 0.48f;
        double now_d = GetTime();
        float now = (float)now_d;

        /* Garante que o mapa exibido em previa esteja sempre atualizado */
        cfg->selected_level = map_level_clamp(cfg->selected_level);
        if (preview_loaded_idx != cfg->selected_level) {
            map_load_level(cfg->selected_level, &level_preview_map);
            preview_loaded_idx = cfg->selected_level;
        }

        /* Gerencia descoberta automatica LAN enquanto estiver na tela Multiplayer */
        if (page == MENU_PAGE_MULTIPLAYER) {
            if (!disc_active) {
                if (net_discovery_open(&disc_sock)) {
                    disc_active = 1;
                    last_probe_t = 0.0;
                }
            }
            if (disc_active) {
                if ((now_d - last_probe_t) >= 0.45) {
                    net_discovery_send_probe(disc_sock, *port_out);
                    last_probe_t = now_d;
                }
                net_discovery_poll(disc_sock, disc_rooms, &disc_count, 4, now_d);
            }
        } else if (disc_active) {
            stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
        }

        /* Sincronizacao continua do Lobby (ate 4 jogadores) enquanto aguarda ou prepara partida */
        if (page == MENU_PAGE_LOBBY && lobby_active) {
            CharacterClass my_cls = player_roster_get(&cfg->roster, cfg->selected_character);
            const MapLevelInfo *cur_lvl = map_level_get(cfg->selected_level);
            int is_host = (lobby_net.mode == NET_MODE_HOST);

            if (is_host) {
                net_host_update_local_slot(&lobby_net, my_cls.name, cfg->selected_character,
                                           my_cls.visual_id, &my_cls.appearance,
                                           cfg->mp_game_mode, cfg->selected_level);
            }

            NetCtrlPacket my_pkt;
            memset(&my_pkt, 0, sizeof(my_pkt));
            my_pkt.magic = NET_CTRL_MAGIC;
            my_pkt.msg_type = NET_MSG_LOBBY_STATE;
            my_pkt.level_idx = cfg->selected_level;
            my_pkt.character_idx = cfg->selected_character;
            my_pkt.ready = is_host ? 1 : lobby_ready;
            my_pkt.game_started = (is_host && lobby_started_burst > 0) ? 1 : 0;
            my_pkt.port = *port_out;
            my_pkt.game_mode = cfg->mp_game_mode;
            my_pkt.visual_id = my_cls.visual_id;
            my_pkt.appearance = my_cls.appearance;
            my_pkt.player_count = lobby_net.active_player_count;
            my_pkt.host_session_id = lobby_net.host_session_id;
            my_pkt.client_id = lobby_net.local_client_id;
            my_pkt.in_game = lobby_net.in_game;
            snprintf(my_pkt.player_name, sizeof(my_pkt.player_name), "%s", my_cls.name);
            snprintf(my_pkt.map_name, sizeof(my_pkt.map_name), "%s", cur_lvl->name);

            /* No modo Online (Relay), acompanha a conexao TLS/WebSocket antes de trocar pacotes */
            int is_relay = (lobby_net.transport == NET_TRANSPORT_RELAY);
            char relay_err[128] = "";
            NetRelayStatus relay_st = is_relay
                ? net_relay_get_status(&lobby_net, relay_err, (int)sizeof(relay_err))
                : NET_RELAY_READY;
            if (is_relay && relay_st == NET_RELAY_CONNECTING) {
                lobby_join_start_t = now_d;
            }
            if (is_relay && (relay_st == NET_RELAY_FAILED || relay_st == NET_RELAY_CLOSED)) {
                leave_mp_lobby(&lobby_active, &lobby_net);
                snprintf(mp_err_msg, sizeof(mp_err_msg), "%s",
                         relay_err[0] ? relay_err : "Conexao online encerrada.");
                page = MENU_PAGE_MULTIPLAYER;
                continue;
            }

            /* Host responde probes de sala e envia roster a cada 80ms; cliente manda seu estado e detecta queda */
            int peer_left = 0;
            int got_lobby = net_poll_lobby(&lobby_net, (is_host && !is_relay) ? &my_pkt : NULL, &peer_left);
            if (got_lobby) {
                last_lobby_rx_t = now_d;
                mp_err_msg[0] = '\0';
            }
            if (is_host) {
                net_lobby_prune_stale(&lobby_net, is_relay ? 5.0 : 3.0);
                if (peer_left) {
                    net_broadcast_lobby_roster(&lobby_net, cfg->selected_level, cur_lvl->name, 0);
                } else if (lobby_net.connected && (!is_relay || relay_st == NET_RELAY_READY) &&
                           (now_d - last_lobby_tx_t) >= 0.08) {
                    net_broadcast_lobby_roster(&lobby_net, cfg->selected_level, cur_lvl->name, 0);
                    last_lobby_tx_t = now_d;
                }
            } else {
                if (peer_left || (lobby_net.connected && (now_d - last_lobby_rx_t) > (is_relay ? 5.5 : 3.5))) {
                    leave_mp_lobby(&lobby_active, &lobby_net);
                    snprintf(mp_err_msg, sizeof(mp_err_msg),
                             peer_left ? "O Anfitriao fechou a sala."
                                       : "Conexao com o Anfitriao perdida.");
                    page = MENU_PAGE_MULTIPLAYER;
                    continue;
                }
                if (lobby_net.has_peer_addr && (!is_relay || relay_st == NET_RELAY_READY) &&
                    (now_d - last_lobby_tx_t) >= 0.08) {
                    net_send_lobby_state(&lobby_net, &my_pkt);
                    last_lobby_tx_t = now_d;
                }
                if (lobby_net.connected) {
                    cfg->selected_level = map_level_clamp(lobby_net.remote_level);
                    cfg->mp_game_mode = lobby_net.game_mode;
                }
                if (lobby_net.remote_started) {
                    cfg->selected_level = map_level_clamp(lobby_net.remote_level);
                    cfg->mp_game_mode = lobby_net.game_mode;
                    if (net_out) {
                        *net_out = lobby_net;
                    }
                    return 3;
                }
            }
        }

        if (IsKeyPressed(KEY_F11)) {
            toggle_borderless_window();
        }
        if (IsKeyPressed(KEY_F2) && listening_bind < 0) {
            cfg->ascii_quality = (cfg->ascii_quality + 1) % 5;
        }
        if (IsKeyPressed(KEY_F3) && listening_bind < 0) {
            cfg->show_fps = !cfg->show_fps;
        }
        if (IsKeyPressed(KEY_F4) &&
            !IsKeyDown(KEY_LEFT_ALT) && !IsKeyDown(KEY_RIGHT_ALT) &&
            listening_bind < 0) {
            cfg->ascii_style = (cfg->ascii_style + 1) % 2;
        }

        /* Captura de tecla quando estiver reconfigurando uma bind */
        if (page == MENU_PAGE_SETTINGS && settings_tab == 1 && listening_bind >= 0) {
            update_bind_capture(cfg, &listening_bind);
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            if (page == MENU_PAGE_MAIN) {
                stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                leave_mp_lobby(&lobby_active, &lobby_net);
                return 4;
            } else if (page == MENU_PAGE_LOBBY) {
                leave_mp_lobby(&lobby_active, &lobby_net);
                page = MENU_PAGE_MULTIPLAYER;
            } else {
                mp_editing_ip = 0;
                mp_editing_code = 0;
                listening_bind = -1;
                page = (page == MENU_PAGE_CREATE) ? MENU_PAGE_CUSTOMIZE : MENU_PAGE_MAIN;
            }
        }

        /* Navegacao por teclado nas telas */
        if (page == MENU_PAGE_MAIN) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
                main_sel = (main_sel + 1) % 5;
            }
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                main_sel = (main_sel + 4) % 5;
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                if (main_sel == 0) page = MENU_PAGE_SINGLEPLAYER;
                if (main_sel == 1) {
                    mp_err_msg[0] = '\0';
                    net_get_local_ipv4(local_ip_str, (int)sizeof(local_ip_str));
                    page = MENU_PAGE_MULTIPLAYER;
                }
                if (main_sel == 2) page = MENU_PAGE_CUSTOMIZE;
                if (main_sel == 3) page = MENU_PAGE_SETTINGS;
                if (main_sel == 4) {
                    stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                    leave_mp_lobby(&lobby_active, &lobby_net);
                    return 4;
                }
            }
        } else if (page == MENU_PAGE_SINGLEPLAYER) {
            int total_lvls = map_level_count();
            if (total_lvls > 1) {
                if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
                    cfg->selected_level = (cfg->selected_level + total_lvls - 1) % total_lvls;
                }
                if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
                    cfg->selected_level = (cfg->selected_level + 1) % total_lvls;
                }
            }
            if (page_at_frame_start == MENU_PAGE_SINGLEPLAYER &&
                (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) {
                return 1;
            }
        } else if (page == MENU_PAGE_MULTIPLAYER && mp_editing_ip) {
            /* Campo de IP LAN aceita apenas digitos e pontos */
            int ch = GetCharPressed();
            while (ch > 0) {
                size_t len = strlen(ip_buf);
                if (((ch >= '0' && ch <= '9') || ch == '.') && len + 1 < ip_size) {
                    ip_buf[len] = (char)ch;
                    ip_buf[len + 1] = '\0';
                }
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE)) {
                size_t len = strlen(ip_buf);
                if (len > 0) ip_buf[len - 1] = '\0';
            }
            if (IsKeyPressed(KEY_ENTER)) {
                if (ip_buf[0] == '\0') snprintf(ip_buf, ip_size, "127.0.0.1");
                mp_editing_ip = 0;
            }
        } else if (page == MENU_PAGE_MULTIPLAYER && mp_editing_code) {
            /* Campo de codigo da sala online aceita 3 a 8 caracteres alfanumericos (converte pra maiuscula) */
            int ch = GetCharPressed();
            while (ch > 0) {
                size_t len = strlen(room_code_buf);
                int is_alnum = (ch >= '0' && ch <= '9') ||
                               (ch >= 'a' && ch <= 'z') ||
                               (ch >= 'A' && ch <= 'Z');
                if (is_alnum && len < 8 && len + 1 < sizeof(room_code_buf)) {
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    room_code_buf[len] = (char)ch;
                    room_code_buf[len + 1] = '\0';
                }
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE)) {
                size_t len = strlen(room_code_buf);
                if (len > 0) room_code_buf[len - 1] = '\0';
            }
            if (IsKeyPressed(KEY_ENTER)) {
                mp_editing_code = 0;
            }
        }

        /* --- Renderizacao do frame do menu --- */
        renderer_set_visual_style(cfg->ascii_style);
        BeginDrawing();
        CharacterClass preview = (page == MENU_PAGE_CREATE)
            ? draft
            : player_roster_get(&cfg->roster, cfg->selected_character);
        showcase_character = preview.visual_id;
        showcase_appearance = preview.appearance;
        snprintf(showcase_name, sizeof(showcase_name), "%s", preview.name);
        draw_cyber_arena_backdrop(font, sw, sh, sc, now, (int)page, cfg->ascii_quality);

        if (page != MENU_PAGE_LOBBY) {
            /* Showcase do personagem grande em Micro-ASCII 3D a direita */
            draw_dense_character_showcase(font, sw, sh, sc, now, (int)page, cfg->ascii_quality);
        }

        int left_x = (int)((float)sw * 0.050f);
        int top_y = (int)((float)sh * 0.048f);

        /* Logo SECTOR 404 densa no topo esquerdo + inicio da area de conteudo */
        int content_y = draw_dense_logo(font, left_x, top_y, sc, now, cfg->ascii_quality, sw, sh);

        int btn_w = (int)(395.0f * sc);
        int btn_h = (int)(54.0f * sc);
        int btn_gap = (int)(13.0f * sc);
        int bevel = (int)(15.0f * sc);
        int btn_font = (int)(20.0f * sc);
        if (btn_w < 235) btn_w = 235;
        if (btn_h < 32) btn_h = 32;
        if (btn_gap < 6) btn_gap = 6;
        if (bevel < 8) bevel = 8;
        if (btn_font < 13) btn_font = 13;

        int footer_top = sh - (int)(36.0f * sc);
        int pad_x = (int)(20.0f * sc);
        if (pad_x < 12) pad_x = 12;

        if (page == MENU_PAGE_MAIN) {
            for (int i = 0; i < 5; i++) {
                int hovered = 0;
                int y = content_y + i * (btn_h + btn_gap);
                int clicked = draw_chamfer_button(left_x, y, btn_w, btn_h, bevel,
                                                  main_items[i], btn_font,
                                                  main_sel == i, &hovered);
                if (hovered) main_sel = i;
                if (clicked) {
                    if (i == 0) page = MENU_PAGE_SINGLEPLAYER;
                    if (i == 1) {
                        mp_err_msg[0] = '\0';
                        net_get_local_ipv4(local_ip_str, (int)sizeof(local_ip_str));
                        page = MENU_PAGE_MULTIPLAYER;
                    }
                    if (i == 2) page = MENU_PAGE_CUSTOMIZE;
                    if (i == 3) page = MENU_PAGE_SETTINGS;
                    if (i == 4) {
                        EndDrawing();
                        stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                        leave_mp_lobby(&lobby_active, &lobby_net);
                        return 4;
                    }
                }
            }
        } else if (page == MENU_PAGE_SINGLEPLAYER) {
            int pw = (int)(525.0f * sc);
            if (pw < 350) pw = 350;
            int ph = (int)(415.0f * sc);
            if (content_y + ph > footer_top) ph = footer_top - content_y;

            DrawRectangle(left_x, content_y, pw, ph, (Color){14, 18, 25, 238});
            DrawRectangleLinesEx((Rectangle){(float)left_x, (float)content_y, (float)pw, (float)ph},
                                 2.0f, (Color){230, 108, 38, 255});

            int sp_title_fs = fit_menu_font("SELECIONAR NIVEL (SINGLEPLAYER)", btn_font, 12, pw - pad_x * 2);
            DrawText("SELECIONAR NIVEL (SINGLEPLAYER)", left_x + pad_x,
                     content_y + (int)(16.0f * sc), sp_title_fs, (Color){255, 200, 90, 255});

            /* Barra seletora de fase (< FASE >) */
            int sel_y = content_y + (int)(52.0f * sc);
            int sel_h = (int)(38.0f * sc);
            if (sel_h < 28) sel_h = 28;
            int arrow_w = (int)(40.0f * sc);
            if (arrow_w < 28) arrow_w = 28;
            int b_fnt = (int)(16.0f * sc);
            if (b_fnt < 12) b_fnt = 12;

            int total_lvls = map_level_count();
            if (total_lvls < 1) total_lvls = 1;
            if (draw_small_button(left_x + pad_x, sel_y, arrow_w, sel_h, "<", b_fnt,
                                  (Color){38, 46, 60, 255}, (Color){215, 90, 35, 255})) {
                cfg->selected_level = (cfg->selected_level + total_lvls - 1) % total_lvls;
            }
            int box_x = left_x + pad_x + arrow_w + 8;
            int box_w = pw - pad_x * 2 - arrow_w * 2 - 16;
            DrawRectangle(box_x, sel_y, box_w, sel_h, (Color){22, 28, 38, 255});
            DrawRectangleLinesEx((Rectangle){(float)box_x, (float)sel_y, (float)box_w, (float)sel_h},
                                 1.5f, (Color){95, 130, 165, 255});
            const MapLevelInfo *lvl = map_level_get(cfg->selected_level);
            const char *lvl_hdr = TextFormat("%s  (%d/%d)", lvl->name, cfg->selected_level + 1, total_lvls);
            int hdr_fnt = fit_menu_font(lvl_hdr, b_fnt, 10, box_w - 14);
            int lhw = MeasureText(lvl_hdr, hdr_fnt);
            DrawText(lvl_hdr, box_x + (box_w - lhw) / 2, sel_y + (sel_h - hdr_fnt) / 2,
                     hdr_fnt, (Color){120, 225, 255, 255});
            if (draw_small_button(box_x + box_w + 8, sel_y, arrow_w, sel_h, ">", b_fnt,
                                  (Color){38, 46, 60, 255}, (Color){215, 90, 35, 255})) {
                cfg->selected_level = (cfg->selected_level + 1) % total_lvls;
            }
            if (preview_loaded_idx != cfg->selected_level) {
                map_load_level(cfg->selected_level, &level_preview_map);
                preview_loaded_idx = cfg->selected_level;
            }
            lvl = map_level_get(cfg->selected_level);

            /* Preview ASCII do mapa + ficha rapida da fase (com quebra e ajuste dentro do painel) */
            int act_y = content_y + ph - btn_h - (int)(14.0f * sc);
            int prev_y = sel_y + sel_h + (int)(16.0f * sc);
            int prev_sz = (int)(168.0f * sc);
            int avail_prev = ph - (prev_y - content_y) - btn_h - (int)(32.0f * sc);
            if (prev_sz > avail_prev) prev_sz = avail_prev;
            if (prev_sz < 96) prev_sz = 96;

            draw_level_minimap_preview(left_x + pad_x, prev_y, prev_sz,
                                       &level_preview_map, lvl->spawn1_x, lvl->spawn1_y);

            int info_x = left_x + pad_x + prev_sz + (int)(14.0f * sc);
            int info_w = (left_x + pw - pad_x) - info_x;
            if (info_w < 80) info_w = 80;
            int info_sz = (int)(15.0f * sc);
            if (info_sz < 11) info_sz = 11;
            int info_max_y = act_y - (int)(8.0f * sc);

            int cur_y = prev_y + 2;
            int desc_h = draw_wrapped_text(lvl->subtitle, info_x, cur_y, info_w,
                                           info_sz, (int)(3.0f * sc), 3, (Color){225, 232, 245, 255});
            cur_y += desc_h + (int)(10.0f * sc);

            const char *p_line = TextFormat("Personagem: %s", preview.name);
            int p_fs = fit_menu_font(p_line, info_sz, 10, info_w);
            if (cur_y + p_fs <= info_max_y) {
                DrawText(p_line, info_x, cur_y, p_fs, (Color){255, 200, 100, 255});
                cur_y += p_fs + (int)(8.0f * sc);
            }

            const char *s_line = TextFormat("Vida: %d  |  Vel: %.1f", preview.max_health, preview.move_speed);
            int s_fs = fit_menu_font(s_line, info_sz - 1, 9, info_w);
            if (cur_y + s_fs <= info_max_y) {
                DrawText(s_line, info_x, cur_y, s_fs, (Color){160, 180, 205, 255});
                cur_y += s_fs + (int)(10.0f * sc);
            }

            const char *w_line = "Ondas de inimigos: 6 por onda";
            int w_fs = fit_menu_font(w_line, info_sz - 1, 9, info_w);
            if (cur_y + w_fs <= info_max_y) {
                DrawText(w_line, info_x, cur_y, w_fs, (Color){145, 215, 155, 255});
                cur_y += w_fs + (int)(8.0f * sc);
            }

            const char *n_line = "Pronto para adicionar novos mapas!";
            int n_fs = fit_menu_font(n_line, info_sz - 2, 9, info_w);
            if (cur_y + n_fs <= info_max_y) {
                DrawText(n_line, info_x, cur_y, n_fs, (Color){130, 145, 168, 255});
            }

            /* Acoes: VOLTAR e INICIAR PARTIDA */
            int back_w = (int)(135.0f * sc);
            if (back_w < 100) back_w = 100;
            if (draw_small_button(left_x + pad_x, act_y, back_w, btn_h, "VOLTAR",
                                  b_fnt, (Color){38, 44, 56, 255}, (Color){195, 75, 30, 255})) {
                page = MENU_PAGE_MAIN;
            }
            int go_x = left_x + pad_x + back_w + 12;
            int go_w = pw - pad_x * 2 - back_w - 12;
            if (draw_chamfer_button(go_x, act_y, go_w, btn_h, bevel,
                                    "INICIAR PARTIDA", btn_font - 1, 1, NULL)) {
                EndDrawing();
                return 1;
            }
        } else if (page == MENU_PAGE_MULTIPLAYER) {
            int pw = (int)(575.0f * sc);
            if (pw < 380) pw = 380;
            int ph = (int)(455.0f * sc);
            if (content_y + ph > footer_top) ph = footer_top - content_y;

            DrawRectangle(left_x, content_y, pw, ph, (Color){14, 18, 25, 238});
            DrawRectangleLinesEx((Rectangle){(float)left_x, (float)content_y, (float)pw, (float)ph},
                                 2.0f, (Color){225, 105, 35, 255});

            const char *mp_hdr = "MULTIPLAYER (ATE 4 JOGADORES - LAN OU ONLINE)";
            int mp_hdr_fs = fit_menu_font(mp_hdr, btn_font - 2, 11, pw - pad_x * 2);
            DrawText(mp_hdr, left_x + pad_x,
                     content_y + (int)(14.0f * sc), mp_hdr_fs, (Color){255, 200, 90, 255});
            int sub_sz = (int)(13.5f * sc);
            if (sub_sz < 10) sub_sz = 10;
            const char *mp_sub = "Use LAN/Hotspot na mesma rede, ou Sala Online por codigo em qualquer Wi-Fi.";
            int mp_sub_fs = fit_menu_font(mp_sub, sub_sz, 9, pw - pad_x * 2);
            DrawText(mp_sub, left_x + pad_x, content_y + (int)(42.0f * sc), mp_sub_fs,
                     (Color){165, 185, 210, 255});

            int host_btn_y = content_y + (int)(66.0f * sc);
            int host_btn_h = (int)(44.0f * sc);
            if (host_btn_h < 32) host_btn_h = 32;
            int cgap = 10;
            int half_bw = (pw - pad_x * 2 - cgap) / 2;
            int cbf = fit_menu_font("CRIAR SALA ONLINE", btn_font - 3, 10, half_bw - 18);

            /* Botoes lado a lado: hospedar na rede local (UDP) ou no servidor relay online */
            if (draw_small_button(left_x + pad_x, host_btn_y, half_bw, host_btn_h,
                                  "CRIAR SALA LAN", cbf,
                                  (Color){175, 76, 26, 255}, (Color){228, 104, 38, 255})) {
                mp_editing_ip = 0;
                mp_editing_code = 0;
                stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                mp_err_msg[0] = '\0';
                if (net_init(&lobby_net, NET_MODE_HOST, NULL, *port_out)) {
                    lobby_active = 1;
                    lobby_ready = 1;
                    lobby_started_burst = 0;
                    last_lobby_tx_t = 0.0;
                    last_lobby_rx_t = now_d;
                    net_get_all_local_ipv4s(local_ip_str, (int)sizeof(local_ip_str));
                    page = MENU_PAGE_LOBBY;
                } else {
                    snprintf(mp_err_msg, sizeof(mp_err_msg),
                             "Nao foi possivel abrir a porta %d para hospedar.", *port_out);
                }
            }

            if (draw_small_button(left_x + pad_x + half_bw + cgap, host_btn_y, half_bw, host_btn_h,
                                  "CRIAR SALA ONLINE", cbf,
                                  (Color){28, 92, 138, 255}, (Color){48, 138, 198, 255})) {
                mp_editing_ip = 0;
                mp_editing_code = 0;
                stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                mp_err_msg[0] = '\0';
                char gen_code[16] = "";
                char r_url[160] = "";
                net_generate_room_code(gen_code, (int)sizeof(gen_code));
                net_get_relay_url(r_url, (int)sizeof(r_url));
                if (net_init_relay(&lobby_net, NET_MODE_HOST, r_url, gen_code)) {
                    snprintf(room_code_buf, sizeof(room_code_buf), "%s", gen_code);
                    lobby_active = 1;
                    lobby_ready = 1;
                    lobby_started_burst = 0;
                    last_lobby_tx_t = 0.0;
                    last_lobby_rx_t = now_d;
                    lobby_join_start_t = now_d;
                    page = MENU_PAGE_LOBBY;
                } else {
                    snprintf(mp_err_msg, sizeof(mp_err_msg),
                             "Erro ao iniciar sala online (%s).", gen_code);
                }
            }

            /* Secao de busca automatica de salas abertas na LAN / localhost */
            int list_hdr_y = host_btn_y + host_btn_h + (int)(12.0f * sc);
            int dots = ((int)(now * 3.0f)) % 4;
            const char *dots_str = (dots == 0) ? "" : (dots == 1 ? "." : (dots == 2 ? ".." : "..."));
            DrawText(TextFormat("SALAS ENCONTRADAS NA LAN%s", dots_str),
                     left_x + pad_x, list_hdr_y, sub_sz + 1, (Color){115, 220, 255, 255});

            int room_y0 = list_hdr_y + (int)(22.0f * sc);
            int foot_h = (int)(36.0f * sc);
            if (foot_h < 28) foot_h = 28;
            int man_h = (int)(34.0f * sc);
            if (man_h < 26) man_h = 26;
            int man_y = content_y + ph - foot_h - (int)(12.0f * sc) - man_h - (int)(24.0f * sc);
            int room_area_h = man_y - room_y0 - (int)(8.0f * sc);

            int b_fnt = (int)(14.5f * sc);
            if (b_fnt < 11) b_fnt = 11;

            if (disc_count > 0) {
                int r_step = (int)(42.0f * sc);
                if (r_step * disc_count > room_area_h && disc_count > 0) {
                    r_step = room_area_h / disc_count;
                }
                if (r_step < 28) r_step = 28;
                for (int i = 0; i < disc_count && i < 3; i++) {
                    int ry = room_y0 + i * r_step;
                    const char *where = (strcmp(disc_rooms[i].ip, "127.0.0.1") == 0)
                        ? "Este PC"
                        : disc_rooms[i].ip;
                    const char *m_tag = (disc_rooms[i].game_mode == NET_GAME_PVP) ? "PvP" : "Co-op";
                    const char *r_lbl = disc_rooms[i].in_game
                        ? TextFormat("VOLTAR/ENTRAR [EM JOGO]: %s | %s | %s (%d/4) [%s]",
                                     disc_rooms[i].host_name,
                                     disc_rooms[i].map_name,
                                     m_tag,
                                     disc_rooms[i].player_count,
                                     where)
                        : TextFormat("ENTRAR: %s | %s | %s (%d/4) [%s]",
                                     disc_rooms[i].host_name,
                                     disc_rooms[i].map_name,
                                     m_tag,
                                     disc_rooms[i].player_count,
                                     where);
                    Color room_base = disc_rooms[i].in_game ? (Color){28, 62, 92, 255} : (Color){30, 68, 52, 255};
                    Color room_hov  = disc_rooms[i].in_game ? (Color){48, 118, 168, 255} : (Color){52, 138, 88, 255};
                    if (draw_small_button(left_x + pad_x, ry, pw - pad_x * 2, r_step - 6,
                                          r_lbl, b_fnt, room_base, room_hov)) {
                        copy_ip_str(ip_buf, ip_size, disc_rooms[i].ip);
                        stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                        mp_err_msg[0] = '\0';
                        if (net_init(&lobby_net, NET_MODE_CLIENT, ip_buf, *port_out)) {
                            lobby_active = 1;
                            lobby_ready = 0;
                            lobby_started_burst = 0;
                            last_lobby_tx_t = 0.0;
                            last_lobby_rx_t = now_d;
                            lobby_join_start_t = now_d;
                            page = MENU_PAGE_LOBBY;
                        } else {
                            snprintf(mp_err_msg, sizeof(mp_err_msg),
                                     "Erro ao conectar em %s.", ip_buf);
                        }
                    }
                }
            } else {
                int empty_h = (int)(66.0f * sc);
                if (empty_h > room_area_h) empty_h = room_area_h;
                if (empty_h < 40) empty_h = 40;
                DrawRectangle(left_x + pad_x, room_y0, pw - pad_x * 2, empty_h, (Color){20, 25, 34, 255});
                DrawRectangleLinesEx((Rectangle){(float)(left_x + pad_x), (float)room_y0,
                                                 (float)(pw - pad_x * 2), (float)empty_h},
                                     1.0f, (Color){56, 70, 92, 255});
                const char *empty_msg = "Nenhuma sala LAN detectada aqui ainda (use Sala Online em Wi-Fi bloqueado).";
                int emp_fs = fit_menu_font(empty_msg, sub_sz, 9, pw - pad_x * 2 - 24);
                DrawText(empty_msg,
                         left_x + pad_x + 12, room_y0 + (int)(10.0f * sc),
                         emp_fs, (Color){165, 178, 198, 255});
                int lh_w = (int)(225.0f * sc);
                if (lh_w < 170) lh_w = 170;
                int lh_bh = (int)(26.0f * sc);
                if (lh_bh < 20) lh_bh = 20;
                if (empty_h >= lh_bh + (int)(30.0f * sc) &&
                    draw_small_button(left_x + pad_x + 12, room_y0 + empty_h - lh_bh - 6,
                                      lh_w, lh_bh, "TESTAR MESMO PC (127.0.0.1)", sub_sz - 1,
                                      (Color){36, 52, 74, 255}, (Color){68, 125, 200, 255})) {
                    snprintf(ip_buf, ip_size, "127.0.0.1");
                    stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                    mp_err_msg[0] = '\0';
                    if (net_init(&lobby_net, NET_MODE_CLIENT, ip_buf, *port_out)) {
                        lobby_active = 1;
                        lobby_ready = 0;
                        lobby_started_burst = 0;
                        last_lobby_tx_t = 0.0;
                        last_lobby_rx_t = now_d;
                        lobby_join_start_t = now_d;
                        page = MENU_PAGE_LOBBY;
                    }
                }
            }

            /* Duas entradas manuais lado a lado: Codigo Online | IP LAN */
            int col_gap = 12;
            int col_w = (pw - pad_x * 2 - col_gap) / 2;
            int col1_x = left_x + pad_x;
            int col2_x = col1_x + col_w + col_gap;
            int lbl_y = man_y - (int)(18.0f * sc);

            DrawText("Entrar Online (Codigo):", col1_x, lbl_y, sub_sz - 1, (Color){125, 215, 255, 255});
            DrawText("Ou IP LAN (opcional):", col2_x, lbl_y, sub_sz - 1, (Color){155, 170, 192, 255});

            int join_bw = (int)(92.0f * sc);
            if (join_bw < 72) join_bw = 72;
            Rectangle code_box = {(float)col1_x, (float)man_y, (float)(col_w - join_bw - 6), (float)man_h};
            Rectangle ip_box   = {(float)col2_x, (float)man_y, (float)(col_w - join_bw - 6), (float)man_h};

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mpos = GetMousePosition();
                if (CheckCollisionPointRec(mpos, code_box)) {
                    mp_editing_code = 1;
                    mp_editing_ip = 0;
                } else if (CheckCollisionPointRec(mpos, ip_box)) {
                    mp_editing_ip = 1;
                    mp_editing_code = 0;
                }
            }

            DrawRectangleRec(code_box, mp_editing_code ? (Color){26, 46, 68, 255} : (Color){20, 26, 36, 255});
            DrawRectangleLinesEx(code_box, 1.5f,
                                 mp_editing_code ? (Color){100, 215, 255, 255} : (Color){72, 102, 132, 255});
            const char *code_disp = (room_code_buf[0] != '\0' || mp_editing_code)
                ? TextFormat("%s%s", room_code_buf, mp_editing_code ? "_" : "")
                : "EX: K7B2";
            DrawText(code_disp, (int)code_box.x + 8, (int)code_box.y + (man_h - b_fnt) / 2, b_fnt,
                     (room_code_buf[0] != '\0' || mp_editing_code) ? (Color){135, 240, 255, 255}
                                                                   : (Color){95, 115, 140, 255});

            if (draw_small_button((int)code_box.x + (int)code_box.width + 6, man_y, join_bw, man_h,
                                  "ENTRAR", b_fnt - 1,
                                  (Color){28, 92, 138, 255}, (Color){48, 138, 198, 255})) {
                mp_editing_code = 0;
                mp_editing_ip = 0;
                stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                mp_err_msg[0] = '\0';
                char r_url[160] = "";
                net_get_relay_url(r_url, (int)sizeof(r_url));
                if (net_init_relay(&lobby_net, NET_MODE_CLIENT, r_url, room_code_buf)) {
                    lobby_active = 1;
                    lobby_ready = 0;
                    lobby_started_burst = 0;
                    last_lobby_tx_t = 0.0;
                    last_lobby_rx_t = now_d;
                    lobby_join_start_t = now_d;
                    page = MENU_PAGE_LOBBY;
                } else {
                    snprintf(mp_err_msg, sizeof(mp_err_msg),
                             "Digite um codigo valido (3 a 8 letras/numeros).");
                }
            }

            DrawRectangleRec(ip_box, mp_editing_ip ? (Color){32, 42, 58, 255} : (Color){22, 26, 34, 255});
            DrawRectangleLinesEx(ip_box, 1.5f,
                                 mp_editing_ip ? (Color){100, 205, 255, 255} : (Color){80, 92, 112, 255});
            DrawText(TextFormat("%s%s", ip_buf, mp_editing_ip ? "_" : ""),
                     (int)ip_box.x + 8, (int)ip_box.y + (man_h - b_fnt) / 2,
                     b_fnt - 1, (Color){255, 232, 120, 255});

            if (draw_small_button((int)ip_box.x + (int)ip_box.width + 6, man_y, join_bw, man_h,
                                  "IP LAN", b_fnt - 1,
                                  (Color){42, 72, 108, 255}, (Color){68, 125, 195, 255})) {
                if (ip_buf[0] == '\0') snprintf(ip_buf, ip_size, "127.0.0.1");
                mp_editing_ip = 0;
                mp_editing_code = 0;
                stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                mp_err_msg[0] = '\0';
                if (net_init(&lobby_net, NET_MODE_CLIENT, ip_buf, *port_out)) {
                    lobby_active = 1;
                    lobby_ready = 0;
                    lobby_started_burst = 0;
                    last_lobby_tx_t = 0.0;
                    last_lobby_rx_t = now_d;
                    lobby_join_start_t = now_d;
                    page = MENU_PAGE_LOBBY;
                } else {
                    snprintf(mp_err_msg, sizeof(mp_err_msg),
                             "IP invalido (%s).", ip_buf);
                }
            }

            int vw = calc_fit_w("VOLTAR", b_fnt, (int)(115.0f * sc), sc);
            if (mp_err_msg[0] != '\0') {
                int err_x = left_x + pad_x + vw + 12;
                int err_w = (left_x + pw - pad_x) - err_x;
                int err_fs = fit_menu_font(mp_err_msg, sub_sz, 9, err_w);
                DrawText(mp_err_msg, err_x,
                         content_y + ph - foot_h + (foot_h - err_fs) / 2 - (int)(10.0f * sc),
                         err_fs, (Color){255, 105, 95, 255});
            }

            if (draw_small_button(left_x + pad_x, content_y + ph - foot_h - (int)(10.0f * sc),
                                  vw, foot_h, "VOLTAR", b_fnt,
                                  (Color){38, 42, 52, 255}, (Color){195, 75, 30, 255})) {
                mp_editing_ip = 0;
                mp_editing_code = 0;
                stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
                page = MENU_PAGE_MAIN;
            }
        } else if (page == MENU_PAGE_LOBBY && lobby_active) {
            int is_host = (lobby_net.mode == NET_MODE_HOST);
            int pw = (int)(1160.0f * sc);
            int max_pw = sw - left_x * 2;
            if (pw > max_pw) pw = max_pw;
            if (pw < 560) pw = 560;
            int ph = (int)(456.0f * sc);
            if (content_y + ph > footer_top) ph = footer_top - content_y;

            int active_mode = is_host ? cfg->mp_game_mode
                                      : (lobby_net.connected ? lobby_net.game_mode : cfg->mp_game_mode);
            int is_pvp = (active_mode == NET_GAME_PVP);

            DrawRectangle(left_x, content_y, pw, ph, (Color){12, 16, 24, 244});
            DrawRectangleLinesEx((Rectangle){(float)left_x, (float)content_y, (float)pw, (float)ph},
                                 2.0f, lobby_net.connected ? (Color){75, 215, 125, 255}
                                                           : (Color){230, 125, 45, 255});

            int b_fnt = (int)(14.5f * sc);
            if (b_fnt < 10) b_fnt = 10;
            int sub_sz = (int)(12.5f * sc);
            if (sub_sz < 9) sub_sz = 9;

            /* Cabecalho do Lobby + Pill com lotacao e modo */
            int is_relay = (lobby_net.transport == NET_TRANSPORT_RELAY);
            char relay_err[128] = "";
            NetRelayStatus relay_st = is_relay
                ? net_relay_get_status(&lobby_net, relay_err, (int)sizeof(relay_err))
                : NET_RELAY_READY;

            const char *role_title = is_relay
                ? (is_host
                    ? TextFormat("SALA ONLINE [%s] (ANFITRIAO) - %d/4", lobby_net.room_code, lobby_net.active_player_count)
                    : TextFormat("SALA ONLINE [%s] (CONVIDADO)", lobby_net.room_code))
                : (is_host
                    ? TextFormat("SALA LAN (ANFITRIAO) - %d/4 JOGADORES", lobby_net.active_player_count)
                    : "SALA LAN (CONVIDADO - ATE 4 JOGADORES)");
            DrawText(role_title, left_x + pad_x, content_y + (int)(12.0f * sc),
                     btn_font - 2, (Color){255, 205, 95, 255});

            const char *conn_lbl = lobby_net.connected
                ? TextFormat("CONECTADO (%d/4) | %s",
                             lobby_net.active_player_count > 1 ? lobby_net.active_player_count : 2,
                             is_pvp ? "PVP" : "CO-OP")
                : (is_relay
                    ? ((relay_st == NET_RELAY_CONNECTING)
                        ? "LIGANDO AO SERVIDOR..."
                        : (is_host ? TextFormat("CODIGO: %s", lobby_net.room_code) : "CHAMANDO HOST..."))
                    : (is_host ? "AGUARDANDO JOGADORES..." : "CONECTANDO NA SALA..."));
            Color conn_bg = lobby_net.connected ? (Color){32, 108, 56, 255}
                          : (is_relay && relay_st == NET_RELAY_READY ? (Color){28, 92, 138, 255}
                                                                     : (Color){135, 78, 22, 255});
            Color conn_fg = lobby_net.connected ? (Color){135, 255, 165, 255}
                          : (is_relay && relay_st == NET_RELAY_READY ? (Color){155, 235, 255, 255}
                                                                     : (Color){255, 220, 130, 255});
            int cw = MeasureText(conn_lbl, b_fnt) + 22;
            int cx = left_x + pw - pad_x - cw;
            int cy = content_y + (int)(10.0f * sc);
            DrawRectangle(cx, cy, cw, (int)(28.0f * sc), conn_bg);
            DrawRectangleLinesEx((Rectangle){(float)cx, (float)cy, (float)cw, (float)(28.0f * sc)},
                                 1.5f, conn_fg);
            DrawText(conn_lbl, cx + 11, cy + ((int)(28.0f * sc) - b_fnt) / 2, b_fnt, conn_fg);

            /* Faixa de status/IP da sala */
            int tip_y = content_y + (int)(44.0f * sc);
            int guest_no_reply = (!is_host && !lobby_net.connected &&
                                  (!is_relay || relay_st == NET_RELAY_READY) &&
                                  (now_d - lobby_join_start_t) > 3.5);
            const char *tip_str = (mp_err_msg[0] != '\0')
                ? mp_err_msg
                : (is_relay
                    ? ((relay_st == NET_RELAY_CONNECTING)
                        ? "Conectando ao servidor online (porta 443)... aguarde ate ~25s se estiver acordando."
                        : (is_host
                            ? TextFormat("Sala online [%s] ativa! Seus amigos entram digitando o codigo: %s (Modo %s).",
                                         lobby_net.room_code, lobby_net.room_code, is_pvp ? "PvP" : "Co-op")
                            : (lobby_net.connected
                                ? TextFormat("Conectado a sala online [%s]! Voce e o Jogador %d.",
                                             lobby_net.room_code,
                                             (lobby_net.local_slot >= 1 ? lobby_net.local_slot + 1 : 2))
                                : (guest_no_reply
                                    ? TextFormat("Sala %s conectada no servidor, aguardando o Anfitriao responder...",
                                                 lobby_net.room_code)
                                    : TextFormat("Chamando Anfitriao da sala online [%s]...", lobby_net.room_code)))))
                    : (is_host
                        ? (lobby_net.connected
                            ? TextFormat("IPs LAN: %s (porta %d) | Ate 4 jogadores | Modo %s ativo.",
                                         local_ip_str, *port_out, is_pvp ? "PvP" : "Co-op")
                            : TextFormat("IP LAN: %s (porta %d) | Mesma rede local ou Hotspot do celular.",
                                         local_ip_str, *port_out))
                        : (lobby_net.connected
                            ? TextFormat("Conectado ao Anfitriao (%s)! Voce e o Jogador %d.",
                                         ip_buf,
                                         (lobby_net.local_slot >= 1 ? lobby_net.local_slot + 1 : 2))
                            : (guest_no_reply
                                ? TextFormat("Sem resposta de %s:%d! Se o Wi-Fi bloquear LAN, use Sala Online ou Hotspot.",
                                             ip_buf, *port_out)
                                : TextFormat("Chamando sala LAN em %s (porta %d)...", ip_buf, *port_out)))));
            int tip_sz = sub_sz;
            while (tip_sz > 9 && MeasureText(tip_str, tip_sz) > pw - pad_x * 2) {
                tip_sz--;
            }
            Color tip_col = (mp_err_msg[0] != '\0' || guest_no_reply)
                ? (Color){255, 115, 100, 255}
                : (Color){180, 200, 225, 255};
            DrawText(tip_str, left_x + pad_x, tip_y, tip_sz, tip_col);

            /* Area central: 4 cartoes de jogador com boneco ASCII ao vivo + 1 coluna de Modo/Fase */
            int col_y = tip_y + (int)(24.0f * sc);
            int foot_h = (int)(40.0f * sc);
            if (foot_h < 28) foot_h = 28;
            int col_h = ph - (col_y - content_y) - foot_h - (int)(18.0f * sc);
            int col_gap = (int)(10.0f * sc);
            if (col_gap < 6) col_gap = 6;
            int inner_w = pw - pad_x * 2;
            int cfg_w = (int)((float)inner_w * 0.24f);
            if (cfg_w < 190) cfg_w = 190;
            if (cfg_w > 275) cfg_w = 275;
            int p_card_w = (inner_w - cfg_w - col_gap * 4) / 4;

            int my_slot = is_host ? 0
                                  : ((lobby_net.local_slot >= 1 && lobby_net.local_slot < NET_MAX_PLAYERS)
                                        ? lobby_net.local_slot : 1);
            int total_chars = player_roster_count(&cfg->roster);
            CharacterClass local_cls = player_roster_get(&cfg->roster, cfg->selected_character);
            int sw_bw = (int)(32.0f * sc);
            if (sw_bw < 22) sw_bw = 22;
            int sw_bh = (int)(26.0f * sc);
            if (sw_bh < 20) sw_bh = 20;

            static const Color SLOT_ACCENTS[4] = {
                {115, 200, 255, 255},
                {255, 185, 100, 255},
                {110, 235, 150, 255},
                {230, 130, 250, 255}
            };

            for (int slot = 0; slot < NET_MAX_PLAYERS; slot++) {
                int cx_card = left_x + pad_x + slot * (p_card_w + col_gap);
                int occupied = (slot == my_slot) || lobby_net.slots[slot].active;
                Color acc = SLOT_ACCENTS[slot];
                Color border_c = occupied ? acc : (Color){58, 70, 92, 255};

                DrawRectangle(cx_card, col_y, p_card_w, col_h, (Color){18, 23, 34, 255});
                DrawRectangleLinesEx((Rectangle){(float)cx_card, (float)col_y, (float)p_card_w, (float)col_h},
                                     occupied ? 1.8f : 1.0f, border_c);

                const char *hdr = (slot == 0)
                    ? TextFormat("P1 (HOST)%s", (slot == my_slot) ? " *" : "")
                    : TextFormat("P%d%s", slot + 1, (slot == my_slot) ? " (VOCE)" : "");
                DrawText(hdr, cx_card + 8, col_y + 8, sub_sz, acc);

                if (occupied) {
                    const char *sname = (slot == my_slot)
                        ? local_cls.name
                        : (lobby_net.slots[slot].name[0] ? lobby_net.slots[slot].name : "Conectado");
                    int s_vid = (slot == my_slot) ? local_cls.visual_id : lobby_net.slots[slot].visual_id;
                    const CharacterAppearance *s_app = (slot == my_slot)
                        ? &local_cls.appearance : &lobby_net.slots[slot].appearance;
                    int s_ready = (slot == 0) ? 1
                        : ((slot == my_slot) ? lobby_ready : lobby_net.slots[slot].ready);

                    int name_sz = sub_sz + 1;
                    while (name_sz > 9 && MeasureText(sname, name_sz) > p_card_w - 16) {
                        name_sz--;
                    }
                    DrawText(sname, cx_card + 8, col_y + (int)(26.0f * sc), name_sz, WHITE);

                    const char *st_txt = (slot == 0)
                        ? "PRONTO (HOST)"
                        : (s_ready ? "PRONTO!" : "PREPARANDO...");
                    Color st_col = s_ready ? (Color){110, 235, 145, 255} : (Color){245, 195, 85, 255};
                    DrawText(st_txt, cx_card + 8, col_y + (int)(44.0f * sc), sub_sz - 1, st_col);

                    /* Preview ASCII animado do personagem do jogador dentro do cartao */
                    int pv_top = col_y + (int)(62.0f * sc);
                    int pv_bot = col_y + col_h - ((slot == my_slot) ? (sw_bh + 14) : 10);
                    int pv_h = pv_bot - pv_top;
                    if (pv_h > 36) {
                        draw_lobby_character_preview(cx_card + 6, pv_top,
                                                     p_card_w - 12, pv_h,
                                                     s_vid, s_app, now + (float)slot * 0.5f);
                    }

                    if (slot == my_slot && total_chars > 1) {
                        int cy_btn = col_y + col_h - sw_bh - 8;
                        if (draw_small_button(cx_card + 8, cy_btn, sw_bw, sw_bh, "<", b_fnt,
                                              (Color){38, 46, 62, 255}, (Color){210, 90, 35, 255})) {
                            cfg->selected_character = (cfg->selected_character + total_chars - 1) % total_chars;
                        }
                        if (draw_small_button(cx_card + p_card_w - sw_bw - 8, cy_btn, sw_bw, sw_bh, ">", b_fnt,
                                              (Color){38, 46, 62, 255}, (Color){210, 90, 35, 255})) {
                            cfg->selected_character = (cfg->selected_character + 1) % total_chars;
                        }
                        const char *skin_lbl = "SKIN";
                        int sk_w = MeasureText(skin_lbl, sub_sz - 1);
                        DrawText(skin_lbl, cx_card + (p_card_w - sk_w) / 2,
                                 cy_btn + (sw_bh - (sub_sz - 1)) / 2,
                                 sub_sz - 1, (Color){165, 180, 200, 255});
                    }
                } else {
                    const char *empty_t = "[ VAGA LIVRE ]";
                    const char *wait_t  = "Aguardando...";
                    int ew = MeasureText(empty_t, sub_sz);
                    int ww = MeasureText(wait_t, sub_sz - 1);
                    DrawText(empty_t, cx_card + (p_card_w - ew) / 2, col_y + col_h / 2 - 14,
                             sub_sz, (Color){95, 110, 132, 255});
                    DrawText(wait_t, cx_card + (p_card_w - ww) / 2, col_y + col_h / 2 + 8,
                             sub_sz - 1, (Color){78, 92, 112, 255});
                }
            }

            /* Coluna 5: MODO (CO-OP / PVP) + FASE + MINI-MAPA */
            int c3_x = left_x + pad_x + 4 * (p_card_w + col_gap);
            int total_lvls = map_level_count();
            int active_lvl_idx = is_host ? cfg->selected_level
                                         : map_level_clamp(lobby_net.connected ? lobby_net.remote_level
                                                                               : cfg->selected_level);
            if (preview_loaded_idx != active_lvl_idx) {
                map_load_level(active_lvl_idx, &level_preview_map);
                preview_loaded_idx = active_lvl_idx;
            }
            const MapLevelInfo *mp_lvl = map_level_get(active_lvl_idx);

            DrawRectangle(c3_x, col_y, cfg_w, col_h, (Color){20, 25, 36, 255});
            DrawRectangleLinesEx((Rectangle){(float)c3_x, (float)col_y, (float)cfg_w, (float)col_h},
                                 1.5f, (Color){95, 130, 165, 255});
            DrawText("MODO E FASE", c3_x + 10, col_y + 10, b_fnt, (Color){135, 230, 175, 255});

            /* Botao/Badge de selecao Co-op vs PvP */
            int mode_by = col_y + (int)(32.0f * sc);
            int mode_bh = (int)(28.0f * sc);
            if (mode_bh < 22) mode_bh = 22;
            const char *mode_btn_lbl = is_pvp ? "MODO: PVP" : "MODO: CO-OP";
            Color mode_base = is_pvp ? (Color){145, 42, 42, 255} : (Color){32, 102, 62, 255};
            Color mode_hov  = is_pvp ? (Color){195, 62, 52, 255} : (Color){52, 148, 88, 255};
            if (is_host) {
                if (draw_small_button(c3_x + 10, mode_by, cfg_w - 20, mode_bh,
                                      mode_btn_lbl, sub_sz, mode_base, mode_hov)) {
                    cfg->mp_game_mode = is_pvp ? NET_GAME_COOP : NET_GAME_PVP;
                    lobby_net.game_mode = cfg->mp_game_mode;
                }
            } else {
                DrawRectangle(c3_x + 10, mode_by, cfg_w - 20, mode_bh, mode_base);
                DrawRectangleLinesEx((Rectangle){(float)(c3_x + 10), (float)mode_by,
                                                 (float)(cfg_w - 20), (float)mode_bh}, 1.0f, WHITE);
                int mw_t = MeasureText(mode_btn_lbl, sub_sz);
                DrawText(mode_btn_lbl, c3_x + 10 + (cfg_w - 20 - mw_t) / 2,
                         mode_by + (mode_bh - sub_sz) / 2, sub_sz, WHITE);
            }

            int lvl_y = mode_by + mode_bh + (int)(8.0f * sc);
            int lvl_sz = sub_sz;
            while (lvl_sz > 9 && MeasureText(mp_lvl->name, lvl_sz) > cfg_w - 20) {
                lvl_sz--;
            }
            DrawText(mp_lvl->name, c3_x + 10, lvl_y, lvl_sz, WHITE);

            int mm_top = lvl_y + lvl_sz + (int)(6.0f * sc);
            int mm_bot = col_y + col_h - (is_host ? (sw_bh + 14) : 10);
            int mp_prev_sz = mm_bot - mm_top;
            if (mp_prev_sz > cfg_w - 22) mp_prev_sz = cfg_w - 22;
            if (mp_prev_sz < 64) mp_prev_sz = 64;
            draw_level_minimap_preview(c3_x + (cfg_w - mp_prev_sz) / 2,
                                       mm_top,
                                       mp_prev_sz, &level_preview_map,
                                       mp_lvl->spawn1_x, mp_lvl->spawn1_y);

            if (is_host && total_lvls > 1) {
                int cy_btn = col_y + col_h - sw_bh - 8;
                if (draw_small_button(c3_x + 10, cy_btn, sw_bw, sw_bh, "<", b_fnt,
                                      (Color){38, 46, 62, 255}, (Color){210, 90, 35, 255})) {
                    cfg->selected_level = (cfg->selected_level + total_lvls - 1) % total_lvls;
                }
                if (draw_small_button(c3_x + cfg_w - sw_bw - 10, cy_btn, sw_bw, sw_bh, ">", b_fnt,
                                      (Color){38, 46, 62, 255}, (Color){210, 90, 35, 255})) {
                    cfg->selected_level = (cfg->selected_level + 1) % total_lvls;
                }
            }

            /* Rodape de acoes do Lobby */
            int foot_y = content_y + ph - foot_h - (int)(10.0f * sc);
            int leave_w = (int)(185.0f * sc);
            if (leave_w < 130) leave_w = 130;
            if (draw_small_button(left_x + pad_x, foot_y, leave_w, foot_h,
                                  is_host ? "FECHAR SALA" : "SAIR DA SALA", b_fnt,
                                  (Color){62, 36, 40, 255}, (Color){195, 65, 55, 255})) {
                leave_mp_lobby(&lobby_active, &lobby_net);
                page = MENU_PAGE_MULTIPLAYER;
            }

            int go_w = (int)(340.0f * sc);
            if (go_w > pw - pad_x * 2 - leave_w - 16) go_w = pw - pad_x * 2 - leave_w - 16;
            int go_x = left_x + pw - pad_x - go_w;
            if (is_host) {
                if (lobby_net.connected && lobby_net.active_player_count >= 2) {
                    const char *start_lbl = TextFormat("INICIAR %s (%d/4)",
                                                       is_pvp ? "PVP" : "CO-OP",
                                                       lobby_net.active_player_count);
                    if (draw_small_button(go_x, foot_y, go_w, foot_h, start_lbl, b_fnt,
                                          (Color){38, 128, 62, 255}, (Color){56, 178, 86, 255})) {
                        net_host_update_local_slot(&lobby_net, local_cls.name, cfg->selected_character,
                                                   local_cls.visual_id, &local_cls.appearance,
                                                   cfg->mp_game_mode, cfg->selected_level);
                        /* Dispara o sinal de inicio varias vezes pra garantir entrega em UDP sem perda */
                        for (int k = 0; k < 6; k++) {
                            net_broadcast_lobby_roster(&lobby_net, cfg->selected_level, mp_lvl->name, 1);
                        }
                        if (net_out) {
                            *net_out = lobby_net;
                        }
                        EndDrawing();
                        return 2;
                    }
                } else {
                    draw_small_button(go_x, foot_y, go_w, foot_h,
                                      "AGUARDANDO CONEXAO...", b_fnt,
                                      (Color){34, 38, 48, 255}, (Color){40, 46, 56, 255});
                }
            } else if (!lobby_net.connected) {
                draw_small_button(go_x, foot_y, go_w, foot_h,
                                  "AGUARDANDO RESPOSTA DO HOST...", b_fnt,
                                  (Color){34, 38, 48, 255}, (Color){40, 46, 56, 255});
            } else {
                const char *r_lbl = lobby_ready
                    ? "PRONTO! (AGUARDANDO ANFITRIAO)"
                    : "ESTOU PRONTO!";
                if (draw_small_button(go_x, foot_y, go_w, foot_h, r_lbl, b_fnt,
                                      lobby_ready ? (Color){38, 128, 62, 255} : (Color){185, 88, 28, 255},
                                      (Color){64, 172, 92, 255})) {
                    lobby_ready = !lobby_ready;
                }
            }
        } else if (page == MENU_PAGE_CUSTOMIZE) {
            /* Lista paginada (4 por pagina) das classes pre-definidas e skins criadas */
            int pw = (int)(490.0f * sc);
            int ph = footer_top - content_y;
            int row_h = (ph - (int)(155 * sc)) / 5;
            int text_sz = (int)(16.0f * sc);
            if (text_sz < 11) text_sz = 11;
            int total = player_roster_count(&cfg->roster);
            int pages = (total + 3) / 4;
            DrawRectangle(left_x, content_y, pw, ph, (Color){14, 18, 25, 240});
            DrawText("SELECIONAR PERSONAGEM", left_x + pad_x, content_y + 12,
                     btn_font, (Color){255, 150, 180, 255});
            if (IsKeyPressed(KEY_DOWN)) {
                cfg->selected_character = (cfg->selected_character + 1) % total;
                list_page = cfg->selected_character / 4;
            }
            if (IsKeyPressed(KEY_UP)) {
                cfg->selected_character = (cfg->selected_character + total - 1) % total;
                list_page = cfg->selected_character / 4;
            }
            int list_y = content_y + (int)(48 * sc);
            for (int slot = 0; slot < 4; slot++) {
                int i = list_page * 4 + slot;
                if (i >= total) break;
                CharacterClass cls = player_roster_get(&cfg->roster, i);
                int selected = cfg->selected_character == i;
                if (draw_small_button(left_x + pad_x, list_y + slot * row_h,
                                      pw - pad_x * 2, row_h - 4, cls.name, text_sz,
                                      selected ? (Color){140, 38, 70, 255} : (Color){35, 40, 52, 255},
                                      (Color){190, 55, 95, 255})) cfg->selected_character = i;
            }
            int create_y = list_y + 4 * row_h;
            int full = cfg->roster.count >= MAX_CUSTOM_CHARACTERS;
            int create = draw_small_button(left_x + pad_x, create_y, pw - pad_x * 2, row_h - 4,
                            full ? "LIMITE DE 8 PERSONAGENS CRIADOS" : "+ CRIAR SKIN [N]", text_sz,
                            full ? (Color){45, 45, 50, 255} : (Color){35, 95, 75, 255}, (Color){50, 135, 95, 255});
            if (!full && (create || IsKeyPressed(KEY_N))) {
                draft = player_get_preset_class(PRESET_FUZILEIRO);
                player_appearance_init(&draft.appearance);

                draft.name[0] = '\0';
                name_editing = 1;
                creator_error = "";
                while (GetCharPressed() > 0) {} /* N nao entra no campo de nome. */
                page = MENU_PAGE_CREATE;
            }
            int pager_y = create_y + row_h;
            int arrow_w = (int)(35 * sc), pager_h = (int)(25 * sc);
            if (draw_small_button(left_x + pad_x, pager_y, arrow_w, pager_h, "<", text_sz,
                                  (Color){35,40,52,255}, (Color){85,70,95,255})) {
                list_page = (list_page + pages - 1) % pages;
                cfg->selected_character = list_page * 4;
            }
            DrawText(TextFormat("PAGINA %d/%d", list_page + 1, pages), left_x + pad_x + arrow_w + 10,
                     pager_y + 4, text_sz, LIGHTGRAY);
            if (draw_small_button(left_x + pw - pad_x - arrow_w, pager_y, arrow_w, pager_h, ">", text_sz,
                                  (Color){35,40,52,255}, (Color){85,70,95,255})) {
                list_page = (list_page + 1) % pages;
                cfg->selected_character = list_page * 4;
            }
            CharacterClass cls = player_roster_get(&cfg->roster, cfg->selected_character);
            DrawText(TextFormat("VIDA %d  |  MUN %d  |  VEL %.1f", cls.max_health, cls.start_ammo, cls.move_speed),
                     left_x + pad_x, pager_y + pager_h + (int)(9 * sc), text_sz, WHITE);
            if (draw_small_button(left_x + pad_x, content_y + ph - (int)(43 * sc),
                                  pw - pad_x * 2, (int)(32 * sc), "CONFIRMAR / VOLTAR", text_sz,
                                  (Color){65, 38, 52, 255}, (Color){190, 55, 95, 255}) ||
                (page == MENU_PAGE_CUSTOMIZE && page_at_frame_start == MENU_PAGE_CUSTOMIZE && IsKeyPressed(KEY_ENTER)))
                page = MENU_PAGE_MAIN;
        } else if (page == MENU_PAGE_CREATE) {
            int result = draw_character_creator(&draft, left_x, content_y, (int)(490 * sc),
                                                 footer_top - content_y, sc, &name_editing, creator_error);
            if (result < 0) page = MENU_PAGE_CUSTOMIZE;
            if (result > 0) {
                int created = player_roster_add(&cfg->roster, &draft);
                if (created < 0) creator_error = "Informe um nome para salvar.";
                else {
                    cfg->selected_character = created;
                    list_page = created / 4;
                    page = MENU_PAGE_CUSTOMIZE;
                }
            }
        } else if (page == MENU_PAGE_SETTINGS) {
            if (draw_settings_panel(cfg, left_x, content_y, footer_top - content_y,
                                    sw, sc, pad_x, 0, &settings_tab,
                                    &listening_bind, "VOLTAR")) {
                page = MENU_PAGE_MAIN;
            }
        }

        if (status_msg && status_msg[0] != '\0') {
            DrawText(status_msg, left_x, footer_top - (int)(26.0f * sc),
                     (int)(16.0f * sc), (Color){255, 95, 85, 255});
        }

        /* Barra de rodape discreta e limpa */
        DrawText("[W/S/MOUSE] NAVEGAR   |   [ESC] VOLTAR / SAIR   |   [F11] TELA CHEIA",
                 left_x, sh - (int)(28.0f * sc), (int)(14.0f * sc),
                 (Color){165, 178, 195, 230});

        /* Contador de FPS no canto superior direito: desativado por padrao */
        if (cfg->show_fps) {
            int fps_sz = (int)(16.0f * sc);
            if (fps_sz < 14) fps_sz = 14;
            const char *fps_txt = TextFormat("FPS: %d", GetFPS());
            int fps_w = MeasureText(fps_txt, fps_sz);
            DrawText(fps_txt, sw - fps_w - 20, 14, fps_sz, (Color){115, 230, 135, 255});
        }

        EndDrawing();
    }

    stop_lan_discovery(&disc_active, &disc_sock, &disc_count);
    leave_mp_lobby(&lobby_active, &lobby_net);
    return 4;
}

/* Retorna 1 se a celula (tx, ty) for chao comum aberto e caminhavel (longe de paredes e armadilhas). */
static int is_safe_mp_tile(const GameMap *map, int tx, int ty) {
    if (!map || tx < 1 || tx >= map->width - 1 || ty < 1 || ty >= map->height - 1) {
        return 0;
    }
    char raw = map_get_raw_tile(map, tx, ty);
    if (raw != '.' && raw != 'P' && raw != ' ' && !map_is_enemy_spawn_char(raw)) {
        return 0;
    }
    float cx = (float)tx + 0.5f;
    float cy = (float)ty + 0.5f;
    const float r = 0.28f;
    if (map_is_wall(map, tx, ty) ||
        map_is_wall(map, (int)floorf(cx - r), (int)floorf(cy - r)) ||
        map_is_wall(map, (int)floorf(cx + r), (int)floorf(cy - r)) ||
        map_is_wall(map, (int)floorf(cx - r), (int)floorf(cy + r)) ||
        map_is_wall(map, (int)floorf(cx + r), (int)floorf(cy + r))) {
        return 0;
    }
    return 1;
}

/* Calcula 4 pontos de spawn garantidamente caminhaveis via BFS a partir de ('P') do mapa:
 * - No modo Co-op: posiciona todos os 4 slots em piso livre lado a lado ao Anfitriao.
 * - No modo PvP : espalha os 4 slots pelo ponto mais distante possivel da arena alcancavel. */
static void compute_mp_spawn_points(const GameMap *map, float base_x, float base_y,
                                    int game_mode,
                                    float out_x[NET_MAX_PLAYERS],
                                    float out_y[NET_MAX_PLAYERS],
                                    float out_ang[NET_MAX_PLAYERS]) {
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        out_x[i] = base_x;
        out_y[i] = base_y;
        out_ang[i] = PI_F * 0.5f;
    }
    if (!map) {
        return;
    }

    int sx = (int)floorf(base_x);
    int sy = (int)floorf(base_y);
    if (!is_safe_mp_tile(map, sx, sy)) {
        int found = 0;
        for (int y = 1; y < map->height - 1 && !found; y++) {
            for (int x = 1; x < map->width - 1; x++) {
                if (is_safe_mp_tile(map, x, y)) {
                    sx = x;
                    sy = y;
                    base_x = (float)x + 0.5f;
                    base_y = (float)y + 0.5f;
                    found = 1;
                    break;
                }
            }
        }
    }

    unsigned char vis[MAP_HEIGHT][MAP_WIDTH];
    memset(vis, 0, sizeof(vis));
    int qx[MAP_WIDTH * MAP_HEIGHT];
    int qy[MAP_WIDTH * MAP_HEIGHT];
    int head = 0, tail = 0;

    if (sx >= 0 && sx < map->width && sy >= 0 && sy < map->height) {
        vis[sy][sx] = 1;
        qx[tail] = sx;
        qy[tail] = sy;
        tail++;
    }

    static const int DX[4] = {1, -1, 0, 0};
    static const int DY[4] = {0, 0, 1, -1};
    while (head < tail) {
        int cx = qx[head];
        int cy = qy[head];
        head++;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d];
            int ny = cy + DY[d];
            if (nx >= 1 && nx < map->width - 1 && ny >= 1 && ny < map->height - 1 &&
                !vis[ny][nx] && is_safe_mp_tile(map, nx, ny)) {
                vis[ny][nx] = 1;
                qx[tail] = nx;
                qy[tail] = ny;
                tail++;
            }
        }
    }

    if (tail <= 0) {
        return;
    }

    out_x[0] = (float)qx[0] + 0.5f;
    out_y[0] = (float)qy[0] + 0.5f;

    if (game_mode == NET_GAME_COOP) {
        /* Em Co-op, mantem P2..P4 nas celulas mais proximas em visao direta ao lado do Host */
        int picked[NET_MAX_PLAYERS];
        picked[0] = 0;
        for (int s = 1; s < NET_MAX_PLAYERS; s++) {
            int best = (s < tail) ? s : (tail - 1);
            picked[s] = best;
            out_x[s] = (float)qx[best] + 0.5f;
            out_y[s] = (float)qy[best] + 0.5f;
            /* Faz P2..P4 olhar para o centro do grupo para se enxergarem no inicio */
            float ddx = out_x[0] - out_x[s];
            float ddy = out_y[0] - out_y[s];
            out_ang[s] = (fabsf(ddx) + fabsf(ddy) > 0.1f) ? atan2f(ddy, ddx) : (PI_F * 0.5f);
        }
        (void)picked;
        return;
    }

    /* Em PvP, usa selecao gulosa pelo ponto mais distante na arena conectada */
    int chosen[NET_MAX_PLAYERS];
    chosen[0] = 0;
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        float best_min_d2 = -1.0f;
        int best_idx = 0;
        for (int k = 0; k < tail; k++) {
            float min_d2 = 1e9f;
            for (int p = 0; p < s; p++) {
                float dx = (float)(qx[k] - qx[chosen[p]]);
                float dy = (float)(qy[k] - qy[chosen[p]]);
                float d2 = dx * dx + dy * dy;
                if (d2 < min_d2) min_d2 = d2;
            }
            if (min_d2 > best_min_d2) {
                best_min_d2 = min_d2;
                best_idx = k;
            }
        }
        chosen[s] = best_idx;
        out_x[s] = (float)qx[best_idx] + 0.5f;
        out_y[s] = (float)qy[best_idx] + 0.5f;
    }

    float cx_mean = 0.0f, cy_mean = 0.0f;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        cx_mean += out_x[s];
        cy_mean += out_y[s];
    }
    cx_mean *= 0.25f;
    cy_mean *= 0.25f;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        float dx = cx_mean - out_x[s];
        float dy = cy_mean - out_y[s];
        if (fabsf(dx) + fabsf(dy) > 0.1f) {
            out_ang[s] = atan2f(dy, dx);
        }
    }
}

/* Faz raymarch simples a cada 0.08 unidades para conferir se nao ha parede entre dois jogadores */
static int mp_has_line_of_sight(const GameMap *map, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.05f) {
        return 1;
    }
    int steps = (int)(dist / 0.08f);
    if (steps < 1) steps = 1;
    for (int i = 1; i < steps; i++) {
        float t = (float)i / (float)steps;
        int tx = (int)floorf(x0 + dx * t);
        int ty = (int)floorf(y0 + dy * t);
        if (map_is_wall(map, tx, ty)) {
            return 0;
        }
    }
    return 1;
}

/* Testa acerto PvP hitscan/espada contra os outros jogadores ativos com checagem de parede */
static int check_pvp_player_hit(const GameMap *map, Player *shooter, NetContext *net,
                                const Weapon *weapons, int my_slot) {
    if (!map || !shooter || !net || !weapons || shooter->health <= 0) {
        return 0;
    }
    int berserk = (shooter->ability_state.berserk_time > 0.0f);
    int multiplier = (shooter->ability == ABILITY_GUERRILLA && shooter->ability_state.boosted_shot) ? 2 : 1;
    shooter->ability_state.boosted_shot = 0;
    int aw = (shooter->active_weapon >= 0 && shooter->active_weapon < MAX_WEAPONS)
        ? shooter->active_weapon : WEAPON_PISTOL;
    float best_dist = berserk ? 1.5f : weapons[aw].range;
    int best_slot = -1;

    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (s == my_slot || !net->slots[s].active || !net->slots[s].has_pos || net->slots[s].health <= 0) {
            continue;
        }
        float dx = net->slots[s].x - shooter->x;
        float dy = net->slots[s].y - shooter->y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 0.05f || dist > best_dist) {
            continue;
        }
        float diff = atan2f(dy, dx) - shooter->angle;
        while (diff > PI_F) diff -= 2.0f * PI_F;
        while (diff < -PI_F) diff += 2.0f * PI_F;

        float cone = berserk ? 0.45f : fmaxf(0.12f, atan2f(0.32f, dist));
        if (fabsf(diff) <= cone && mp_has_line_of_sight(map, shooter->x, shooter->y,
                                                         net->slots[s].x, net->slots[s].y)) {
            best_dist = dist;
            best_slot = s;
        }
    }

    if (best_slot >= 0) {
        int base_dmg = berserk ? 45 : weapons[aw].damage;
        int dmg = player_attack_damage(shooter, base_dmg * multiplier);
        if (dmg < 1) dmg = 1;
        net->outgoing_pvp_dmg[best_slot] += dmg;
        shooter->score += dmg;
        return 1;
    }
    return 0;
}

/* Desenha painel compacto no canto superior direito com nome e barra de vida
 * de todos os jogadores ativos (somente quem permanece conectado na partida). */
static void draw_mp_players_hp_hud(int sw, int sh, float sc,
                                   const Player *local_player, const NetContext *net,
                                   int my_slot, int is_pvp) {
    (void)sh;
    if (!local_player || !net) {
        return;
    }

    int active_slots[NET_MAX_PLAYERS];
    int n_show = 0;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (s == my_slot || net->slots[s].active) {
            active_slots[n_show++] = s;
        }
    }
    if (n_show <= 0) {
        return;
    }

    int f_hud = (sw < 1000) ? 12 : 16;
    int start_y = 12 + (f_hud + 14) + 3 + (f_hud + 10) + 10;

    int panel_w = (int)(230.0f * sc);
    if (panel_w < 175) panel_w = 175;
    if (panel_w > 265) panel_w = 265;
    int panel_x = sw - panel_w - 12;

    int hdr_sz = (int)(12.0f * sc);
    if (hdr_sz < 10) hdr_sz = 10;
    int nm_sz = (int)(12.0f * sc);
    if (nm_sz < 10) nm_sz = 10;
    int row_h = (int)(30.0f * sc);
    if (row_h < 25) row_h = 25;
    int pad_x = 9;
    int hdr_h = hdr_sz + 10;
    int panel_h = hdr_h + n_show * row_h + 6;

    DrawRectangle(panel_x, start_y, panel_w, panel_h, (Color){12, 16, 24, 218});
    DrawRectangleLinesEx((Rectangle){(float)panel_x, (float)start_y, (float)panel_w, (float)panel_h},
                         1.5f, is_pvp ? (Color){225, 90, 68, 220} : (Color){82, 195, 138, 220});

    const char *hdr_txt = TextFormat("JOGADORES (%d/4)", net->active_player_count);
    DrawText(hdr_txt, panel_x + pad_x, start_y + 5, hdr_sz,
             is_pvp ? (Color){255, 190, 130, 255} : (Color){145, 240, 185, 255});

    static const Color SLOT_COLS[NET_MAX_PLAYERS] = {
        {115, 200, 255, 255},
        {255, 185, 100, 255},
        {110, 235, 150, 255},
        {230, 130, 250, 255}
    };

    for (int i = 0; i < n_show; i++) {
        int s = active_slots[i];
        int ry = start_y + hdr_h + i * row_h;
        int is_me = (s == my_slot);

        int hp = is_me ? local_player->health : net->slots[s].health;
        int max_hp = is_me ? local_player->max_health : net->slots[s].max_health;
        if (max_hp <= 0) max_hp = 100;
        if (hp < 0) hp = 0;

        const char *raw_name = is_me
            ? (local_player->class_name[0] ? local_player->class_name : "Voce")
            : (net->slots[s].name[0] ? net->slots[s].name : "Jogador");

        char name_buf[48];
        snprintf(name_buf, sizeof(name_buf), "P%d %s%s",
                 s + 1, raw_name, is_me ? "*" : "");

        char val_buf[48];
        if (hp <= 0) {
            snprintf(val_buf, sizeof(val_buf), "CAIU");
        } else if (is_pvp) {
            snprintf(val_buf, sizeof(val_buf), "%d/%d K:%d", hp, max_hp, net->slot_pvp_kills[s]);
        } else {
            snprintf(val_buf, sizeof(val_buf), "%d/%d", hp, max_hp);
        }

        int vw = MeasureText(val_buf, nm_sz);
        int max_nw = panel_w - pad_x * 2 - vw - 8;
        while (strlen(name_buf) > 4 && MeasureText(name_buf, nm_sz) > max_nw) {
            name_buf[strlen(name_buf) - 1] = '\0';
        }

        Color hp_col = (hp <= 0) ? (Color){235, 68, 68, 255}
                     : (hp * 100 > max_hp * 60) ? (Color){78, 226, 122, 255}
                     : (hp * 100 > max_hp * 30) ? (Color){248, 200, 58, 255}
                                                : (Color){240, 72, 72, 255};

        DrawText(name_buf, panel_x + pad_x, ry + 2, nm_sz, SLOT_COLS[s]);
        DrawText(val_buf, panel_x + panel_w - pad_x - vw, ry + 2, nm_sz,
                 (hp <= 0) ? (Color){255, 95, 85, 255} : WHITE);

        int bar_y = ry + nm_sz + 5;
        int bar_w = panel_w - pad_x * 2;
        int bar_h = 7;
        float ratio = (float)hp / (float)max_hp;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;

        DrawRectangle(panel_x + pad_x, bar_y, bar_w, bar_h, (Color){28, 34, 46, 255});
        int fill_w = (int)roundf((float)bar_w * ratio);
        if (fill_w > 0) {
            DrawRectangle(panel_x + pad_x, bar_y, fill_w, bar_h, hp_col);
        }
        DrawRectangleLines(panel_x + pad_x, bar_y, bar_w, bar_h, (Color){58, 70, 92, 255});
    }
}

/* Indicativo visual de queimadura (burn) do jogador nas bordas da tela e alerta */
static void draw_player_damage_overlay(int sw, int sh, float hurt_timer, int last_damage) {
    if (hurt_timer <= 0.0f) {
        return;
    }
    int hud_h = (int)((float)sh * 0.145f);
    if (hud_h < 82) hud_h = 82;
    int view_h = sh - hud_h;
    if (view_h < 40) view_h = sh;

    float t = hurt_timer / 0.35f;
    if (t > 1.0f) t = 1.0f;

    /* Flash vermelho sutil na tela toda + vinheta forte nas bordas */
    unsigned char wash_a = (unsigned char)(62.0f * t);
    unsigned char edge_a = (unsigned char)(195.0f * t);
    unsigned char inner_a = (unsigned char)(105.0f * t);
    int b1 = (int)((float)sw * 0.022f);
    if (b1 < 12) b1 = 12;
    if (b1 > 30) b1 = 30;
    int b2 = b1 / 2;

    DrawRectangle(0, 0, sw, view_h, (Color){210, 20, 24, wash_a});
    DrawRectangle(0, 0, sw, b1, (Color){235, 28, 32, edge_a});
    DrawRectangle(0, view_h - b1, sw, b1, (Color){235, 28, 32, edge_a});
    DrawRectangle(0, b1, b1, view_h - b1 * 2, (Color){235, 28, 32, edge_a});
    DrawRectangle(sw - b1, b1, b1, view_h - b1 * 2, (Color){235, 28, 32, edge_a});
    DrawRectangle(b1, b1, sw - b1 * 2, b2, (Color){255, 70, 55, inner_a});
    DrawRectangle(b1, view_h - b1 - b2, sw - b1 * 2, b2, (Color){255, 70, 55, inner_a});

    /* Aviso compacto "-X HP" no centro-topo do viewport */
    if (last_damage > 0) {
        char dmg_txt[32];
        snprintf(dmg_txt, sizeof(dmg_txt), "-%d HP", last_damage);
        int fs = (sw < 1000) ? 18 : 24;
        int tw = MeasureText(dmg_txt, fs);
        int tx = (sw - tw) / 2;
        int ty = (int)((float)view_h * 0.14f) - (int)((1.0f - t) * 14.0f);
        unsigned char txt_a = (unsigned char)(255.0f * t);
        DrawText(dmg_txt, tx + 2, ty + 2, fs, (Color){20, 0, 0, txt_a});
        DrawText(dmg_txt, tx, ty, fs, (Color){255, 85, 75, txt_a});
    }
}

/* Moldura pulsante de fogo e chamas ASCII no rodape do viewport enquanto o jogador queima */
static void draw_player_burning_overlay(int sw, int sh) {
    int hud_h = (int)((float)sh * 0.145f);
    if (hud_h < 82) hud_h = 82;
    int rh = 1;
    renderer_get_api()->get_grid_size(NULL, &rh);
    if (rh < 1) rh = 1;
    int view_h = sh - hud_h;
    if (view_h < rh) view_h = rh;
    float glyph_size = ((float)view_h / (float)rh) * 1.85f;
    if (glyph_size < 4.0f) glyph_size = 4.0f;

    float t_burn = (float)GetTime();
    float pulse = 0.55f + 0.45f * sinf(t_burn * 18.0f);
    unsigned char edge_a = (unsigned char)(65.0f + pulse * 75.0f);
    unsigned char glow_a = (unsigned char)(30.0f + pulse * 45.0f);
    Color fire_outer = (Color){255, 65, 15, edge_a};
    Color fire_inner = (Color){255, 165, 25, glow_a};
    int border_px = (int)((float)sw * 0.018f);
    if (border_px < 10) border_px = 10;
    if (border_px > 26) border_px = 26;

    /* Moldura incandescente pulsante ao redor do viewport */
    DrawRectangle(0, 0, sw, border_px, fire_outer);
    DrawRectangle(0, view_h - border_px, sw, border_px, fire_outer);
    DrawRectangle(0, border_px, border_px, view_h - border_px * 2, fire_outer);
    DrawRectangle(sw - border_px, border_px, border_px, view_h - border_px * 2, fire_outer);
    DrawRectangle(border_px, border_px, sw - border_px * 2, border_px / 2, fire_inner);
    DrawRectangle(border_px, view_h - border_px - border_px / 2, sw - border_px * 2, border_px / 2, fire_inner);

    /* Linguas de fogo ASCII na base do viewport */
    int flame_fs = (int)(glyph_size * 1.25f);
    if (flame_fs < 14) flame_fs = 14;
    if (flame_fs > 28) flame_fs = 28;
    int step_x = flame_fs;
    for (int fx = 6; fx < sw - 6; fx += step_x) {
        float wave = sinf(t_burn * 16.0f + (float)fx * 0.09f);
        int lift = (int)((0.5f + 0.5f * wave) * (float)(flame_fs / 2));
        const char *fch = (wave > 0.2f) ? "^" : "*";
        Color fcol = (wave > 0.2f) ? (Color){255, 225, 70, 220} : (Color){255, 95, 20, 210};
        DrawText(fch, fx, view_h - flame_fs - lift, flame_fs, fcol);
    }

    /* Badge de alerta "EM CHAMAS!" */
    const char *burn_lbl = "!! EM CHAMAS !!";
    int b_fs = (int)((float)hud_h * 0.22f);
    if (b_fs < 14) b_fs = 14;
    if (b_fs > 24) b_fs = 24;
    int b_tw = MeasureText(burn_lbl, b_fs);
    int b_pad_x = 12;
    int b_pad_y = 4;
    int bx = (sw - b_tw) / 2 - b_pad_x;
    int by = view_h - flame_fs - b_fs - 18;
    DrawRectangle(bx, by, b_tw + b_pad_x * 2, b_fs + b_pad_y * 2, (Color){145, 22, 8, 210});
    DrawRectangleLines(bx, by, b_tw + b_pad_x * 2, b_fs + b_pad_y * 2, (Color){255, 195, 45, 240});
    DrawText(burn_lbl, bx + b_pad_x, by + b_pad_y, b_fs, (Color){255, 235, 110, 255});
}

/* Executa uma partida completa (Singleplayer Offline, Host UDP ou Client UDP de ate 4 jogadores)
 * mantendo velocidade independente de FPS via Delta Time (dt), controle de pause,
 * sincronizacao multiplayer Co-op/PvP e ordem canonica das chamadas da biblioteca renderer. */
static void run_game_session(NetMode mode, const char *host_ip, int port,
                             AppConfig *cfg, NetContext *existing_net,
                             char *err_buf, size_t err_size) {
    int level_idx = map_level_clamp(cfg ? cfg->selected_level : 0);
    const MapLevelInfo *lvl_info = map_level_get(level_idx);
    GameMap map;
    map_load_level(level_idx, &map);

    Player player;
    NetContext net;

    /* Reaproveita a conexao ja aberta no lobby (ou inicia uma nova se chamado direto) */
    if (existing_net && existing_net->initialized && mode != NET_MODE_OFFLINE) {
        net = *existing_net;
        existing_net->relay_handle = 0;
        existing_net->initialized = 0;
    } else if (!net_init(&net, mode, host_ip, port)) {
        snprintf(err_buf, err_size,
                 "Erro ao iniciar rede UDP na porta %d (IP: %s).", port, host_ip);
        return;
    }

    if (mode != NET_MODE_OFFLINE && cfg) {
        if (mode == NET_MODE_HOST) {
            net.game_mode = cfg->mp_game_mode;
        }
        net.remote_level = level_idx;
    }
    /* Zera acumuladores de dano e kills PvP antes de anunciar inicio da partida */
    net.applied_pvp_dmg = 0;
    memset(net.outgoing_pvp_dmg, 0, sizeof(net.outgoing_pvp_dmg));
    memset(net.killed_by_counts, 0, sizeof(net.killed_by_counts));
    memset(net.remote_pvp_dmg_matrix, 0, sizeof(net.remote_pvp_dmg_matrix));
    memset(net.remote_killed_by_matrix, 0, sizeof(net.remote_killed_by_matrix));
    memset(net.total_dmg_to_slot, 0, sizeof(net.total_dmg_to_slot));
    memset(net.slot_pvp_kills, 0, sizeof(net.slot_pvp_kills));
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        net.last_attacker_of_slot[i] = -1;
    }
    if (mode != NET_MODE_OFFLINE) {
        net_begin_match(&net, level_idx, lvl_info->name);
    }

    int is_multiplayer = (mode != NET_MODE_OFFLINE);
    int my_slot = 0;
    if (mode == NET_MODE_CLIENT) {
        my_slot = (net.local_slot >= 1 && net.local_slot < NET_MAX_PLAYERS) ? net.local_slot : 1;
    }

    float base_sx = lvl_info->spawn1_x;
    float base_sy = lvl_info->spawn1_y;
    map_apply_pending_spawn(&base_sx, &base_sy);

    float mp_sx[NET_MAX_PLAYERS];
    float mp_sy[NET_MAX_PLAYERS];
    float mp_sang[NET_MAX_PLAYERS];
    compute_mp_spawn_points(&map, base_sx, base_sy,
                            is_multiplayer ? net.game_mode : NET_GAME_COOP,
                            mp_sx, mp_sy, mp_sang);

    if (is_multiplayer) {
        player_init(&player, mp_sx[my_slot], mp_sy[my_slot], mp_sang[my_slot]);
        player.id = my_slot + 1;
    } else {
        player_init(&player, base_sx, base_sy, PI_F * 0.5f);
    }

    CharacterClass selected_class = player_roster_get(&cfg->roster, cfg->selected_character);
    int restored_rejoin = 0;
    /* Se o cliente caiu e voltou para a mesma sala, restaura skin, posicao, vida e score salvos no host */
    if (mode == NET_MODE_CLIENT && net.has_restore_state && net.restore_state.valid) {
        const NetSavedPlayerState *sv = &net.restore_state;
        int total_r = player_roster_count(&cfg->roster);
        if (sv->character_idx >= 0 && sv->character_idx < total_r) {
            CharacterClass cand = player_roster_get(&cfg->roster, sv->character_idx);
            if (cand.visual_id == sv->visual_id &&
                memcmp(&cand.appearance, &sv->appearance, sizeof(sv->appearance)) == 0) {
                selected_class = cand;
                cfg->selected_character = sv->character_idx;
            }
        } else if (!sv->appearance.enabled) {
            selected_class = player_get_preset_class(sv->visual_id);
        } else {
            selected_class = player_get_preset_class(PRESET_FUZILEIRO);
            selected_class.visual_id = sv->visual_id;
            selected_class.appearance = sv->appearance;
            selected_class.ability = ABILITY_NONE;
            snprintf(selected_class.name, sizeof(selected_class.name), "%s", sv->name);
        }
        player_apply_class(&player, &selected_class);

        if (!map_is_wall(&map, (int)floorf(sv->x), (int)floorf(sv->y))) {
            player.x = sv->x;
            player.y = sv->y;
        }
        player.angle = sv->angle;
        if (sv->max_health > 0) player.max_health = sv->max_health;
        player.health = (sv->health > 0) ? sv->health : player.max_health;
        if (player.health > player.max_health) player.health = player.max_health;
        player.ammo = sv->ammo;
        player.active_weapon = sv->active_weapon;
        player.score = sv->score;
        player.ability_state = sv->ability_state;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            net.outgoing_pvp_dmg[i] = sv->outgoing_pvp_dmg[i];
            net.killed_by_counts[i] = sv->killed_by_counts[i];
        }
        net.applied_pvp_dmg = (net.total_dmg_to_slot[my_slot] > sv->applied_pvp_dmg)
            ? net.total_dmg_to_slot[my_slot] : sv->applied_pvp_dmg;
        restored_rejoin = 1;
    } else {
        player_apply_class(&player, &selected_class);
    }

    if (mode == NET_MODE_HOST) {
        net_host_update_local_slot(&net, selected_class.name, cfg->selected_character,
                                   selected_class.visual_id, &selected_class.appearance,
                                   net.game_mode, level_idx);
    }

    if (is_multiplayer) {
        net_send_match_state(&net, &player, 0);
    }

    if (IsWindowFocused()) {
        DisableCursor();
    } else {
        EnableCursor();
    }
    int mouse_locked = 1;
    int running = 1;
    int paused = 0;
    int pause_subpage = 0; /* 0 = Menu de Pause, 1 = Configuracoes */
    int pause_sel = 0;     /* 0 = Continuar, 1 = Config, 2 = Menu Principal */
    int pause_tab = 0;
    int pause_listening_bind = -1;
    float cam_pitch = 0.0f;
    int third_person = 0;
    float pvp_hit_timer = 0.0f;
    float pvp_hurt_timer = 0.0f;
    float respawn_banner_timer = restored_rejoin ? 2.6f : 0.0f;
    char respawn_msg[96] = "";
    if (restored_rejoin) {
        snprintf(respawn_msg, sizeof(respawn_msg), "RECONECTADO! PROGRESSO RESTAURADO.");
    }
    EnemyManager enemies = {0};
    Weapon weapons[MAX_WEAPONS];
    weapons_init_defaults(weapons);

    int use_coop_enemies = (!is_multiplayer || net.game_mode == NET_GAME_COOP);
    if (use_coop_enemies) {
        map_begin_enemy_spawn();
        enemies_spawn_wave(&enemies, &map, &player);
        map_end_enemy_spawn();
        map_set_enemies_alive(enemies_required_alive(&enemies));
    }
    float wave_timer = 3.0f;

    while (!WindowShouldClose() && running) {
        int win_active = sync_window_focus_and_borderless();
        int alt_down = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

        /* Pausa sozinho e solta o mouse se o jogador trocar de janela */
        if (!IsWindowFocused() && !paused) {
            paused = 1;
            pause_subpage = 0;
            pause_sel = 0;
            pause_listening_bind = -1;
            EnableCursor();
        }

        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        float sc = fminf((float)sw / 1280.0f, (float)sh / 720.0f);
        if (sc < 0.48f) sc = 0.48f;

        /* Limita dt a 0.1s para evitar saltos de colisao caso o frame trave */
        float dt = GetFrameTime();
        if (dt > 0.1f) {
            dt = 0.1f;
        }
        if (!paused) {
            renderer_update_auto_perf(dt);
        }
        if (pvp_hit_timer > 0.0f) pvp_hit_timer = fmaxf(0.0f, pvp_hit_timer - dt);
        if (pvp_hurt_timer > 0.0f) pvp_hurt_timer = fmaxf(0.0f, pvp_hurt_timer - dt);
        if (respawn_banner_timer > 0.0f) respawn_banner_timer = fmaxf(0.0f, respawn_banner_timer - dt);

        if (IsKeyPressed(KEY_F11)) {
            toggle_borderless_window();
        }
        if (IsKeyPressed(KEY_F2) && pause_listening_bind < 0) {
            cfg->ascii_quality = (cfg->ascii_quality + 1) % 5;
        }
        if (IsKeyPressed(KEY_F3) && pause_listening_bind < 0) {
            cfg->show_fps = !cfg->show_fps;
        }
        if (IsKeyPressed(KEY_F4) && !alt_down && pause_listening_bind < 0) {
            cfg->ascii_style = (cfg->ascii_style + 1) % 2;
        }

        if (paused && pause_subpage == 1 && pause_tab == 1 && pause_listening_bind >= 0) {
            update_bind_capture(cfg, &pause_listening_bind);
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            if (!paused) {
                paused = 1;
                pause_subpage = 0;
                pause_sel = 0;
                pause_listening_bind = -1;
                EnableCursor();
            } else if (pause_subpage == 1) {
                pause_subpage = 0;
                pause_listening_bind = -1;
            } else {
                paused = 0;
                if (mouse_locked && win_active) {
                    DisableCursor();
                } else {
                    EnableCursor();
                }
            }
        }

        int did_shoot = 0;
        if (!paused) {
            /* TAB solta ou trava o mouse; V alterna camera 1a/3a pessoa */
            if (IsKeyPressed(KEY_TAB) && !alt_down) {
                mouse_locked = !mouse_locked;
                if (mouse_locked && win_active) {
                    DisableCursor();
                } else {
                    EnableCursor();
                }
            } else if (mouse_locked && win_active && !IsCursorHidden()) {
                DisableCursor();
            }

            if (IsKeyPressed(KEY_V)) {
                third_person = !third_person;
            }

            player_tick_ability(&player, dt);
            if (IsKeyPressed(KEY_E)) player_activate_ability(&player);
            handle_custom_input(&player, &map, &cfg->binds, dt, &running, &did_shoot, &cam_pitch);
        }

        int is_pvp = (is_multiplayer && net.game_mode == NET_GAME_PVP);
        if (is_multiplayer) {
            if (mode == NET_MODE_CLIENT && net.local_slot >= 1 && net.local_slot < NET_MAX_PLAYERS) {
                my_slot = net.local_slot;
                player.id = my_slot + 1;
            }
            use_coop_enemies = (net.game_mode == NET_GAME_COOP);
        }

        if (!paused) {
            if (did_shoot) {
                /* Em PvP checa acerto contra jogadores; em Co-op/Singleplayer acerta inimigos do mapa */
                if (is_pvp) {
                    if (check_pvp_player_hit(&map, &player, &net, weapons, my_slot)) {
                        pvp_hit_timer = 0.22f;
                    }
                } else {
                    int aw = (player.active_weapon >= 0 && player.active_weapon < MAX_WEAPONS)
                        ? player.active_weapon : WEAPON_PISTOL;
                    enemies_check_hit(&enemies, &map, &player, &weapons[aw]);
                }
            }
            if (!is_pvp) {
                enemies_update_fireball(&enemies, &map, &player, dt);
                enemies_update(&enemies, &map, &player, dt);
                int alive = map_set_enemies_alive(enemies_required_alive(&enemies));
                if (!is_multiplayer) {
                    /* No singleplayer, zerar inimigos libera o portal 'E' e avanca de fase ao entrar nele */
                    if (map_has_exit_tile(&map) && alive == 0) {
                        wave_timer = 3.0f;
                    }
                    int nav = map_try_advance_level(&map, &player.x, &player.y, &player.angle);
                    if (nav > 0) {
                        map_begin_enemy_spawn();
                        enemies_spawn_wave(&enemies, &map, &player);
                        map_end_enemy_spawn();
                        map_set_enemies_alive(enemies_required_alive(&enemies));
                        wave_timer = 3.0f;
                    } else if (nav < 0) {
                        snprintf(err_buf, err_size,
                                 "Todas as fases concluidas! Kills: %d.",
                                 player.ability_state.kills);
                        running = 0;
                    } else if (alive == 0) {
                        wave_timer -= dt;
                        if (wave_timer <= 0.0f) {
                            map_begin_enemy_spawn();
                            enemies_spawn_wave(&enemies, &map, &player);
                            map_end_enemy_spawn();
                            map_set_enemies_alive(enemies_required_alive(&enemies));
                            wave_timer = 3.0f;
                        }
                    }
                } else if (alive == 0) {
                    wave_timer -= dt;
                    if (wave_timer <= 0.0f) {
                        map_begin_enemy_spawn();
                        enemies_spawn_wave(&enemies, &map, &player);
                        map_end_enemy_spawn();
                        map_set_enemies_alive(enemies_required_alive(&enemies));
                        wave_timer = 3.0f;
                    }
                }
            }
        }

        if (is_multiplayer) {
            net_receive_match_state(&net);
            if (mode == NET_MODE_CLIENT && net.host_disconnected) {
                snprintf(err_buf, err_size,
                         "O Anfitriao encerrou a partida. Voltando ao menu.");
                running = 0;
            }
            if (mode == NET_MODE_CLIENT && net.local_slot >= 1 && net.local_slot < NET_MAX_PLAYERS) {
                my_slot = net.local_slot;
                player.id = my_slot + 1;
            }
            is_pvp = (net.game_mode == NET_GAME_PVP);

            /* Aplica apenas o incremento de dano PvP novo recebido pelo host desde o ultimo frame */
            if (running && is_pvp && net.total_dmg_to_slot[my_slot] > net.applied_pvp_dmg) {
                int delta = net.total_dmg_to_slot[my_slot] - net.applied_pvp_dmg;
                net.applied_pvp_dmg = net.total_dmg_to_slot[my_slot];
                player_take_damage(&player, delta);
                pvp_hurt_timer = 0.28f;
            }
            if (running && is_pvp) {
                player.ability_state.kills = net.slot_pvp_kills[my_slot];
            }
            if (running) {
                net_send_match_state(&net, &player, did_shoot);
            }
        }

        if (player.health <= 0) {
            if (!is_multiplayer) {
                snprintf(err_buf, err_size,
                         "Fim de partida: %d kills. Inicie outra para tentar novamente.",
                         player.ability_state.kills);
                running = 0;
            } else {
                /* Em Co-op e PvP, reaparece num ponto seguro sem derrubar a sala */
                int killer = net.last_attacker_of_slot[my_slot];
                if (is_pvp && killer >= 0 && killer < NET_MAX_PLAYERS && killer != my_slot) {
                    net.killed_by_counts[killer]++;
                    snprintf(respawn_msg, sizeof(respawn_msg),
                             "ELIMINADO POR P%d (%s)! REAPARECENDO...",
                             killer + 1,
                             net.slots[killer].name[0] ? net.slots[killer].name : "Rival");
                } else {
                    snprintf(respawn_msg, sizeof(respawn_msg), "VOCE CAIU! REAPARECENDO...");
                }

                compute_mp_spawn_points(&map, base_sx, base_sy, net.game_mode, mp_sx, mp_sy, mp_sang);
                int pick = my_slot;
                if (is_pvp) {
                    /* Escolhe o ponto de respawn mais longe dos outros jogadores vivos */
                    float best_r_d2 = -1.0f;
                    for (int cand = 0; cand < NET_MAX_PLAYERS; cand++) {
                        float min_d2 = 1e9f;
                        for (int r = 0; r < NET_MAX_PLAYERS; r++) {
                            if (r != my_slot && net.slots[r].active && net.slots[r].has_pos &&
                                net.slots[r].health > 0) {
                                float ddx = mp_sx[cand] - net.slots[r].x;
                                float ddy = mp_sy[cand] - net.slots[r].y;
                                float d2 = ddx * ddx + ddy * ddy;
                                if (d2 < min_d2) min_d2 = d2;
                            }
                        }
                        if (min_d2 > best_r_d2) {
                            best_r_d2 = min_d2;
                            pick = cand;
                        }
                    }
                }
                player.x = mp_sx[pick];
                player.y = mp_sy[pick];
                player.angle = mp_sang[pick];
                player.health = player.max_health;
                player.ammo = selected_class.start_ammo;
                respawn_banner_timer = 2.2f;
                net_send_match_state(&net, &player, 0);
            }
        }

        /* Monta a lista de sprites dos jogadores remotos (com cor de time positiva no Co-op ou negativa no PvP) */
        SpriteEntity mp_sprites[NET_MAX_PLAYERS];
        int mp_count = 0;
        SpriteEntity mm_markers[MAX_SPRITES];
        int mm_count = 0;
        if (!is_pvp) {
            for (int i = 0; i < enemies.count && mm_count < MAX_SPRITES; i++) {
                const Enemy *e = &enemies.enemies[i];
                if (e->active) {
                    mm_markers[mm_count++] = (SpriteEntity){e->x, e->y, 1, 0, e->symbol, 0, {0}, 0};
                }
            }
        }
        if (is_multiplayer) {
            for (int s = 0; s < NET_MAX_PLAYERS; s++) {
                if (s == my_slot || !net.slots[s].active || !net.slots[s].has_pos ||
                    net.slots[s].health <= 0) {
                    continue;
                }
                SpriteEntity rem;
                memset(&rem, 0, sizeof(rem));
                rem.x = net.slots[s].x;
                rem.y = net.slots[s].y;
                rem.active = 1;
                rem.shooting = net.slots[s].shooting;
                rem.label = 'O';
                rem.visual_id = net.slots[s].visual_id;
                rem.appearance = net.slots[s].appearance;
                rem.team_accent = is_pvp ? -(s + 1) : (s + 1);
                mp_sprites[mp_count++] = rem;
                if (mm_count < MAX_SPRITES) {
                    mm_markers[mm_count++] = rem;
                }
            }
        }

        /* Ordem fixa de desenho no grid ASCII: paredes -> inimigos -> remotos -> armadilhas -> habilidades -> minimapa -> arma -> HUD */
        const RendererApi *r_api = renderer_get_api();
        renderer_set_visual_style(cfg->ascii_style);
        renderer_set_ascii_density(cfg->ascii_quality, sw, sh);
        renderer_set_camera_view(cam_pitch, third_person);
        renderer_clear();
        renderer_draw_walls(&map, &player);
        if (!is_pvp) {
            entities_render(&enemies, &player, r_api);
        }
        renderer_draw_sprites(&player, mp_sprites, mp_count);
        map_render_hazards(&map, &player, r_api);
        player_render_abilities(&player, r_api);
        draw_sliding_minimap(&map, &player, mm_markers, mm_count);
        if (third_person || !player_render_weapon(&player, r_api)) {
            renderer_draw_weapon(&player);
        }
        renderer_draw_hud(&player);

        BeginDrawing();
        renderer_draw_scene_and_hud(&player, is_multiplayer, net.connected,
                                    mouse_locked, cfg->show_fps ? cfg->fps_cap : -1);
        player_draw_ability_hud(&player, sw, sh);
        if (player.hurt_timer > 0.0f) {
            draw_player_damage_overlay(sw, sh, player.hurt_timer, player.last_damage);
        }
        if (enemies_player_is_burning()) {
            draw_player_burning_overlay(sw, sh);
        }

        /* Feedback visual de multiplayer (modo, placar PvP, marcador de acerto e dano) */
        if (is_multiplayer) {
            int hud_h_px = (int)((float)sh * 0.145f);
            if (hud_h_px < 82) hud_h_px = 82;
            int view_h_px = sh - hud_h_px;

            if (pvp_hit_timer > 0.0f) {
                int cx_m = sw / 2, cy_m = view_h_px / 2;
                DrawLine(cx_m - 10, cy_m - 10, cx_m - 4, cy_m - 4, (Color){255, 85, 65, 255});
                DrawLine(cx_m + 10, cy_m - 10, cx_m + 4, cy_m - 4, (Color){255, 85, 65, 255});
                DrawLine(cx_m - 10, cy_m + 10, cx_m - 4, cy_m + 4, (Color){255, 85, 65, 255});
                DrawLine(cx_m + 10, cy_m + 10, cx_m + 4, cy_m + 4, (Color){255, 85, 65, 255});
            }
            if (pvp_hurt_timer > 0.0f) {
                unsigned char a = (unsigned char)(190.0f * (pvp_hurt_timer / 0.28f));
                DrawRectangleLinesEx((Rectangle){0, 0, (float)sw, (float)view_h_px},
                                     6.0f, (Color){235, 42, 42, a});
            }

            int mf_sz = (int)(13.5f * sc);
            if (mf_sz < 10) mf_sz = 10;
            char top_bar[160];
            if (is_pvp) {
                snprintf(top_bar, sizeof(top_bar), "PVP (%d/4)", net.active_player_count);
                for (int s = 0; s < NET_MAX_PLAYERS; s++) {
                    if (net.slots[s].active) {
                        char part[32];
                        snprintf(part, sizeof(part), " | P%d:%d", s + 1, net.slot_pvp_kills[s]);
                        strncat(top_bar, part, sizeof(top_bar) - strlen(top_bar) - 1);
                    }
                }
            } else {
                snprintf(top_bar, sizeof(top_bar), "CO-OP (%d/4 JOGADORES)", net.active_player_count);
            }
            int tw_b = MeasureText(top_bar, mf_sz) + 20;
            int bx_b = (sw - tw_b) / 2;
            DrawRectangle(bx_b, 8, tw_b, mf_sz + 10, (Color){12, 16, 24, 210});
            DrawRectangleLinesEx((Rectangle){(float)bx_b, 8.0f, (float)tw_b, (float)(mf_sz + 10)},
                                 1.5f, is_pvp ? (Color){235, 95, 70, 235} : (Color){95, 215, 145, 235});
            DrawText(top_bar, bx_b + 10, 13, mf_sz,
                     is_pvp ? (Color){255, 195, 135, 255} : (Color){145, 245, 185, 255});

            if (respawn_banner_timer > 0.0f && respawn_msg[0] != '\0') {
                int r_sz = (int)(18.0f * sc);
                if (r_sz < 13) r_sz = 13;
                int rw = MeasureText(respawn_msg, r_sz) + 28;
                int rx_b = (sw - rw) / 2;
                int ry_b = view_h_px / 3;
                DrawRectangle(rx_b, ry_b, rw, r_sz + 16, (Color){18, 12, 16, 225});
                DrawRectangleLinesEx((Rectangle){(float)rx_b, (float)ry_b, (float)rw, (float)(r_sz + 16)},
                                     2.0f, (Color){255, 110, 80, 255});
                DrawText(respawn_msg, rx_b + 14, ry_b + 8, r_sz, WHITE);
            }

            draw_mp_players_hp_hud(sw, sh, sc, &player, &net, my_slot, is_pvp);
        }

        if (paused) {
            /* Sobreposicao esmaecida mantendo a cena 3D e o HUD visiveis ao fundo */
            DrawRectangle(0, 0, sw, sh, (Color){6, 9, 15, 175});

            int pad_x = (int)(20.0f * sc);
            if (pad_x < 12) pad_x = 12;

            if (pause_subpage == 0) {
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
                    pause_sel = (pause_sel + 1) % 3;
                }
                if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                    pause_sel = (pause_sel + 2) % 3;
                }
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                    if (pause_sel == 0) {
                        paused = 0;
                        if (mouse_locked && win_active) DisableCursor();
                        else EnableCursor();
                    } else if (pause_sel == 1) {
                        pause_subpage = 1;
                    } else {
                        running = 0;
                    }
                }

                int box_w = (int)(440.0f * sc);
                if (box_w < 300) box_w = 300;
                if (box_w > sw - 32) box_w = sw - 32;
                int btn_h = (int)(46.0f * sc);
                if (btn_h < 34) btn_h = 34;
                int btn_gap = (int)(12.0f * sc);
                if (btn_gap < 8) btn_gap = 8;
                int bevel = (int)(12.0f * sc);
                if (bevel < 8) bevel = 8;
                int title_sz = (int)(30.0f * sc);
                if (title_sz < 20) title_sz = 20;
                int sub_sz = (int)(14.0f * sc);
                if (sub_sz < 11) sub_sz = 11;
                int btn_font = (int)(18.0f * sc);
                if (btn_font < 13) btn_font = 13;

                int head_h = (int)(78.0f * sc);
                int foot_note_h = (int)(36.0f * sc);
                int box_h = head_h + 3 * btn_h + 2 * btn_gap + foot_note_h;
                int box_x = (sw - box_w) / 2;
                int box_y = (sh - box_h) / 2;

                DrawRectangle(box_x, box_y, box_w, box_h, (Color){12, 16, 24, 240});
                DrawRectangleLinesEx((Rectangle){(float)box_x, (float)box_y, (float)box_w, (float)box_h},
                                     2.0f, (Color){230, 108, 38, 255});

                const char *p_title = "JOGO PAUSADO";
                int p_tw = MeasureText(p_title, title_sz);
                DrawText(p_title, box_x + (box_w - p_tw) / 2, box_y + (int)(16.0f * sc),
                         title_sz, (Color){255, 200, 95, 255});

                const char *p_sub = "PROGRESSO DA PARTIDA PRESERVADO";
                int p_sw = MeasureText(p_sub, sub_sz);
                DrawText(p_sub, box_x + (box_w - p_sw) / 2, box_y + (int)(50.0f * sc),
                         sub_sz, (Color){145, 165, 190, 255});

                const char *p_items[3] = {
                    "CONTINUAR PARTIDA",
                    "CONFIGURACOES",
                    "VOLTAR AO MENU PRINCIPAL"
                };
                int by0 = box_y + head_h;
                for (int i = 0; i < 3; i++) {
                    int hov = 0;
                    int y = by0 + i * (btn_h + btn_gap);
                    if (draw_chamfer_button(box_x + pad_x, y, box_w - pad_x * 2, btn_h, bevel,
                                            p_items[i], btn_font, pause_sel == i, &hov)) {
                        if (i == 0) {
                            paused = 0;
                            if (mouse_locked && win_active) DisableCursor();
                            else EnableCursor();
                        } else if (i == 1) {
                            pause_subpage = 1;
                        } else {
                            running = 0;
                        }
                    }
                    if (hov) pause_sel = i;
                }

                const char *p_hint = "[ESC] RETOMAR   |   [F11] TELA CHEIA";
                int h_sz = sub_sz - 1;
                if (h_sz < 10) h_sz = 10;
                int h_w = MeasureText(p_hint, h_sz);
                DrawText(p_hint, box_x + (box_w - h_w) / 2,
                         box_y + box_h - h_sz - (int)(12.0f * sc),
                         h_sz, (Color){145, 160, 180, 220});
            } else {
                int p_max_h = sh - (int)(48.0f * sc);
                if (p_max_h < 280) p_max_h = sh - 20;
                if (draw_settings_panel(cfg, 0, 0, p_max_h, sw, sc, pad_x, 1,
                                        &pause_tab, &pause_listening_bind, "VOLTAR AO PAUSE")) {
                    pause_subpage = 0;
                    pause_listening_bind = -1;
                }
            }
        }

        EndDrawing();
    }

    if (is_multiplayer && !net.host_disconnected) {
        net_send_lobby_leave(&net);
    }
    renderer_set_camera_view(0.0f, 0);
    net_close(&net);
}

/* Ponto de entrada: suporta modo headless (--test-frame) para CI/testes ou loop
 * menu <-> partida mantendo configuracoes, roster e hooks ativos durante todo o jogo. */
int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--test-frame") == 0) {
        return run_single_test_frame();
    }

    renderer_init();
    register_renderer_hooks();

    AppConfig cfg;
    app_config_init(&cfg);

    char host_ip[64] = "127.0.0.1";
    char status_msg[128] = "";
    int port = DEFAULT_PORT;

    while (!WindowShouldClose()) {
        NetContext lobby_net;
        memset(&lobby_net, 0, sizeof(lobby_net));
        int choice = run_menu_screen(&cfg, host_ip, sizeof(host_ip), &port, &lobby_net, status_msg);
        status_msg[0] = '\0';

        if (choice == 1) {
            run_game_session(NET_MODE_OFFLINE, host_ip, port, &cfg, NULL,
                             status_msg, sizeof(status_msg));
        } else if (choice == 2) {
            run_game_session(NET_MODE_HOST, host_ip, port, &cfg, &lobby_net,
                             status_msg, sizeof(status_msg));
        } else if (choice == 3) {
            run_game_session(NET_MODE_CLIENT, host_ip, port, &cfg, &lobby_net,
                             status_msg, sizeof(status_msg));
        } else {
            break;
        }
    }

    cleanup_raylib_screenshots();
    renderer_close();
    return 0;
}
