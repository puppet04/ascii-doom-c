# Arquitetura do Código — ASCII DOOM em C (Raylib 165 FPS)

Este documento explica como cada parte do jogo funciona para facilitar a implementação de novas funcionalidades pela equipe.

---

## 1. Estrutura de Pastas

```text
Projeto DOOM - C/
├── assets/
│   └── maps/
│       └── level1.txt          # Mapa em texto puro (16x16 com materiais #, @, %)
├── docs/                       # Documentacao da equipe e entregas da A2
├── include/                    # Arquivos de cabecalho (.h) com structs e prototipos
│   ├── config.h                # Resolucao (1280x720 / 160x80 ASCII), TARGET_FPS=165, velocidades
│   ├── map.h                   # Modulo 1 (Lucas): Mapa, fases e colisao
│   ├── player.h                # Modulo 2 (Strazzer): Criador de personagens, 4 classes base e fisica
│   ├── renderer.h              # Modulo 3 (Checchia): Motor grafico ASCII RGB em Raylib + HUD
│   ├── network.h               # Modulo 4 (Checchia): Multiplayer UDP (headers Win32 isolados)
│   └── entities.h              # Modulo 5 (Guilherme): Armas e inimigos
├── src/                        # Implementacoes (.c)
│   ├── map.c
│   ├── player.c
│   ├── entities.c
│   ├── renderer.c
│   ├── network.c
│   └── main.c                  # Menu grafico e Game Loop a 165 FPS
├── tests/
│   └── test_main.c             # Testes automatizados unitarios e de rede
├── build.bat                   # Script de compilacao (auto-instala Raylib se faltar)
└── Makefile                    # Alternativa para compilar com make
```

---

## 2. O Game Loop e o Delta Time (`src/main.c` e `src/player.c`)

O jogo roda dentro de uma janela gráfica da **Raylib** (`1280x720`) configurada inicialmente para **`165 FPS`** (`SetTargetFPS(165)`).

Para impedir que a taxa de quadros altere a velocidade do jogo entre PCs diferentes (ex.: 60 FPS vs 165 FPS vs FPS ilimitado), usamos **Delta Time (`dt = GetFrameTime()`)**:

- Todas as velocidades em `include/config.h` são definidas em **unidades por segundo** (`MOVE_SPEED = 4.2` blocos/s, `ROT_SPEED = 2.6` rad/s, `FIRE_COOLDOWN = 0.25` s).
- A cada frame, multiplicamos o deslocamento por `dt`: `deslocamento = velocidade * dt`.
- Assim, o movimento, a oscilação da arma (`bob_phase`) e a cadência de tiro têm exatamente a mesma duração real em segundos em qualquer monitor!

Ordem de cada quadro no laço `while (!WindowShouldClose() && running)`:
1. **Entrada (`player_handle_input`):** lê `GetMouseDelta().x` (Mouse Look 360°), `IsKeyDown(KEY_W/A/S/D/SHIFT)`, clique esquerdo para atirar e `R` para recarregar.
2. **Rede (`net_receive_state` + `net_send_state`):** atualiza e envia pacotes UDP caso esteja no modo Multiplayer.
3. **Renderização (`renderer_*`):**
   - `renderer_clear()` — limpa a grade `AsciiCell screen_grid[80][160]` e o `z_buffer`.
   - `renderer_draw_walls()` — calcula o Raycasting DDA em 160 colunas com cores por material (`#` vermelho, `@` azul, `%` verde), fog por distância e bordas de bloco.
   - `renderer_draw_sprites()` — projeta jogadores remotos ou alvos respeitando o `z_buffer`.
   - `renderer_draw_minimap()` — desenha o minimapa colorido no canto superior esquerdo.
   - `renderer_draw_weapon()` — desenha a arma em ASCII Art colorido com movimento de caminhada e clarão de disparo.
   - `renderer_draw_hud()` — desenha a mira central.
   - `renderer_present_window()` — desenha toda a grade ASCII colorida e a barra de status inferior na janela da Raylib.

---

## 3. Como funciona o Raycasting ASCII Colorido (`src/renderer.c`)

1. Para cada coluna `x` da grade (`0` a `159`), lançamos um raio saindo de `(player->x, player->y)` dentro do campo de visão (`FOV = 60°`).
2. O algoritmo **DDA (Digital Differential Analyzer)** avança na matriz 2D até encontrar uma parede (`#`, `@` ou `%`).
3. Calculamos a distância perpendicular `perp_dist` (sem distorção olho de peixe) e definimos:
   - A altura da parede na coluna (`SCREEN_HEIGHT / perp_dist`).
   - O caractere de sombreamento por distância (`#@%*=+-:. `), além de `|` nas quinas dos blocos.
   - A cor RGB baseada no caractere da parede no mapa, escurecida conforme a distância e o lado atingido (`side == 1`).
4. A distância de cada coluna é salva no `z_buffer[x]` para que sprites escondidos atrás de paredes não sejam desenhados por cima delas.

---

## 4. Como funciona o Multiplayer UDP (`src/network.c`)

- Utiliza **Winsock2** (`-lws2_32`) com sockets **UDP não-bloqueantes** (`FIONBIO`).
- Os cabeçalhos `<winsock2.h>` ficam isolados dentro de `src/network.c` para evitar conflito de símbolos do Windows (`CloseWindow`, `DrawText`, `ShowCursor`, `Rectangle`) com a Raylib.
- Os dois jogadores trocam uma `struct NetPacket` contendo `player_id`, `x`, `y`, `angle`, `health` e `shooting`.
