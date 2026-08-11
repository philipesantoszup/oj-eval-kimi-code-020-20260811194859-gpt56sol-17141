CC ?= gcc
CFLAGS ?= -O2 -std=gnu11

.PHONY: all clean
all: code

code: main.c buddy.c buddy.h utils.h
	$(CC) $(CFLAGS) -o $@ main.c buddy.c $(LDFLAGS)

clean:
	rm -f code
