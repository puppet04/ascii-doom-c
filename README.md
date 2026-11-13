# ASCII DOOM em C — Projeto Final (A2)

Jogo interativo em primeira pessoa estilo **DOOM em ASCII** rodando diretamente no terminal, desenvolvido em **linguagem C** para a Segunda Avaliação (A2) da disciplina de **Linguagem de Programação** (Engenharia da Computação).

O projeto utiliza **Raycasting DDA** para projetar um mapa 2D em perspectiva 3D no console e conta com base de **Multiplayer em rede local (LAN/UDP)** usando Winsock2.

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
# Apenas compilar o jogo (gera bin\doom_ascii.exe)
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

| Tecla | Ação |
| :--- | :--- |
| `W` / `S` ou Seta Cima/Baixo | Andar para frente / para trás |
| `A` / `D` | Andar lateralmente (strafe) |
| `Q` / `E` ou `J` / `L` ou Setas Esq/Dir | Girar a câmera |
| `Espaço` | Atirar |
| `ESC` | Sair do jogo |

---

## Divisão Modular para a Equipe (até 4 integrantes)

O código foi estruturado em módulos independentes para que todos possam programar ao mesmo tempo no GitHub sem gerar conflitos:

| Integrante | Módulo | Arquivos Principais | Responsabilidades sugeridas |
| :--- | :--- | :--- | :--- |
| **Pessoa 1** | Mapa, Arquivos e Colisão | `include/map.h`, `src/map.c`, `assets/maps/` | Novos mapas `.txt`, portas/itens no mapa, seleção de fases |
| **Pessoa 2** | Jogador, Física e Input | `include/player.h`, `src/player.c` | Movimentação, vida, munição, recarga, corrida, dano |
| **Pessoa 3** | Motor Gráfico ASCII e HUD | `include/renderer.h`, `src/renderer.c` | Texturas ASCII, desenho da arma na tela, inimigos/sprites, HUD |
| **Pessoa 4** | Multiplayer LAN/UDP e Fluxo | `include/network.h`, `src/network.c`, `src/main.c` | Sincronização de tiros/vida, placar multiplayer, inimigos/IA |

---

## Documentação da Equipe

- **Tutorial para o grupo usar Git, GitHub e configurar o PC:** [`docs/GUIA_GITHUB_E_AMBIENTE.md`](docs/GUIA_GITHUB_E_AMBIENTE.md)
- **Como o código funciona por dentro (Raycasting, Loop e Rede):** [`docs/ARQUITETURA_DO_CODIGO.md`](docs/ARQUITETURA_DO_CODIGO.md)
- **Documento base da Etapa 1 / Etapa 2 (para o Word):** [`docs/ETAPA1_DOCUMENTO_BASE.md`](docs/ETAPA1_DOCUMENTO_BASE.md)
