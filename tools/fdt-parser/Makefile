-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
CFLAGS=-Wall -g -ggdb
CFLAGS+=-I../../include -DWOLFBOOT_FDT -DPRINTF_ENABLED
EXE=fdt-parser

LIBS=

all: $(EXE)

$(EXE):
	$(CC) -o $@ $(CFLAGS) $(LIBS) fdt-parser.c ../../src/fdt.c

clean:
	rm -f *.o $(EXE)
