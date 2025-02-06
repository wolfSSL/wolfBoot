-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
USE_GCC=1
CFLAGS+=-Wall -g -ggdb
EXE=bin-assemble

LIBS=

$(EXE): $(EXE).o
	$(Q)$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
