-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
CFLAGS+=-DBUILD_TOOL -Wall -g -ggdb -I../../include -I../../hal -Wextra
Q?=@
ifeq ($(V),1)
   Q=
endif

EXE=ufserver

$(EXE): $(EXE).o libwolfboot.o
	$(Q)$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $(@) $(^)

libwolfboot.o: ../../src/libwolfboot.c
	$(Q)$(CC) $(CFLAGS) -c -o $(@) $(^)


clean:
	$(Q)rm -f *.o $(EXE)
