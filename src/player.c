#include "player.h"
#include "raylib.h"
#include <math.h>
#include <stddef.h>

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
    p->bob_phase = 0.0f;
    p->shoot_timer = 0.0f;
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

void player_handle_input(Player *p, const GameMap *map, float dt,
                         int *running, int *did_shoot) {
    if (!p || !map || !running || !did_shoot) {
        return;
    }

    *did_shoot = 0;

    if (p->shoot_timer > 0.0f) {
        p->shoot_timer -= dt;
        if (p->shoot_timer < 0.0f) {
            p->shoot_timer = 0.0f;
        }
    }

    /* Rotacao pelo mouse (Mouse Look 360) quando o cursor esta preso */
    if (IsCursorHidden()) {
        Vector2 mouse_delta = GetMouseDelta();
        if (mouse_delta.x != 0.0f) {
            player_rotate(p, mouse_delta.x * MOUSE_SENSITIVITY);
        }
    }

    /* Rotacao opcional por teclado (Q/E ou Setas esquerda/direita) */
    float rot_dir = 0.0f;
    if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT))  rot_dir -= 1.0f;
    if (IsKeyDown(KEY_E) || IsKeyDown(KEY_RIGHT)) rot_dir += 1.0f;
    if (rot_dir != 0.0f) {
        player_rotate(p, rot_dir * ROT_SPEED * dt);
    }

    /* Movimentacao continua em WASD (ou setas cima/baixo) */
    float fwd_input = 0.0f;
    float str_input = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   fwd_input += 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) fwd_input -= 1.0f;
    if (IsKeyDown(KEY_A)) str_input -= 1.0f;
    if (IsKeyDown(KEY_D)) str_input += 1.0f;

    if (fwd_input != 0.0f || str_input != 0.0f) {
        /* Normaliza vetor diagonal para nao correr mais rapido na diagonal */
        float mag = sqrtf(fwd_input * fwd_input + str_input * str_input);
        fwd_input /= mag;
        str_input /= mag;

        float speed = MOVE_SPEED;
        int sprinting = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (sprinting) {
            speed *= SPRINT_MULT;
        }

        if (player_move(p, map, fwd_input * speed * dt, str_input * speed * dt)) {
            p->bob_phase += dt * (sprinting ? 14.0f : 10.0f);
        }
    }

    /* Recarregar municao */
    if (IsKeyPressed(KEY_R) && p->ammo < 30) {
        p->ammo = 30;
    }

    /* Disparo com clique esquerdo ou Espaco, limitado por FIRE_COOLDOWN (dt) */
    int want_shoot = (IsCursorHidden() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
                     IsKeyDown(KEY_SPACE);
    if (want_shoot && p->shoot_timer <= 0.0f && p->ammo > 0) {
        p->ammo--;
        p->shoot_timer = FIRE_COOLDOWN;
        *did_shoot = 1;
    }

    /* ESC solicita encerramento da partida */
    if (IsKeyPressed(KEY_ESCAPE)) {
        *running = 0;
    }
}
