CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -DWOLFSSL_DEBUG -DTFM_TIMING_RESISTANT -DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_HASH_SHA256 -g -ggdb -I../../include -I../../hal -Wextra

EXE=ufserver

$(EXE): $(EXE).o ../../src/libwolfboot.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE) ../../src/libwolfboot.o
