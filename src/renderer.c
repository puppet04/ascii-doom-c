#include "renderer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static char screen_buf[SCREEN_HEIGHT][SCREEN_WIDTH + 1];
static float z_buffer[SCREEN_WIDTH];

static float wrap_angle_signed(float angle) {
    while (angle > PI_F) {
        angle -= 2.0f * PI_F;
    }
    while (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    return angle;
}

static char get_wall_shade(float dist, int side) {
    const char *shades = "#@%*=+-:. ";
    int num_shades = 9;
    float shade_range = 10.0f;
    float norm = dist / shade_range;

    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    int idx = (int)(norm * (num_shades - 1));
    if (side == 1 && idx < num_shades - 1) {
        idx++; /* Escurece levemente paredes no eixo Y para dar contraste 3D */
    }
    return shades[idx];
}

static char get_floor_shade(int y) {
    float row_ratio = (float)(y - SCREEN_HEIGHT / 2) / (float)(SCREEN_HEIGHT / 2);
    if (row_ratio > 0.75f) return '.';
    if (row_ratio > 0.45f) return ',';
    if (row_ratio > 0.20f) return '-';
    return ' ';
}

void renderer_init(void) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_CURSOR_INFO cursor_info;
        if (GetConsoleCursorInfo(hOut, &cursor_info)) {
            cursor_info.bVisible = FALSE;
            SetConsoleCursorInfo(hOut, &cursor_info);
        }
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            SetConsoleMode(hOut, mode | 0x0004); /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
        }
    }
#endif
}

void renderer_clear(void) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        memset(screen_buf[y], ' ', SCREEN_WIDTH);
        screen_buf[y][SCREEN_WIDTH] = '\0';
    }
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        z_buffer[x] = MAX_DEPTH;
    }
}

void renderer_draw_walls(const GameMap *map, const Player *player) {
    if (!map || !player) {
        return;
    }

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        float ray_angle = (player->angle - player->fov * 0.5f) +
                          ((float)x / (float)SCREEN_WIDTH) * player->fov;

        float ray_dir_x = cosf(ray_angle);
        float ray_dir_y = sinf(ray_angle);

        int map_x = (int)floorf(player->x);
        int map_y = (int)floorf(player->y);

        float delta_dist_x = (fabsf(ray_dir_x) < 1e-6f) ? 1e30f : fabsf(1.0f / ray_dir_x);
        float delta_dist_y = (fabsf(ray_dir_y) < 1e-6f) ? 1e30f : fabsf(1.0f / ray_dir_y);

        int step_x;
        int step_y;
        float side_dist_x;
        float side_dist_y;

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
            if (side == 0) {
                raw_dist = side_dist_x - delta_dist_x;
            } else {
                raw_dist = side_dist_y - delta_dist_y;
            }
        }

        /* Corrige distorcao olho de peixe usando o cosseno da diferenca de angulo */
        float perp_dist = raw_dist * cosf(ray_angle - player->angle);
        if (perp_dist < 0.1f) {
            perp_dist = 0.1f;
        }
        if (perp_dist > MAX_DEPTH) {
            perp_dist = MAX_DEPTH;
        }

        z_buffer[x] = perp_dist;

        int line_height = (int)((float)SCREEN_HEIGHT / perp_dist);
        int draw_start = (SCREEN_HEIGHT - line_height) / 2;
        int draw_end = (SCREEN_HEIGHT + line_height) / 2;

        if (draw_start < 0) draw_start = 0;
        if (draw_end >= SCREEN_HEIGHT) draw_end = SCREEN_HEIGHT - 1;

        char wall_char = hit ? get_wall_shade(perp_dist, side) : ' ';

        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            if (y < draw_start) {
                screen_buf[y][x] = ' ';
            } else if (y <= draw_end) {
                screen_buf[y][x] = wall_char;
            } else {
                screen_buf[y][x] = get_floor_shade(y);
            }
        }
    }
}

void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count) {
    if (!player || !sprites || count <= 0) {
        return;
    }

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
        if (sprite_h < 3) sprite_h = 3;
        if (sprite_h > SCREEN_HEIGHT) sprite_h = SCREEN_HEIGHT;

        int sprite_w = sprite_h / 2;
        if (sprite_w < 3) sprite_w = 3;

        int top_y = (SCREEN_HEIGHT - sprite_h) / 2;
        int bot_y = top_y + sprite_h - 1;
        int left_x = center_x - sprite_w / 2;
        int right_x = center_x + sprite_w / 2;

        for (int sx = left_x; sx <= right_x; sx++) {
            if (sx < 0 || sx >= SCREEN_WIDTH) {
                continue;
            }
            if (dist >= z_buffer[sx]) {
                continue; /* Sprite atras da parede */
            }

            for (int sy = top_y; sy <= bot_y; sy++) {
                if (sy < 0 || sy >= SCREEN_HEIGHT) {
                    continue;
                }

                float ny = (float)(sy - top_y) / (float)(sprite_h);
                float nx = (float)(sx - left_x) / (float)(sprite_w > 1 ? sprite_w - 1 : 1);
                char glyph = ' ';

                if (ny < 0.28f) {
                    if (nx > 0.25f && nx < 0.75f) {
                        glyph = sprites[i].label ? sprites[i].label : 'O';
                    }
                } else if (ny < 0.72f) {
                    if (sprites[i].shooting && ny > 0.35f && ny < 0.55f) {
                        glyph = '*';
                    } else if (nx > 0.15f && nx < 0.85f) {
                        glyph = '|';
                    }
                } else {
                    if (nx < 0.42f) glyph = '/';
                    else if (nx > 0.58f) glyph = '\\';
                }

                if (glyph != ' ') {
                    screen_buf[sy][sx] = glyph;
                }
            }
        }
    }
}

void renderer_draw_minimap(const GameMap *map, const Player *player,
                           const SpriteEntity *sprites, int count) {
    if (!map || !player) {
        return;
    }

    int max_rows = (map->height < SCREEN_HEIGHT - 1) ? map->height : SCREEN_HEIGHT - 1;
    int max_cols = (map->width < SCREEN_WIDTH - 1) ? map->width : SCREEN_WIDTH - 1;

    for (int y = 0; y < max_rows; y++) {
        for (int x = 0; x < max_cols; x++) {
            screen_buf[y][x] = map_is_wall(map, x, y) ? '#' : ' ';
        }
    }

    if (sprites && count > 0) {
        for (int i = 0; i < count; i++) {
            if (!sprites[i].active) continue;
            int sx = (int)sprites[i].x;
            int sy = (int)sprites[i].y;
            if (sx >= 0 && sx < max_cols && sy >= 0 && sy < max_rows) {
                screen_buf[sy][sx] = 'E';
            }
        }
    }

    int px = (int)player->x;
    int py = (int)player->y;
    if (px >= 0 && px < max_cols && py >= 0 && py < max_rows) {
        screen_buf[py][px] = 'P';
    }
}

void renderer_draw_hud(const Player *player, int is_multiplayer,
                       int shoot_flash, int remote_connected) {
    if (!player) {
        return;
    }

    /* Mira central */
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;
    screen_buf[cy][cx] = shoot_flash ? '*' : '+';

    if (shoot_flash && cx > 1 && cx < SCREEN_WIDTH - 2) {
        screen_buf[cy][cx - 1] = '-';
        screen_buf[cy][cx + 1] = '-';
    }

    /* Badge de status no canto superior direito */
    char badge[32];
    if (is_multiplayer) {
        snprintf(badge, sizeof(badge), "[MP:%s]", remote_connected ? "ONLINE" : "AGUARDANDO");
    } else {
        snprintf(badge, sizeof(badge), "[SINGLEPLAYER]");
    }

    int badge_len = (int)strlen(badge);
    int start_x = SCREEN_WIDTH - badge_len - 1;
    if (start_x > 0) {
        memcpy(&screen_buf[0][start_x], badge, (size_t)badge_len);
    }
}

void renderer_present(int show_hud_footer, const Player *player,
                      int is_multiplayer, int remote_connected) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD console_mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &console_mode)) {
        COORD home = {0, 0};
        SetConsoleCursorPosition(hOut, home);
    }
#endif

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        puts(screen_buf[y]);
    }

    if (show_hud_footer && player) {
        printf("================================================================================================\n");
        printf(" VIDA: %3d%%  |  MUNICAO: %2d  |  PONTOS: %3d  |  POS: (%.1f, %.1f)  |  MODO: %s\n",
               player->health,
               player->ammo,
               player->score,
               player->x,
               player->y,
               is_multiplayer ? (remote_connected ? "MULTIPLAYER (CONECTADO)" : "MULTIPLAYER (AGUARDANDO)")
                              : "SINGLEPLAYER");
        printf(" CONTROLES: [W/A/S/D] Mover  [Q/E ou Setas] Girar  [Espaco] Atirar  [ESC] Sair\n");
        printf("================================================================================================\n");
    }

    fflush(stdout);
}
