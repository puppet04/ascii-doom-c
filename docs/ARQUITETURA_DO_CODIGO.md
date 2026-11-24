# Arquitetura do Código — ASCII DOOM em C (Raylib 165+ FPS)

Este documento explica como cada parte do jogo funciona para facilitar a implementação de novas funcionalidades pela equipe.

---

## 1. Estrutura de Pastas

```text
Projeto DOOM - C/
├── assets/
│   └── maps/
│       ├── level1.txt          # Fases 1 a 5 em texto puro (18x18 a 32x32 com #, @, %, $, E, ^, +, F)
│       ├── ...
│       └── level5.txt
├── docs/                       # Documentacao da equipe e entregas da A2
│   ├── ARQUITETURA_DO_CODIGO.md
│   ├── ETAPA1_DOCUMENTO_BASE.md
│   ├── GUIA_GITHUB_E_AMBIENTE.md
│   └── GUIA_LIB_RENDERIZACAO.md # Manual completo da biblioteca grafica (render_api.h + renderer.h)
├── include/                    # Arquivos de cabecalho (.h) com structs e prototipos
│   ├── config.h                # Resolucao, TARGET_FPS=165, limites e velocidades
│   ├── render_api.h            # Interface pura (sem Raylib) consumida por entities.c e map.c
│   ├── map.h                   # Modulo 1 (Lucas): Mapa, 5 fases, armadilhas/kits e hooks de mapa
│   ├── player.h                # Modulo 2 (Strazzer): Criador de personagens, classes, skins e fisica
│   ├── renderer.h              # Modulo 3 (Checchia): Motor grafico ASCII GPU Shader + HUD
│   ├── network.h               # Modulo 4 (Checchia): Multiplayer UDP + Discovery LAN (Winsock2)
│   └── entities.h              # Modulo 5 (Guilherme): Armas, projeteis e inimigos em ASCII
├── src/                        # Implementacoes (.c)
│   ├── map.c
│   ├── player.c
│   ├── entities.c
│   ├── renderer.c
│   ├── network.c
│   └── main.c                  # Menu grafico GPU, Lobby LAN, preview 16x16 e Game Loop
├── tests/
│   └── test_main.c             # Suite de testes automatizados unitarios e de rede (138 checks)
├── build.bat                   # Script de compilacao Windows (MSYS2 UCRT64 + Raylib)
└── Makefile                    # Alternativa para compilar com make
```

---

## 2. O Game Loop e o Delta Time (`src/main.c` e `src/player.c`)

O jogo roda dentro de uma janela gráfica da **Raylib** (`1280x720`, redimensionável e com suporte `F11` sem bordas) configurada inicialmente para **`165 FPS`** (`SetTargetFPS(165)`), com opção de `60 FPS`, `165 FPS` ou `ILIMITADO` nas Configurações.

Para impedir que a taxa de quadros altere a velocidade do jogo entre PCs diferentes, usamos **Delta Time (`dt = GetFrameTime()`)**:

- Todas as velocidades em `include/config.h` são definidas em **unidades por segundo** (`MOVE_SPEED`, `ROT_SPEED`, `FIRE_COOLDOWN`).
- A cada frame, multiplicamos o deslocamento por `dt`: `deslocamento = velocidade * dt`.
- Assim, o movimento, a oscilação da arma (`bob_phase`), projéteis e recargas têm exatamente a mesma duração real em segundos em qualquer monitor.

Ordem de cada quadro no laço `while (!WindowShouldClose() && running)` em `run_game_session`:
1. **Entrada (`handle_custom_input`):** lê `GetMouseDelta()` (Mouse Look 360° + pitch vertical), teclas configuráveis de movimento/sprint, clique esquerdo para atirar e `R` para recarregar.
2. **IA e Mapa (`enemies_update`, `enemies_check_hit`, `map_try_advance_level`):** atualiza inimigos, projéteis, armadilhas, kits médicos e avanço de fase no tile `'E'`.
3. **Rede (`net_receive_match_state` + `net_send_match_state`):** troca estado em topologia estrela (até 4 jogadores) em Co-op ou PvP via UDP não-bloqueante, com spawns seguros por BFS e renderização da skin de cada participante (`visual_id` + `appearance`).
4. **Renderização (`renderer_*` + biblioteca `RendererApi`):**
   - `renderer_clear()` — limpa `screen_grid` e reseta `z_buffer` / `cell_depth` com `memcpy`.
   - `renderer_draw_walls()` — Passe 1 (DDA por coluna) + Passe 2 (teto/chão em ordem de linha, consultando hooks de parede/chão).
   - `entities_render(&enemies, &player, r_api)` — projeta inimigos e projéteis usando `RendererApi` (`draw_world_sprite`, `draw_world_orb`, `project_point`).
   - `renderer_draw_sprites()` — projeta jogador remoto (LAN) ou silhueta em 3ª pessoa (`[V]`).
   - `map_render_hazards(&map, &player, r_api)` — renderiza kits médicos e chamas 3D no mundo via `RendererApi`.
   - `draw_sliding_minimap()` — recorta janela 16×16 e desenha minimapa colorido.
   - `renderer_draw_weapon()` — desenha arma em 1ª pessoa (rifle ou espada Berserk).
   - `renderer_present_window()` — envia a grade ASCII para a **GPU em 1 draw call** (GLSL `#version 330` com atlas mipmapped `16×6`) e sobrepõe mira, HUD e mugshot reativo via GPU.

---

## 3. Como funciona o Motor ASCII na GPU (`src/renderer.c`)

Consulte também o manual detalhado da biblioteca em [`docs/GUIA_LIB_RENDERIZACAO.md`](GUIA_LIB_RENDERIZACAO.md).

1. **Biblioteca Desacoplada:** `map.c` e `entities.c` não acessam `raylib.h` diretamente; eles usam hooks registrados (`renderer_set_wall_hook`, `renderer_set_floor_hook`, `renderer_set_minimap_hook`) e a tabela de funções `RendererApi` (`renderer_get_api()`).
2. **Dois Passe em CPU + 1 Draw Call na GPU:**
   - Para cada coluna `x` (`0..r_width-1`), o algoritmo **DDA** avança na matriz do mapa até encontrar uma parede, calcula `perp_dist` (corrigindo olho-de-peixe) e grava `z_buffer[x]`.
   - No **Passe 2**, teto e chão são preenchidos linha a linha em L1 cache.
   - Em seguida, `renderer_draw_ascii_buffer()` compacta a grade em uma textura RGBA e um único fragment shader (`ASCII_FS_CODE`) amostra o atlas mipmapped `16×6` (`textureGrad`) na GPU.
3. **Estilos (`F4`) e Qualidade (`F2`):**
   - **`ESTILO 1: SOBREPOSTO`:** caracteres densos sobrepostos clássicos (`OVERLAP = 1.72`).
   - **`ESTILO 2: MICRO-ASCII`:** caracteres pequenos lado a lado sem sobreposição e sem quadrados de fundo pintados.
   - **Qualidade `F2`:** `BAIXO`, `MEDIO`, `ALTO`, `ULTRA` e `AUTO (JANELA)`.

---

## 4. Como funciona o Multiplayer UDP (`src/network.c`)

- Utiliza **Winsock2** (`-lws2_32`) com sockets **UDP não-bloqueantes** (`FIONBIO`) em topologia **estrela** (1 Anfitrião + até 3 Convidados = **4 jogadores**).
- Os cabeçalhos `<winsock2.h>` / `<windows.h>` ficam isolados dentro de `src/network.c` para evitar conflito de símbolos da Win32 API com a Raylib.
- **Modos Co-op e PvP:** alternáveis pelo Anfitrião no lobby; no lobby cada vaga ocupada renderiza ao vivo a prévia ASCII do personagem escolhido, e no PvP o dano/abates são sincronizados via contadores cumulativos monotônicos resistentes à perda de datagramas.
- Mantém descoberta automática na LAN/localhost (`NET_MSG_DISCOVER_REQ`/`OFFER` com lotação e modo) e preserva `net_send_state`/`net_receive_state` (`NetPacket`) para compatibilidade dos testes unitários.
