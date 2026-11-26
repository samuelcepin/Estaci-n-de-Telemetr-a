CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Isrc -Ilib/sqlite
BUILD_DIR = build
TARGET = estacion

ifeq ($(OS),Windows_NT)
	LDFLAGS = -lcurl
	RM = rm -rf $(BUILD_DIR) $(TARGET) $(TARGET).exe
else
	LDFLAGS = -lcurl -lpthread -ldl
	RM = rm -rf $(BUILD_DIR) $(TARGET)
endif

OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/red.o \
       $(BUILD_DIR)/datos.o \
       $(BUILD_DIR)/sensor.o \
       $(BUILD_DIR)/sqlite3.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/main.o: src/main.c src/datos.h src/red.h src/sensor.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/main.c -o $(BUILD_DIR)/main.o

$(BUILD_DIR)/red.o: src/red.c src/red.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/red.c -o $(BUILD_DIR)/red.o

$(BUILD_DIR)/datos.o: src/datos.c src/datos.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/datos.c -o $(BUILD_DIR)/datos.o

$(BUILD_DIR)/sensor.o: src/sensor.c src/sensor.h src/datos.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/sensor.c -o $(BUILD_DIR)/sensor.o

$(BUILD_DIR)/sqlite3.o: lib/sqlite/sqlite3.c lib/sqlite/sqlite3.h | $(BUILD_DIR)
	$(CC) -O2 -Ilib/sqlite -c lib/sqlite/sqlite3.c -o $(BUILD_DIR)/sqlite3.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET) config

clean:
	$(RM)

	