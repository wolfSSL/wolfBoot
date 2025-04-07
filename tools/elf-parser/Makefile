-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
CFLAGS=-Wall -g -ggdb
CFLAGS+=-I../../include -DWOLFBOOT_ELF -DELF_PARSER -DPRINTF_ENABLED -DMMU -DARCH_FLASH_OFFSET=0
EXE=elf-parser

LIBS=

all: $(EXE)

$(EXE):
	$(CC) -o $@ $(CFLAGS) $(LIBS) elf-parser.c ../../src/elf.c

clean:
	rm -f *.o $(EXE)
