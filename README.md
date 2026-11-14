# ASCII DOOM em C — Projeto Final (A2)

Jogo interativo em primeira pessoa estilo **DOOM em ASCII colorido** em janela gráfica dedicada (**Raylib a 165 FPS**), desenvolvido em **linguagem C** para a Segunda Avaliação (A2) da disciplina de **Linguagem de Programação** (Engenharia da Computação).

O projeto combina **Raycasting DDA colorido em alta resolução (`160x80` caracteres ASCII)**, física independente da taxa de quadros via **Delta Time**, **Mouse Look 360°** e base de **Multiplayer em rede local (LAN/UDP)** usando Winsock2.

---

## Cronograma da Disciplina (A2)

- **Etapa 1 (13/11/2026):** Definição da equipe (até 4 alunos) e do tema base (esboço da ideia). *Entregável: documento Word.*
- **Etapa 2 (20/11/2026):** Desenvolvimento do software, documentação e testes. *Entregáveis: documento Word atualizado e código-fonte em C.*
- **Etapa 3 (27/11/2026):** Workshop de apresentação e testes entre equipes e professores. *Entregáveis: documento Word atualizado, código-fonte em C e avaliação de sistema de outra equipe.*

> Veja o rascunho pronto para colar no Word em [`docs/ETAPA1_DOCUMENTO_BASE.md`](docs/ETAPA1_DOCUMENTO_BASE.md).

---

## Como Compilar e Rodar (Windows)

Abra o terminal na pasta do projeto e execute:

```powershell
# Compilar o jogo (instala a Raylib automaticamente caso falte no MSYS2!)
.\build.bat

# Compilar e abrir o jogo
.\build.bat run

# Compilar e rodar a suíte de testes automatizados
.\build.bat test

# Limpar arquivos compilados
.\build.bat clean
```

> **Dica no VS Code:** Pressione `Ctrl + Shift + B` para compilar automaticamente usando a task configurada em `.vscode/tasks.json`.

---

## Controles do Jogo

| Tecla / Botão | Ação |
| :--- | :--- |
| `Mouse` (movimento) ou `Q`/`E` / Setas | Girar a câmera (360° fluido) |
| `W` / `A` / `S` / `D` | Movimentação contínua (com movimento diagonal normalizado) |
| `Shift` | Correr (*Sprint*) |
| `Clique Esquerdo` ou `Espaço` | Atirar |
| `R` | Recarregar munição (`30/30`) |
| `TAB` | Soltar / travar o cursor do mouse na janela |
| `F3` | Alternar limite de FPS (`165 FPS` / `Ilimitado`) |
| `F11` | Alternar Tela Cheia sem bordas |
| `ESC` | Voltar ao menu / Sair |

---

## Divisão Modular para a Equipe (Branches no GitHub)

Cada integrante possui sua própria branch de desenvolvimento para testar e commitar antes de enviar para a `main`:

| Integrante | Branch | Módulo | Arquivos Principais | Responsabilidades sugeridas |
| :--- | :--- | :--- | :--- | :--- |
| **Checchia** | `checchia` | Multiplayer LAN/UDP e Fluxo | `include/network.h`, `src/network.c`, `src/main.c` | Sincronização de tiros/vida, placar multiplayer, inimigos/IA |
| **Strazzer** | `strazzer` | Mapa, Arquivos e Colisão | `include/map.h`, `src/map.c`, `assets/maps/` | Novos mapas `.txt` com materiais coloridos (`#`, `@`, `%`), portas/itens, seleção de fases |
| **Guilherme** | `guilherme` | Jogador, Física e Input | `include/player.h`, `src/player.c` | Movimentação com Delta Time, vida, munição, recarga, corrida, dano |
| **Lucas** | `lucas` | Motor Gráfico ASCII e HUD | `include/renderer.h`, `src/renderer.c` | Efeitos visuais ASCII RGB, sprites de inimigos, animação da arma, HUD |

---

## Documentação da Equipe

- **Tutorial para o grupo usar Git, GitHub e configurar o PC:** [`docs/GUIA_GITHUB_E_AMBIENTE.md`](docs/GUIA_GITHUB_E_AMBIENTE.md)
- **Como o código funciona por dentro (Raycasting, Delta Time e Rede):** [`docs/ARQUITETURA_DO_CODIGO.md`](docs/ARQUITETURA_DO_CODIGO.md)
- **Documento base da Etapa 1 / Etapa 2 (para o Word):** [`docs/ETAPA1_DOCUMENTO_BASE.md`](docs/ETAPA1_DOCUMENTO_BASE.md)
