#ifndef CONFIG_H
#define CONFIG_H

/* Dimensoes da area de renderizacao 3D no terminal */
#define SCREEN_WIDTH  96
#define SCREEN_HEIGHT 26

/* Dimensoes padrao da matriz do mapa */
#define MAP_WIDTH  16
#define MAP_HEIGHT 16

/* Parametros matematicos e de camera */
#define PI_F      3.14159265f
#define FOV       (PI_F / 3.0f) /* 60 graus de campo de visao */
#define MAX_DEPTH 16.0f

/* Parametros de fisica e movimentacao */
#define MOVE_SPEED    0.14f
#define ROT_SPEED     0.09f
#define PLAYER_RADIUS 0.22f

/* Configuracoes de rede e loop */
#define DEFAULT_PORT  5000
#define FRAME_DELAY_MS 33 /* ~30 FPS */
#define MAX_SPRITES   8

#endif /* CONFIG_H */
