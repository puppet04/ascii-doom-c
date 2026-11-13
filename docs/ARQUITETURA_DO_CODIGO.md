# Arquitetura do Código — ASCII DOOM em C

Este documento explica como cada parte do jogo funciona para facilitar a implementação de novas funcionalidades pela equipe.

---

## 1. Estrutura de Pastas

```text
Projeto DOOM - C/
├── assets/
│   └── maps/
│       └── level1.txt          # Mapa em texto puro (16x16)
├── docs/                       # Documentacao da equipe e entregas da A2
├── include/                    # Arquivos de cabecalho (.h) com structs e prototipos
│   ├── config.h                # Constantes globais (resolucao, FOV, velocidades, porta)
│   ├── map.h                   # Modulo 1: Mapa e colisao
│   ├── player.h                # Modulo 2: Jogador, fisica e input
│   ├── renderer.h              # Modulo 3: Motor grafico ASCII e HUD
│   └── network.h               # Modulo 4: Multiplayer UDP
├── src/                        # Implementacoes (.c)
│   ├── map.c
│   ├── player.c
│   ├── renderer.c
│   ├── network.c
│   └── main.c                  # Menu inicial e Game Loop principal
├── tests/
│   └── test_main.c             # Testes automatizados unitarios e de rede
├── .github/workflows/ci.yml    # Compilacao e testes automaticos no GitHub
├── build.bat                   # Script de compilacao rapida no Windows
└── Makefile                    # Alternativa para compilar com make
```

---

## 2. O Game Loop (`src/main.c`)

Todo jogo interativo funciona dentro de um laço `while (running)` que roda ~30 vezes por segundo (`FRAME_DELAY_MS = 33`):

1. **Entrada (`player_handle_input`):** lê o teclado sem travar o programa (`_kbhit()` e `_getch()`), move/gira o jogador e detecta disparos (`Espaço`) ou saída (`ESC`).
2. **Rede (`net_receive_state` + `net_send_state`):** se estiver no modo Multiplayer, lê os pacotes UDP pendentes do outro jogador e envia a posição atual do jogador local.
3. **Renderização:**
   - `renderer_clear()` — limpa o buffer de caracteres e o `z_buffer`.
   - `renderer_draw_walls()` — calcula as paredes 3D por Raycasting.
   - `renderer_draw_sprites()` — desenha o outro jogador ou alvo na cena 3D.
   - `renderer_draw_minimap()` — desenha o minimapa 2D no canto superior esquerdo.
   - `renderer_draw_hud()` — desenha a mira central e a tag de status.
   - `renderer_present()` — reposiciona o cursor do console em `(0,0)` e imprime tudo de uma vez (sem usar `system("cls")`, evitando que a tela fique piscando).

---

## 3. Como funciona o Raycasting ASCII (`src/renderer.c`)

O mapa é uma matriz 2D de `16x16` caracteres (`#` = parede, `.` = chão). Para gerar a ilusão de 3D:

1. Para cada coluna `x` da tela (de `0` a `SCREEN_WIDTH - 1`), lançamos um raio saindo da posição `(player->x, player->y)` com ângulo variando dentro do campo de visão (`FOV = 60°`).
2. O algoritmo **DDA (Digital Differential Analyzer)** avança célula por célula na grade do mapa até encontrar um `#`.
3. Calculamos a distância perpendicular `perp_dist` (multiplicando pelo cosseno da diferença de ângulo para evitar distorção "olho de peixe").
4. Quanto menor a distância, maior a altura da parede naquela coluna (`line_height = SCREEN_HEIGHT / perp_dist`) e mais denso é o caractere ASCII usado no sombreamento (`#@%*=+-:. `).
5. A distância de cada coluna fica salva em `z_buffer[x]`. Quando vamos desenhar um sprite (outro jogador ou inimigo), só desenhamos a coluna do sprite se ela estiver mais perto do que `z_buffer[x]` — assim o jogador não aparece "através" das paredes!

---

## 4. Como funciona o Multiplayer UDP (`src/network.c`)

- Utiliza a biblioteca **Winsock2** (`-lws2_32`) com sockets **UDP não-bloqueantes** (`FIONBIO`).
- Usamos UDP em vez de TCP porque em jogos em tempo real é melhor receber sempre a posição mais recente sem travar o FPS esperando confirmação de pacotes antigos.
- Os dois jogadores trocam uma `struct NetPacket` contendo `player_id`, `x`, `y`, `angle`, `health` e `shooting`.
- **Host:** abre a porta UDP (padrão `5000`) com `bind()` e descobre o endereço do Client automaticamente quando recebe o primeiro pacote.
- **Client:** aponta para o IP do Host (ex.: `127.0.0.1` no mesmo PC, ou o IP local `192.168.x.x` na rede da faculdade/Wi-Fi) e começa a enviar sua posição.
