CC = gcc
WINDRES = windres
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -static-libgcc -Wl,-Bstatic -lraylib -lglfw3 -lwinpthread -Wl,-Bdynamic -lopengl32 -lgdi32 -lwinmm -lshell32 -lws2_32 -lwinhttp -lm

BIN_DIR = bin
TARGET = $(BIN_DIR)/sector404.exe
TEST_TARGET = $(BIN_DIR)/test_runner.exe
ICON_RES = $(BIN_DIR)/app_icon.o

GAME_SRCS = src/map.c src/player.c src/entities.c src/renderer.c src/network.c src/main.c src/embedded_assets.c src/glfw_static_shim.c
TEST_SRCS = src/map.c src/player.c src/entities.c src/network.c src/embedded_assets.c src/glfw_static_shim.c tests/test_main.c

.PHONY: all run test clean

all: $(TARGET)

$(BIN_DIR):
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)

$(ICON_RES): assets/app_icon.rc assets/icon.ico | $(BIN_DIR)
	$(WINDRES) --include-dir=assets assets/app_icon.rc -O coff -o $(ICON_RES)

$(TARGET): $(GAME_SRCS) $(ICON_RES) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(GAME_SRCS) $(ICON_RES) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	$(TARGET)

test: $(TEST_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET) $(LDFLAGS)
	$(TEST_TARGET)

clean:
	@if exist $(BIN_DIR) rmdir /s /q $(BIN_DIR)
	@if exist build rmdir /s /q build
