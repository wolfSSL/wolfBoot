-include ../../.config
-include ../../tools/config.mk
-include ../../options.mk

CC=gcc
CFLAGS+=-DBUILD_TOOL -Wall -g -ggdb -I../../include -I../../hal -Wextra

EXE=ufserver

$(EXE): $(EXE).o libwolfboot.o
	$(Q)$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $(@) $(^)

libwolfboot.o: ../../src/libwolfboot.c
	$(Q)$(CC) $(CFLAGS) -c -o $(@) $(^)

test_overflow: test_overflow.c
	$(Q)$(CC) -Wall -g -o $@ $^ -lutil

test: $(EXE) test_overflow
	$(Q)./test_overflow ./$(EXE)

clean:
	$(Q)rm -f *.o $(EXE) test_overflow
