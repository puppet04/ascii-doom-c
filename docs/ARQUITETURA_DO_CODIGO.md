# Arquitetura do Codigo — SECTOR 404 (ASCII DOOM em C)

Este documento resume como os modulos do **SECTOR 404** se comunicam entre si para facilitar manutencao, testes e novas adicoes sem quebrar a compilacao dos outros integrantes.

---

## 1. Estrutura de Pastas

```text
Projeto DOOM - C/
├── assets/
│   ├── app_icon.rc             # Recurso Win32 para embutir o icone no .exe
│   ├── icon.ico / icon.png     # Icone oficial do SECTOR 404
│   └── maps/
│       └── level1.txt .. level5.txt
├── docs/
│   ├── ARQUITETURA_DO_CODIGO.md
│   ├── ETAPA1_DOCUMENTO_BASE.md
│   ├── GUIA_GITHUB_E_AMBIENTE.md
│   └── GUIA_LIB_RENDERIZACAO.md
├── include/
│   ├── config.h                # Resolucao, TARGET_FPS=165, limites e fisica
│   ├── render_api.h            # Interface pura (sem Raylib) usada por entities/map/player
│   ├── map.h                   # Modulo 1 (Lucas): mapa, 5 fases, armadilhas, kits e hooks
│   ├── player.h                # Modulo 2 (Strazzer): personagens, skins, armas e fisica
│   ├── renderer.h              # Modulo 3 (Checchia): motor ASCII GPU Shader + HUD
│   ├── network.h               # Modulo 4 (Checchia): multiplayer LAN (UDP) + Online Relay
│   └── entities.h              # Modulo 5 (Guilherme): 9 armas, projeteis, bosses e inimigos
├── relay/
│   ├── package.json            # Configuracao Node.js do relay WebSocket
│   └── server.js               # Relay de salas multiplayer online (Render / local)
├── src/
│   ├── map.c
│   ├── player.c
│   ├── entities.c
│   ├── renderer.c
│   ├── network.c
│   ├── embedded_assets.c       # Mapas level1..5 e icone RGBA embutidos no binario
│   └── main.c                  # Menu grafico, criador de skins, lobby e game loop
├── tests/
│   └── test_main.c             # Suite automatizada unitaria e de loopback UDP (173 checks)
├── build.bat                   # Script de compilacao Windows (MSYS2 UCRT64 + Raylib)
└── Makefile                    # Alternativa via make
```

---

## 2. Game Loop e Delta Time (`src/main.c`, `src/player.c`)

O jogo abre em uma janela Raylib (`1280x720`, redimensionavel, `F11` para tela cheia) com alvo inicial de **165 FPS** (`SetTargetFPS`), alternavel entre `60 FPS`, `165 FPS` e `ILIMITADO` (`F3` ou Configuracoes).

Para que a fisica nao mude conforme a taxa de quadros de cada PC, toda movimentacao usa **Delta Time (`dt = GetFrameTime()`)**:
- Velocidades e intervalos em `include/config.h` ficam em unidades por segundo (`MOVE_SPEED`, `ROT_SPEED`, `FIRE_COOLDOWN`).
- Cada quadro multiplica deslocamentos e timers por `dt`.

Ordem de execucao por quadro em `run_game_session`:
1. **Entrada (`handle_custom_input` / `player_handle_input`):** leitura de mouse 360° + pitch vertical, `WASD`/`Shift`, troca de arma por **scroll do mouse**, `F` ou `1`–`9`, recarga (`R`), disparo, habilidades (`E`) e alternancia de camera (`V`).
2. **Armas, Inimigos e Mapa (`entities_update`, `entities_check_hit`, `map_try_advance_level`):**
   - Gerencia 9 armas (5 padrao + 4 de bosses), municao finita com pente/reserva e remocao automatica da torre quando uma arma secundaria fica zerada.
   - `entities_update` usa **cache de flow-field BFS** por tile de destino + hash do mapa para evitar recalcular pathfinding quando o jogador e as paredes nao mudaram de celula.
   - `map.c` usa uma lookup table O(1) (`g_tile_lut[256]`) para colisoes de parede e contador rapido de armadilhas ativas.
   - 4 bosses de arena (`U`, `V`, `W`, `X`) dropam armas exclusivas no chao ao morrer.
3. **Rede (`net_receive_match_state` + `net_send_match_state`):** sincroniza ate 4 jogadores em Co-op ou PvP com posicao, skin (`visual_id` + `appearance`), arma e combate.
4. **Renderizacao (`renderer_*` + `RendererApi`):**
   - `renderer_clear()` reseta o frame buffer e z-buffer.
   - `renderer_draw_walls()` faz Passe 1 (DDA por coluna) e Passe 2 (teto/chao em ordem de linha).
   - `entities_render()`, `renderer_draw_sprites()`, `map_render_hazards()` e `player_render_abilities()` desenham inimigos, projeteis, jogadores remotos, kits/armadilhas e efeitos na grade ASCII via `RendererApi`.
   - `draw_sliding_minimap()` desenha o minimapa 16x16 no canto superior esquerdo.
   - `player_render_weapon()` / `renderer_draw_weapon()` desenham a arma equipada (ou a Dragonslayer no modo Berserk).
   - `renderer_draw_scene_and_hud()` envia a grade para a GPU em 1 draw call e `player_draw_ability_hud()` sobrepoe habilidades, kills e a torre de armas no lado direito do HUD.

---

## 3. Motor Grafico ASCII na GPU (`src/renderer.c`)

Veja detalhes completos de uso em [`docs/GUIA_LIB_RENDERIZACAO.md`](GUIA_LIB_RENDERIZACAO.md).

- **Desacoplamento:** `map.c`, `entities.c` e `player.c` nao dependem de `<raylib.h>` para renderizar no mundo 3D; eles usam `render_api.h` e hooks registrados no `main.c`.
- **Celula compacta (`AsciiCell`):** cada celula ocupa **4 bytes** (`char ch` + `AsciiColor fg`), melhorando o uso de cache L1 durante o raycasting e o preenchimento de piso/teto.
- **Resolucao interna e GPU:** grade dinamica de ate `544x272` enviada para uma textura RGBA (`576x352`, permitindo o preview do menu em ate `280x340` direto na GPU) e desenhada em **1 draw call** com um atlas mipmapped `16x6` (`textureGrad`).
- **Dois shaders GLSL `#version 330`:**
  - `ASCII_FS_CLASSIC_CODE` (**Sobreposto**, `F4`): glifos densos com sobreposicao `1.72x`.
  - `ASCII_FS_MICRO_CODE` (**Micro-ASCII**, `F4`): caracteres lado a lado sem sobreposicao e sem fundo pintado, com LUT de contraste.
- **Qualidade (`F2`):** `BAIXO`, `MEDIO`, `ALTO`, `ULTRA` e `AUTO (JANELA)`, que ajusta a densidade suavemente conforme o frame time medido.

---

## 4. Multiplayer LAN e Online (`src/network.c` e `relay/server.js`)

O modulo de rede atende dois transportes mantendo a mesma interface de jogo para ate **4 jogadores** (1 host + 3 convidados):

1. **LAN / Localhost (Winsock2 UDP):**
   - Sockets UDP nao-bloqueantes (`FIONBIO`) na porta padrao `5000`, com descoberta automatica de salas (`NET_MSG_DISCOVER_REQ` / `OFFER`).
   - Os cabecalhos `<winsock2.h>` / `<windows.h>` ficam restritos a `src/network.c` para nao colidir com simbolos da Raylib.
2. **Online via Relay WebSocket (WinHTTP + Node.js):**
   - `src/network.c` conecta via WinHTTP WebSocket em `wss://sector404-relay.onrender.com` (ou URL customizada via `SECTOR404_RELAY_URL`) usando `relay/server.js` para criar/entrar em salas por codigo mesmo atras de NAT/roteadores diferentes.
3. **Co-op e PvP:** alternavel pelo host no lobby; o dano e abates em PvP usam contadores cumulativos monotônicos resistentes a perda de pacotes.
