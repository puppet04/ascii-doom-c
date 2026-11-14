# Documento Base — Projeto Final A2 (Etapa 1 e Etapa 2)

> **Instruções de uso:** Copie e cole as seções abaixo no documento Word da equipe, substituindo os campos entre `[colchetes]` pelos nomes, RAs e detalhes do grupo.

---

## 1. Identificação do Projeto e da Equipe

- **Disciplina:** Linguagem de Programação — Engenharia da Computação
- **Avaliação:** Projeto Final — Segunda Avaliação (A2)
- **Título do Projeto:** ASCII DOOM em C — Jogo FPS Interativo em Janela Gráfica (Raylib) com Suporte Multiplayer LAN
- **Integrantes da Equipe (4 alunos):**
  1. **Strazzer** — RA: `[000000]` *(Responsável: Criador de personagens e 4 personagens base — branch `strazzer`)*
  2. **Lucas** — RA: `[000000]` *(Responsável: Criação do mapa, fases e colisão — branch `lucas`)*
  3. **Guilherme** — RA: `[000000]` *(Responsável: Criação das armas e dos inimigos — branch `guilherme`)*
  4. **Checchia** — RA: `[000000]` *(Responsável: HUD, gráficos ASCII 3D, menu e multiplayer — branch `checchia`)*

---

## 2. Tema Base e Esboço da Ideia (Etapa 1)

### 2.1 Objetivo
Desenvolver um jogo de tiro em primeira pessoa (FPS) inspirado no clássico **DOOM**, com estética **ASCII colorida em alta resolução** renderizada em janela gráfica própria via **Raylib** (a até 165 FPS com movimentação independente de taxa de quadros via *Delta Time*) e escrito em **linguagem C**. O software contará com modo **Singleplayer** (navegação em labirintos, alvo/inimigos e gerenciamento de munição/vida) e modo **Multiplayer em rede local (LAN/Localhost)** utilizando comunicação UDP para sincronizar dois jogadores em tempo real na mesma arena.

### 2.2 Escopo Funcional Planejado
1. **Menu Interativo na Janela:** escolha entre jogar Singleplayer, hospedar uma partida Multiplayer (Host), conectar-se a uma partida existente informando o IP (Client) ou sair.
2. **Motor Gráfico 3D em ASCII Colorido:** conversão de uma matriz 2D de caracteres em visão em primeira pessoa utilizando o algoritmo de **Raycasting DDA** (`160x80` células ASCII RGB), com materiais coloridos (`#`, `@`, `%`), sombreamento de profundidade (`#@%*=+-:. `), arma em primeira pessoa em ASCII Art, minimapa colorido em tempo real e renderização de sprites com *z-buffer*.
3. **Movimentação Fluida com Mouse Look e Delta Time:** rotação da câmera pelo mouse (360°), clique esquerdo para atirar, deslocamento contínuo `WASD` + corrida (`Shift`) multiplicados por `dt` (`GetFrameTime()`) e detecção de colisão suave contra paredes.
4. **Carregamento de Mapas em Arquivo:** leitura de fases a partir de arquivos texto (`.txt`), permitindo criar e testar novos cenários sem recompilar o programa.
5. **Multiplayer LAN (UDP):** envio e recebimento não-bloqueante de pacotes contendo posição, ângulo, vida e disparo do adversário.
6. **HUD e Sistema de Combate:** exibição de vida, munição, pontuação, coordenadas, mira central e registro de acertos.

---

## 3. Conceitos de Linguagem C Aplicados

O desenvolvimento aplica diretamente os principais conteúdos da disciplina:

- **Tipos Compostos (`struct` e `enum`):** representação de entidades como `GameMap`, `Player`, `CharacterClass`, `Weapon`, `Enemy`, `SpriteEntity`, `NetPacket` e `NetContext`.
- **Ponteiros e Passagem por Referência:** atualização eficiente do estado do jogador, inimigos, buffers de tela e sockets de rede sem cópias desnecessárias em memória.
- **Matrizes e Vetores (Arrays):** armazenamento do mapa bidimensional (`tiles[16][17]`), grade ASCII colorida (`screen_grid[80][160]`) e `z_buffer`.
- **Manipulação de Arquivos (`FILE *`, `fopen`, `fgets`):** leitura dinâmica de arquivos de fase (`assets/maps/level1.txt`) e salvamento/leitura de personagens.
- **Modularização (`.h` e `.c`):** separação clara de responsabilidades em cinco módulos independentes (`map`, `player`, `entities`, `renderer`, `network`).
- **Entrada/Saída em Tempo Real e Renderização Gráfica (Raylib):** leitura contínua de teclado e mouse (`Mouse Look`), controle de taxa de quadros (`165 FPS`) e movimentação por *Delta Time*.
- **Comunicação em Rede (Sockets UDP / Winsock2):** serialização de estruturas em pacotes de rede para partidas entre duas máquinas.

---

## 4. Divisão de Trabalho na Equipe (GitHub Flow)

O projeto está hospedado no GitHub (`https://github.com/puppet04/ascii-doom-c`) com uma branch dedicada para cada integrante testar antes de integrar à `main` e conta com suíte de testes automatizados (`.\build.bat test`):

- **Strazzer (branch `strazzer` — Módulo `player`):** criador de personagens, definição dos 4 personagens base (`CharacterClass`), atributos, movimentação e controles.
- **Lucas (branch `lucas` — Módulo `map`):** criação e carregamento de mapas/fases, materiais do cenário e detecção de colisão.
- **Guilherme (branch `guilherme` — Módulo `entities`):** criação das armas (arsenal, dano, cadência) e dos inimigos (spawn, inteligência de movimento e combate).
- **Checchia (branch `checchia` — Módulos `renderer`, `network` e `main`):** HUD, motor gráfico ASCII colorido em Raylib, menu inicial, efeitos visuais e integração multiplayer.

---

## 5. Plano de Testes (Base para a Etapa 2)

Os testes são executados de forma automatizada pelo script `.\build.bat test` (arquivo `tests/test_main.c`) e manualmente via workshop:

1. **Teste de Inicialização e Limites do Mapa:** verificar carregamento padrão `16x16` e proteção contra acessos fora da matriz (`x < 0`, `y >= height`).
2. **Teste de Leitura de Arquivo:** validar carregamento correto de `assets/maps/level1.txt` e tratamento seguro quando o arquivo não existe.
3. **Teste de Física e Colisão:** garantir que o jogador avança em células livres e é bloqueado ao tentar atravessar paredes.
4. **Teste de Rotação:** confirmar normalização do ângulo da câmera no intervalo $[0, 2\pi)$.
5. **Teste de Comunicação UDP (Loopback):** abrir um socket Host e um Client em `127.0.0.1`, enviar um `NetPacket` e validar se posição, ID e estado de disparo são recebidos corretamente.
6. **Teste de Renderização (`--test-frame`):** confirmar geração íntegra do quadro ASCII 3D com minimapa e HUD sem erros de memória.
