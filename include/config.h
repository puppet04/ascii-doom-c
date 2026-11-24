#ifndef CONFIG_H
#define CONFIG_H

/* Dimensoes da janela grafica (Raylib) */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720
#define HUD_HEIGHT    80

/* Grade ASCII de alta resolucao (160x80 celulas de 8x8 pixels = 1280x640) */
#define SCREEN_WIDTH  160
#define SCREEN_HEIGHT 80
#define CELL_WIDTH    8
#define CELL_HEIGHT   8

/* Dimensoes padrao da matriz do mapa */
#define MAP_WIDTH  16
#define MAP_HEIGHT 16

/* Parametros matematicos e de camera */
#define PI_F      3.14159265f
#define FOV       (PI_F / 3.0f) /* 60 graus de campo de visao */
#define MAX_DEPTH 16.0f

/* Parametros fisicos baseados em segundos (independentes de FPS via Delta Time) */
#define MOVE_SPEED        4.2f    /* blocos por segundo */
#define PLAYER_MAX_HEALTH  100    /* vida base de todos os personagens */
#define SPRINT_MULT       1.55f   /* multiplicador ao segurar Shift */
#define ROT_SPEED         2.6f    /* radianos por segundo no teclado */
#define MOUSE_SENSITIVITY 0.0022f /* radianos por pixel do mouse */
#define FIRE_COOLDOWN     0.25f   /* intervalo minimo entre disparos (s) */
#define PLAYER_RADIUS     0.22f

/* Configuracoes de rede e taxa de quadros */
#define DEFAULT_PORT 5000
#define TARGET_FPS   165
#define MAX_SPRITES  64

#endif /* CONFIG_H */
