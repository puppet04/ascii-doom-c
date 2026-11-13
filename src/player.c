#include "player.h"
#include <math.h>
#include <stddef.h>

#ifdef _WIN32
#include <conio.h>
#endif

static float normalize_angle(float angle) {
    float two_pi = 2.0f * PI_F;
    while (angle < 0.0f) {
        angle += two_pi;
    }
    while (angle >= two_pi) {
        angle -= two_pi;
    }
    return angle;
}

static int is_position_blocked(const GameMap *map, float x, float y) {
    float r = PLAYER_RADIUS;

    if (map_is_wall(map, (int)floorf(x - r), (int)floorf(y - r))) return 1;
    if (map_is_wall(map, (int)floorf(x + r), (int)floorf(y - r))) return 1;
    if (map_is_wall(map, (int)floorf(x - r), (int)floorf(y + r))) return 1;
    if (map_is_wall(map, (int)floorf(x + r), (int)floorf(y + r))) return 1;

    return 0;
}

void player_init(Player *p, float x, float y, float angle) {
    if (!p) {
        return;
    }

    p->id = 1;
    p->x = x;
    p->y = y;
    p->angle = normalize_angle(angle);
    p->fov = FOV;
    p->health = 100;
    p->ammo = 30;
    p->score = 0;
}

int player_move(Player *p, const GameMap *map, float forward, float strafe) {
    if (!p || !map) {
        return 0;
    }

    float dx = cosf(p->angle) * forward - sinf(p->angle) * strafe;
    float dy = sinf(p->angle) * forward + cosf(p->angle) * strafe;
    int moved = 0;

    if (!is_position_blocked(map, p->x + dx, p->y)) {
        p->x += dx;
        moved = 1;
    }
    if (!is_position_blocked(map, p->x, p->y + dy)) {
        p->y += dy;
        moved = 1;
    }

    return moved;
}

void player_rotate(Player *p, float delta_angle) {
    if (!p) {
        return;
    }
    p->angle = normalize_angle(p->angle + delta_angle);
}

void player_handle_input(Player *p, const GameMap *map, int *running, int *did_shoot) {
    if (!p || !map || !running || !did_shoot) {
        return;
    }

    *did_shoot = 0;

#ifdef _WIN32
    while (_kbhit()) {
        int key = _getch();

        /* Teclas estendidas (setas do teclado) chegam com prefixo 0 ou 224 */
        if (key == 0 || key == 224) {
            int ext = _getch();
            switch (ext) {
                case 72: /* Seta cima */
                    player_move(p, map, MOVE_SPEED, 0.0f);
                    break;
                case 80: /* Seta baixo */
                    player_move(p, map, -MOVE_SPEED, 0.0f);
                    break;
                case 75: /* Seta esquerda */
                    player_rotate(p, -ROT_SPEED);
                    break;
                case 77: /* Seta direita */
                    player_rotate(p, ROT_SPEED);
                    break;
                default:
                    break;
            }
            continue;
        }

        switch (key) {
            case 'w':
            case 'W':
                player_move(p, map, MOVE_SPEED, 0.0f);
                break;
            case 's':
            case 'S':
                player_move(p, map, -MOVE_SPEED, 0.0f);
                break;
            case 'a':
            case 'A':
                player_move(p, map, 0.0f, -MOVE_SPEED);
                break;
            case 'd':
            case 'D':
                player_move(p, map, 0.0f, MOVE_SPEED);
                break;
            case 'q':
            case 'Q':
            case 'j':
            case 'J':
                player_rotate(p, -ROT_SPEED);
                break;
            case 'e':
            case 'E':
            case 'l':
            case 'L':
                player_rotate(p, ROT_SPEED);
                break;
            case ' ':
                if (p->ammo > 0) {
                    p->ammo--;
                    *did_shoot = 1;
                }
                break;
            case 27: /* ESC */
                *running = 0;
                break;
            default:
                break;
        }
    }
#else
    (void)p;
    (void)map;
    (void)running;
    (void)did_shoot;
#endif
}
