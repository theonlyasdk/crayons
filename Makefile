CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk+-3.0) -lm -O3
LIBS = $(shell pkg-config --libs gtk+-3.0)
TARGET = crayons
BIN_DIR = bin

.PHONY: all clean run

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/$(TARGET): main.c | $(BIN_DIR)
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

run: $(BIN_DIR)/$(TARGET)
	./$<

clean:
	rm -rf $(BIN_DIR)

