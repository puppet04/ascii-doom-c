#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "map.h"
#include "network.h"
#include "player.h"
#include "raylib.h"
#include "renderer.h"

/* ============================================================================
 * ESTRUTURAS LOCAIS DE MENU, CONFIGURACOES E KEYBINDS (EXCLUSIVO DE main.c)
 * ============================================================================ */

typedef enum {
    MENU_PAGE_MAIN = 0,
    MENU_PAGE_MULTIPLAYER = 1,
    MENU_PAGE_CUSTOMIZE = 2,
    MENU_PAGE_SETTINGS = 3
} MenuPage;

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

typedef struct {
    MenuKeyBinds binds;
    int fps_cap;
    int show_fps;
    int res_idx;
    int ascii_quality;
} AppConfig;

static void binds_init_default(MenuKeyBinds *b) {
    if (!b) return;
    b->move_forward  = KEY_W;
    b->move_backward = KEY_S;
    b->move_left     = KEY_A;
    b->move_right    = KEY_D;
    b->turn_left     = KEY_Q;
    b->turn_right    = KEY_E;
    b->sprint        = KEY_LEFT_SHIFT;
    b->reload        = KEY_R;
    b->shoot_key     = KEY_SPACE;
    b->mouse_sens    = MOUSE_SENSITIVITY;
}

static void app_config_init(AppConfig *cfg) {
    if (!cfg) return;
    binds_init_default(&cfg->binds);
    cfg->fps_cap = TARGET_FPS;
    cfg->show_fps = 0; /* Desativado por padrao */
    cfg->res_idx = 1;  /* 1280 x 720 padrao */
    cfg->ascii_quality = ASCII_QUALITY_AUTO;
}

static const char *quality_to_label(int q) {
    switch (q) {
        case ASCII_QUALITY_LEVE:  return "LEVE";
        case ASCII_QUALITY_MEDIA: return "MEDIA";
        case ASCII_QUALITY_ALTA:  return "ALTA";
        case ASCII_QUALITY_ULTRA: return "ULTRA";
        default:                  return "AUTO (JANELA)";
    }
}

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

static int g_borderless_mode = 0;
static int g_borderless_unfocused_shrunk = 0;
static int g_f12_cleanup_frames = 0;
static int g_saved_win_x = 100;
static int g_saved_win_y = 80;
static int g_saved_win_w = 1280;
static int g_saved_win_h = 720;

static void cleanup_raylib_screenshots(void) {
    char path[32];
    for (int i = 0; i <= 15; i++) {
        snprintf(path, sizeof(path), "screenshot%03d.png", i);
        if (FileExists(path)) {
            (void)remove(path);
        }
    }
}

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
        /* +1px de altura evita promoção WGL Exclusive Fullscreen que trava o Alt+Tab no Windows */
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

static int calc_fit_w(const char *txt, int font_sz, int min_w, float sc) {
    int w = MeasureText(txt, font_sz) + (int)(26.0f * sc);
    return (w > min_w) ? w : min_w;
}

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

/* ============================================================================
 * LOGICA DO JOGO E INPUT COM KEYBINDS CUSTOMIZAVEIS
 * ============================================================================ */

static void load_initial_map(GameMap *map) {
    if (!map_load_from_file(map, "assets/maps/level1.txt")) {
        map_init_default(map);
    }
}

static int run_single_test_frame(void) {
    GameMap map;
    Player player;
    SpriteEntity demo_sprite;

    load_initial_map(&map);
    player_init(&player, 2.5f, 4.5f, 0.15f);

    demo_sprite.x = 5.5f;
    demo_sprite.y = 4.5f;
    demo_sprite.active = 1;
    demo_sprite.shooting = 0;
    demo_sprite.label = 'O';

    renderer_clear();
    renderer_draw_walls(&map, &player);
    renderer_draw_sprites(&player, &demo_sprite, 1);
    renderer_draw_minimap(&map, &player, &demo_sprite, 1);
    renderer_draw_weapon(&player);
    renderer_draw_hud(&player);
    renderer_present_stdout(&player, 0, 0);

    return 0;
}

static float angle_diff_abs(float a, float b) {
    float diff = a - b;
    while (diff > PI_F) diff -= 2.0f * PI_F;
    while (diff < -PI_F) diff += 2.0f * PI_F;
    return fabsf(diff);
}

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
        float mag = sqrtf(fwd * fwd + str * str);
        fwd /= mag;
        str /= mag;

        float speed = (p->move_speed > 0.5f) ? p->move_speed : MOVE_SPEED;
        int sprinting = IsKeyDown(b->sprint) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (sprinting) {
            speed *= SPRINT_MULT;
        }

        if (player_move(p, map, fwd * speed * dt, str * speed * dt)) {
            p->bob_phase += dt * (sprinting ? 14.0f : 10.0f);
        }
    }

    if (IsKeyPressed(b->reload) && p->ammo < 30) {
        p->ammo = 30;
    }

    int want_shoot = (IsCursorHidden() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
                     IsKeyDown(b->shoot_key);
    if (want_shoot && p->shoot_timer <= 0.0f && p->ammo > 0) {
        p->ammo--;
        p->shoot_timer = FIRE_COOLDOWN;
        *did_shoot = 1;
    }

    (void)running;
}

/* ============================================================================
 * DESENHO DE UI AUTORAL (ARENA CYBER + PERSONAGEM + LOGO EM ASCII ULTRA-DENSO PURO)
 * ============================================================================ */

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

/* Matriz Bayer 4x4 retornando limiar em [-0.18, +0.18] para dithering ASCII */
static float bayer4x4(int x, int y) {
    static const int B[4][4] = {
        {  0,  8,  2, 10 },
        { 12,  4, 14,  6 },
        {  3, 11,  1,  9 },
        { 15,  7, 13,  5 }
    };
    int xi = ((x % 4) + 4) % 4;
    int yi = ((y % 4) + 4) % 4;
    return ((float)B[yi][xi] / 15.0f - 0.5f) * 0.36f;
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

enum { SHOWCASE_GW = 210, SHOWCASE_GH = 260 };

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

static ShowcaseLayout calc_showcase_layout(int sw, int sh, float sc, int page, int quality) {
    ShowcaseLayout L;
    float dscale = calc_effective_density_scale(quality, sw, sh);
    float grid_f = (dscale > 1.0f) ? 1.0f : dscale;

    L.gw = (int)roundf((float)SHOWCASE_GW * grid_f);
    L.gh = (int)roundf((float)SHOWCASE_GH * grid_f);
    if (L.gw < 105) L.gw = 105;
    if (L.gh < 130) L.gh = 130;
    if (L.gw > SHOWCASE_GW) L.gw = SHOWCASE_GW;
    if (L.gh > SHOWCASE_GH) L.gh = SHOWCASE_GH;

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

/* Avalia geometria 2.5D continua do soldado placeholder no espaco canonico gx in [-34..34], gy in [0..86] */
static void eval_showcase_point(float gx, float gy, float breath,
                                int *mat_out, float *nx_out, float *ny_out) {
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

/* Cenario cyber-industrial (pilares, brilho no horizonte, piso 3D e brasas) 100% em micro-ASCII continuo */
static void draw_cyber_arena_backdrop(Font font, int sw, int sh, float sc, float t,
                                      int page, int quality) {
    float dscale = calc_effective_density_scale(quality, sw, sh);
    int bg_gw = (int)roundf(320.0f * dscale);
    int bg_gh = (int)roundf(160.0f * dscale);
    if (bg_gw < 140) bg_gw = 140;
    if (bg_gh < 70)  bg_gh = 70;

    static const char *SOLID_BG_RAMP = "=+*xX#%8&BWM@"; /* 13 caracteres de alta cobertura */
    static const char *GLOW_RAMP     = ":-~=+*#%8&WM@"; /* 13 niveis para nevoa do horizonte */
    static const float PILLAR_X[7] = {0.08f, 0.22f, 0.36f, 0.50f, 0.64f, 0.78f, 0.92f};

    ClearBackground((Color){5, 7, 11, 255});

    ShowcaseLayout L = calc_showcase_layout(sw, sh, sc, page, quality);
    float breath = sinf(t * 2.3f) * 0.60f;

    float cw = (float)sw / (float)bg_gw;
    float ch = (float)sh / (float)bg_gh;
    float glyph_sz = ch * 1.82f;
    if (glyph_sz < 3.5f) glyph_sz = 3.5f;

    const float horizon = 0.62f;

    for (int r = 0; r < bg_gh; r++) {
        float py = (float)r * ch;
        float py_c = py + ch * 0.5f;
        float yn = ((float)r + 0.5f) / (float)bg_gh;

        for (int c = 0; c < bg_gw; c++) {
            float px = (float)c * cw;
            float px_c = px + cw * 0.5f;
            float xn = ((float)c + 0.5f) / (float)bg_gw;

            /* Recorte limpo sob a silhueta do personagem para manter zero pintura opaca por tras */
            if (px_c >= (float)L.x0 && px_c <= (float)L.x0 + L.total_w &&
                py_c >= (float)L.y0 && py_c <= (float)L.y0 + L.total_h) {
                float sx = ((px_c - (float)L.x0) / L.total_w - 0.5f) * 68.0f;
                float sy = ((py_c - (float)L.y0) / L.total_h) * 86.0f;
                int s_mat = 0;
                float s_nx = 0.0f, s_ny = 0.0f;
                eval_showcase_point(sx, sy, breath, &s_mat, &s_nx, &s_ny);
                if (s_mat != 0) {
                    continue;
                }
            }

            /* Atenuacao suave a esquerda para destacar logo, botoes e paineis sem caixas opacas */
            float left_atten = 0.46f + 0.54f * smoothstep_f(0.06f, 0.45f, xn);
            float dither = bayer4x4(c, r);

            /* 1. Feixe luminoso exatamente na linha do horizonte */
            if (fabsf(yn - horizon) < 0.0080f) {
                float h_focus = 0.65f + 0.45f * expf(-fabsf(xn - 0.70f) * 3.8f);
                float h_lum = clamp_f01(h_focus * (0.72f + 0.28f * left_atten));
                static const char *H_CH = "=#@W";
                char hch = H_CH[(c + r) % 4];
                Color hcol = (Color){
                    clamp_u8(255.0f * h_lum),
                    clamp_u8(146.0f * h_lum),
                    clamp_u8(54.0f * h_lum),
                    245
                };
                DrawTextCodepoint(font, (int)hch, (Vector2){px, py}, glyph_sz, hcol);
                continue;
            }

            /* 2. Zona superior: pilares industriais, brilho no horizonte e brasas ASCII */
            if (yn < horizon) {
                float dy_h = horizon - yn;
                float glow = expf(-dy_h * 11.2f) *
                             (0.64f + 0.14f * sinf(xn * 6.5f + t * 0.45f) +
                              0.32f * expf(-fabsf(xn - 0.70f) * 3.0f));

                int in_pillar = 0;
                int pil_idx = 0;
                float pil_u = 0.0f;
                for (int i = 0; i < 7; i++) {
                    float pw = 0.015f + 0.004f * fabsf((float)i - 3.0f);
                    float dist_p = fabsf(xn - PILLAR_X[i]);
                    if (dist_p <= pw) {
                        in_pillar = 1;
                        pil_idx = i;
                        pil_u = (xn - PILLAR_X[i]) / pw;
                        break;
                    }
                }

                if (in_pillar) {
                    float cyl = sqrtf(fmaxf(0.10f, 1.0f - pil_u * pil_u));
                    int is_rib = (fmodf(yn + 0.02f, 0.09f) < 0.012f);
                    int is_neon = (fabsf(pil_u) < 0.16f && yn >= 0.12f && yn <= 0.60f);

                    if (is_neon) {
                        float pulse = 0.70f + 0.30f * sinf(t * 2.4f - yn * 11.0f + (float)pil_idx * 1.15f);
                        float nlum = clamp_f01(pulse * (0.48f + 0.52f * left_atten));
                        static const char *N_CH = "@W#%=";
                        int ni = (int)(clamp_f01(1.0f - pulse + fabsf(pil_u) * 2.5f) * 4.5f);
                        Color ncol = (Color){
                            clamp_u8(255.0f * nlum),
                            clamp_u8(138.0f * nlum),
                            clamp_u8(44.0f * nlum),
                            245
                        };
                        DrawTextCodepoint(font, (int)N_CH[ni], (Vector2){px, py}, glyph_sz, ncol);
                    } else {
                        int edge = (fabsf(pil_u) > 0.78f);
                        float illum = (0.20f + 0.28f * cyl + glow * 0.42f + (is_rib ? 0.18f : 0.0f)) * left_atten;
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
                        Color pcol = (Color){
                            clamp_u8((44.0f + glow * 160.0f) * illum * 1.40f),
                            clamp_u8((50.0f + glow * 76.0f) * illum * 1.35f),
                            clamp_u8((66.0f + glow * 28.0f) * illum * 1.35f),
                            235
                        };
                        DrawTextCodepoint(font, (int)gch, (Vector2){px, py}, glyph_sz, pcol);
                    }
                    continue;
                }

                /* Brasas / fagulhas ascendentes no fundo escuro */
                int scroll_r = r + (int)(t * 17.0f + (float)((c * 19) % 7));
                unsigned h = (unsigned)(c * 73856093u ^ scroll_r * 19349663u);
                if ((h % 263u) == 0u && yn > 0.08f && left_atten > 0.55f) {
                    static const char *SPARK_CH = "*+#%";
                    char sch = SPARK_CH[h % 4u];
                    float sp_lum = (0.68f + 0.32f * sinf(t * 4.0f + (float)c)) * left_atten;
                    Color scol = (Color){
                        clamp_u8(255.0f * sp_lum),
                        clamp_u8(168.0f * sp_lum),
                        clamp_u8(68.0f * sp_lum),
                        235
                    };
                    DrawTextCodepoint(font, (int)sch, (Vector2){px, py}, glyph_sz, scol);
                    continue;
                }

                /* Nevoa luminosa continua acima do horizonte em ASCII denso */
                float g_lum = glow * left_atten;
                if (g_lum + dither * 0.35f > 0.11f) {
                    int idx = (int)(clamp_f01(g_lum + dither * 0.7f) * 12.9f);
                    if (idx < 0) idx = 0;
                    if (idx > 12) idx = 12;
                    Color gcol = (Color){
                        clamp_u8(232.0f * g_lum),
                        clamp_u8(98.0f * g_lum),
                        clamp_u8(40.0f * g_lum),
                        225
                    };
                    DrawTextCodepoint(font, (int)GLOW_RAMP[idx], (Vector2){px, py}, glyph_sz, gcol);
                }
                continue;
            }

            /* 3. Piso da arena em perspectiva 3D continua (yn >= horizon) */
            float dz = yn - horizon + 0.008f;
            float z_world = 0.32f / dz;
            float x_world = (xn - 0.56f) * z_world * 3.1f;
            float dist_fade = expf(-z_world * 0.14f);

            float fx = fabsf(fmodf(fabsf(x_world * 1.15f) + 0.5f, 1.0f) - 0.5f);
            float fz = fabsf(fmodf(z_world * 0.65f + 0.5f, 1.0f) - 0.5f);

            float radial_w = 0.0088f * z_world * 1.15f;
            if (radial_w < 0.018f) radial_w = 0.018f;
            float trans_w = 0.018f * z_world * z_world * 0.65f;
            if (trans_w < 0.025f) trans_w = 0.025f;

            int on_radial = (fx < radial_w * 0.90f);
            int on_trans  = (fz < trans_w * 0.78f);

            /* Reflexo quente sob o pedestal do personagem */
            float ped_dx = (xn - 0.70f) / 0.21f;
            float ped_dy = (yn - 0.80f) / 0.13f;
            float ped_glow = expf(-(ped_dx * ped_dx + ped_dy * ped_dy) * 1.35f) * 0.62f;
            float hor_glow = expf(-(yn - horizon) * 16.0f) * 0.45f;
            float pulse_ring = 0.86f + 0.14f * sinf(z_world * 1.8f - t * 2.6f);

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

                Color lcol = (Color){
                    clamp_u8(250.0f * line_lum),
                    clamp_u8(126.0f * line_lum),
                    clamp_u8(48.0f * line_lum),
                    245
                };
                DrawTextCodepoint(font, (int)lch, (Vector2){px, py}, glyph_sz, lcol);
            } else {
                float tile_n = 0.22f + 0.09f * sinf(x_world * 4.2f) * cosf(z_world * 4.2f);
                float plate_lum = (dist_fade * tile_n + ped_glow + hor_glow) * left_atten;
                if (plate_lum < 0.025f) continue;

                int idx = (int)(clamp_f01(plate_lum * 1.15f + dither * 0.6f) * 12.9f);
                if (idx < 0) idx = 0;
                if (idx > 12) idx = 12;

                float col_scale = 0.24f + plate_lum * 1.05f;
                Color fcol = (Color){
                    clamp_u8((115.0f + ped_glow * 140.0f) * col_scale),
                    clamp_u8((66.0f + ped_glow * 74.0f) * col_scale),
                    clamp_u8((44.0f + dist_fade * 36.0f) * col_scale),
                    225
                };
                DrawTextCodepoint(font, (int)SOLID_BG_RAMP[idx], (Vector2){px, py}, glyph_sz, fcol);
            }
        }
    }
}

static const char *glyph_rows_5x7(char ch, int row) {
    static const char *A[7] = {".###.","##.##","##.##","#####","##.##","##.##","##.##"};
    static const char *S[7] = {".####","##...","##...",".###.","...##","...##","####."};
    static const char *C[7] = {".####","##...","##...","##...","##...","##...",".####"};
    static const char *I[7] = {"#####","..#..","..#..","..#..","..#..","..#..","#####"};
    static const char *D[7] = {"####.","##.##","##.##","##.##","##.##","##.##","####."};
    static const char *O[7] = {".###.","##.##","##.##","##.##","##.##","##.##",".###."};
    static const char *M[7] = {"##.##","#####","#####","##.##","##.##","##.##","##.##"};
    if (row < 0 || row >= 7) return ".....";
    switch (ch) {
        case 'A': return A[row];
        case 'S': return S[row];
        case 'C': return C[row];
        case 'I': return I[row];
        case 'D': return D[row];
        case 'O': return O[row];
        case 'M': return M[row];
        default:  return ".....";
    }
}

/* Desenha uma palavra em micro-malha ASCII continua com dithering e sombra ASCII (ZERO DrawRectangle) */
static void draw_dense_ascii_word(Font font, const char *word, int x, int y,
                                  float cell, int sub_div, Color top_col, Color bot_col,
                                  float t) {
    static const char *LOGO_RAMP = "=+*x#%8&WM@"; /* 11 niveis de alta cobertura */
    int len = (int)strlen(word);
    int char_cols = 5 * sub_div;
    int gap_cols = sub_div;
    int total_rows = 7 * sub_div;
    float glyph_sz = cell * 2.65f;
    if (glyph_sz < 3.5f) glyph_sz = 3.5f;

    /* Passada 1: sombra discreta apenas em ASCII puro nas bordas inferiores/direitas */
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
                if (!empty_right && !empty_bot) continue;

                for (int sr = 0; sr < sub_div; sr++) {
                    int mr = br * sub_div + sr;
                    for (int sc_i = 0; sc_i < sub_div; sc_i++) {
                        int is_r = (empty_right && sc_i == sub_div - 1);
                        int is_b = (empty_bot && sr == sub_div - 1);
                        if (!is_r && !is_b) continue;
                        int mc = bc * sub_div + sc_i;
                        float sx = gx0 + (float)mc * cell + sh_off;
                        float sy = (float)y + (float)mr * cell + sh_off;
                        DrawTextCodepoint(font, ':', (Vector2){sx, sy}, glyph_sz,
                                          (Color){8, 10, 16, 225});
                    }
                }
            }
        }
    }

    /* Passada 2: preenchimento ultra-denso continuo em ASCII puro */
    for (int idx = 0; idx < len; idx++) {
        char ch = word[idx];
        float gx0 = (float)x + (float)(idx * (char_cols + gap_cols)) * cell;
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
                        int mc = bc * sub_div + sc_i;
                        float px = gx0 + (float)mc * cell;
                        float py = (float)y + (float)mr * cell;

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

                        Color c_fg = shade_rgb(cr, cg, cb, boost, 255);
                        DrawTextCodepoint(font, (int)gch, (Vector2){px, py}, glyph_sz, c_fg);
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
    draw_dense_ascii_word(font, "ASCII", left_x, top_y, sm_cell, sub_sm,
                          (Color){232, 240, 252, 255}, (Color){140, 162, 195, 255}, t);

    int tag_x = left_x + (int)((29.0f * (float)sub_sm) * sm_cell) + (int)(12.0f * sc);
    int tag_sz = (int)(14.0f * sc);
    if (tag_sz < 10) tag_sz = 10;
    DrawText("ENGINE", tag_x, top_y + (int)(2.0f * sc), tag_sz, (Color){255, 185, 80, 245});
    DrawText("RAYLIB A2", tag_x, top_y + (int)(17.0f * sc), tag_sz - 1, (Color){175, 190, 212, 235});

    float big_cell = (1.78f * (7.0f / (float)sub_bg)) * sc;
    int doom_y = top_y + (int)((7.0f * (float)sub_sm) * sm_cell) + (int)(10.0f * sc);
    draw_dense_ascii_word(font, "DOOM", left_x, doom_y, big_cell, sub_bg,
                          (Color){255, 220, 95, 255}, (Color){232, 54, 36, 255}, t);

    int sub_y = doom_y + (int)((7.0f * (float)sub_bg) * big_cell) + (int)(10.0f * sc);
    int sub_sz = (int)(15.0f * sc);
    if (sub_sz < 11) sub_sz = 11;
    DrawText("SHOOTER 3D EM C  //  LAN & SINGLEPLAYER", left_x, sub_y,
             sub_sz, (Color){205, 218, 235, 245});

    return sub_y + sub_sz + (int)(24.0f * sc);
}

/* Showcase do soldado placeholder em micro-ASCII com densidade dinamica */
static void draw_dense_character_showcase(Font font, int sw, int sh, float sc, float t,
                                          int page, int quality) {
    static const char *SOLID_RAMP = "=-+*xX#%8&BWM@"; /* 14 niveis densos de alta cobertura */
    static int mat_grid[SHOWCASE_GH][SHOWCASE_GW];
    static float nx_grid[SHOWCASE_GH][SHOWCASE_GW];
    static float ny_grid[SHOWCASE_GH][SHOWCASE_GW];

    ShowcaseLayout L = calc_showcase_layout(sw, sh, sc, page, quality);
    float breath = sinf(t * 2.3f) * 0.60f;
    float scan_row = fmodf(t * 22.0f, 112.0f) - 12.0f;

    /* Primeira passada: amostra geometria continua na grade 210x260 para detectar contornos e juntas */
    for (int r = 0; r < L.gh; r++) {
        float gy = (((float)r + 0.5f) / (float)L.gh) * 86.0f;
        for (int c = 0; c < L.gw; c++) {
            float gx = (((float)c + 0.5f) / (float)L.gw - 0.5f) * 68.0f;
            eval_showcase_point(gx, gy, breath,
                                &mat_grid[r][c], &nx_grid[r][c], &ny_grid[r][c]);
        }
    }

    /* Segunda passada: desenha exclusivamente caracteres densos encostando entre si */
    for (int r = 0; r < L.gh; r++) {
        float gy = (((float)r + 0.5f) / (float)L.gh) * 86.0f;
        float py = (float)L.y0 + (float)r * L.ch;

        for (int c = 0; c < L.gw; c++) {
            int mat = mat_grid[r][c];
            if (mat == 0) continue;

            float nx = nx_grid[r][c];
            float ny = ny_grid[r][c];
            float nz = sqrtf(fmaxf(0.08f, 1.0f - nx * nx * 0.62f - ny * ny * 0.45f));
            float ndotl = fmaxf(0.20f, (-0.44f * nx - 0.50f * ny + 0.74f * nz));
            float spec = (mat == 2 || mat == 3)
                ? powf(fmaxf(0.0f, (-0.30f * nx - 0.42f * ny + 0.86f * nz)), 10.0f) * 0.36f
                : 0.0f;
            float rim = powf(fmaxf(0.0f, 1.0f - nz), 1.8f) * 0.60f;
            float scan = expf(-fabsf(gy - scan_row) * 0.45f) * 0.24f;

            float cr, cg, cb;
            if (mat == 1) {
                cr = 152.0f; cg = 178.0f; cb = 208.0f;
            } else if (mat == 2) {
                cr = 222.0f; cg = 234.0f; cb = 250.0f;
            } else if (mat == 3) {
                cr = 255.0f; cg = 188.0f; cb = 68.0f;
                ndotl = fmaxf(ndotl, 0.90f);
            } else if (mat == 5) {
                cr = 112.0f; cg = 136.0f; cb = 166.0f;
            } else {
                cr = 102.0f; cg = 118.0f; cb = 140.0f;
            }

            float lum = ndotl * 1.05f + spec + rim * 0.44f + scan;
            float px = (float)L.x0 + (float)c * L.cw;

            int m_l = (c > 0) ? mat_grid[r][c - 1] : 0;
            int m_r = (c + 1 < L.gw) ? mat_grid[r][c + 1] : 0;
            int m_u = (r > 0) ? mat_grid[r - 1][c] : 0;
            int m_d = (r + 1 < L.gh) ? mat_grid[r + 1][c] : 0;

            int is_outer = (m_l == 0 || m_r == 0 || m_u == 0 || m_d == 0);
            int is_seam = (!is_outer && (m_l != mat || m_r != mat || m_u != mat || m_d != mat));

            char glyph;
            float ch_lum = fmaxf(lum, 0.22f);

            if (is_outer) {
                if (m_u == 0 || m_d == 0) {
                    glyph = (mat == 5) ? '~' : '_';
                } else if (m_l == 0) {
                    glyph = (ny < -0.25f) ? '/' : (ny > 0.25f ? '\\' : '[');
                } else {
                    glyph = (ny < -0.25f) ? '\\' : (ny > 0.25f ? '/' : ']');
                }
                ch_lum = fmaxf(ch_lum * 1.18f, 0.78f);
            } else if (mat == 5) {
                static const char *PAD_CH = "=#*+WM";
                glyph = PAD_CH[(c + r * 2) % 6];
            } else {
                float d_lum = lum + bayer4x4(c, r) * 0.75f;
                if (is_seam) {
                    d_lum = fmaxf(d_lum, 0.65f);
                }
                int r_idx = (int)(clamp_f01(d_lum) * 13.9f);
                if (r_idx < 0) r_idx = 0;
                if (r_idx > 13) r_idx = 13;
                glyph = is_seam ? ((m_u != mat || m_d != mat) ? '=' : '#') : SOLID_RAMP[r_idx];
            }

            float edge_tint = rim * ((mat == 3) ? 0.25f : 0.78f);
            Color fg = (Color){
                clamp_u8(cr * ch_lum + edge_tint * 92.0f),
                clamp_u8(cg * ch_lum + edge_tint * 55.0f),
                clamp_u8(cb * ch_lum + edge_tint * 25.0f),
                255
            };
            DrawTextCodepoint(font, (int)glyph, (Vector2){px, py}, L.glyph_sz, fg);
        }
    }

    /* Etiqueta compacta acoplada logo abaixo da plataforma ASCII */
    int tag_y = L.y0 + (int)L.total_h - (int)(6.0f * sc);
    int tag_sz = (int)(15.0f * sc);
    if (tag_sz < 10) tag_sz = 10;
    const char *tag = "[ PLACEHOLDER  //  SELECIONADO EM PERSONALIZAR ]";
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

/* ============================================================================
 * SISTEMA DE MENU COM SUB-TELAS (PRINCIPAL, MULTIPLAYER, PERSONALIZAR, CONFIG)
 * ============================================================================ */

static void update_bind_capture(AppConfig *cfg, int *listening_bind) {
    if (!cfg || !listening_bind || *listening_bind < 0) {
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        *listening_bind = -1;
        return;
    }
    int pressed = GetKeyPressed();
    if (pressed > 0 && pressed != KEY_F11 && pressed != KEY_F12 &&
        pressed != KEY_F3 && pressed != KEY_F2) {
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

static int draw_settings_panel(AppConfig *cfg, int panel_x, int panel_y,
                               int max_h, int sw, float sc, int pad_x,
                               int center_x, int *settings_tab,
                               int *listening_bind, const char *back_label) {
    int clicked_back = 0;
    int lbl_sz = (int)(15.5f * sc);
    if (lbl_sz < 11) lbl_sz = 11;
    int b_font = (int)(13.5f * sc);
    if (b_font < 10) b_font = 10;

    int pw = (int)(555.0f * sc);
    int max_pw = center_x ? (sw - 32) : (int)((float)sw * 0.56f);
    int min_sw = MeasureText("Modo de Exibicao (F11):", lbl_sz) +
                 calc_fit_w("MODO: SEM BORDAS", b_font, (int)(145.0f * sc), sc) +
                 pad_x * 2 + (int)(20.0f * sc);
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
        int row_step = avail_h / 6;
        if (row_step > (int)(48.0f * sc)) row_step = (int)(48.0f * sc);
        if (row_step < 24) row_step = 24;
        int row_bh = (int)(28.0f * sc);
        if (row_bh > row_step - 5) row_bh = row_step - 5;
        if (row_bh < 19) row_bh = 19;

        int sq_w = (int)(28.0f * sc);
        if (sq_w < 22) sq_w = 22;
        int val_w = calc_fit_w("AUTO (JANELA)", b_font, (int)(116.0f * sc), sc);
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

        /* Linha 2: Densidade Dinamica ASCII */
        int r1y = sy + row_step;
        DrawText("Densidade ASCII (F2):", panel_x + pad_x, r1y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
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

        /* Linha 3: Modo de Exibicao (Janela / Janela Sem Bordas) */
        int r2y = sy + row_step * 2;
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

        /* Linha 4: Mostrar FPS na Tela */
        int r3y = sy + row_step * 3;
        DrawText("Mostrar FPS na Tela (F3):", panel_x + pad_x, r3y + (row_bh - lbl_sz) / 2, lbl_sz, WHITE);
        const char *sfps_txt = cfg->show_fps ? "FPS: ATIVADO" : "FPS: DESATIVADO";
        int sfps_bw = calc_fit_w("FPS: DESATIVADO", b_font, (int)(152.0f * sc), sc);
        if (draw_small_button(right_edge - sfps_bw, r3y, sfps_bw, row_bh,
                              sfps_txt, b_font,
                              cfg->show_fps ? (Color){52, 130, 68, 255} : (Color){44, 48, 58, 255},
                              (Color){205, 95, 35, 255})) {
            cfg->show_fps = !cfg->show_fps;
        }

        /* Linha 5: Limite de FPS */
        int r4y = sy + row_step * 4;
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

        /* Linha 6: Sensibilidade do Mouse */
        int r5y = sy + row_step * 5;
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

static int run_menu_screen(AppConfig *cfg, char *ip_buf, size_t ip_size,
                           int *port_out, const char *status_msg) {
    const char *main_items[5] = {
        "JOGAR SINGLEPLAYER",
        "JOGAR MULTIPLAYER",
        "PERSONALIZAR",
        "CONFIGURACOES",
        "SAIR"
    };

    MenuPage page = MENU_PAGE_MAIN;
    int main_sel = 0;
    int mp_editing_ip = 0;
    int settings_tab = 0; /* 0 = Geral/Video, 1 = Controles/Binds */
    int listening_bind = -1;
    *port_out = DEFAULT_PORT;

    if (IsCursorHidden()) {
        EnableCursor();
    }

    Font font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);

    while (!WindowShouldClose()) {
        sync_window_focus_and_borderless();
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        float sc = fminf((float)sw / 1280.0f, (float)sh / 720.0f);
        if (sc < 0.48f) sc = 0.48f;
        float now = (float)GetTime();

        if (IsKeyPressed(KEY_F11)) {
            toggle_borderless_window();
        }
        if (IsKeyPressed(KEY_F2) && listening_bind < 0) {
            cfg->ascii_quality = (cfg->ascii_quality + 1) % 5;
        }
        if (IsKeyPressed(KEY_F3) && listening_bind < 0) {
            cfg->show_fps = !cfg->show_fps;
        }

        /* Captura de tecla quando estiver reconfigurando uma bind */
        if (page == MENU_PAGE_SETTINGS && settings_tab == 1 && listening_bind >= 0) {
            update_bind_capture(cfg, &listening_bind);
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            if (page == MENU_PAGE_MAIN) {
                return 4;
            } else {
                mp_editing_ip = 0;
                listening_bind = -1;
                page = MENU_PAGE_MAIN;
            }
        }

        /* Navegacao por teclado no menu principal */
        if (page == MENU_PAGE_MAIN) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
                main_sel = (main_sel + 1) % 5;
            }
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                main_sel = (main_sel + 4) % 5;
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                if (main_sel == 0) return 1;
                if (main_sel == 1) page = MENU_PAGE_MULTIPLAYER;
                if (main_sel == 2) page = MENU_PAGE_CUSTOMIZE;
                if (main_sel == 3) page = MENU_PAGE_SETTINGS;
                if (main_sel == 4) return 4;
            }
        } else if (page == MENU_PAGE_MULTIPLAYER && mp_editing_ip) {
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
        }

        /* ======================== DESENHO DO FRAME ======================== */
        BeginDrawing();
        draw_cyber_arena_backdrop(font, sw, sh, sc, now, (int)page, cfg->ascii_quality);

        /* Showcase do personagem placeholder grande em Micro-ASCII 3D */
        draw_dense_character_showcase(font, sw, sh, sc, now, (int)page, cfg->ascii_quality);

        int left_x = (int)((float)sw * 0.050f);
        int top_y = (int)((float)sh * 0.048f);

        /* Logo ASCII DOOM densa no topo esquerdo + inicio da area de conteudo */
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
                    if (i == 0) { EndDrawing(); return 1; }
                    if (i == 1) page = MENU_PAGE_MULTIPLAYER;
                    if (i == 2) page = MENU_PAGE_CUSTOMIZE;
                    if (i == 3) page = MENU_PAGE_SETTINGS;
                    if (i == 4) { EndDrawing(); return 4; }
                }
            }
        } else if (page == MENU_PAGE_MULTIPLAYER) {
            int pw = (int)(490.0f * sc);
            if (pw < 350) pw = 350;
            int ph = (int)(345.0f * sc);
            if (content_y + ph > footer_top) ph = footer_top - content_y;

            DrawRectangle(left_x, content_y, pw, ph, (Color){14, 18, 25, 236});
            DrawRectangleLinesEx((Rectangle){(float)left_x, (float)content_y, (float)pw, (float)ph},
                                 2.0f, (Color){225, 105, 35, 255});

            DrawText("MULTIPLAYER LAN / UDP", left_x + pad_x, content_y + (int)(16.0f * sc),
                     btn_font, (Color){255, 200, 90, 255});

            if (draw_chamfer_button(left_x + pad_x, content_y + (int)(56.0f * sc),
                                    pw - pad_x * 2, btn_h, bevel,
                                    "HOSPEDAR PARTIDA (HOST UDP)", btn_font - 2, 0, NULL)) {
                EndDrawing();
                return 2;
            }

            if (draw_chamfer_button(left_x + pad_x, content_y + (int)(120.0f * sc),
                                    pw - pad_x * 2, btn_h, bevel,
                                    "CONECTAR AO HOST (CLIENT UDP)", btn_font - 2, 0, NULL)) {
                if (ip_buf[0] == '\0') snprintf(ip_buf, ip_size, "127.0.0.1");
                EndDrawing();
                return 3;
            }

            int ip_y = content_y + (int)(190.0f * sc);
            DrawText("IP do Host (clique para editar):", left_x + pad_x, ip_y,
                     (int)(15.0f * sc), (Color){175, 190, 210, 255});
            Rectangle ip_box = {(float)(left_x + pad_x), (float)(ip_y + (int)(22.0f * sc)),
                                (float)(pw - pad_x * 2), (float)(42.0f * sc)};
            if (CheckCollisionPointRec(GetMousePosition(), ip_box) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                mp_editing_ip = 1;
            }
            DrawRectangleRec(ip_box, mp_editing_ip ? (Color){32, 42, 58, 255}
                                                   : (Color){22, 26, 34, 255});
            DrawRectangleLinesEx(ip_box, 1.5f,
                                 mp_editing_ip ? (Color){100, 205, 255, 255}
                                               : (Color){85, 95, 115, 255});
            DrawText(TextFormat("%s%s  (Porta: %d)", ip_buf, mp_editing_ip ? "_" : "", *port_out),
                     (int)ip_box.x + 12, (int)ip_box.y + (int)(10.0f * sc), (int)(17.0f * sc),
                     (Color){255, 235, 120, 255});

            int v_font = (int)(15.0f * sc);
            if (v_font < 13) v_font = 13;
            int vw = calc_fit_w("VOLTAR", v_font, (int)(120.0f * sc), sc);
            int vh = (int)(36.0f * sc);
            if (draw_small_button(left_x + pad_x, content_y + ph - vh - (int)(16.0f * sc),
                                  vw, vh, "VOLTAR", v_font,
                                  (Color){38, 42, 52, 255}, (Color){195, 75, 30, 255})) {
                mp_editing_ip = 0;
                page = MENU_PAGE_MAIN;
            }
        } else if (page == MENU_PAGE_CUSTOMIZE) {
            int desc_sz = (int)(15.0f * sc);
            if (desc_sz < 12) desc_sz = 12;
            const char *line_longest = "O personagem selecionado aqui sera exibido no pedestal";
            int min_cw = MeasureText(line_longest, desc_sz) + pad_x * 2 + 12;
            int pw = (int)(520.0f * sc);
            if (pw < min_cw) pw = min_cw;
            int ph = (int)(300.0f * sc);
            if (content_y + ph > footer_top) ph = footer_top - content_y;

            DrawRectangle(left_x, content_y, pw, ph, (Color){14, 18, 25, 236});
            DrawRectangleLinesEx((Rectangle){(float)left_x, (float)content_y, (float)pw, (float)ph},
                                 2.0f, (Color){225, 145, 55, 255});

            DrawText("PERSONALIZAR PERSONAGEM", left_x + pad_x, content_y + (int)(18.0f * sc),
                     btn_font, (Color){255, 200, 90, 255});

            int badge_sz = (int)(16.0f * sc);
            if (badge_sz < 13) badge_sz = 13;
            DrawText("[ PLACEHOLDER - MODULO DO RAFAEL STRAZZER ]",
                     left_x + pad_x, content_y + (int)(58.0f * sc),
                     badge_sz, (Color){155, 240, 125, 255});

            int dy = content_y + (int)(98.0f * sc);
            int dstep = (int)(26.0f * sc);
            Color desc_col = (Color){210, 220, 230, 255};

            DrawText("Espaco reservado para a integracao do Criador de",
                     left_x + pad_x, dy, desc_sz, desc_col);
            DrawText("Personagens e selecao dos 4 Personagens Base.",
                     left_x + pad_x, dy + dstep, desc_sz, desc_col);
            DrawText(line_longest,
                     left_x + pad_x, dy + dstep * 2 + 6, desc_sz, desc_col);
            DrawText("do menu principal e aplicado ao iniciar a partida.",
                     left_x + pad_x, dy + dstep * 3 + 6, desc_sz, desc_col);

            int v_font = (int)(15.0f * sc);
            if (v_font < 13) v_font = 13;
            int vw = calc_fit_w("VOLTAR", v_font, (int)(120.0f * sc), sc);
            int vh = (int)(36.0f * sc);
            if (draw_small_button(left_x + pad_x, content_y + ph - vh - (int)(16.0f * sc),
                                  vw, vh, "VOLTAR", v_font,
                                  (Color){38, 42, 52, 255}, (Color){195, 75, 30, 255})) {
                page = MENU_PAGE_MAIN;
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

    return 4;
}

static void run_game_session(NetMode mode, const char *host_ip, int port,
                             AppConfig *cfg, char *err_buf, size_t err_size) {
    GameMap map;
    load_initial_map(&map);

    Player player;
    NetContext net;

    if (mode == NET_MODE_HOST) {
        player_init(&player, 2.5f, 4.5f, 0.0f);
        player.id = 1;
    } else if (mode == NET_MODE_CLIENT) {
        player_init(&player, 13.5f, 11.5f, PI_F);
        player.id = 2;
    } else {
        player_init(&player, 2.5f, 4.5f, 0.0f);
    }

    if (!net_init(&net, mode, host_ip, port)) {
        snprintf(err_buf, err_size,
                 "Erro ao iniciar rede UDP na porta %d (IP: %s).", port, host_ip);
        return;
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
    int is_multiplayer = (mode != NET_MODE_OFFLINE);
    float cam_pitch = 0.0f;
    int third_person = 0;

    while (!WindowShouldClose() && running) {
        int win_active = sync_window_focus_and_borderless();
        int alt_down = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

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

        float dt = GetFrameTime();
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        if (IsKeyPressed(KEY_F11)) {
            toggle_borderless_window();
        }
        if (IsKeyPressed(KEY_F2) && pause_listening_bind < 0) {
            cfg->ascii_quality = (cfg->ascii_quality + 1) % 5;
        }
        if (IsKeyPressed(KEY_F3) && pause_listening_bind < 0) {
            cfg->show_fps = !cfg->show_fps;
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

            handle_custom_input(&player, &map, &cfg->binds, dt, &running, &did_shoot, &cam_pitch);
        }

        if (is_multiplayer) {
            net_receive_state(&net);
            net_send_state(&net, &player, did_shoot);

            if (!paused && did_shoot && net.connected) {
                float dx = net.remote_state.x - player.x;
                float dy = net.remote_state.y - player.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float target_angle = atan2f(dy, dx);
                if (dist < 10.0f && angle_diff_abs(target_angle, player.angle) < 0.12f) {
                    player.score += 10;
                }
            }
        }

        SpriteEntity remote_sprite;
        int sprite_count = 0;

        if (is_multiplayer && net.connected) {
            remote_sprite.x = net.remote_state.x;
            remote_sprite.y = net.remote_state.y;
            remote_sprite.active = 1;
            remote_sprite.shooting = net.remote_state.shooting;
            remote_sprite.label = 'O';
            sprite_count = 1;
        } else if (!is_multiplayer) {
            remote_sprite.x = 7.5f;
            remote_sprite.y = 4.5f;
            remote_sprite.active = 1;
            remote_sprite.shooting = 0;
            remote_sprite.label = 'X';
            sprite_count = 1;

            if (!paused && did_shoot) {
                float dx = remote_sprite.x - player.x;
                float dy = remote_sprite.y - player.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float target_angle = atan2f(dy, dx);
                if (dist < 10.0f && angle_diff_abs(target_angle, player.angle) < 0.10f) {
                    player.score += 10;
                }
            }
        }

        renderer_set_ascii_density(cfg->ascii_quality, sw, sh);
        renderer_set_camera_view(cam_pitch, third_person);
        renderer_clear();
        renderer_draw_walls(&map, &player);
        renderer_draw_sprites(&player, &remote_sprite, sprite_count);
        renderer_draw_minimap(&map, &player, &remote_sprite, sprite_count);
        renderer_draw_weapon(&player);
        renderer_draw_hud(&player);

        BeginDrawing();
        renderer_draw_scene_and_hud(&player, is_multiplayer, net.connected,
                                    mouse_locked, cfg->show_fps ? cfg->fps_cap : -1);

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

    renderer_set_camera_view(0.0f, 0);
    net_close(&net);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--test-frame") == 0) {
        return run_single_test_frame();
    }

    renderer_init();

    AppConfig cfg;
    app_config_init(&cfg);

    char host_ip[64] = "127.0.0.1";
    char status_msg[128] = "";
    int port = DEFAULT_PORT;

    while (!WindowShouldClose()) {
        int choice = run_menu_screen(&cfg, host_ip, sizeof(host_ip), &port, status_msg);
        status_msg[0] = '\0';

        if (choice == 1) {
            run_game_session(NET_MODE_OFFLINE, host_ip, port, &cfg,
                             status_msg, sizeof(status_msg));
        } else if (choice == 2) {
            run_game_session(NET_MODE_HOST, host_ip, port, &cfg,
                             status_msg, sizeof(status_msg));
        } else if (choice == 3) {
            run_game_session(NET_MODE_CLIENT, host_ip, port, &cfg,
                             status_msg, sizeof(status_msg));
        } else {
            break;
        }
    }

    cleanup_raylib_screenshots();
    renderer_close();
    return 0;
}
