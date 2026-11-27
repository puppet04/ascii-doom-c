# Projeto Final — Segunda Avaliacao (A2): SECTOR 404 (ASCII DOOM em C)

---

## 1. Identificacao do Projeto e da Equipe

- **Disciplina:** Linguagem de Programacao — Engenharia da Computacao
- **Avaliacao:** Projeto Final — Segunda Avaliacao (A2)
- **Nome do Projeto:** **SECTOR 404** (FPS 3D em ASCII colorido escrito em C com Raylib, suporte Singleplayer e Multiplayer LAN/Online)
- **Equipe:** **404 NF**
  1. **Rafael Strazzer** *(5 personagens base, habilidades, criador de skins e fisica — branch `strazzer`)*
  2. **Lucas** *(5 fases em arquivo texto, materiais, armadilhas e colisao — branch `lucas`)*
  3. **Guilherme Trentin** *(9 armas, 4 bosses secretos, drops e IA de inimigos — branch `guilherme`)*
  4. **Guilherme Checchia** *(Motor ASCII 3D GPU, HUD/menus, otimizacoes e multiplayer LAN/Online — branch `checchia`)*

---

## 2. Escopo Funcional Entregue

1. **Menu, Configuracoes e Criador de Skins:** escolha entre Singleplayer, Multiplayer (Host/Join LAN ou Online por codigo de sala), Selecao de Personagem (5 classes base + ate 8 skins customizadas por sessao), Configuracoes de video/FPS/teclas e Criador de Personagens modular.
2. **Motor Grafico 3D em ASCII Colorido (GPU Shader):** raycasting DDA em tempo real com 2 estilos visuais (`Sobreposto` e `Micro-ASCII`), presets de qualidade (`F2`), camera em 1a e 3a pessoa (`V`), minimapa deslizante `16x16` e z-buffer para sprites e projeteis.
3. **Movimentacao e Combate por Delta Time:** rotacao 360° pelo mouse e teclado, movimentacao `WASD` + corrida (`Shift`), troca de armas pelo scroll do mouse / `F` / `1`–`9`, torre de arsenal no lado direito do HUD e remocao automatica de armas vazias.
4. **5 Fases Editaveis e Bosses:** carregamento de `assets/maps/level1.txt` a `level5.txt` (com copia embutida no `.exe`), portais de saida `'E'` liberados ao derrotar os inimigos, kits medicos `+`, armadilhas `^`/`F` e 4 bosses secretos com drops exclusivos.
5. **Multiplayer LAN (UDP) e Online (WebSocket Relay):** partidas para ate 4 jogadores em **Co-op** ou **PvP**, com descoberta automatica na LAN, salas Online via servidor Relay e sincronizacao de skin, posicao e dano.

---

## 3. Conceitos de Linguagem C Aplicados

- **Tipos Compostos (`struct` e `enum`):** `GameMap`, `Player`, `CharacterClass`, `CharacterAppearance`, `Weapon`, `Enemy`, `NetPacket`, `NetContext` e `RendererApi`.
- **Ponteiros e Passagem por Referencia:** manipulacao de estado de jogo, buffers de grade e callbacks (`RendererApi` + hooks) sem copias desnecessarias.
- **Matrizes, Vetores e LUTs:** mapa 2D, grade ASCII (`AsciiCell` de 4 bytes), `z_buffer`, tabela O(1) de tipos de celula e matriz de distancia para flow-field BFS.
- **Manipulacao de Arquivos (`FILE *`, `fopen`, `fgets`):** leitura de fases em `.txt` com fallback embutido no binario (`src/embedded_assets.c`).
- **Modularizacao (`.h` e `.c`):** separacao em cinco modulos com interface grafica desacoplada (`include/render_api.h`).
- **Sockets e WinAPI (Winsock2 + WinHTTP):** multiplayer UDP nao-bloqueante na LAN e WebSocket client nativo no Windows para o servidor Relay online.

---

## 4. Plano e Verificacao de Testes (`tests/test_main.c`)

A suite automatizada (`.\build.bat test`) executa **173 verificacoes** cobrindo:

1. Inicializacao e limites do mapa, leitura de arquivos `.txt` e fallback embutido.
2. Fisica, colisao contra paredes, normalizacao de angulos e sistema de movimentacao.
3. Arsenal completo (9 armas, recarga, consumo de pente/reserva e remocao de arma sem municao).
4. Personagens, criador de skins e regras das habilidades (Teto, Arauto, Guts Berserk e Rambo Guerrilheiro).
5. Spawn, pathfinding BFS e combate contra inimigos, projeteis e bosses.
6. Loopback UDP Host/Client com multi-slot e modos Co-op/PvP, alem de teste de renderizacao headless (`--test-frame`).
