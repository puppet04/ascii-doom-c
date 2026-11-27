# Guia do Grupo: Ambiente, Git e GitHub

Guia rapido para todos os integrantes da equipe **404 NF** configurarem o PC, compilarem o projeto e subirem alteracoes no GitHub sem gerar conflitos.

---

## Parte 1: Instalar o ambiente no Windows

Precisamos de 3 ferramentas: **VS Code**, **GCC (MSYS2 UCRT64)** e **Git**.

### 1. Visual Studio Code
1. Baixe e instale o [VS Code](https://code.visualstudio.com/).
2. Em **Extensions** (`Ctrl + Shift + X`), instale **C/C++** (Microsoft).

### 2. Compilador C (MSYS2 + GCC + Raylib)
1. Baixe e instale o [MSYS2](https://www.msys2.org/) com as opcoes padrao.
2. Abra o terminal **MSYS2 UCRT64** no Menu Iniciar e rode:
   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-raylib git
   ```
   *(Se esquecer a Raylib, o proprio `.\build.bat` tenta instalar via pacman antes de compilar.)*
3. Adicione no **Path** das variaveis de ambiente do Windows:
   - `C:\msys64\ucrt64\bin`
   - `C:\msys64\usr\bin`
4. Abra um PowerShell novo e confira:
   ```powershell
   gcc --version
   git --version
   ```

*(Opcional: quem preferir pode usar o instalador oficial [Git for Windows](https://git-scm.com/download/win) para o Git.)*

---

## Parte 2: Configurar Git e baixar o repositorio

### 1. Configurar usuario (1 vez por PC)
```powershell
git config --global user.name "Seu Nome"
git config --global user.email "seuemail@exemplo.com"
```

### 2. Clonar e testar
```powershell
git clone https://github.com/puppet04/ascii-doom-c.git
cd ascii-doom-c
.\build.bat
.\build.bat test
.\build.bat run
```

---

## Parte 3: Fluxo diario com Branches

Evite commitar direto na `main` sem testar. Cada integrante tem sua branch:

- **Strazzer:** `strazzer` *(5 personagens base, criador de skins e fisica — `player.h`/`.c`)*
- **Lucas:** `lucas` *(5 fases em `.txt`, materiais, portas e colisao — `map.h`/`.c`, `assets/maps/`)*
- **Guilherme Trentin:** `guilherme` *(9 armas, 4 bosses, drops e IA — `entities.h`/`.c`)*
- **Guilherme Checchia:** `checchia` *(motor ASCII GPU, HUD, menus e multiplayer LAN/Online — `renderer.*`, `network.*`, `main.c`, `relay/`)*

### Passos basicos no terminal

1. Entre na sua branch:
   ```powershell
   git checkout SUA_BRANCH
   ```
2. Puxe novidades da `main` antes de comecar:
   ```powershell
   git pull origin main
   ```
3. Depois de editar, compile e rode os testes:
   ```powershell
   .\build.bat
   .\build.bat test
   ```
4. Salve e envie suas mudancas:
   ```powershell
   git status
   git add .
   git commit -m "descreva em uma frase o que voce mudou"
   git push origin SUA_BRANCH
   ```
5. Abra um **Pull Request** no GitHub comparando `base: main <- compare: SUA_BRANCH` e faca o merge quando estiver tudo compilando e passando nos testes.

> **Importante:** o executavel final compilado (`bin/sector404.exe`) fica fora do Git (listado no `.gitignore`). Quando quiser distribuir uma versao pronta para jogar, publique pelos **Releases** do GitHub usando o binario compilado em `bin/sector404.exe`.

---

## Parte 4: Usando VS Code ou GitHub Desktop (sem terminal)

- **VS Code:** na barra inferior esquerda troque para sua branch; na aba **Source Control** (`Ctrl + Shift + G`) use *Pull from...* -> `origin/main`, clique em `+` para preparar as mudancas, escreva a mensagem, clique em **Commit** e depois em **Sync Changes**. Para compilar, aperte `Ctrl + Shift + B`.
- **GitHub Desktop:** escolha sua branch em *Current branch*, escreva a descricao no canto inferior esquerdo, clique em **Commit to ...** e **Push origin**.

---

## Parte 5: Cuidados para evitar conflitos

1. Combine no grupo antes de mexer em arquivos compartilhados (`include/*.h` e `src/main.c`).
2. Nunca suba `.exe`, `.o` ou binarios temporarios no commit (sempre confira com `git status`).
3. Sempre rode `.\build.bat` e `.\build.bat test` antes de enviar para a `main`.
4. Se der conflito de merge, escolha os blocos corretos no VS Code, remova os marcadores `<<<<<<<` / `=======` / `>>>>>>>`, compile novamente e finalize com `git commit`.
