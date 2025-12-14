CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -Iinclude
LDFLAGS := -lSDL2 -lSDL2_ttf

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := the_beginning

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN)

run: $(BIN)
	./$(BIN)
