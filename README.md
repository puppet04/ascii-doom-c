# SECTOR 404 — Equipe 404 NF (A2)

Shooter interativo em primeira pessoa em **ASCII colorido** com janela gráfica própria (motor Raylib/OpenGL embutido estaticamente, rodando a **165 FPS**), desenvolvido em **linguagem C** pela equipe **404 NF** para a Segunda Avaliação (A2) da disciplina de **Linguagem de Programação** (Engenharia da Computação).

O projeto combina **Raycasting DDA em alta resolução micro-ASCII**, física independente da taxa de quadros via **Delta Time**, **Mouse Look 360°**, sistema de personagens/habilidades e **Multiplayer LAN/UDP para até 4 jogadores** (Co-op e PvP) usando Winsock2.

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

No menu, abra **Selecionar personagem**, escolha **Fuzileiro**, **Kasane Teto**,
**Arauto das Sombras** ou **Guts** e use **Confirmar / Voltar** antes de iniciar uma partida.
Fuzileiro vem selecionado por padrao (100 de vida, 30 de municao e velocidade 4.2). Use o mouse ou as setas; Enter confirma. A escolha vale enquanto o jogo estiver
aberto, tanto no singleplayer quanto no multiplayer.

Teto tem 100 de vida, 30 de municao e velocidade 4.2, com visual ASCII proprio
no pedestal, no retrato do HUD e na camera local em terceira pessoa (`V`).
O visual escolhido ainda nao e transmitido aos outros jogadores pela rede.

Use **+ Criar skin** (ou `N` na selecao) para informar um nome e montar
seu avatar: 3 cabecas, 3 corpos, 5 cabelos, 4 roupas,
6 cores de cabelo, 6 cores de roupa e 4 tons de pele. As setas de cada linha
trocam somente aquela parte, com previa ao lado. A combinacao aparece tambem
no HUD e na camera em terceira pessoa.
As criacoes sao apenas cosmeticas: usam os atributos do Fuzileiro
(100 de vida, 30 de municao e velocidade 4.2). **Salvar** adiciona e seleciona a skin; **Cancelar**
ou `Esc` descarta o rascunho. As setas `<` e `>` mudam a pagina da lista.
Ha oito vagas de criacao por sessao: os personagens criados permanecem ao voltar
das partidas, mas nao sao gravados em disco ao fechar o jogo.

Arauto das Sombras tem 100 de vida, 30 de municao e velocidade 4.2. Seu visual
ASCII inclui capuz, manto de penas, correntes e detalhes vermelhos, sem cajado.

Guts tem 100 de vida, 30 de municao e velocidade 4.2. Seu visual inclui armadura
escura, capa, braco metalico e Dragonslayer nas costas, no menu, HUD e terceira
pessoa. No modo Berserk, a espada passa a ser a arma de combate corpo a corpo.

**Rambo** esta disponivel na segunda pagina da selecao (`>`), com bandana vermelha,
cabelo comprido, cartucheira e calca camuflada. Tem 100 de vida, 30 de municao e
velocidade 4.2. Sua passiva e Guerrilheiro, descrita abaixo.

### Habilidades e combate singleplayer

- **Kasane Teto:** `E`, quando a habilidade esta pronta, cria uma area na
  posicao atual da Teto com coracoes flutuantes e um circulo rosa no chao. Dura 10 segundos, tem raio de 3 blocos
  e concede +35% de velocidade e +25% de dano nos tiros somente dentro dela
  (dano arredondado para inteiro). Cooldown de 30 segundos
  contado a partir da ativacao. Nao exige kills e fica pronta no inicio da partida.
- **Arauto das Sombras:** `E` dispara uma bola de fogo negra que causa 5 de dano
  ao primeiro inimigo atingido e o incendeia por 5 segundos (2 de dano por
  segundo). Fogo violeta animado com nucleo escuro, rastro e brasas. Para nas
  paredes. Cooldown de 30 segundos.
- **Guts:** depois de 5 kills, `E` ativa Berserk por 10 segundos, uma vez por
  partida. Troca para a armadura Berserker e espada com alcance de 1.5 bloco,
  eliminacao em um golpe, sem gastar municao, e dano recebido dobrado.
  Clique esquerdo/Espaco golpeia; ao terminar, a arma e o visual originais voltam.
- **Fuzileiro e skins criadas:** sem habilidades.
- **Rambo — Guerrilheiro:** apos 5 segundos sem sofrer dano, o proximo tiro
  causa dano dobrado. O disparo consome a carga mesmo se errar e inicia uma nova
  contagem de 5 segundos. Sofrer dano zera a carga; tentar atirar sem municao nao
  a consome. O HUD indica quando o tiro dobrado esta pronto.

O singleplayer agora usa inimigos com vida, ataques e mortes reais. Ha seis
inimigos por onda e uma nova onda chega 3 segundos apos eliminar todos. Kills,
cooldowns e o uso do Berserk reiniciam em cada partida; a pausa congela os tempos.
O HUD mostra o progresso e os cooldowns. Ao morrer, o jogador retorna ao menu.
O multiplayer LAN ainda sincroniza apenas o estado basico dos jogadores: nao
sincroniza essas habilidades, inimigos ou dano PvP.

| Tecla / Botão | Ação |
| :--- | :--- |
| `Mouse` (movimento), `Q` ou Setas esquerda/direita | Girar a câmera (360° fluido) |
| `E` | Area da Teto / habilidade do Arauto / Berserk do Guts quando pronto (tecla reservada) |
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

| Integrante | Branch | Módulo | Arquivos Principais | Responsabilidades |
| :--- | :--- | :--- | :--- | :--- |
| **Rafael Strazzer** | `strazzer` | Personagens e Criador | `include/player.h`, `src/player.c` | Kasane Teto (visual e atributos), física e controles |
| **Lucas** | `lucas` | Mapa, Fases e Colisão | `include/map.h`, `src/map.c`, `assets/maps/` | Criação dos mapas `.txt`, materiais coloridos (`#`, `@`, `%`), portas e seleção de fases |
| **Guilherme Trentin** | `guilherme` | Armas e Inimigos | `include/entities.h`, `src/entities.c` | Criação das armas (dano, cadência, alcance) e dos inimigos (spawn, IA e combate) |
| **Guilherme Checchia** | `checchia` | HUD, Gráficos e Integração | `include/renderer.h`, `src/renderer.c`, `src/main.c`, `src/network.c` | Motor gráfico ASCII 3D (Raylib), HUD, menu, efeitos visuais e multiplayer UDP |

---

## Documentação da Equipe

- **Tutorial para o grupo usar Git, GitHub e configurar o PC:** [`docs/GUIA_GITHUB_E_AMBIENTE.md`](docs/GUIA_GITHUB_E_AMBIENTE.md)
- **Como o código funciona por dentro (Raycasting, Delta Time e Rede):** [`docs/ARQUITETURA_DO_CODIGO.md`](docs/ARQUITETURA_DO_CODIGO.md)
- **Documento base da Etapa 1 / Etapa 2 (para o Word):** [`docs/ETAPA1_DOCUMENTO_BASE.md`](docs/ETAPA1_DOCUMENTO_BASE.md)

### Integracao dos personagens com o renderer

O modulo `player` fornece o visual por `visual_id`, sem depender do nome:

- `player_get_visual(id)`: descricao e configuracao do retrato no HUD.
- `player_character_material(id, x, y, back_view)`: silhueta frontal/traseira.
- `player_get_material_color(material)`: paleta RGBA, independente de Raylib.

Avatares montados usam `CharacterAppearance`, copiado da classe para o jogador.
`player_part_count`, `player_part_label` e `player_part_option` descrevem as pecas
para o menu. `player_appearance_visual` e `player_appearance_material` fornecem
o retrato e a silhueta composta ao renderer, preservando os visuais originais
quando a personalizacao esta desativada.

Para adicionar personagens nesse formato, registre os atributos, o visual e a
silhueta em `src/player.c`, com o identificador em `include/player.h`.
O menu e o renderer consomem esses dados automaticamente. O Fuzileiro continua
usando o desenho original do motor (`custom_sprite = 0`).
