CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Isrc -Ilib/sqlite
LDFLAGS = -lcurl

ifneq ($(OS),Windows_NT)
	LDFLAGS += -lpthread -ldl
endif

BUILD_DIR = build
SRC_DIR = src
SQLITE_SRC = lib/sqlite/sqlite3.c
SQLITE_OBJ = $(BUILD_DIR)/sqlite3.o

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
BIN = estacion

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJS) $(SQLITE_OBJ)
	$(CC) $(OBJS) $(SQLITE_OBJ) -o $(BIN) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SQLITE_OBJ): $(SQLITE_SRC) | $(BUILD_DIR)
	$(CC) -O2 -c $(SQLITE_SRC) -o $(SQLITE_OBJ)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: all
	./$(BIN) $(ARGS)

clean:
	rm -rf $(BUILD_DIR) $(BIN) $(BIN).exe