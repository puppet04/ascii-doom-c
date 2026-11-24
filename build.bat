@echo off
setlocal enabledelayedexpansion

set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"

if not exist "C:\msys64\ucrt64\bin\gcc.exe" (
    if not exist "C:\msys64\usr\bin\pacman.exe" (
        echo [ERRO] MSYS2 nao encontrado em C:\msys64. Consulte docs\GUIA_GITHUB_E_AMBIENTE.md.
        exit /b 1
    )
    echo [SETUP] Instalando compilador GCC UCRT64...
    C:\msys64\usr\bin\pacman.exe -S --noconfirm --needed mingw-w64-ucrt-x86_64-gcc
    if errorlevel 1 (
        echo [ERRO] Nao foi possivel instalar o GCC UCRT64.
        exit /b 1
    )
)

if not exist "C:\msys64\ucrt64\include\raylib.h" (
    if exist "C:\msys64\usr\bin\pacman.exe" (
        echo [SETUP] Raylib nao encontrada. Instalando automaticamente via MSYS2...
        C:\msys64\usr\bin\pacman.exe -S --noconfirm --needed mingw-w64-ucrt-x86_64-raylib
        if errorlevel 1 (
            echo [ERRO] Nao foi possivel instalar a Raylib automaticamente.
            exit /b 1
        )
    ) else (
        echo [ERRO] MSYS2 nao encontrado em C:\msys64. Consulte docs\GUIA_GITHUB_E_AMBIENTE.md.
        exit /b 1
    )
)

set "CC=C:\msys64\ucrt64\bin\gcc.exe"
set "WINDRES=C:\msys64\ucrt64\bin\windres.exe"
set CFLAGS=-Wall -Wextra -std=c11 -O2 -Iinclude
set LDFLAGS=-static-libgcc -Wl,-Bstatic -lraylib -lglfw3 -lwinpthread -Wl,-Bdynamic -lopengl32 -lgdi32 -lwinmm -lshell32 -lws2_32 -lm

set SRCS=src\map.c src\player.c src\entities.c src\renderer.c src\network.c src\main.c src\glfw_static_shim.c
set TEST_SRCS=src\map.c src\player.c src\entities.c src\network.c src\glfw_static_shim.c tests\test_main.c

set TARGET=bin\sector404.exe
set TEST_TARGET=bin\test_runner.exe
set ICON_OBJ=

if /i "%~1"=="clean" (
    echo [CLEAN] Removendo arquivos compilados...
    if exist bin rmdir /s /q bin
    if exist build rmdir /s /q build
    echo [CLEAN] Concluido.
    exit /b 0
)

if not exist bin mkdir bin

if /i "%~1"=="test" (
    echo [TEST] Compilando suite de testes...
    %CC% %CFLAGS% %TEST_SRCS% -o %TEST_TARGET% %LDFLAGS%
    if errorlevel 1 (
        echo [ERRO] Falha ao compilar os testes.
        exit /b 1
    )
    echo [TEST] Executando testes...
    echo.
    %TEST_TARGET%
    exit /b !errorlevel!
)

if exist "assets\app_icon.rc" (
    if exist "%WINDRES%" (
        "%WINDRES%" --include-dir=assets assets\app_icon.rc -O coff -o bin\app_icon.o
        if errorlevel 1 (
            echo [ERRO] Falha ao compilar recurso de icone!
            exit /b 1
        )
        set ICON_OBJ=bin\app_icon.o
    )
)

echo [BUILD] Compilando %TARGET%...
%CC% %CFLAGS% %SRCS% %ICON_OBJ% -o %TARGET% %LDFLAGS%
if errorlevel 1 (
    echo [ERRO] Falha na compilacao! Verifique os erros acima.
    exit /b 1
)
echo [BUILD] Compilado com sucesso: %TARGET%

if /i "%~1"=="run" (
    echo [RUN] Iniciando SECTOR 404...
    %TARGET%
    exit /b !errorlevel!
)

exit /b 0
