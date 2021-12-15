CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -DWOLFSSL_DEBUG -DTFM_TIMING_RESISTANT -DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_HASH_SHA256 -g -ggdb -I../../include -I../../hal -Wextra
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
