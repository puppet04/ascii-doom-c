#include "renderer.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static AsciiCell screen_grid[SCREEN_HEIGHT][SCREEN_WIDTH];
static float z_buffer[SCREEN_WIDTH];
static Font ui_font;
static int font_loaded = 0;

static AsciiColor make_color(unsigned char r, unsigned char g, unsigned char b) {
    AsciiColor c = {r, g, b};
    return c;
}

static AsciiColor scale_color(AsciiColor c, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    AsciiColor out;
    out.r = (unsigned char)((float)c.r * factor);
    out.g = (unsigned char)((float)c.g * factor);
    out.b = (unsigned char)((float)c.b * factor);
    return out;
}

static void set_cell(int x, int y, char ch, AsciiColor fg, AsciiColor bg) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    screen_grid[y][x].ch = ch;
    screen_grid[y][x].fg = fg;
    screen_grid[y][x].bg = bg;
}

static float wrap_angle_signed(float angle) {
    while (angle > PI_F) {
        angle -= 2.0f * PI_F;
    }
    while (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    return angle;
}

static AsciiColor get_tile_base_color(char tile) {
    switch (tile) {
        case '#': return make_color(225, 65, 55);   /* Tijolo vermelho DOOM */
        case '@': return make_color(70, 155, 245);  /* Metal azul tech */
        case '%': return make_color(65, 210, 110);  /* Parede verde musgo */
        default:  return make_color(220, 165, 65);  /* Ambar */
    }
}

static char get_wall_shade(float dist, int side) {
    const char *shades = "#@%*=+-:. ";
    int num_shades = 9;
    float norm = dist / 11.0f;

    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    int idx = (int)(norm * (num_shades - 1));
    if (side == 1 && idx < num_shades - 1) {
        idx++;
    }
    return shades[idx];
}

void renderer_init(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "ASCII DOOM C - 165 FPS (Raylib)");
    SetWindowMinSize(640, 480);
    SetExitKey(KEY_NULL);
    SetTargetFPS(TARGET_FPS);
    ui_font = GetFontDefault();
    font_loaded = 1;
    DisableCursor();
}

void renderer_close(void) {
    if (IsWindowReady()) {
        if (IsCursorHidden()) {
            EnableCursor();
        }
        CloseWindow();
    }
    font_loaded = 0;
}

void renderer_clear(void) {
    AsciiColor black = make_color(0, 0, 0);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            screen_grid[y][x].ch = ' ';
            screen_grid[y][x].fg = black;
            screen_grid[y][x].bg = black;
        }
    }
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        z_buffer[x] = MAX_DEPTH;
    }
}

void renderer_draw_walls(const GameMap *map, const Player *player) {
    if (!map || !player) {
        return;
    }

    AsciiColor bg_black = make_color(0, 0, 0);

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        float ray_angle = (player->angle - player->fov * 0.5f) +
                          ((float)x / (float)SCREEN_WIDTH) * player->fov;

        float ray_dir_x = cosf(ray_angle);
        float ray_dir_y = sinf(ray_angle);

        int map_x = (int)floorf(player->x);
        int map_y = (int)floorf(player->y);

        float delta_dist_x = (fabsf(ray_dir_x) < 1e-6f) ? 1e30f : fabsf(1.0f / ray_dir_x);
        float delta_dist_y = (fabsf(ray_dir_y) < 1e-6f) ? 1e30f : fabsf(1.0f / ray_dir_y);

        int step_x, step_y;
        float side_dist_x, side_dist_y;

        if (ray_dir_x < 0.0f) {
            step_x = -1;
            side_dist_x = (player->x - (float)map_x) * delta_dist_x;
        } else {
            step_x = 1;
            side_dist_x = ((float)map_x + 1.0f - player->x) * delta_dist_x;
        }

        if (ray_dir_y < 0.0f) {
            step_y = -1;
            side_dist_y = (player->y - (float)map_y) * delta_dist_y;
        } else {
            step_y = 1;
            side_dist_y = ((float)map_y + 1.0f - player->y) * delta_dist_y;
        }

        int hit = 0;
        int side = 0;
        int steps = 0;

        while (!hit && steps < 64) {
            if (side_dist_x < side_dist_y) {
                side_dist_x += delta_dist_x;
                map_x += step_x;
                side = 0;
            } else {
                side_dist_y += delta_dist_y;
                map_y += step_y;
                side = 1;
            }

            if (map_is_wall(map, map_x, map_y)) {
                hit = 1;
            }
            steps++;
        }

        float raw_dist = MAX_DEPTH;
        if (hit) {
            raw_dist = (side == 0) ? (side_dist_x - delta_dist_x)
                                   : (side_dist_y - delta_dist_y);
        }

        float perp_dist = raw_dist * cosf(ray_angle - player->angle);
        if (perp_dist < 0.1f) perp_dist = 0.1f;
        if (perp_dist > MAX_DEPTH) perp_dist = MAX_DEPTH;

        z_buffer[x] = perp_dist;

        /* Posicao exata do impacto na face do bloco para desenhar bordas verticais */
        float wall_hit_coord = (side == 0)
            ? (player->y + raw_dist * ray_dir_y)
            : (player->x + raw_dist * ray_dir_x);
        wall_hit_coord -= floorf(wall_hit_coord);
        int is_block_edge = (wall_hit_coord < 0.045f || wall_hit_coord > 0.955f);

        int line_height = (int)((float)SCREEN_HEIGHT / perp_dist);
        int draw_start = (SCREEN_HEIGHT - line_height) / 2;
        int draw_end = (SCREEN_HEIGHT + line_height) / 2;

        if (draw_start < 0) draw_start = 0;
        if (draw_end >= SCREEN_HEIGHT) draw_end = SCREEN_HEIGHT - 1;

        char tile = hit ? map_get_tile(map, map_x, map_y) : '#';
        AsciiColor base_wall = get_tile_base_color(tile);
        float fog = 1.0f - (perp_dist / 13.0f);
        if (fog < 0.16f) fog = 0.16f;
        if (side == 1) fog *= 0.70f;

        AsciiColor wall_fg = scale_color(base_wall, fog);
        AsciiColor edge_fg = scale_color(base_wall, fog * 0.45f);
        char wall_ch = hit ? get_wall_shade(perp_dist, side) : ' ';
        if (is_block_edge && perp_dist < 8.5f) {
            wall_ch = '|';
            wall_fg = edge_fg;
        }

        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            if (y < draw_start) {
                /* Teto estilizado discreto */
                float ceil_ratio = 1.0f - ((float)y / (float)(SCREEN_HEIGHT / 2));
                if ((x + y) % 5 == 0 && ceil_ratio > 0.3f) {
                    unsigned char v = (unsigned char)(25 + ceil_ratio * 35);
                    set_cell(x, y, '.', make_color(v, v, (unsigned char)(v + 15)), bg_black);
                }
            } else if (y <= draw_end) {
                set_cell(x, y, wall_ch, wall_fg, bg_black);
            } else {
                /* Chao sombreado por profundidade */
                float floor_ratio = (float)(y - SCREEN_HEIGHT / 2) / (float)(SCREEN_HEIGHT / 2);
                char floor_ch = ' ';
                if (floor_ratio > 0.72f) floor_ch = '.';
                else if (floor_ratio > 0.42f) floor_ch = ',';
                else if (floor_ratio > 0.18f) floor_ch = '-';

                if (floor_ch != ' ') {
                    unsigned char shade = (unsigned char)(55 + floor_ratio * 85);
                    set_cell(x, y, floor_ch, make_color(shade, (unsigned char)(shade * 0.75f), (unsigned char)(shade * 0.55f)), bg_black);
                }
            }
        }
    }
}

void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count) {
    if (!player || !sprites || count <= 0) {
        return;
    }

    AsciiColor bg_black = make_color(0, 0, 0);

    for (int i = 0; i < count; i++) {
        if (!sprites[i].active) {
            continue;
        }

        float dx = sprites[i].x - player->x;
        float dy = sprites[i].y - player->y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist < 0.25f || dist >= MAX_DEPTH) {
            continue;
        }

        float sprite_angle = wrap_angle_signed(atan2f(dy, dx) - player->angle);
        if (fabsf(sprite_angle) > player->fov * 0.65f) {
            continue;
        }

        int center_x = (int)((0.5f + sprite_angle / player->fov) * (float)SCREEN_WIDTH);
        int sprite_h = (int)((float)SCREEN_HEIGHT / dist);
        if (sprite_h < 4) sprite_h = 4;
        if (sprite_h > SCREEN_HEIGHT) sprite_h = SCREEN_HEIGHT;

        int sprite_w = sprite_h / 2;
        if (sprite_w < 4) sprite_w = 4;

        int top_y = (SCREEN_HEIGHT - sprite_h) / 2;
        int bot_y = top_y + sprite_h - 1;
        int left_x = center_x - sprite_w / 2;
        int right_x = center_x + sprite_w / 2;

        float fog = 1.0f - (dist / 14.0f);
        if (fog < 0.25f) fog = 0.25f;

        AsciiColor head_col = scale_color(make_color(255, 220, 120), fog);
        AsciiColor body_col = scale_color(make_color(240, 70, 210), fog);
        AsciiColor leg_col  = scale_color(make_color(120, 190, 255), fog);
        AsciiColor fire_col = make_color(255, 240, 70);

        for (int sx = left_x; sx <= right_x; sx++) {
            if (sx < 0 || sx >= SCREEN_WIDTH || dist >= z_buffer[sx]) {
                continue;
            }

            for (int sy = top_y; sy <= bot_y; sy++) {
                if (sy < 0 || sy >= SCREEN_HEIGHT) {
                    continue;
                }

                float ny = (float)(sy - top_y) / (float)sprite_h;
                float nx = (float)(sx - left_x) / (float)(sprite_w > 1 ? sprite_w - 1 : 1);
                char glyph = ' ';
                AsciiColor col = body_col;

                if (ny < 0.26f) {
                    if (nx > 0.24f && nx < 0.76f) {
                        glyph = sprites[i].label ? sprites[i].label : 'O';
                        col = head_col;
                    }
                } else if (ny < 0.68f) {
                    if (sprites[i].shooting && ny > 0.34f && ny < 0.54f) {
                        glyph = '*';
                        col = fire_col;
                    } else if (nx > 0.14f && nx < 0.86f) {
                        glyph = (nx < 0.28f || nx > 0.72f) ? '|' : '#';
                        col = body_col;
                    }
                } else {
                    if (nx < 0.42f) {
                        glyph = '/';
                        col = leg_col;
                    } else if (nx > 0.58f) {
                        glyph = '\\';
                        col = leg_col;
                    }
                }

                if (glyph != ' ') {
                    set_cell(sx, sy, glyph, col, bg_black);
                }
            }
        }
    }
}

void renderer_draw_weapon(const Player *player) {
    if (!player) {
        return;
    }

    static const char *WEAPON_ART[] = {
        "       ||       ",
        "      |==|      ",
        "      |##|      ",
        "     /####\\     ",
        "    |######|    ",
        "   [|##**##|]   ",
        "   [|######|]   ",
        "  //|======|\\\\  ",
        " // |======| \\\\ ",
        " || |######| || ",
        " || |######| || "
    };
    int art_rows = (int)(sizeof(WEAPON_ART) / sizeof(WEAPON_ART[0]));
    int art_cols = (int)strlen(WEAPON_ART[0]);

    int firing = (player->shoot_timer > 0.12f);
    int bob_x = (int)roundf(cosf(player->bob_phase * 0.5f) * 2.5f);
    int bob_y = (int)roundf(fabsf(sinf(player->bob_phase)) * 2.0f) - (firing ? 3 : 0);

    int base_x = (SCREEN_WIDTH - art_cols) / 2 + bob_x;
    int base_y = SCREEN_HEIGHT - art_rows + bob_y;

    AsciiColor bg_black = make_color(0, 0, 0);
    AsciiColor metal = make_color(195, 205, 220);
    AsciiColor dark_metal = make_color(115, 125, 145);
    AsciiColor accent = make_color(235, 165, 65);

    if (firing) {
        static const char *FLASH_ART[] = {
            "    \\  *  /    ",
            "  -- *###* --  ",
            "    *#####*    ",
            "   /  ***  \\   "
        };
        int f_rows = 4;
        int f_cols = (int)strlen(FLASH_ART[0]);
        int fx = (SCREEN_WIDTH - f_cols) / 2 + bob_x;
        int fy = base_y - f_rows;

        for (int r = 0; r < f_rows; r++) {
            for (int c = 0; c < f_cols; c++) {
                char ch = FLASH_ART[r][c];
                if (ch != ' ') {
                    AsciiColor fc = (ch == '#') ? make_color(255, 245, 120)
                                                : make_color(255, 145, 35);
                    set_cell(fx + c, fy + r, ch, fc, bg_black);
                }
            }
        }
    }

    for (int r = 0; r < art_rows; r++) {
        for (int c = 0; c < art_cols; c++) {
            char ch = WEAPON_ART[r][c];
            if (ch == ' ') {
                continue;
            }

            AsciiColor col = metal;
            if (ch == '#' || ch == '=') {
                col = dark_metal;
            } else if (ch == '*') {
                col = accent;
            }
            set_cell(base_x + c, base_y + r, ch, col, bg_black);
        }
    }
}

void renderer_draw_minimap(const GameMap *map, const Player *player,
                           const SpriteEntity *sprites, int count) {
    if (!map || !player) {
        return;
    }

    AsciiColor mm_bg = make_color(12, 14, 22);
    AsciiColor border = make_color(110, 120, 145);
    int w = map->width;
    int h = map->height;

    /* Borda ao redor do minimapa */
    for (int y = 0; y <= h + 1; y++) {
        for (int x = 0; x <= w + 1; x++) {
            int is_edge = (x == 0 || y == 0 || x == w + 1 || y == h + 1);
            if (is_edge) {
                set_cell(x, y, '+', border, mm_bg);
            }
        }
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            char tile = map_get_tile(map, x, y);
            if (map_is_wall(map, x, y)) {
                set_cell(x + 1, y + 1, tile, get_tile_base_color(tile), mm_bg);
            } else {
                set_cell(x + 1, y + 1, '.', make_color(45, 50, 62), mm_bg);
            }
        }
    }

    if (sprites && count > 0) {
        for (int i = 0; i < count; i++) {
            if (!sprites[i].active) continue;
            int sx = (int)sprites[i].x;
            int sy = (int)sprites[i].y;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                set_cell(sx + 1, sy + 1, 'E', make_color(255, 90, 220), mm_bg);
            }
        }
    }

    /* Direcao da mira no minimapa */
    int look_x = (int)(player->x + cosf(player->angle) * 1.1f);
    int look_y = (int)(player->y + sinf(player->angle) * 1.1f);
    if (look_x >= 0 && look_x < w && look_y >= 0 && look_y < h &&
        !map_is_wall(map, look_x, look_y)) {
        set_cell(look_x + 1, look_y + 1, '*', make_color(255, 190, 60), mm_bg);
    }

    int px = (int)player->x;
    int py = (int)player->y;
    if (px >= 0 && px < w && py >= 0 && py < h) {
        set_cell(px + 1, py + 1, 'P', make_color(255, 240, 70), mm_bg);
    }
}

void renderer_draw_hud(const Player *player) {
    if (!player) {
        return;
    }

    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;
    int firing = (player->shoot_timer > 0.12f);

    AsciiColor bg_black = make_color(0, 0, 0);
    AsciiColor cross_col = firing ? make_color(255, 90, 60) : make_color(110, 255, 130);

    set_cell(cx, cy, firing ? '*' : '+', cross_col, bg_black);
    set_cell(cx - 2, cy, '-', cross_col, bg_black);
    set_cell(cx + 2, cy, '-', cross_col, bg_black);
    set_cell(cx, cy - 1, '|', cross_col, bg_black);
    set_cell(cx, cy + 1, '|', cross_col, bg_black);
}

void renderer_present_window(const Player *player, int is_multiplayer,
                             int remote_connected, int mouse_locked, int fps_cap) {
    if (!font_loaded) {
        return;
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int hud_h = (int)((float)sh * 0.115f);
    if (hud_h < 68) {
        hud_h = 68;
    }
    int view_h = sh - hud_h;
    if (view_h < SCREEN_HEIGHT) {
        view_h = SCREEN_HEIGHT;
    }

    float cell_w = (float)sw / (float)SCREEN_WIDTH;
    float cell_h = (float)view_h / (float)SCREEN_HEIGHT;
    float glyph_size = cell_h * 1.15f;
    if (glyph_size < 8.0f) {
        glyph_size = 8.0f;
    }
    int rect_w = (int)ceilf(cell_w);
    int rect_h = (int)ceilf(cell_h);

    BeginDrawing();
    ClearBackground((Color){10, 11, 16, 255});

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        float py = (float)y * cell_h;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            AsciiCell cell = screen_grid[y][x];
            float px = (float)x * cell_w;

            if (cell.bg.r != 0 || cell.bg.g != 0 || cell.bg.b != 0) {
                DrawRectangle((int)px, (int)py, rect_w, rect_h,
                              (Color){cell.bg.r, cell.bg.g, cell.bg.b, 255});
            }

            if (cell.ch != ' ' && cell.ch != '\0') {
                DrawTextCodepoint(
                    ui_font,
                    (int)cell.ch,
                    (Vector2){px, py},
                    glyph_size,
                    (Color){cell.fg.r, cell.fg.g, cell.fg.b, 255}
                );
            }
        }
    }

    /* Painel inferior de HUD responsivo */
    int hud_y = view_h;
    int hud_font = (int)((float)hud_h * 0.28f);
    if (hud_font < 16) hud_font = 16;
    int hint_font = (int)((float)hud_h * 0.19f);
    if (hint_font < 12) hint_font = 12;

    DrawRectangle(0, hud_y, sw, hud_h, (Color){18, 20, 28, 255});
    DrawRectangle(0, hud_y, sw, 3, (Color){190, 45, 45, 255});

    if (player) {
        Color hp_col = (player->health > 60) ? (Color){70, 220, 110, 255}
                     : (player->health > 30) ? (Color){245, 200, 60, 255}
                                             : (Color){235, 65, 65, 255};

        int x_hp = (int)((float)sw * 0.02f);
        int hp_bar_w = (int)((float)sw * 0.13f);
        if (hp_bar_w < 100) hp_bar_w = 100;
        int row1_y = hud_y + (int)((float)hud_h * 0.16f);
        int bar_y = hud_y + (int)((float)hud_h * 0.52f);

        DrawText(TextFormat("VIDA: %d%%", player->health), x_hp, row1_y, hud_font, hp_col);
        DrawRectangle(x_hp, bar_y, hp_bar_w, 8, (Color){40, 44, 56, 255});
        int hp_w = (player->health * hp_bar_w) / 100;
        if (hp_w < 0) hp_w = 0;
        if (hp_w > hp_bar_w) hp_w = hp_bar_w;
        DrawRectangle(x_hp, bar_y, hp_w, 8, hp_col);

        Color ammo_col = (player->ammo > 5) ? (Color){250, 210, 80, 255}
                                            : (Color){240, 75, 75, 255};
        DrawText(TextFormat("MUNICAO: %02d/30", player->ammo),
                 (int)((float)sw * 0.19f), row1_y, hud_font, ammo_col);
        DrawText(TextFormat("PONTOS: %d", player->score),
                 (int)((float)sw * 0.37f), row1_y, hud_font, (Color){120, 215, 255, 255});

        const char *mode_str = is_multiplayer
            ? (remote_connected ? "MP: ONLINE" : "MP: AGUARDANDO")
            : "SINGLEPLAYER";
        Color mode_col = (is_multiplayer && !remote_connected)
            ? (Color){250, 185, 65, 255}
            : (Color){115, 235, 145, 255};
        DrawText(mode_str, (int)((float)sw * 0.53f), row1_y, hud_font, mode_col);

        const char *cap_str = (fps_cap > 0) ? TextFormat("%d", fps_cap) : "MAX";
        DrawText(TextFormat("FPS: %d (Alvo: %s)", GetFPS(), cap_str),
                 (int)((float)sw * 0.74f), row1_y, hud_font, (Color){235, 235, 245, 255});

        const char *mouse_hint = mouse_locked ? "[TAB] Soltar Mouse" : "[TAB] Travar Mouse";
        DrawText(
            TextFormat("[Mouse] Mirar/Atirar  [WASD+Shift] Mover  [R] Recarregar  %s  [F3] FPS  [F11] Tela Cheia  [ESC] Menu",
                       mouse_hint),
            x_hp, hud_y + (int)((float)hud_h * 0.72f), hint_font, (Color){165, 175, 195, 255}
        );
    }

    EndDrawing();
}

void renderer_present_stdout(const Player *player, int is_multiplayer,
                             int remote_connected) {
    /* Amostra 80x26 da grade 160x80 para validacao em terminal/CI */
    for (int y = 0; y < SCREEN_HEIGHT; y += 3) {
        char line[81];
        int col = 0;
        for (int x = 0; x < SCREEN_WIDTH && col < 80; x += 2) {
            line[col++] = screen_grid[y][x].ch;
        }
        line[col] = '\0';
        puts(line);
    }

    if (player) {
        printf("================================================================================\n");
        printf(" VIDA: %3d%% | MUNICAO: %2d | PONTOS: %3d | POS: (%.1f, %.1f) | MODO: %s\n",
               player->health,
               player->ammo,
               player->score,
               player->x,
               player->y,
               is_multiplayer ? (remote_connected ? "MP-ONLINE" : "MP-WAIT") : "SINGLEPLAYER");
        printf("================================================================================\n");
    }

    fflush(stdout);
}
