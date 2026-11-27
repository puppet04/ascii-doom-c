#ifndef RENDER_API_H
#define RENDER_API_H

/*
 * INTERFACE PURA DE BIBLIOTECA DO MOTOR GRAFICO (ZERO DEPENDENCIA DE RAYLIB)
 * ============================================================================
 * Objetivo arquitetural:
 *   Permite que qualquer modulo de logica (entities.c, map.c, player.c, main.c)
 *   desenhe sprites 3D, projeteis, decals de chao, estilos de parede e marcadores
 *   de minimapa usando a mesma grade dinamica ASCII, z-buffer e shader GPU,
 *   SEM precisar incluir "raylib.h" e SEM editar src/renderer.c.
 *   Isso tambem permite que bin/test_runner.exe teste toda a logica de jogo sem
 *   depender da janela grafica durante o disparo de sprites ou mapas.
 */

/* Cor RGB de 8 bits por canal (0..255), independente da struct Color da Raylib. */
typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} AsciiColor;

/* Representa uma celula logica compacta de 4 bytes da grade ASCII (tela 3D ou UI):
 * - ch: caractere ASCII imprimivel [33..126], ou ' ' / '\0' para celula transparente.
 * - fg: cor RGB do caractere (4 bytes totais = 1 palavra de 32 bits na CPU). */
typedef struct {
    char ch;
    AsciiColor fg;
} AsciiCell;

/* Especificacao geometrica de um sprite 3D no mundo (mundo em blocos [0..MAP_WIDTH]).
 * height_scale e width_ratio determinam as proporcoes apos a projecao em perspectiva. */
typedef struct {
    float world_x;       /* Posicao X no mundo em blocos (ex.: centro da celula = col + 0.5f) */
    float world_y;       /* Posicao Y no mundo em blocos */
    float height_scale;  /* Escala vertical relativa a altura padrao de parede (1.0 = padrao) */
    float width_ratio;   /* Largura / Altura na tela (ex.: 0.625f) */
    int min_w;           /* Largura minima em celulas ASCII para manter legivel de longe */
    int min_h;           /* Altura minima em celulas ASCII */
    float min_fog;       /* Luminosidade minima [0..1] para silhuetas visiveis na neblina */
} RenderSpriteSpec;

/* Callback por celula para desenhar qualquer sprite 3D no mundo com oclusao por z-buffer.
 * Recebe coordenadas normalizadas (u, v) em [0..1], coordenada na grade (sx, sy),
 * tamanho projetado (sprite_w, sprite_h), distancia real, fator fog [0..1] e tempo atual.
 * user_data repassa um ponteiro opaco da entidade (ex.: Enemy*, WeaponDrop*) sem acoplar tipos.
 * Retorna 1 e preenche out_ch / out_col se visivel, ou 0 se transparente. */
typedef int (*RendererSpriteSamplerFn)(
    float u, float v,
    int sx, int sy,
    int sprite_w, int sprite_h,
    float dist, float fog,
    float time_now,
    const void *user_data,
    char *out_ch, AsciiColor *out_col
);

/* Hook para decals de chao por celula (ex.: armadilhas '^', chamas 'F', kits medicos '+').
 * Recebe (mx, my) no mapa, UV fracionario (fu, fv) em [0..1] e distancia row_dist.
 * Deve retornar 1 preenchendo out_ch/out_col caso pinte o decal, ou 0 para piso padrao. */
typedef int (*RendererFloorHookFn)(
    const void *map_ptr,
    int mx, int my,
    float fu, float fv,
    float row_dist,
    char *out_ch, AsciiColor *out_col
);

/* Hook para estilo/cor de parede por tile (ex.: saida 'E' liberada ou paredes atravessaveis).
 * Permite alterar cor base, fog_dist (negativo = auto-iluminado), quina e sombreamento lateral. */
typedef void (*RendererWallStyleHookFn)(
    const void *map_ptr,
    int mx, int my,
    char tile,
    AsciiColor *io_color,
    float *io_fog_dist,
    int *io_is_edge,
    int *io_side
);

/* Hook para marcadores customizados no minimapa (ex.: fogo ativo '^', kit '+').
 * Deve retornar 1 e preencher out_ch/out_col para sobrescrever o piso padrao do minimapa. */
typedef int (*RendererMinimapHookFn)(
    const void *mm_map_ptr,
    int mx, int my,
    char *out_ch, AsciiColor *out_col
);

/* Vtable (tabela de ponteiros de funcao) exposta por renderer_get_api().
 * Permite que entities.c e map.c invoquem o motor grafico mantendo baixo acoplamento. */
typedef struct {
    /* Projeta e rasteriza um sprite 3D no mundo com z-buffer e mipmaps/densidade dinamica. */
    void (*draw_world_sprite)(const void *player_ptr,
                              const RenderSpriteSpec *spec,
                              RendererSpriteSamplerFn sampler,
                              const void *user_data);

    /* Rasteriza um projetil esferico (orbe/bola de fogo) em 3D com oclusao por paredes. */
    void (*draw_world_orb)(const void *player_ptr,
                           float world_x, float world_y, float height,
                           float radius_scale, int min_r, int max_r,
                           char core_ch, AsciiColor core_col,
                           char edge_ch, AsciiColor edge_col,
                           float edge_threshold);

    /* Desenha um anel circular no chao 3D em (cx, cy) com oclusao por paredes e sprites. */
    void (*draw_world_floor_ring)(float cx, float cy, float radius,
                                  char inner_ch, AsciiColor inner_col,
                                  char outer_ch, AsciiColor outer_col);

    /* Projeta um ponto 3D (x, y, height) para celula de tela (out_sx, out_sy) e profundidade.
     * Retorna 1 se estiver dentro do FOV e a frente da parede do z-buffer; 0 caso contrario. */
    int (*project_point)(const void *player_ptr,
                         float world_x, float world_y, float height,
                         int *out_sx, int *out_sy, float *out_depth);

    /* Escreve uma celula diretamente na grade do frame sem checar profundidade. */
    void (*set_cell)(int x, int y, char ch, AsciiColor fg, AsciiColor bg);

    /* Escreve uma celula na grade somente se depth for menor que z_buffer[x] e cell_depth[y][x]. */
    void (*set_cell_if_closer)(int x, int y, float depth,
                               char ch, AsciiColor fg, AsciiColor bg);

    /* Consulta a resolucao dinamica atual (em celulas ASCII) configurada por F2 / janela. */
    void (*get_grid_size)(int *out_w, int *out_h);

    /* Seleciona glifo da rampa ASCII conforme luminosidade [0..1] com dithering Bayer 4x4. */
    char (*sample_ramp)(float lum, int x, int y);

    /* Escala uma cor RGB em float [0..255] pelo fator dado, com saturacao segura. */
    AsciiColor (*scale_rgb)(float r, float g, float b, float factor);

    /* Retorna o relogio de animacao da engine (em segundos). */
    float (*get_time)(void);

    /* Retorna o angulo de visao real da camera (considerando modo 1a ou 3a pessoa). */
    float (*get_view_angle)(const void *player_ptr);
} RendererApi;

#endif /* RENDER_API_H */
