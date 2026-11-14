CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lm

BIN_DIR = bin
TARGET = $(BIN_DIR)/doom_ascii.exe
TEST_TARGET = $(BIN_DIR)/test_runner.exe

GAME_SRCS = src/map.c src/player.c src/entities.c src/renderer.c src/network.c src/main.c
TEST_SRCS = src/map.c src/player.c src/entities.c src/network.c tests/test_main.c

.PHONY: all run test clean

all: $(TARGET)

$(BIN_DIR):
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)

$(TARGET): $(GAME_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(GAME_SRCS) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	$(TARGET)

test: $(TEST_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET) $(LDFLAGS)
	$(TEST_TARGET)

clean:
	@if exist $(BIN_DIR) rmdir /s /q $(BIN_DIR)
	@if exist build rmdir /s /q build
