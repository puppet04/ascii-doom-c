# Documento Base — Projeto Final A2 (Etapa 1 e Etapa 2)

> **Instruções de uso:** Copie e cole as seções abaixo no documento Word da equipe, substituindo os campos entre `[colchetes]` pelos nomes, RAs e detalhes do grupo.

---

## 1. Identificação do Projeto e da Equipe

- **Disciplina:** Linguagem de Programação — Engenharia da Computação
- **Avaliação:** Projeto Final — Segunda Avaliação (A2)
- **Título do Projeto:** ASCII DOOM em C — Jogo FPS Interativo em Terminal com Suporte Multiplayer LAN
- **Integrantes da Equipe (até 4 alunos):**
  1. `[Nome Completo 1]` — RA: `[000000]` *(Responsável: Mapa, Arquivos e Colisão)*
  2. `[Nome Completo 2]` — RA: `[000000]` *(Responsável: Jogador, Física e Controles)*
  3. `[Nome Completo 3]` — RA: `[000000]` *(Responsável: Motor Gráfico Raycasting ASCII e HUD)*
  4. `[Nome Completo 4]` — RA: `[000000]` *(Responsável: Multiplayer UDP em Rede e Game Loop)*

---

## 2. Tema Base e Esboço da Ideia (Etapa 1)

### 2.1 Objetivo
Desenvolver um jogo de tiro em primeira pessoa (FPS) inspirado no clássico **DOOM**, renderizado integralmente em **caracteres ASCII no terminal do Windows** e escrito em **linguagem C**. O software contará com modo **Singleplayer** (navegação em labirintos, alvo/inimigos e gerenciamento de munição/vida) e modo **Multiplayer em rede local (LAN/Localhost)** utilizando comunicação UDP para sincronizar dois jogadores em tempo real na mesma arena.

### 2.2 Escopo Funcional Planejado
1. **Menu Interativo:** escolha entre jogar Singleplayer, hospedar uma partida Multiplayer (Host), conectar-se a uma partida existente (Client) ou sair.
2. **Motor Gráfico 3D em ASCII:** conversão de uma matriz 2D de caracteres em visão em primeira pessoa utilizando o algoritmo de **Raycasting DDA**, com sombreamento de profundidade (`#@%*=+-:. `), chão texturizado, minimapa em tempo real e renderização de sprites com *z-buffer*.
3. **Movimentação e Física:** deslocamento para frente/trás, movimentação lateral (*strafe*), rotação da câmera e detecção de colisão suave contra paredes (impedindo atravessar obstáculos).
4. **Carregamento de Mapas em Arquivo:** leitura de fases a partir de arquivos texto (`.txt`), permitindo criar e testar novos cenários sem recompilar o programa.
5. **Multiplayer LAN (UDP):** envio e recebimento não-bloqueante de pacotes contendo posição, ângulo, vida e disparo do adversário.
6. **HUD e Sistema de Combate:** exibição de vida, munição, pontuação, coordenadas, mira central e registro de acertos.

---

## 3. Conceitos de Linguagem C Aplicados

O desenvolvimento aplica diretamente os principais conteúdos da disciplina:

- **Tipos Compostos (`struct` e `enum`):** representação de entidades como `GameMap`, `Player`, `SpriteEntity`, `NetPacket` e `NetContext`.
- **Ponteiros e Passagem por Referência:** atualização eficiente do estado do jogador, buffers de tela e sockets de rede sem cópias desnecessárias em memória.
- **Matrizes e Vetores (Arrays):** armazenamento do mapa bidimensional (`tiles[16][17]`), buffer de renderização do console (`screen_buf`) e `z_buffer`.
- **Manipulação de Arquivos (`FILE *`, `fopen`, `fgets`):** leitura dinâmica de arquivos de fase (`assets/maps/level1.txt`).
- **Modularização (`.h` e `.c`):** separação clara de responsabilidades em quatro módulos independentes (`map`, `player`, `renderer`, `network`).
- **Entrada/Saída em Tempo Real e Controle de Terminal:** leitura não-bloqueante de teclado e atualização de frame sem piscar o console.
- **Comunicação em Rede (Sockets UDP / Winsock2):** serialização de estruturas em pacotes de rede para partidas entre duas máquinas.

---

## 4. Divisão de Trabalho na Equipe (GitHub Flow)

O projeto está hospedado no GitHub com divisão de arquivos por módulo para evitar conflitos de código e com **Integração Contínua (GitHub Actions)** para validar compilação e testes a cada envio:

- **Integrante 1 (Módulo `map`):** carregamento de fases, criação de novos mapas, detecção de paredes/portas/itens.
- **Integrante 2 (Módulo `player`):** controles, movimentação com colisão, cálculo de dano, recarga e pontuação.
- **Integrante 3 (Módulo `renderer`):** projeção 3D por Raycasting, desenho da arma/mira, HUD e sprites de inimigos/jogadores.
- **Integrante 4 (Módulo `network` e `main`):** gerenciamento do Game Loop, menu, conexão Host/Client via UDP e validação de acertos em rede.

---

## 5. Plano de Testes (Base para a Etapa 2)

Os testes são executados de forma automatizada pelo script `.\build.bat test` (arquivo `tests/test_main.c`) e manualmente via workshop:

1. **Teste de Inicialização e Limites do Mapa:** verificar carregamento padrão `16x16` e proteção contra acessos fora da matriz (`x < 0`, `y >= height`).
2. **Teste de Leitura de Arquivo:** validar carregamento correto de `assets/maps/level1.txt` e tratamento seguro quando o arquivo não existe.
3. **Teste de Física e Colisão:** garantir que o jogador avança em células livres e é bloqueado ao tentar atravessar paredes.
4. **Teste de Rotação:** confirmar normalização do ângulo da câmera no intervalo $[0, 2\pi)$.
5. **Teste de Comunicação UDP (Loopback):** abrir um socket Host e um Client em `127.0.0.1`, enviar um `NetPacket` e validar se posição, ID e estado de disparo são recebidos corretamente.
6. **Teste de Renderização (`--test-frame`):** confirmar geração íntegra do quadro ASCII 3D com minimapa e HUD sem erros de memória.
