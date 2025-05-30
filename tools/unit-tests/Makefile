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
CFLAGS+=-DUNIT_TEST -DWOLFSSL_USER_SETTINGS
LDFLAGS+=-fprofile-arcs
LDFLAGS+=-ftest-coverage
WOLFCRYPT=../../lib/wolfssl/
WOLFPKCS11=../../lib/wolfPKCS11/



TESTS:=unit-parser unit-extflash unit-aes128 unit-aes256 unit-chacha20 unit-pci \
	   unit-mock-state unit-sectorflags unit-image unit-nvm unit-nvm-flagshome \
	   unit-enc-nvm unit-enc-nvm-flagshome unit-delta unit-update-flash \
	   unit-update-ram unit-pkcs11_store

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
		WOLFBOOT_SECTOR_SIZE=0x400 ./$$unit || exit 1; \
	done


WOLFCRYPT_SRC:=$(WOLFCRYPT)/wolfcrypt/src/sha.c \
               $(WOLFCRYPT)/wolfcrypt/src/sha256.c \
               $(WOLFCRYPT)/wolfcrypt/src/sp_int.c \
               $(WOLFCRYPT)/wolfcrypt/src/sp_c64.c \
               $(WOLFCRYPT)/wolfcrypt/src/random.c \
               $(WOLFCRYPT)/wolfcrypt/src/memory.c

unit-aes128:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES128
unit-aes256:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES256
unit-chacha20:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA
unit-parser:CFLAGS+=-DNVM_FLASH_WRITEONCE
unit-nvm:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS
unit-nvm-flagshome:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DFLAGS_HOME
unit-enc-nvm:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DEXT_ENCRYPTED \
	-DENCRYPT_WITH_CHACHA -DEXT_FLASH -DHAVE_CHACHA
unit-enc-nvm:WOLFCRYPT_SRC+=$(WOLFCRYPT)/wolfcrypt/src/chacha.c
unit-enc-nvm-flagshome:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS \
	-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DEXT_FLASH -DHAVE_CHACHA -DFLAGS_HOME
unit-enc-nvm-flagshome:WOLFCRYPT_SRC+=$(WOLFCRYPT)/wolfcrypt/src/chacha.c
unit-delta:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DDELTA_UPDATES -DDELTA_BLOCK_SIZE=512
unit-pkcs11_store:CFLAGS+=-I$(WOLFPKCS11) -DMOCK_PARTITIONS -DMOCK_KEYVAULT -DSECURE_PKCS11
unit-update-flash:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT
unit-update-ram:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP


WOLFCRYPT_CFLAGS+=-DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_SIGN_ECC256 -DHAVE_ECC_KEY_IMPORT -D__WOLFBOOT



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
	gcc -o $@ $< $(CFLAGS) -DWOLFBOOT_USE_PCI $(LDFLAGS)

unit-mock-state: ../../include/target.h unit-mock-state.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-sectorflags: ../../include/target.h unit-sectorflags.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-image:  unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ $^ $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-nvm: ../../include/target.h unit-nvm.c
	gcc -o $@ unit-nvm.c $(CFLAGS) $(LDFLAGS)

unit-nvm-flagshome: ../../include/target.h unit-nvm.c
	gcc -o $@ unit-nvm.c $(CFLAGS) $(LDFLAGS)

unit-enc-nvm: ../../include/target.h unit-enc-nvm.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-enc-nvm.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-enc-nvm-flagshome: ../../include/target.h unit-enc-nvm.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-enc-nvm.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-delta: ../../include/target.h unit-delta.c
	gcc -o $@ unit-delta.c $(CFLAGS) $(LDFLAGS)

unit-update-flash: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c ../../lib/wolfssl/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-ram: ../../include/target.h unit-update-ram.c
	gcc -o $@ unit-update-ram.c ../../src/image.c ../../lib/wolfssl/wolfcrypt/src/sha256.c  $(CFLAGS) $(LDFLAGS)

unit-pkcs11_store: ../../include/target.h unit-pkcs11_store.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-pkcs11_store.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)


covclean:
	rm -f *.gcov *.gcno *.gcda coverage.*

clean: covclean
	rm -f $(TESTS) *.o *.gcno *.gcda coverage.*

.PHONY: FORCE
