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

static void draw_centered_text(const char *text, int center_x, int y, int font_size, Color color) {
    int width = MeasureText(text, font_size);
    DrawText(text, center_x - width / 2, y, font_size, color);
}

static int run_menu_screen(char *ip_buf, size_t ip_size, int *port_out, const char *status_msg) {
    static const char *LOGO[] = {
        "    ___   _____ ______________    ____  ____  ____  __  ___",
        "   /   | / ___// ____/  _/  _/   / __ \\/ __ \\/ __ \\/  |/  /",
        "  / /| | \\__ \\/ /    / / / /    / / / / / / / / / / /|_/ / ",
        " / ___ |___/ / /____/ /_/ /    / /_/ / /_/ / /_/ / /  / /  ",
        "/_/  |_/____/\\____/___/___/   /_____/\\____/\\____/_/  /_/   "
    };

    const char *options[4] = {
        "1. Jogar Singleplayer (Offline)",
        "2. Hospedar Multiplayer (Host UDP)",
        "3. Conectar a um Host Multiplayer (Client UDP)",
        "4. Sair"
    };

    int selected = 0;
    int editing_ip = 0;
    *port_out = DEFAULT_PORT;

    if (IsCursorHidden()) {
        EnableCursor();
    }

    Font font = GetFontDefault();

    while (!WindowShouldClose()) {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        float sc = fminf((float)sw / 1280.0f, (float)sh / 720.0f);
        if (sc < 0.55f) sc = 0.55f;

        int cx = sw / 2;
        int box_w = (int)(780.0f * sc);
        int box_h = (int)(260.0f * sc);
        int box_x = cx - box_w / 2;
        int box_y = (int)((float)sh * 0.36f);
        int item_x = box_x + (int)(25.0f * sc);
        int item_w = box_w - (int)(50.0f * sc);
        int item_h = (int)(44.0f * sc);
        int item_step = (int)(56.0f * sc);
        int first_item_y = box_y + (int)(26.0f * sc);

        if (IsKeyPressed(KEY_F11)) {
            ToggleBorderlessWindowed();
        }

        if (!editing_ip) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
                selected = (selected + 1) % 4;
            }
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                selected = (selected + 3) % 4;
            }
            if (IsKeyPressed(KEY_ONE))   return 1;
            if (IsKeyPressed(KEY_TWO))   return 2;
            if (IsKeyPressed(KEY_THREE)) { selected = 2; editing_ip = 1; }
            if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_ESCAPE)) return 4;

            /* Suporte a mouse no menu (hover seleciona, clique esquerdo confirma) */
            Vector2 mouse = GetMousePosition();
            for (int i = 0; i < 4; i++) {
                Rectangle btn = {
                    (float)item_x,
                    (float)(first_item_y + i * item_step - (int)(6.0f * sc)),
                    (float)item_w,
                    (float)item_h
                };
                if (CheckCollisionPointRec(mouse, btn)) {
                    selected = i;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        if (i == 2) {
                            editing_ip = 1;
                        } else {
                            return i + 1;
                        }
                    }
                }
            }

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                if (selected == 2) {
                    editing_ip = 1;
                } else {
                    return selected + 1;
                }
            }
        } else {
            int key = GetCharPressed();
            while (key > 0) {
                size_t len = strlen(ip_buf);
                if (((key >= '0' && key <= '9') || key == '.') && len + 1 < ip_size) {
                    ip_buf[len] = (char)key;
                    ip_buf[len + 1] = '\0';
                }
                key = GetCharPressed();
            }

            if (IsKeyPressed(KEY_BACKSPACE)) {
                size_t len = strlen(ip_buf);
                if (len > 0) {
                    ip_buf[len - 1] = '\0';
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                editing_ip = 0;
            }
            if (IsKeyPressed(KEY_ENTER)) {
                if (ip_buf[0] == '\0') {
                    snprintf(ip_buf, ip_size, "127.0.0.1");
                }
                return 3;
            }
        }

        BeginDrawing();
        ClearBackground((Color){12, 13, 20, 255});

        /* Desenha logo ASCII em grade monoespacada fixa para nao entortar */
        int logo_cols = (int)strlen(LOGO[0]);
        float logo_cell_w = 13.0f * sc;
        float logo_cell_h = 24.0f * sc;
        float logo_font_sz = 22.0f * sc;
        float logo_start_x = (float)cx - ((float)logo_cols * logo_cell_w) * 0.5f;
        float logo_start_y = (float)sh * 0.08f;

        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < logo_cols; c++) {
                char ch = LOGO[r][c];
                if (ch != ' ') {
                    DrawTextCodepoint(
                        font,
                        (int)ch,
                        (Vector2){logo_start_x + (float)c * logo_cell_w,
                                  logo_start_y + (float)r * logo_cell_h},
                        logo_font_sz,
                        (Color){230, 65, 55, 255}
                    );
                }
            }
        }

        int sub_font = (int)(20.0f * sc);
        if (sub_font < 14) sub_font = 14;
        draw_centered_text("PROJETO FINAL (A2) - LINGUAGEM DE PROGRAMACAO | RAYLIB 165 FPS",
                           cx, (int)(logo_start_y + 5.0f * logo_cell_h + 18.0f * sc),
                           sub_font, (Color){165, 180, 210, 255});

        DrawRectangleLines(box_x, box_y, box_w, box_h, (Color){70, 80, 110, 255});

        int opt_font = (int)(22.0f * sc);
        if (opt_font < 15) opt_font = 15;

        for (int i = 0; i < 4; i++) {
            int y = first_item_y + i * item_step;
            int is_sel = (i == selected);
            if (is_sel) {
                DrawRectangle(item_x, y - (int)(6.0f * sc), item_w, item_h, (Color){45, 28, 34, 255});
                DrawRectangleLines(item_x, y - (int)(6.0f * sc), item_w, item_h, (Color){230, 75, 65, 255});
            }
            Color text_col = is_sel ? (Color){255, 230, 100, 255} : (Color){210, 215, 225, 255};
            DrawText(options[i], item_x + (int)(24.0f * sc), y + (int)(4.0f * sc), opt_font, text_col);
        }

        int footer_y = box_y + box_h + (int)(22.0f * sc);
        if (editing_ip) {
            int ip_w = (int)(660.0f * sc);
            int ip_h = (int)(78.0f * sc);
            int ip_x = cx - ip_w / 2;
            DrawRectangle(ip_x, footer_y, ip_w, ip_h, (Color){22, 26, 38, 255});
            DrawRectangleLines(ip_x, footer_y, ip_w, ip_h, (Color){100, 195, 255, 255});
            DrawText("Digite o IP do Host e aperte ENTER (ESC para voltar):",
                     ip_x + (int)(20.0f * sc), footer_y + (int)(10.0f * sc),
                     (int)(18.0f * sc), (Color){165, 215, 255, 255});
            DrawText(TextFormat("IP: %s_", ip_buf),
                     ip_x + (int)(20.0f * sc), footer_y + (int)(40.0f * sc),
                     (int)(24.0f * sc), (Color){255, 240, 110, 255});
        } else {
            int hint_font = (int)(18.0f * sc);
            if (hint_font < 13) hint_font = 13;
            draw_centered_text("Use [Mouse], [Setas] ou [1-4] + [ENTER]   |   [F11] Tela Cheia   |   [Mouse + WASD] no jogo",
                               cx, footer_y + (int)(12.0f * sc),
                               hint_font, (Color){145, 155, 175, 255});
        }

        if (status_msg && status_msg[0] != '\0') {
            draw_centered_text(status_msg, cx, footer_y + (int)(92.0f * sc),
                               (int)(18.0f * sc), (Color){245, 95, 85, 255});
        }

        const char *fps_txt = TextFormat("FPS: %d  |  %dx%d", GetFPS(), sw, sh);
        int fps_w = MeasureText(fps_txt, 18);
        DrawText(fps_txt, sw - fps_w - 20, 16, 18, (Color){115, 225, 135, 255});
        EndDrawing();
    }

    return 4;
}

static void run_game_session(NetMode mode, const char *host_ip, int port,
                             int *fps_cap, char *err_buf, size_t err_size) {
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

    DisableCursor();
    int mouse_locked = 1;
    int running = 1;
    int is_multiplayer = (mode != NET_MODE_OFFLINE);

    while (!WindowShouldClose() && running) {
        /* Delta Time limitado a 0.1s para impedir saltos se a janela for arrastada */
        float dt = GetFrameTime();
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        if (IsKeyPressed(KEY_TAB)) {
            mouse_locked = !mouse_locked;
            if (mouse_locked) {
                DisableCursor();
            } else {
                EnableCursor();
            }
        }

        if (IsKeyPressed(KEY_F3)) {
            *fps_cap = (*fps_cap == TARGET_FPS) ? 0 : TARGET_FPS;
            SetTargetFPS(*fps_cap);
        }

        if (IsKeyPressed(KEY_F11)) {
            ToggleBorderlessWindowed();
        }

        int did_shoot = 0;
        player_handle_input(&player, &map, dt, &running, &did_shoot);

        if (is_multiplayer) {
            net_receive_state(&net);
            net_send_state(&net, &player, did_shoot);

            if (did_shoot && net.connected) {
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
            /* Alvo de treino estatico no modo Singleplayer */
            remote_sprite.x = 7.5f;
            remote_sprite.y = 4.5f;
            remote_sprite.active = 1;
            remote_sprite.shooting = 0;
            remote_sprite.label = 'X';
            sprite_count = 1;

            if (did_shoot) {
                float dx = remote_sprite.x - player.x;
                float dy = remote_sprite.y - player.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float target_angle = atan2f(dy, dx);
                if (dist < 10.0f && angle_diff_abs(target_angle, player.angle) < 0.10f) {
                    player.score += 10;
                }
            }
        }

        renderer_clear();
        renderer_draw_walls(&map, &player);
        renderer_draw_sprites(&player, &remote_sprite, sprite_count);
        renderer_draw_minimap(&map, &player, &remote_sprite, sprite_count);
        renderer_draw_weapon(&player);
        renderer_draw_hud(&player);
        renderer_present_window(&player, is_multiplayer, net.connected,
                                mouse_locked, *fps_cap);
    }

    net_close(&net);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--test-frame") == 0) {
        return run_single_test_frame();
    }

    renderer_init();

    char host_ip[64] = "127.0.0.1";
    char status_msg[128] = "";
    int port = DEFAULT_PORT;
    int fps_cap = TARGET_FPS;

    while (!WindowShouldClose()) {
        int choice = run_menu_screen(host_ip, sizeof(host_ip), &port, status_msg);
        status_msg[0] = '\0';

        if (choice == 1) {
            run_game_session(NET_MODE_OFFLINE, host_ip, port, &fps_cap,
                             status_msg, sizeof(status_msg));
        } else if (choice == 2) {
            run_game_session(NET_MODE_HOST, host_ip, port, &fps_cap,
                             status_msg, sizeof(status_msg));
        } else if (choice == 3) {
            run_game_session(NET_MODE_CLIENT, host_ip, port, &fps_cap,
                             status_msg, sizeof(status_msg));
        } else {
            break;
        }
    }

    renderer_close();
    return 0;
}
