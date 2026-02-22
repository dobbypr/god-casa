CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wpedantic
LDFLAGS = -lncurses -lm
TARGET  = god-casa

SRCS = main.c simulation.c

$(TARGET): $(SRCS) simulation.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
