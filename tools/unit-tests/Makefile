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
WOLFBOOT_LIB_WOLFHSM?=../../lib/wolfHSM

ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFPSA)),)
WOLFBOOT_LIB_WOLFPSA=../../../external-libs/wolfPSA
endif
ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFTPM)),)
WOLFBOOT_LIB_WOLFTPM=../../../external-libs/wolfTPM
endif
ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFHSM)),)
WOLFBOOT_LIB_WOLFHSM=../../../external-libs/wolfHSM
endif

# Convert to absolute paths for standalone usage
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))
WOLFBOOT_LIB_WOLFPKCS11:=$(abspath $(WOLFBOOT_LIB_WOLFPKCS11))
WOLFBOOT_LIB_WOLFPSA:=$(abspath $(WOLFBOOT_LIB_WOLFPSA))
WOLFBOOT_LIB_WOLFTPM:=$(abspath $(WOLFBOOT_LIB_WOLFTPM))
WOLFBOOT_LIB_WOLFHSM:=$(abspath $(WOLFBOOT_LIB_WOLFHSM))

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
       unit-uart-flash \
       unit-aes256 unit-chacha20 unit-pci unit-mock-state unit-sectorflags \
       unit-max-space \
       unit-image unit-image-hybrid unit-image-rsa unit-nvm unit-nvm-flagshome unit-enc-nvm \
       unit-enc-nvm-flagshome unit-delta unit-gzip unit-update-flash unit-update-flash-delta \
       unit-update-flash-hook \
       unit-update-flash-self-update \
       unit-update-flash-enc unit-update-ram unit-update-ram-enc unit-update-ram-enc-nopart unit-update-ram-nofixed unit-update-ram-noramboot unit-update-flash-hwswap unit-pkcs11_store unit-psa_store unit-wolfhsm_flash_hal unit-disk \
       unit-update-disk unit-update-disk-oob unit-multiboot unit-boot-x86-fsp unit-loader-tpm-init unit-qspi-flash unit-fwtpm-stub unit-tpm-rsa-exp \
       unit-image-nopart unit-image-sha384 unit-image-sha3-384 unit-store-sbrk \
       unit-tpm-blob unit-policy-create unit-policy-sign unit-rot-auth unit-sdhci-response-bits \
       unit-sdhci-disk-unaligned unit-sign-encrypted-output \
       unit-keygen-xmss-params
TESTS+=unit-tpm-check-rot-auth
TESTS+=unit-tpm-api-names
TESTS+=unit-fit-gzip unit-fit-nogzip
TESTS+=unit-fit-fpga
TESTS+=unit-mpusize
TESTS+=unit-flash-erase-h7
TESTS+=unit-flash-erase-wb
TESTS+=unit-flash-erase-l0
TESTS+=unit-flash-erase-g0
TESTS+=unit-flash-erase-c0
TESTS+=unit-flash-erase-u3
TESTS+=unit-otp-keystore
TESTS+=unit-x86-paging-oob
TESTS+=unit-fwtpm-nv-oob
TESTS+=unit-elf-bss-guard
TESTS+=unit-arm-tee-psa-ipc
TESTS+=unit-va416x0-fram

# linux_loader.c is x86 32-bit only, so its unit tests need a working 32-bit
# (multilib) toolchain. Probe whether "gcc -m32" can link, and only add the
# tests when it can, so a default "make" stays portable on hosts without 32-bit
# support. Set ENABLE_32BIT_TESTS=1 to force them on (e.g. to fail loudly if the
# toolchain is unexpectedly missing multilib).
HAVE_M32 := $(shell printf 'int main(void){return 0;}' | gcc -m32 -x c - -o /dev/null >/dev/null 2>&1 && echo 1 || echo 0)
ENABLE_32BIT_TESTS ?= $(HAVE_M32)
ifeq ($(ENABLE_32BIT_TESTS),1)
TESTS+=unit-linux-loader-e820
TESTS+=unit-linux-loader-syssize
else
$(info Skipping 32-bit x86 linux-loader unit tests: 'gcc -m32' unavailable (set ENABLE_32BIT_TESTS=1 to force))
endif

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
	python3 unit-sign-delta-tlv.py || exit 1
	python3 unit-sign-delta-cert-inv-off.py || exit 1
	python3 unit-sign-custom-tlv-le.py || exit 1


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
unit-update-flash-hook:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT \
	-DWOLFBOOT_HOOK_BOOT -DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT \
	-DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
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
unit-update-flash-hwswap:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-ram-noramboot:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP -DWOLFBOOT_NO_RAMBOOT \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-ram-enc:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP -DMMU \
	-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DHAVE_CHACHA \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
# Same source as unit-update-ram-enc, but with a WOLFBOOT_RAMBOOT_MAX_SIZE that
# is tighter than the partition cap (NO_PARTITIONS / RAM-load-region flavour),
# so the bound check is exercised through its RAMBOOT_MAX_SIZE branch.
unit-update-ram-enc-nopart:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP -DMMU \
	-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DHAVE_CHACHA \
	-DWOLFBOOT_NO_PARTITIONS -DWOLFBOOT_RAMBOOT_MAX_SIZE=0x1000 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-ram-nofixed:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN \
	-DUNIT_TEST_AUTH -DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH \
	-DPART_UPDATE_EXT -DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT \
	-DNO_XIP -DWOLFBOOT_NO_PARTITIONS -DUNIT_TEST_NO_FIXED_PARTITIONS \
	-DWOLFBOOT_RAMBOOT_MAX_SIZE=WOLFBOOT_PARTITION_SIZE \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT \
	-DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
# Bound the non-FSP disk load to this test's 64-byte load_buffer (TEST_PAYLOAD_SIZE),
# the cap update_disk.c now requires; all images here are exactly that size.
unit-update-disk:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED -DWOLFBOOT_RAMBOOT_MAX_SIZE=0x40 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
# Non-FSP disk-boot OOB regression (CRIT-03). WOLFBOOT_RAMBOOT_MAX_SIZE is the
# cap the loader applies to the unauthenticated header fw_size before loading to
# RAM, the same bound update_disk.c and update_ram.c enforce.
unit-update-disk-oob:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED \
	-DWOLFBOOT_RAMBOOT_MAX_SIZE=0x1000 \
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

unit-uart-flash: ../../include/target.h unit-uart-flash.c ../../src/uart_flash.c
	gcc -o $@ unit-uart-flash.c $(CFLAGS) $(LDFLAGS)

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

unit-tpm-api-names: ../../include/target.h unit-tpm-api-names.c ../../src/string.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-fwtpm-stub: ../../include/target.h unit-fwtpm-stub.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS -ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections

unit-fwtpm-nv-oob: ../../include/target.h unit-fwtpm-nv-oob.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS $(LDFLAGS)

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

unit-keygen-xmss-params: ../../include/target.h unit-keygen-xmss-params.c
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

unit-arm-tee-psa-ipc: ../../include/target.h unit-arm-tee-psa-ipc.c ../../src/arm_tee_psa_ipc.c
	gcc -o $@ unit-arm-tee-psa-ipc.c $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFPSA)/wolfpsa \
		-ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections

unit-va416x0-fram: unit-va416x0-fram.c ../../hal/va416x0.c
	gcc -o $@ unit-va416x0-fram.c $(CFLAGS) $(LDFLAGS)

unit-max-space: ../../include/target.h unit-max-space.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

# unit-mpusize includes src/boot_arm.c directly (guarded to its host-portable
# MPU helpers via WOLFBOOT_UNIT_TEST_MPU), so boot_arm.c is not a separate input.
unit-mpusize: ../../include/target.h unit-mpusize.c
	gcc -o $@ unit-mpusize.c $(CFLAGS) $(LDFLAGS)

# unit-flash-erase-h7 includes hal/stm32h7.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so stm32h7.c is not a separate input.
unit-flash-erase-h7: unit-flash-erase-h7.c ../../hal/stm32h7.c
	gcc -o $@ unit-flash-erase-h7.c $(CFLAGS) $(LDFLAGS)

# unit-flash-erase-wb includes hal/stm32wb.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so stm32wb.c is not a separate input.
unit-flash-erase-wb: unit-flash-erase-wb.c ../../hal/stm32wb.c
	gcc -o $@ unit-flash-erase-wb.c $(CFLAGS) $(LDFLAGS)

# unit-flash-erase-l0 includes hal/stm32l0.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so stm32l0.c is not a separate input.
unit-flash-erase-l0: unit-flash-erase-l0.c ../../hal/stm32l0.c
	gcc -o $@ unit-flash-erase-l0.c $(CFLAGS) $(LDFLAGS)

# unit-flash-erase-g0 includes hal/stm32g0.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so stm32g0.c is not a separate input.
unit-flash-erase-g0: unit-flash-erase-g0.c ../../hal/stm32g0.c
	gcc -o $@ unit-flash-erase-g0.c $(CFLAGS) $(LDFLAGS)

# unit-flash-erase-c0 includes hal/stm32c0.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so stm32c0.c is not a separate input.
unit-flash-erase-c0: unit-flash-erase-c0.c ../../hal/stm32c0.c
	gcc -o $@ unit-flash-erase-c0.c $(CFLAGS) $(LDFLAGS)

# unit-flash-erase-u3 includes hal/stm32u3.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so stm32u3.c is not a separate input.
# -I../../ so that #include "hal/stm32u3.h" resolves from the repo root.
unit-flash-erase-u3: unit-flash-erase-u3.c ../../hal/stm32u3.c ../../hal/stm32u3.h
	gcc -o $@ unit-flash-erase-u3.c -I../../ $(CFLAGS) $(LDFLAGS)

# unit-otp-keystore includes src/flash_otp_keystore.c directly (guarded to its
# host-portable code via WOLFBOOT_UNIT_TEST_OTP_KEYSTORE), so it is not a
# separate input.
unit-otp-keystore: unit-otp-keystore.c ../../src/flash_otp_keystore.c
	gcc -o $@ unit-otp-keystore.c $(CFLAGS) $(LDFLAGS)

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

# linux_loader.c is x86 32bit only and pulls in inline asm guarded on 32bit;
# build standalone with -m32 and without coverage (no 32bit gcov/check libs).
unit-linux-loader-e820: ../../include/target.h unit-linux-loader-e820.c
	gcc -m32 -o $@ unit-linux-loader-e820.c -I. -I../../src -I../../include \
		-g -DUNIT_TEST -DWOLFBOOT_FSP -DUCODE0_ADDRESS=0 \
		-DWOLFBOOT_LOAD_BASE=0x100000

unit-linux-loader-syssize: ../../include/target.h unit-linux-loader-syssize.c
	gcc -m32 -o $@ unit-linux-loader-syssize.c -I. -I../../src -I../../include \
		-g -DUNIT_TEST -DWOLFBOOT_FSP -DUCODE0_ADDRESS=0 \
		-DWOLFBOOT_LOAD_BASE=0x100000

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

unit-image-hybrid: ../../include/target.h unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DUNIT_IMAGE_HYBRID_ONLY \
		-DSIGN_HYBRID -DWOLFBOOT_SIGN_SECONDARY_ECC256 \
		-DIMAGE_HEADER_SIZE=512 $(LDFLAGS)

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

unit-gzip: ../../include/target.h unit-gzip.c
	gcc -o $@ unit-gzip.c $(CFLAGS) -DWOLFBOOT_GZIP $(LDFLAGS)

# FIT-loader gzip / unsupported-compression branch coverage. Built twice
# from the same source: once with WOLFBOOT_GZIP (success + decompress
# failure paths) and once without (compile-time fail-closed path).
unit-fit-gzip: ../../include/target.h unit-fit-gzip.c
	gcc -o $@ unit-fit-gzip.c $(CFLAGS) -DWOLFBOOT_FDT -DWOLFBOOT_GZIP \
		-DWOLFBOOT_NO_PRINTF \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-fit-nogzip: ../../include/target.h unit-fit-gzip.c
	gcc -o $@ unit-fit-gzip.c $(CFLAGS) -DWOLFBOOT_FDT \
		-DWOLFBOOT_NO_PRINTF \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

# FIT fpga-subimage discovery (fit_find_images / fit_get_compatible).
unit-fit-fpga: ../../include/target.h unit-fit-fpga.c
	gcc -o $@ unit-fit-fpga.c $(CFLAGS) -DWOLFBOOT_FDT \
		-DWOLFBOOT_NO_PRINTF \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-update-flash: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-flash-hook: ../../include/target.h unit-update-flash.c
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

unit-update-ram-enc: ../../include/target.h unit-update-ram-enc.c
	gcc -o $@ unit-update-ram-enc.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c \
		$(CFLAGS) $(LDFLAGS)

unit-update-ram-enc-nopart: ../../include/target.h unit-update-ram-enc.c
	gcc -o $@ unit-update-ram-enc.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c \
		$(CFLAGS) $(LDFLAGS)

unit-update-ram-nofixed: ../../include/target.h unit-update-ram-nofixed.c
	gcc -o $@ unit-update-ram-nofixed.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-ram-noramboot: ../../include/target.h unit-update-ram-noramboot.c
	gcc -o $@ unit-update-ram-noramboot.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-flash-hwswap: ../../include/target.h unit-update-flash-hwswap.c
	gcc -o $@ unit-update-flash-hwswap.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-disk: ../../include/target.h unit-update-disk.c
	gcc -o $@ unit-update-disk.c $(CFLAGS) $(LDFLAGS)

unit-update-disk-oob: ../../include/target.h unit-update-disk-oob.c
	gcc -o $@ unit-update-disk-oob.c $(CFLAGS) $(LDFLAGS)

unit-pkcs11_store: ../../include/target.h unit-pkcs11_store.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-pkcs11_store.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-psa_store: ../../include/target.h unit-psa_store.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-psa_store.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

unit-wolfhsm_flash_hal:CFLAGS+=-I$(WOLFBOOT_LIB_WOLFHSM) -DWOLFCRYPT_TZ_WOLFHSM -DWOLFHSM_CFG_NO_SYS_TIME -DMOCK_PARTITIONS
unit-wolfhsm_flash_hal:WOLFCRYPT_SRC:=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c \
                                     $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/misc.c
unit-wolfhsm_flash_hal: ../../include/target.h unit-wolfhsm_flash_hal.c
	gcc -o $@ $(WOLFCRYPT_SRC) unit-wolfhsm_flash_hal.c $(CFLAGS) $(WOLFCRYPT_CFLAGS) $(LDFLAGS)

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

unit-x86-paging-oob: ../../include/target.h unit-x86-paging-oob.c
	gcc -o $@ unit-x86-paging-oob.c $(CFLAGS) \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-elf-bss-guard: unit-elf-bss-guard.c
	gcc -o $@ $< -I../../include -DWOLFBOOT_ELF -DARCH_FLASH_OFFSET=0 \
		-DWOLFBOOT_NO_PRINTF -g $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)


covclean:
	rm -f *.gcov *.gcno *.gcda coverage.*
	rm -f unit-sign-encrypted-output-*

clean: covclean
	rm -f $(TESTS) *.o *.gcno *.gcda coverage.*

.PHONY: FORCE
