-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
CFLAGS+=-Wall -g -ggdb
EXE=bin-assemble

LIBS=

Q?=@
ifeq ($(V),1)
   Q=
endif

$(EXE): $(EXE).o
	$(Q)$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
