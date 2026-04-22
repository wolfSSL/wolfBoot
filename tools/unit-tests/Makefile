LDFLAGS=-lcheck -lm -pthread

UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
	LDFLAGS+=-lrt -lsubunit
endif

# Default library paths (can be overridden)
WOLFBOOT_LIB_WOLFSSL?=../../lib/wolfssl
WOLFBOOT_LIB_WOLFPKCS11?=../../lib/wolfPKCS11
WOLFBOOT_LIB_WOLFPSA?=../../lib/wolfPSA
WOLFBOOT_LIB_WOLFTPM?=../../lib/wolfTPM

ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFPSA)),)
WOLFBOOT_LIB_WOLFPSA=../../../external-libs/wolfPSA
endif
ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFTPM)),)
WOLFBOOT_LIB_WOLFTPM=../../../external-libs/wolfTPM
endif

# Convert to absolute paths for standalone usage
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))
WOLFBOOT_LIB_WOLFPKCS11:=$(abspath $(WOLFBOOT_LIB_WOLFPKCS11))
WOLFBOOT_LIB_WOLFPSA:=$(abspath $(WOLFBOOT_LIB_WOLFPSA))
WOLFBOOT_LIB_WOLFTPM:=$(abspath $(WOLFBOOT_LIB_WOLFTPM))

CFLAGS=-I. -I../../src -I../../include -I$(WOLFBOOT_LIB_WOLFSSL)
CFLAGS+=-g -ggdb
CFLAGS+=-fprofile-arcs
CFLAGS+=-ftest-coverage
CFLAGS+=--coverage
CFLAGS+=-DUNIT_TEST_COVERAGE
CFLAGS+=-DUNIT_TEST -DWOLFSSL_USER_SETTINGS
LDFLAGS+=-fprofile-arcs
LDFLAGS+=-ftest-coverage

ASAN?=0
ifeq ($(ASAN),1)
	CFLAGS+=-fsanitize=address -fno-omit-frame-pointer -O1
	LDFLAGS+=-fsanitize=address
endif




TESTS:=unit-parser unit-fdt unit-extflash unit-string unit-spi-flash unit-aes128 \
       unit-aes256 unit-chacha20 unit-pci unit-mock-state unit-sectorflags \
       unit-max-space \
       unit-image unit-image-rsa unit-nvm unit-nvm-flagshome unit-enc-nvm \
       unit-enc-nvm-flagshome unit-delta unit-update-flash unit-update-flash-delta \
       unit-update-flash-self-update \
       unit-update-flash-enc unit-update-ram unit-update-ram-nofixed unit-pkcs11_store unit-psa_store unit-disk \
       unit-update-disk unit-multiboot unit-boot-x86-fsp unit-loader-tpm-init unit-qspi-flash unit-fwtpm-stub unit-tpm-rsa-exp \
       unit-image-nopart unit-image-sha384 unit-image-sha3-384 unit-store-sbrk \
       unit-tpm-blob unit-policy-create unit-policy-sign unit-rot-auth unit-sdhci-response-bits \
       unit-sdhci-disk-unaligned unit-sign-encrypted-output
TESTS+=unit-tpm-check-rot-auth

include unit-sign-encrypted-output.mkfrag

all: $(TESTS)

cov:
	rm -f unit-sign-encrypted-output-*
	gcovr -f "^\.\.\/\.\.\/src.*\.c" -r ../.. --verbose \
		--exclude-directories 'tools/keytools$$' \
		--gcov-exclude '.*tools/keytools/.*' \
		--gcov-exclude '.*unit-sign-encrypted-output-.*' \
		--merge-mode-functions merge-use-line-0 \
		--html-medium-threshold 60 \
		--html-high-threshold 80 \
		--html-details coverage.html
	firefox coverage.html

run: $(TESTS)
	for unit in $(TESTS); do \
		WOLFBOOT_SECTOR_SIZE=0x400 ./$$unit || exit 1; \
	done


WOLFCRYPT_SRC:=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha.c \
               $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
               $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.c \
               $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c64.c \
               $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.c \
               $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c

unit-aes128:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES128
unit-aes256:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES256
unit-chacha20:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA
unit-parser:CFLAGS+=-DNVM_FLASH_WRITEONCE
unit-fdt:CFLAGS+=-DWOLFBOOT_FDT
unit-nvm:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS
unit-nvm-flagshome:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DFLAGS_HOME
unit-enc-nvm:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DEXT_ENCRYPTED \
	-DENCRYPT_WITH_CHACHA -DEXT_FLASH -DHAVE_CHACHA
unit-enc-nvm:WOLFCRYPT_SRC+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c
unit-enc-nvm-flagshome:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS \
	-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DEXT_FLASH -DHAVE_CHACHA -DFLAGS_HOME
unit-enc-nvm-flagshome:WOLFCRYPT_SRC+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c
unit-delta:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DDELTA_UPDATES -DDELTA_BLOCK_SIZE=512
unit-pkcs11_store:CFLAGS+=-I$(WOLFBOOT_LIB_WOLFPKCS11) -DMOCK_PARTITIONS -DMOCK_KEYVAULT -DSECURE_PKCS11 -DWOLFPKCS11_USER_SETTINGS
unit-psa_store:CFLAGS+=-I$(WOLFBOOT_LIB_WOLFPSA) -DMOCK_PARTITIONS -DMOCK_KEYVAULT -DWOLFCRYPT_TZ_PSA
unit-update-flash:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-flash-delta:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT \
	-DDELTA_UPDATES -DDELTA_BLOCK_SIZE=512 -D__WOLFBOOT \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-flash-self-update:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT \
	-DRAM_CODE -DARCH_SIM -DUNIT_TEST_SELF_UPDATE_ONLY \
	-DARCH_FLASH_OFFSET=MOCK_ADDRESS_BOOT -DWOLFBOOT_VERSION=7 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-ram:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-ram-nofixed:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN \
	-DUNIT_TEST_AUTH -DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH \
	-DPART_UPDATE_EXT -DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT \
	-DNO_XIP -DWOLFBOOT_NO_PARTITIONS -DUNIT_TEST_NO_FIXED_PARTITIONS \
	-DWOLFBOOT_RAMBOOT_MAX_SIZE=WOLFBOOT_PARTITION_SIZE \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT \
	-DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-disk:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-string:CFLAGS+=-fno-builtin


WOLFCRYPT_CFLAGS+=-DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_SIGN_ECC256 -DHAVE_ECC_KEY_IMPORT -D__WOLFBOOT



../../include/target.h: FORCE
	cp -f target.h $@

unit-extflash.o: FORCE
	rm -f $@
	gcc -c -o $@ unit-extflash.c $(CFLAGS)

unit-parser: ../../include/target.h unit-parser.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-fdt: ../../include/target.h unit-fdt.c ../../src/fdt.c
	gcc -o $@ $^ $(CFLAGS) -ffunction-sections -fdata-sections $(LDFLAGS) \
		-Wl,--gc-sections

unit-extflash: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-spi-flash: ../../include/target.h unit-spi-flash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-qspi-flash: ../../include/target.h unit-qspi-flash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-tpm-rsa-exp: ../../include/target.h unit-tpm-rsa-exp.c ../../src/string.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_TPM_VERIFY -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-tpm-check-rot-auth: ../../include/target.h unit-tpm-check-rot-auth.c ../../src/string.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_TPM_VERIFY -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-fwtpm-stub: ../../include/target.h unit-fwtpm-stub.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS -ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections

unit-tpm-blob: ../../include/target.h unit-tpm-blob.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_TPM_SEAL -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-policy-create: ../../include/target.h unit-policy-create.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c
	gcc -o $@ $^ -I../tpm $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-policy-sign: ../../include/target.h unit-policy-sign.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c
	gcc -o $@ $^ -I../tpm $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_HASH_SHA256 \
		-DHAVE_ECC_KEY_IMPORT \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-sign-encrypted-output: ../../include/target.h unit-sign-encrypted-output.c \
		$(KEYTOOLS_SIGN_SRCS)
	gcc -o $@ $^ -I../keytools $(CFLAGS) -DML_DSA_LEVEL=2 \
		-D"LMS_LEVELS=1" -D"LMS_HEIGHT=10" -D"LMS_WINTERNITZ=8" \
		-DWOLFBOOT_XMSS_PARAMS=\"XMSS-SHA2_10_256\" \
		-ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections
unit-rot-auth: ../../include/target.h unit-rot-auth.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c
	gcc -o $@ $^ -I../tpm $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-store-sbrk: unit-store-sbrk.c ../../src/store_sbrk.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-string: ../../include/target.h unit-string.c
	gcc -o $@ $^ $(CFLAGS) -DDEBUG_UART -DPRINTF_ENABLED $(LDFLAGS)

unit-max-space: ../../include/target.h unit-max-space.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-update-flash-self-update: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
	$(CFLAGS) $(LDFLAGS)

unit-sdhci-response-bits: ../../include/target.h unit-sdhci-response-bits.c
	gcc -o $@ $^ $(CFLAGS) -ffunction-sections -fdata-sections $(LDFLAGS) \
		-Wl,--gc-sections

unit-sdhci-disk-unaligned: ../../include/target.h unit-sdhci-disk-unaligned.c
	gcc -o $@ $^ $(CFLAGS) -ffunction-sections -fdata-sections $(LDFLAGS) \
		-Wl,--gc-sections

unit-aes128: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-aes256: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-chacha20: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-pci:  unit-pci.c ../../src/pci.c
	gcc -o $@ $< $(CFLAGS) -DWOLFBOOT_USE_PCI $(LDFLAGS)

unit-boot-x86-fsp: ../../include/target.h unit-boot-x86_fsp.c
	gcc -o $@ $^ $(CFLAGS) -DWOLFBOOT_LOAD_BASE=0x100000 -DWOLFBOOT_FSP \
		-DUCODE0_ADDRESS=0 -ffunction-sections -fdata-sections $(LDFLAGS) \
		-Wl,--gc-sections

unit-loader-tpm-init: ../../include/target.h unit-loader-tpm-init.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_LOADER_MAIN -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS \
		-DWOLFBOOT_HOOK_PANIC -DWOLFBOOT_SIGN_ECC256 \
		-DWOLFBOOT_HASH_SHA256 -ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections

unit-mock-state: ../../include/target.h unit-mock-state.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-sectorflags: ../../include/target.h unit-sectorflags.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-image:  unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ $^ $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-image-nopart: ../../include/target.h unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DWOLFBOOT_NO_PARTITIONS -DMOCK_PARTITIONS \
		-DWOLFBOOT_RAMBOOT_MAX_SIZE=0x1000 $(LDFLAGS)

unit-image-sha384: ../../include/target.h unit-image.c unit-common.c
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.c \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DUNIT_IMAGE_KEYHASH_ONLY \
		-DWOLFBOOT_HASH_SHA384 $(LDFLAGS)

unit-image-sha3-384: ../../include/target.h unit-image.c unit-common.c
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.c \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DUNIT_IMAGE_KEYHASH_ONLY \
		-DWOLFBOOT_HASH_SHA3_384 $(LDFLAGS)

unit-image-rsa: CFLAGS += -DWOLFBOOT_SIGN_RSA2048
unit-image-rsa: ../../include/target.h unit-image.c unit-common.c
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) \
		$(CFLAGS) -D__WOLFBOOT $(LDFLAGS)

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
	gcc -o $@ unit-update-flash.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-flash-delta: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c ../../src/delta.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-flash-enc:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DHAVE_CHACHA \
	-DCUSTOM_ENCRYPT_KEY -DUNIT_TEST_FALLBACK_ONLY \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-flash-enc: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c \
	$(CFLAGS) $(LDFLAGS)

unit-update-ram: ../../include/target.h unit-update-ram.c
	gcc -o $@ unit-update-ram.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-ram-nofixed: ../../include/target.h unit-update-ram-nofixed.c
	gcc -o $@ unit-update-ram-nofixed.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-disk: ../../include/target.h unit-update-disk.c
	gcc -o $@ unit-update-disk.c $(CFLAGS) $(LDFLAGS)

unit-pkcs11_store: ../../include/target.h unit-pkcs11_store.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-pkcs11_store.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-psa_store: ../../include/target.h unit-psa_store.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-psa_store.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

gpt-sfdisk-test.h:
	truncate -s 131072 .gpt-tmp.img
	printf 'label: gpt\nfirst-lba: 34\nstart=34, size=67, name="boot"\nstart=101, size=100, name="rootfs"\n' \
		| sfdisk --no-reread --no-tell-kernel .gpt-tmp.img >/dev/null 2>&1
	dd if=.gpt-tmp.img bs=512 count=3 of=gpt_sfdisk_data 2>/dev/null
	(echo '/* Generated — do not edit. Regenerate: make gpt-sfdisk-test.h */'; \
	 xxd -i gpt_sfdisk_data | sed 's/^unsigned/static const unsigned/') > $@
	rm -f .gpt-tmp.img gpt_sfdisk_data

unit-disk: unit-disk.c gpt-sfdisk-test.h
	gcc -o $@ $< $(CFLAGS) $(LDFLAGS)

unit-multiboot: unit-multiboot.c
	gcc -o $@ unit-multiboot.c $(CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)


covclean:
	rm -f *.gcov *.gcno *.gcda coverage.*
	rm -f unit-sign-encrypted-output-*

clean: covclean
	rm -f $(TESTS) *.o *.gcno *.gcda coverage.*

.PHONY: FORCE
