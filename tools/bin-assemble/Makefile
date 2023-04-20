-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
CFLAGS+=-Wall -g -ggdb
EXE=bin-assemble

LIBS=

$(EXE): $(EXE).o
	$(Q)$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
