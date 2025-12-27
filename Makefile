# Define the compiler
CC = gcc

# Get the flags for GTK 3 (Change to gtk4 if using GTK 4)
CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
LIBS = $(shell pkg-config --libs gtk+-3.0)

# The target
all: crayons

crayons: main.c
	$(CC) -o crayons main.c $(CFLAGS) $(LIBS)

clean:
	rm -f crayons
