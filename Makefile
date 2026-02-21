CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wpedantic
LDFLAGS = -lncurses -lm
TARGET  = god-casa

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
