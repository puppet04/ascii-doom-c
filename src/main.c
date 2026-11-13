#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "config.h"
#include "map.h"
#include "network.h"
#include "player.h"
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
    renderer_draw_hud(&player, 0, 0, 0);
    renderer_present(1, &player, 0, 0);

    return 0;
}

static void clear_terminal(void) {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[2J\033[H");
#endif
}

static void show_menu(int *choice, char *ip_out, size_t ip_size, int *port_out) {
    char line[64];

    clear_terminal();
    printf("============================================================\n");
    printf("                  ASCII DOOM - PROJETO EM C                 \n");
    printf("============================================================\n");
    printf("  1. Jogar Singleplayer (Offline)\n");
    printf("  2. Hospedar Multiplayer (Host UDP)\n");
    printf("  3. Conectar a um Host Multiplayer (Client UDP)\n");
    printf("  4. Sair\n");
    printf("------------------------------------------------------------\n");
    printf("Escolha uma opcao: ");

    if (!fgets(line, sizeof(line), stdin)) {
        *choice = 4;
        return;
    }

    *choice = atoi(line);
    *port_out = DEFAULT_PORT;
    snprintf(ip_out, ip_size, "127.0.0.1");

    if (*choice == 2) {
        printf("Porta UDP (Enter para %d): ", DEFAULT_PORT);
        if (fgets(line, sizeof(line), stdin) && atoi(line) > 0) {
            *port_out = atoi(line);
        }
    } else if (*choice == 3) {
        printf("IP do Host (Enter para 127.0.0.1): ");
        if (fgets(line, sizeof(line), stdin)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[len - 1] = '\0';
                len--;
            }
            if (len > 0) {
                snprintf(ip_out, ip_size, "%s", line);
            }
        }
        printf("Porta UDP (Enter para %d): ", DEFAULT_PORT);
        if (fgets(line, sizeof(line), stdin) && atoi(line) > 0) {
            *port_out = atoi(line);
        }
    }
}

static float angle_diff_abs(float a, float b) {
    float diff = a - b;
    while (diff > PI_F) diff -= 2.0f * PI_F;
    while (diff < -PI_F) diff += 2.0f * PI_F;
    return fabsf(diff);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--test-frame") == 0) {
        return run_single_test_frame();
    }

    int choice = 0;
    char host_ip[64];
    int port = DEFAULT_PORT;

    show_menu(&choice, host_ip, sizeof(host_ip), &port);
    if (choice < 1 || choice > 3) {
        printf("Saindo...\n");
        return 0;
    }

    GameMap map;
    load_initial_map(&map);

    Player player;
    NetContext net;
    NetMode mode = NET_MODE_OFFLINE;

    if (choice == 2) {
        mode = NET_MODE_HOST;
        player_init(&player, 2.5f, 4.5f, 0.0f);
        player.id = 1;
    } else if (choice == 3) {
        mode = NET_MODE_CLIENT;
        player_init(&player, 13.5f, 11.5f, PI_F);
        player.id = 2;
    } else {
        player_init(&player, 2.5f, 4.5f, 0.0f);
    }

    if (!net_init(&net, mode, host_ip, port)) {
        printf("[ERRO] Nao foi possivel iniciar o modo de rede na porta %d.\n", port);
        return 1;
    }

    clear_terminal();
    renderer_init();

    int running = 1;
    int shoot_flash = 0;
    int is_multiplayer = (mode != NET_MODE_OFFLINE);

    while (running) {
        int did_shoot = 0;
        player_handle_input(&player, &map, &running, &did_shoot);

        if (did_shoot) {
            shoot_flash = 3;
        } else if (shoot_flash > 0) {
            shoot_flash--;
        }

        if (is_multiplayer) {
            net_receive_state(&net);
            net_send_state(&net, &player, did_shoot);

            if (did_shoot && net.connected) {
                float dx = net.remote_state.x - player.x;
                float dy = net.remote_state.y - player.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float target_angle = atan2f(dy, dx);
                if (dist < 8.0f && angle_diff_abs(target_angle, player.angle) < 0.12f) {
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
            remote_sprite.y = 7.5f;
            remote_sprite.active = 1;
            remote_sprite.shooting = 0;
            remote_sprite.label = 'X';
            sprite_count = 1;
        }

        renderer_clear();
        renderer_draw_walls(&map, &player);
        renderer_draw_sprites(&player, &remote_sprite, sprite_count);
        renderer_draw_minimap(&map, &player, &remote_sprite, sprite_count);
        renderer_draw_hud(&player, is_multiplayer, shoot_flash > 0, net.connected);
        renderer_present(1, &player, is_multiplayer, net.connected);

#ifdef _WIN32
        Sleep(FRAME_DELAY_MS);
#endif
    }

    net_close(&net);
    clear_terminal();
    printf("Obrigado por jogar ASCII DOOM! Pontuacao final: %d\n", player.score);
    return 0;
}
