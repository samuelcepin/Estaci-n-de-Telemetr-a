CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2
CPPFLAGS = -Isrc
BUILD_DIR = build
TARGET = estacion

SQLITE_DIR = lib/sqlite
SQLITE_C = $(SQLITE_DIR)/sqlite3.c
SQLITE_H = $(SQLITE_DIR)/sqlite3.h
SQLITE_OBJ = $(BUILD_DIR)/sqlite3.o

# La entrega exige compilar la amalgamacion de SQLite como objeto separado.
ifeq ($(wildcard $(SQLITE_C)),)
    $(error Falta $(SQLITE_C). Debes incluir sqlite3.c y sqlite3.h en $(SQLITE_DIR))
endif
ifeq ($(wildcard $(SQLITE_H)),)
    $(error Falta $(SQLITE_H). Debes incluir sqlite3.c y sqlite3.h en $(SQLITE_DIR))
endif

OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/red.o \
       $(BUILD_DIR)/datos.o \
       $(BUILD_DIR)/sensor.o \
       $(SQLITE_OBJ)

LDLIBS = -lcurl -lm -ldl -lpthread

.PHONY: all run clean test test-local test-server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/main.o: src/main.c src/datos.h src/red.h src/sensor.h $(SQLITE_H) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(SQLITE_DIR) -c $< -o $@

$(BUILD_DIR)/red.o: src/red.c src/red.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/datos.o: src/datos.c src/datos.h $(SQLITE_H) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(SQLITE_DIR) -c $< -o $@

$(BUILD_DIR)/sensor.o: src/sensor.c src/sensor.h src/datos.h $(SQLITE_H) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(SQLITE_DIR) -c $< -o $@

$(SQLITE_OBJ): $(SQLITE_C) $(SQLITE_H) | $(BUILD_DIR)
	$(CC) -std=c11 -O0 -I$(SQLITE_DIR) -c $(SQLITE_C) -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET) config

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TARGET).exe

# Pruebas automatizadas: locales (sin credenciales) y remotas (solo lectura).
test: test-local test-server

test-local:
	python3 -m unittest discover -s tests -p 'test_local.py' -v

test-server:
	python3 -m unittest discover -s tests -p 'test_server.py' -v


