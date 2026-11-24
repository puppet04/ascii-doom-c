# Guia da Biblioteca de Renderização ASCII (`render_api.h` + `renderer.h`)

Este guia documenta a biblioteca gráfica do **ASCII DOOM em C** (Módulo 3 — Checchia). A engine de renderização funciona como uma **biblioteca isolada**: nenhum outro módulo da equipe (`map.c`, `entities.c`, `player.c`, `network.c`) precisa editar `src/renderer.c` nem incluir `raylib.h` para desenhar no mundo 3D, no minimapa ou na interface.

---

## 1. Arquitetura em Duas Camadas

A biblioteca é dividida em dois cabeçalhos para manter baixo acoplamento e testes unitários rápidos:

1. **`include/render_api.h` (Interface Pura em C, Zero Raylib):**
   - Define os tipos leves `AsciiColor`, `AsciiCell`, `RenderSpriteSpec`, os ponteiros de hooks e a vtable `RendererApi`.
   - Pode ser incluído livremente em `entities.h`, `map.h` e `player.h` sem puxar `<raylib.h>` — garantindo baixo acoplamento entre os módulos da equipe.
2. **`include/renderer.h` + `src/renderer.c` (Backend Raylib + GPU Shader `#version 330`):**
   - Implementa o raycasting 3D em 2 passes, o z-buffer, o atlas mipmapped `16×6` de glifos ASCII `64×64` (renderizado em `50px` inteiros), os presets de qualidade (`F2`) e estilo (`F4`), o suporte a skins remotas em `SpriteEntity` (`visual_id`, `appearance`, `team_accent`), o HUD e a `RendererApi`.
   - Expõe `renderer_get_api()` e os registradores de hooks usados em `src/main.c`.

---

## 2. Como o Pipeline Funciona

1. **Grade Lógica Dinâmica (`screen_grid`):** cada frame preenche uma matriz até `640×360` células (`r_width × r_height`), variando conforme a qualidade (`BAIXO`, `MEDIO`, `ALTO`, `ULTRA`, `AUTO`) e chegando a células ultra-finas no Estilo 2 (`MICRO-ASCII`).
2. **Raycasting + Oclusão (`z_buffer` e `cell_depth`):**
   - **Passe 1 (`renderer_draw_walls`):** lança `r_width` raios via DDA, corrige olho-de-peixe (`perp_dist`), salva `z_buffer[x]` e rasteriza cada parede.
   - **Passe 2 (`renderer_draw_walls`):** preenche teto e chão em ordem de linha (L1 cache), consultando o `floor_hook` para armadilhas/kits/chamas.
   - **Sprites, Jogadores Remotos, Projéteis, Arma e Minimapa:** escrevem na grade respeitando `z_buffer[x]` e `cell_depth[y][x]` (jogadores remotos em `renderer_draw_sprites` usam seu próprio `visual_id`/`appearance`).
3. **Upload + Renderização GPU em 1 Draw Call (`renderer_draw_ascii_buffer`):**
   - Compacta a grade numa textura RGBA (`RGB = cor foreground`, `A = índice do glifo 1..95`) e dispara um único `DrawTexturePro` sob o fragment shader GLSL `#version 330`.
   - **Estilo 1 (`ESTILO 1: SOBREPOSTO`):** glifos densos com sobreposição `1.72×` no shader.
   - **Estilo 2 (`ESTILO 2: MICRO-ASCII`):** micro-caracteres lado a lado (`1.0×`, curva gama via LUT de brilho e zero sobreposição / zero quadrados de fundo).

### Ordem Canônica por Frame (`run_game_session` em `src/main.c`)

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

## 3. Referência Rápida da `RendererApi` (`include/render_api.h`)

### Estruturas Principais

- **`AsciiColor`**: `{ unsigned char r, g, b; }`
- **`AsciiCell`**: `{ char ch; AsciiColor fg; AsciiColor bg; }` (`' '` ou `'\0'` é transparente na GPU).
- **`RenderSpriteSpec`**:
  - `world_x`, `world_y`: posição no mapa (ex.: centro da célula `mx + 0.5f`).
  - `height_scale`: altura relativa à parede (`1.0f` = altura padrão).
  - `width_ratio`: razão `largura / altura` do sprite projetado.
  - `min_w`, `min_h`: dimensões mínimas em células para sprites distantes não sumirem.
  - `min_fog`: luminosidade mínima `[0.0f..1.0f]` aplicada na neblina distante.

### Funções Disponíveis em `const RendererApi *r`

| Método | O que faz |
| :--- | :--- |
| `r->draw_world_sprite(player, &spec, sampler, ctx)` | Projeta um billboard 3D, calcula fog + z-buffer e chama `sampler(...)` apenas nos pixels visíveis. |
| `r->draw_world_orb(player, x, y, h, rad_scale, min_r, max_r, core_ch, core_col, edge_ch, edge_col, thresh)` | Desenha um projétil esférico 3D (bola de fogo, orbe de energia) com oclusão por paredes. |
| `r->draw_world_floor_ring(cx, cy, radius, inner_ch, inner_col, outer_ch, outer_col)` | Desenha um anel circular no chão 3D (ex.: área de habilidade) com oclusão por paredes e sprites. |
| `r->project_point(player, x, y, h, &sx, &sy, &depth)` | Converte `(x, y, altura)` do mundo para célula `(sx, sy)` e retorna `1` se estiver à frente de `z_buffer[sx]`. |
| `r->set_cell(x, y, ch, fg, bg)` | Escreve uma célula diretamente na grade ASCII do frame. |
| `r->set_cell_if_closer(x, y, depth, ch, fg, bg)` | Escreve apenas se `depth < z_buffer[x]` e `depth < cell_depth[y][x]`. |
| `r->get_grid_size(&w, &h)` | Retorna as dimensões atuais (`r_width`, `r_height`) da malha dinâmica. |
| `r->sample_ramp(lum, x, y)` | Escolhe o caractere da rampa densa (`=-+*xX#%8&BWM@`) com dithering Bayer 4×4 conforme `lum` em `[0..1]`. |
| `r->scale_rgb(r, g, b, factor)` | Multiplica uma cor RGB por `factor` (com saturação segura em `[0..255]`). |
| `r->get_time()` | Tempo atual de execução em segundos (para animações). |
| `r->get_view_angle(player)` | Ângulo real da câmera (respeita 1ª e 3ª pessoa). |

---

## 4. Sistema de Hooks (`renderer_set_*_hook`)

Para customizar paredes, piso ou minimapa sem alterar `src/renderer.c`, crie callbacks com as assinaturas de `include/render_api.h` no seu módulo e registre-os em `register_renderer_hooks()` (`src/main.c`):

- `renderer_set_wall_hook(RendererWallStyleHookFn hook)`: chamado por coluna ao atingir uma parede. Usado por `map_wall_style_hook()` para deixar a saída `'E'` branca/brilhante quando os inimigos morrem e clarear paredes atravessáveis (`$`, `&`, `/`).
- `renderer_set_floor_hook(RendererFloorHookFn hook)`: chamado por célula visível do chão. Usado por `map_floor_decal_hook()` para desenhar placas de armadilha (`^`), fogo ativo (`F`) e kits médicos (`+`) sobre o piso metálico.
- `renderer_set_minimap_hook(RendererMinimapHookFn hook)`: chamado no minimapa 2D para tiles transitáveis. Usado por `map_minimap_tile_hook()` para marcar fogo ativo (`^`) e kits médicos (`+`).

---

## 5. Exemplos Práticos de Uso

### Exemplo A: Renderizar um Inimigo ou Objeto 3D (`draw_world_sprite`)

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

    /* Descarta fundo fora da silhueta oval */
    float dx = u - 0.5f;
    float dy = v - 0.5f;
    if (dx * dx + dy * dy > 0.22f) {
        return 0;
    }

    /* Usa r->sample_ramp para acompanhar automaticamente a qualidade F2 e o estilo F4 */
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

### Exemplo B: Desenhar um Projétil Esférico (`draw_world_orb`)

```c
AsciiColor nucleo = {255, 190, 50};
AsciiColor borda  = {255, 60, 15};
r->draw_world_orb(player_ptr, proj_x, proj_y, 0.46f,
                  0.10f, 3, 16,
                  '@', nucleo, '*', borda, 0.55f);
```

### Exemplo C: Desenhar Qualquer Matriz 2D na GPU (`renderer_draw_ascii_buffer`)

Para interfaces, menus, retratos ou animações 2D em ASCII sem centenas de chamadas `DrawTextCodepoint`:

```c
AsciiCell painel[40 * 80];
/* preencha painel[y * 80 + x] com { ch, fg, {0, 0, 0} } (' ' fica transparente) */
renderer_draw_ascii_buffer(painel, 80, 40, 80, pos_x, pos_y, largura_px, altura_px);
```

---

## 6. Boas Práticas para a Equipe

1. **Nunca edite `src/renderer.c` para adicionar inimigos, armas/habilidades ou itens de mapa:** crie tudo no seu próprio arquivo (`entities.c`, `player.c` ou `map.c`) via `RendererApi` e hooks.
2. **Não inclua `"raylib.h"` em módulos de lógica:** inclua apenas `"render_api.h"`, preservando a compilação limpa da suíte `tests/test_main.c`.
3. **Respeite a qualidade dinâmica (`F2`):** em vez de travar o corpo do sprite em um único caractere fixo (`#`), use `r->sample_ramp(lum, sx, sy)` nas superfícies principais e reserve símbolos fixos apenas para detalhes finos (olhos, chifres, mira, contornos).
4. **Zero quadrados de fundo em UI:** sempre preencha `.bg = {0, 0, 0}` e use `' '` para espaços vazios ao chamar `renderer_draw_ascii_buffer()`.
