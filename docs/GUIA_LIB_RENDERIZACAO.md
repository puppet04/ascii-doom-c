# Guia da Biblioteca de Renderizacao ASCII (`render_api.h` + `renderer.h`)

Este guia documenta a interface grafica do **SECTOR 404** (`src/renderer.c`). A engine foi isolada como uma biblioteca interna para que `map.c`, `entities.c`, `player.c` e `network.c` consigam desenhar no mundo 3D e no minimapa sem incluir `<raylib.h>` nem alterar o codigo do raycaster.

---

## 1. Arquitetura em Duas Camadas

1. **`include/render_api.h` (Interface Pura em C, sem Raylib):**
   - Define `AsciiColor`, `AsciiCell`, `RenderSpriteSpec`, os tipos de callback de hooks e a vtable `RendererApi`.
   - Pode ser incluida diretamente em modulos de logica e na suite `tests/test_main.c` sem puxar dependencias graficas.
2. **`include/renderer.h` + `src/renderer.c` (Backend Raylib + GLSL `#version 330`):**
   - Implementa o raycasting DDA em 2 passes, o z-buffer, o atlas mipmapped `16x6`, os presets de qualidade (`F2`) e estilo (`F4`), o suporte a skins de jogadores remotos e a `RendererApi`.

---

## 2. Pipeline do Frame

1. **Grade logica (`screen_grid`):** cada frame preenche uma grade de ate `544x272` celulas (`r_width x r_height`), ajustada conforme o preset (`BAIXO`, `MEDIO`, `ALTO`, `ULTRA`, `AUTO`) e o estilo visual (`F4`).
2. **Raycasting + Oclusao (`z_buffer` e `cell_depth`):**
   - **Passe 1 (`renderer_draw_walls`):** lanca `r_width` raios via DDA, corrige olho-de-peixe (`perp_dist`), grava `z_buffer[x]` e rasteriza as colunas de parede.
   - **Passe 2 (`renderer_draw_walls`):** preenche teto e chao em ordem de linha (amigavel ao cache L1), consultando o `floor_hook` para armadilhas, fogo e kits medicos.
   - **Sprites, projeteis, arma e minimapa:** escrevem na grade respeitando `z_buffer[x]` e `cell_depth[y][x]`.
3. **Upload + GPU (`renderer_draw_ascii_buffer`):**
   - Compacta a grade numa textura RGBA (`576x352` no maximo; `RGB = cor do caractere`, `A = indice do glifo 1..95`) e renderiza em **1 draw call**.
   - `ASCII_FS_CLASSIC_CODE` (**Sobreposto**): glifos sobrepostos (`1.72x`) com visual classico denso.
   - `ASCII_FS_MICRO_CODE` (**Micro-ASCII**): micro-caracteres lado a lado (`1.0x`), sem quadrados de fundo e com LUT de contraste.

### Ordem por Quadro (`run_game_session` em `src/main.c`)

```c
const RendererApi *r_api = renderer_get_api();
renderer_set_visual_style(cfg->ascii_style);
renderer_set_ascii_density(cfg->ascii_quality, sw, sh);
renderer_set_camera_view(cam_pitch, third_person);
renderer_clear();

renderer_draw_walls(&map, &player);
entities_render(&enemies, &player, r_api);      /* Modulo 5 (Guilherme) */
renderer_draw_sprites(&player, mp_sprites, mp_count);
map_render_hazards(&map, &player, r_api);       /* Modulo 1 (Lucas) */
player_render_abilities(&player, r_api);        /* Modulo 2 (Strazzer) */
draw_sliding_minimap(&map, &player, mm_markers, mm_count);
if (third_person || !player_render_weapon(&player, r_api)) {
    renderer_draw_weapon(&player);
}
renderer_draw_hud(&player);

renderer_draw_scene_and_hud(&player, is_multiplayer, net.connected, mouse_locked,
                            cfg->show_fps ? cfg->fps_cap : -1);
player_draw_ability_hud(&player, sw, sh);
```

---

## 3. Referencia Rapida da `RendererApi` (`include/render_api.h`)

### Estruturas Principais

- **`AsciiColor`**: `{ unsigned char r, g, b; }`
- **`AsciiCell`**: `{ char ch; AsciiColor fg; }` — ocupa 4 bytes (`' '` ou `'\0'` fica transparente na GPU; `.bg` foi removido para reduzir banda de memoria, mantendo o parametro `bg` em `set_cell`/`set_cell_if_closer` apenas por compatibilidade de assinatura).
- **`RenderSpriteSpec`**:
  - `world_x`, `world_y`: posicao no mapa (ex.: `mx + 0.5f`).
  - `height_scale`: altura relativa a parede (`1.0f` = altura padrao).
  - `width_ratio`: razao `largura / altura` do sprite projetado.
  - `min_w`, `min_h`: tamanho minimo em celulas quando distante.
  - `min_fog`: luminosidade minima `[0.0f..1.0f]` na neblina distante.

### Funcoes em `const RendererApi *r`

| Metodo | O que faz |
| :--- | :--- |
| `r->draw_world_sprite(player, &spec, sampler, ctx)` | Projeta um billboard 3D com fog + z-buffer e chama `sampler(...)` apenas nos pixels visiveis. |
| `r->draw_world_orb(player, x, y, h, rad_scale, min_r, max_r, core_ch, core_col, edge_ch, edge_col, thresh)` | Desenha um projetil esferico 3D (bola de fogo, orbe de energia) respeitando o z-buffer. |
| `r->draw_world_floor_ring(cx, cy, radius, inner_ch, inner_col, outer_ch, outer_col)` | Desenha um anel circular no chao 3D (ex.: area da Teto) com oclusao por paredes. |
| `r->project_point(player, x, y, h, &sx, &sy, &depth)` | Converte `(x, y, altura)` do mundo para celula `(sx, sy)` e retorna `1` se estiver a frente de `z_buffer[sx]`. |
| `r->set_cell(x, y, ch, fg, bg)` | Escreve uma celula diretamente na grade ASCII do frame (`bg` e mantido por compatibilidade). |
| `r->set_cell_if_closer(x, y, depth, ch, fg, bg)` | Escreve apenas se `depth < z_buffer[x]` e `depth < cell_depth[y][x]`. |
| `r->get_grid_size(&w, &h)` | Retorna as dimensoes atuais (`r_width`, `r_height`) da grade dinamica. |
| `r->sample_ramp(lum, x, y)` | Escolhe o caractere da rampa densa (`=-+*xX#%8&BWM@`) com dithering Bayer 4x4 conforme `lum` em `[0..1]`. |
| `r->scale_rgb(r, g, b, factor)` | Multiplica uma cor RGB por `factor` com clamp seguro em `[0..255]`. |
| `r->get_time()` | Tempo atual de execucao em segundos para animacoes. |
| `r->get_view_angle(player)` | Angulo real da camera (respeita 1a e 3a pessoa). |

---

## 4. Sistema de Hooks (`renderer_set_*_hook`)

Os hooks sao registrados em `register_renderer_hooks()` (`src/main.c`):

- `renderer_set_wall_hook(RendererWallStyleHookFn hook)`: chamado por coluna ao atingir uma parede (`map_wall_style_hook()` destaca saidas `'E'` liberadas e paredes atravessaveis `$`, `&`, `/`).
- `renderer_set_floor_hook(RendererFloorHookFn hook)`: chamado por celula visivel do chao (`map_floor_decal_hook()` desenha armadilhas `^`, fogo ativo `F` e kits medicos `+`).
- `renderer_set_minimap_hook(RendererMinimapHookFn hook)`: chamado no minimapa 2D (`map_minimap_tile_hook()` marca fogo `^`/`F` e kits `+`).

---

## 5. Exemplos Praticos de Uso

### Exemplo A: Sprite 3D com `draw_world_sprite`

```c
#include "render_api.h"

typedef struct {
    int hp;
    const RendererApi *api;
} MeuObjetoCtx;

static int meu_objeto_sampler(float u, float v, int sx, int sy,
                              int sprite_w, int sprite_h,
                              float dist, float fog, float time_now,
                              const void *user_data,
                              char *out_ch, AsciiColor *out_col) {
    (void)sprite_w; (void)sprite_h; (void)dist; (void)time_now;
    const MeuObjetoCtx *ctx = (const MeuObjetoCtx *)user_data;
    const RendererApi *r = ctx->api;

    float dx = u - 0.5f;
    float dy = v - 0.5f;
    if (dx * dx + dy * dy > 0.22f) {
        return 0;
    }

    float lum = (0.85f - (dx * dx + dy * dy)) * fog;
    *out_ch = r->sample_ramp(lum, sx, sy);
    *out_col = r->scale_rgb(240.0f, 120.0f, 40.0f, fog);
    return 1;
}

void desenhar_meu_objeto(const void *player_ptr, float x, float y, const RendererApi *r) {
    if (!player_ptr || !r) return;
    RenderSpriteSpec spec = {
        .world_x = x,
        .world_y = y,
        .height_scale = 0.85f,
        .width_ratio = 0.65f,
        .min_w = 4,
        .min_h = 6,
        .min_fog = 0.35f
    };
    MeuObjetoCtx ctx = { .hp = 100, .api = r };
    r->draw_world_sprite(player_ptr, &spec, meu_objeto_sampler, &ctx);
}
```

### Exemplo B: Projetil Esferico com `draw_world_orb`

```c
AsciiColor nucleo = {255, 190, 50};
AsciiColor borda  = {255, 60, 15};
r->draw_world_orb(player_ptr, proj_x, proj_y, 0.46f,
                  0.10f, 3, 16,
                  '@', nucleo, '*', borda, 0.55f);
```

### Exemplo C: Matriz 2D Direto na GPU (`renderer_draw_ascii_buffer`)

```c
AsciiCell painel[40 * 80];
/* preencha painel[y * 80 + x] com { ch, fg } (' ' fica transparente) */
renderer_draw_ascii_buffer(painel, 80, 40, 80, pos_x, pos_y, largura_px, altura_px);
```

---

## 6. Boas Praticas

1. Mantenha efeitos de inimigos, habilidades e mapa dentro de `entities.c`, `player.c` ou `map.c` via `RendererApi` e hooks — sem misturar logica de gameplay em `src/renderer.c`.
2. Evite incluir `"raylib.h"` nos headers compartilhados para manter a compilacao rapida e os testes isolados.
3. Use `r->sample_ramp(lum, sx, sy)` nas superficies maiores dos sprites para escalar bem nos modos de qualidade `F2` e nos dois estilos `F4`.
4. Use `' '` para celulas transparentes ao chamar `renderer_draw_ascii_buffer()`.
