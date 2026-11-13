# Guia Completo do Grupo: Ambiente, Git e GitHub

Este guia foi feito para que **todos os integrantes do grupo** consigam configurar o computador, baixar o projeto, programar na sua parte e enviar alterações para o GitHub **sem quebrar o código dos colegas**.

---

## Parte 1: Como instalar o ambiente no Windows

Cada integrante precisa ter 3 ferramentas instaladas no computador: **VS Code**, **GCC (compilador C)** e **Git**.

### 1. Instalar o Visual Studio Code (Editor)
1. Baixe e instale o [VS Code](https://code.visualstudio.com/).
2. Abra o VS Code, vá na aba **Extensions** (`Ctrl + Shift + X`) e instale:
   - **C/C++** (da Microsoft)
   - **Portuguese (Brazil) Language Pack** (opcional, se preferir em português)

### 2. Instalar o Compilador C (MSYS2 + GCC)
Se você ainda não tem o `gcc` instalado:
1. Baixe o instalador do [MSYS2](https://www.msys2.org/) e instale usando as opções padrão.
2. Abra o terminal **MSYS2 UCRT64** (procure no Menu Iniciar) e rode:
   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb mingw-w64-ucrt-x86_64-make git
   ```
3. Adicione as pastas abaixo no **PATH do Windows** (para o `gcc` e o `git` funcionarem em qualquer terminal):
   - Aperte a tecla `Windows`, digite **variáveis de ambiente** e abra *"Editar as variáveis de ambiente do sistema"*.
   - Clique em **Variáveis de Ambiente...**
   - Em **Variáveis de usuário**, clique em `Path` -> **Editar** -> **Novo** e adicione:
     - `C:\msys64\ucrt64\bin`
     - `C:\msys64\usr\bin`
   - Clique em **OK** em todas as janelas e **reinicie o VS Code / terminal**.

> **Alternativa para o Git:** Caso prefira, você também pode baixar o instalador oficial [Git for Windows](https://git-scm.com/download/win) (basta clicar em *Next* em tudo).

### 3. Testar se tudo funcionou
Abra um novo terminal PowerShell (ou o terminal integrado do VS Code com `Ctrl + '`) e digite:

```powershell
gcc --version
git --version
```

Se os dois comandos mostrarem a versão instalada, o ambiente está pronto!

---

## Parte 2: Configurar o Git e conectar ao GitHub

### Passo A: Identificar seu nome e e-mail (só faz 1 vez no PC)
Use o mesmo e-mail da sua conta do GitHub:

```powershell
git config --global user.name "Seu Nome"
git config --global user.email "seuemail@exemplo.com"
```

### Passo B: Para o Líder do Grupo (subir o repositório pela 1ª vez)
1. Entre no [GitHub](https://github.com/), faça login e clique em **New repository** (Novo repositório).
2. Dê um nome (ex.: `ascii-doom-c`), deixe **sem marcar** README/.gitignore (porque este projeto já tem!) e clique em **Create repository**.
3. Copie a URL do repositório (ex.: `https://github.com/puppet04/ascii-doom-c.git`).
4. Na pasta deste projeto no seu computador, rode:
   ```powershell
   git remote add origin https://github.com/puppet04/ascii-doom-c.git
   git push -u origin main
   ```
5. **Convidar os colegas do grupo:**
   - No GitHub do repositório, vá em **Settings** -> **Collaborators** -> **Add people**.
   - Adicione o `@usuario` ou e-mail dos outros integrantes do grupo.
   - Eles receberão um convite por e-mail/notificação do GitHub e precisam clicar em **Accept invitation**.

### Passo C: Para os Colegas do Grupo (baixar o projeto)
Depois de aceitar o convite no GitHub:
1. Abra o terminal na pasta onde quer salvar seus projetos (ex.: `Área de Trabalho` ou `Documentos`).
2. Rode o comando `git clone` com o link do repositório do grupo:
   ```powershell
   git clone https://github.com/puppet04/ascii-doom-c.git
   cd ascii-doom-c
   ```
3. Teste se compila e roda os testes:
   ```powershell
   .\build.bat
   .\build.bat test
   .\build.bat run
   ```

---

## Parte 3: Fluxo Diário de Trabalho (Comandos no Terminal)

Nunca programe diretamente na branch `main`! Cada tarefa ou módulo deve ser feito em uma **branch** separada.

### 1. Antes de começar a programar: atualize a `main`
```powershell
git checkout main
git pull origin main
```

### 2. Crie sua branch para a tarefa do dia
Use um nome curto indicando o que você vai fazer (sem espaços):
```powershell
git checkout -b feat/melhorar-mapa
```
*(Outros exemplos: `feat/hud-arma`, `feat/dano-inimigo`, `fix/colisao-parede`)*

### 3. Programe, compile e teste bastante!
Sempre confira antes de subir:
```powershell
.\build.bat
.\build.bat test
```

### 4. Salve suas alterações no Git (Add + Commit)
Veja quais arquivos você modificou:
```powershell
git status
```
Adicione os arquivos e crie um commit explicando o que fez:
```powershell
git add .
git commit -m "feat: adiciona selecao de fases no map.c"
```

### 5. Envie sua branch para o GitHub (Push)
```powershell
git push -u origin feat/melhorar-mapa
```

### 6. Abra um Pull Request (PR) no GitHub
1. Acesse a página do repositório no GitHub.
2. Vai aparecer um botão verde **"Compare & pull request"** — clique nele.
3. Preencha o checklist que aparece automaticamente, descreva o que mudou e clique em **Create pull request**.
4. O GitHub Actions vai compilar e rodar os testes automaticamente! Quando ficar verde e um colega revisar, clique em **Merge pull request**.

---

## Parte 4: Como fazer TUDO sem usar Terminal (VS Code ou GitHub Desktop)

Se você não gosta de digitar comandos no terminal, dá para usar apenas botões!

### Opção A: Direto no VS Code (aba Source Control)
1. **Trocar ou criar branch:** No canto inferior esquerdo do VS Code, clique no nome da branch atual (ex.: `main`) -> **+ Create new branch...** -> digite o nome (ex.: `feat/minha-parte`) e aperte `Enter`.
2. **Baixar novidades (`pull`):** Clique no ícone de `...` na aba **Source Control** (ícone dos três pontinhos conectados na barra lateral esquerda, ou `Ctrl + Shift + G`) -> **Pull**.
3. **Salvar commit:**
   - Passe o mouse em *Changes* e clique no `+` para preparar os arquivos (*Stage All Changes*).
   - Digite a mensagem na caixinha acima (ex.: `feat: melhora mira da arma`) e clique no botão azul **Commit**.
4. **Enviar para o GitHub (`push`):** Clique no botão **Publish Branch** (ou **Sync Changes**).
5. **Compilar sem terminal:** Aperte `Ctrl + Shift + B` no VS Code!

### Opção B: Usando o GitHub Desktop
1. Baixe o [GitHub Desktop](https://desktop.github.com/) e faça login com sua conta do GitHub.
2. Vá em **File -> Clone repository** e escolha o repositório do grupo (ou **Add local repository** se já baixou).
3. No topo, clique em **Current branch -> New branch** para criar sua branch.
4. Quando terminar de editar no VS Code, volte no GitHub Desktop: ele mostra todas as linhas alteradas.
5. Escreva o título do commit no canto inferior esquerdo, clique em **Commit to ...** e depois em **Push origin** no topo.
6. Clique em **Preview Pull Request -> Create Pull Request** para abrir no site.

---

## Parte 5: Regras de Ouro para NÃO dar conflito no código em C

1. **Respeite os módulos da equipe:**
   - Pessoa 1 foca em `src/map.c` e `assets/maps/`
   - Pessoa 2 foca em `src/player.c`
   - Pessoa 3 foca em `src/renderer.c`
   - Pessoa 4 foca em `src/network.c` e `src/main.c`
2. **Combinem no grupo antes de editar `.h` ou `src/main.c`:** se duas pessoas mudarem `include/player.h` ao mesmo tempo, vai dar conflito na hora do merge.
3. **Nunca suba arquivo `.exe` ou `.o`:** o `.gitignore` já bloqueia `bin/` e `*.exe`, mas sempre cheque com `git status`.
4. **Commits pequenos e frequentes:** melhor subir 1 funcionalidade pronta por vez do que acumular 5 dias de alterações.
5. **Só dê Merge se compilar e passar nos testes (`.\build.bat test`).**

---

## Parte 6: Socorro! Como resolver conflitos e erros comuns

### Caso 1: Deu conflito de merge (Merge Conflict)
Isso acontece quando duas pessoas mexeram na mesma linha do mesmo arquivo.
1. No VS Code, o arquivo aparece marcado em vermelho com blocos assim:
   ```c
   <<<<<<< HEAD (Current Change)
   int vida = 100;
   =======
   int vida = 150;
   >>>>>>> origin/main (Incoming Change)
   ```
2. Acima do bloco no VS Code aparecem botões clicáveis: **Accept Current Change**, **Accept Incoming Change** ou **Accept Both Changes**.
3. Escolha o que deve ficar (ou edite manualmente removendo as linhas `<<<<<<<`, `=======`, `>>>>>>>`).
4. Teste compilando com `.\build.bat` e `.\build.bat test`.
5. Salve com `git add .` e `git commit -m "fix: resolve conflito de merge"`.

### Caso 2: Esqueci e comecei a programar na `main`!
Se você ainda não deu commit, basta criar uma branch agora que suas alterações vão junto com você:
```powershell
git checkout -b feat/minha-mudanca
```

### Caso 3: Quero desfazer tudo que mexi em um arquivo que quebrei
```powershell
git checkout -- src/arquivo_quebrado.c
```

### Caso 4: O comando `git push` pediu usuário e senha e recusou minha senha
O GitHub não aceita mais a senha da conta no terminal. Ao rodar `git push`, o Git no Windows normalmente abre uma janela do navegador para autorizar o **Git Credential Manager**. Basta clicar em **Authorize** no navegador! Se não abrir, instale o [GitHub Desktop](https://desktop.github.com/) ou o [Git for Windows](https://git-scm.com/download/win) que traz o gerenciador de login integrado.
