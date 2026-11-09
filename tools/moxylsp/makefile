MOXY ?= ../../build/moxy
CC ?= cc
CFLAGS ?= -Wall -O2

all: moxylsp

moxylsp: moxylsp.mxy lsp.mxy json.h json.c
	$(MOXY) emit moxylsp.mxy -o gen.c
	$(CC) -std=c11 $(CFLAGS) -o moxylsp gen.c json.c -lm
	rm -f gen.c

clean:
	rm -f moxylsp gen.c

.PHONY: all clean
