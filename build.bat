@echo off
setlocal enabledelayedexpansion

set CC=gcc
set CFLAGS=-Wall -Wextra -std=c11 -O2 -Iinclude
set LDFLAGS=-lws2_32 -lm

set SRCS=src\map.c src\player.c src\renderer.c src\network.c src\main.c
set TEST_SRCS=src\map.c src\player.c src\network.c tests\test_main.c

set TARGET=bin\doom_ascii.exe
set TEST_TARGET=bin\test_runner.exe

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
    exit /b %errorlevel%
)

echo [BUILD] Compilando %TARGET%...
%CC% %CFLAGS% %SRCS% -o %TARGET% %LDFLAGS%
if errorlevel 1 (
    echo [ERRO] Falha na compilacao! Verifique os erros acima.
    exit /b 1
)
echo [BUILD] Compilado com sucesso: %TARGET%

if /i "%~1"=="run" (
    echo [RUN] Iniciando ASCII DOOM...
    %TARGET%
    exit /b %errorlevel%
)

exit /b 0
