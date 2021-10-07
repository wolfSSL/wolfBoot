all: bmdiff bmpatch
CFLAGS+=-Wall -Werror -Wextra

ifeq ($(HASH),SHA3)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA3_384"
  SIGN_OPTIONS+=--sha3
else
  CFLAGS+=-D"WOLFBOOT_HASH_SHA256"
endif

bmdiff: delta.o bmdiff.o
	gcc -o bmdiff delta.o bmdiff.o

bmpatch: delta.o bmdiff.o
	gcc -o bmpatch delta.o bmdiff.o

lib: delta.o

delta.o:
	gcc -c -o delta.o ../../src/delta.c -I../../include -ggdb $(CFLAGS)

bmdiff.o:
	gcc -c -o bmdiff.o bmdiff.c -I../../include -ggdb $(CFLAGS)

clean:
	rm -f bmpatch bmdiff delta.o

delta-test: FORCE bmdiff bmpatch
	@./bmdiff delta-test/0.txt delta-test/1.txt 0-to-1.patch
	@./bmdiff delta-test/1.txt delta-test/0.txt 1-to-0.patch
	@cp delta-test/0.txt 0p.txt
	@cp delta-test/1.txt 1p.txt
	@./bmpatch 0p.txt 0-to-1.patch
	@./bmpatch 1p.txt 1-to-0.patch
	@diff 0p.txt delta-test/1.txt && echo "Test 0-to-1: OK"
	@diff 1p.txt delta-test/0.txt && echo "Test 1-to-0: OK"
	@rm -f 0-to-1.patch 1-to-0.patch 0p.txt 1p.txt

.PHONY: FORCE
