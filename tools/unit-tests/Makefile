LDFLAGS=-lcheck -lm -pthread

UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
	LDFLAGS+=-lrt -lsubunit
endif

CFLAGS=-I. -I../../src -I../../include -I../../lib/wolfssl
CFLAGS+=-g -ggdb
CFLAGS+=-fprofile-arcs
CFLAGS+=-ftest-coverage
CFLAGS+=--coverage
LDFLAGS+=-fprofile-arcs
LDFLAGS+=-ftest-coverage
WOLFCRYPT=../../lib/wolfssl/



TESTS:=unit-parser unit-extflash unit-aes128 unit-aes256 unit-chacha20 unit-pci \
	   unit-mock-state unit-sectorflags unit-image

all: $(TESTS)

cov:
	gcovr -f "^\.\.\/\.\.\/src.*\.c" -r ../.. --verbose \
		--merge-mode-functions merge-use-line-0 \
		--html-medium-threshold 60 \
		--html-high-threshold 80 \
		--html-details coverage.html
	firefox coverage.html

run: $(TESTS)
	for unit in $(TESTS); do \
		./$$unit || exit 1; \
	done


unit-aes128:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES128
unit-aes256:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES256
unit-chacha20:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA
unit-parser:CFLAGS+=-DNVM_FLASH_WRITEONCE


WOLFCRYPT_SRC:=$(WOLFCRYPT)/wolfcrypt/src/sha.c \
               $(WOLFCRYPT)/wolfcrypt/src/sha256.c \
               $(WOLFCRYPT)/wolfcrypt/src/sp_int.c \
               $(WOLFCRYPT)/wolfcrypt/src/sp_c64.c \
               $(WOLFCRYPT)/wolfcrypt/src/random.c \
               $(WOLFCRYPT)/wolfcrypt/src/memory.c

WOLFCRYPT_CFLAGS+=-DWOLFSSL_USER_SETTINGS -DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_SIGN_ECC256 -DHAVE_ECC_KEY_IMPORT -DWOLFBOOT_HASH_SHA256 -D__WOLFBOOT



../../include/target.h: FORCE
	cp -f target.h $@

unit-extflash.o: FORCE
	rm -f $@
	gcc -c -o $@ unit-extflash.c $(CFLAGS)

unit-parser: ../../include/target.h unit-parser.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-extflash: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-aes128: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-aes256: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-chacha20: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-pci:  unit-pci.c ../../src/pci.c
	gcc -o $@ $< $(CFLAGS) $(LDFLAGS)

unit-mock-state: ../../include/target.h unit-mock-state.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-sectorflags: ../../include/target.h unit-sectorflags.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-image:  unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ $^ $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)


covclean:
	rm -f *.gcov *.gcno *.gcda coverage.*

clean: covclean
	rm -f $(TESTS) *.o *.gcno *.gcda coverage.*

.PHONY: FORCE
