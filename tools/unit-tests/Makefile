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
WOLFBOOT_LIB_WOLFCOSE?=../../lib/wolfCOSE

ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFPSA)),)
WOLFBOOT_LIB_WOLFPSA=../../../external-libs/wolfPSA
endif
ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFTPM)),)
WOLFBOOT_LIB_WOLFTPM=../../../external-libs/wolfTPM
endif
ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFHSM)),)
WOLFBOOT_LIB_WOLFHSM=../../../external-libs/wolfHSM
endif
ifeq ($(wildcard $(WOLFBOOT_LIB_WOLFCOSE)),)
WOLFBOOT_LIB_WOLFCOSE=../../../external-libs/wolfCOSE
endif

# Convert to absolute paths for standalone usage
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))
WOLFBOOT_LIB_WOLFPKCS11:=$(abspath $(WOLFBOOT_LIB_WOLFPKCS11))
WOLFBOOT_LIB_WOLFPSA:=$(abspath $(WOLFBOOT_LIB_WOLFPSA))
WOLFBOOT_LIB_WOLFTPM:=$(abspath $(WOLFBOOT_LIB_WOLFTPM))
WOLFBOOT_LIB_WOLFHSM:=$(abspath $(WOLFBOOT_LIB_WOLFHSM))
WOLFBOOT_LIB_WOLFCOSE:=$(abspath $(WOLFBOOT_LIB_WOLFCOSE))

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




TESTS:=unit-parser unit-parser-large-header unit-fdt unit-extflash unit-string \
       unit-spi-flash unit-aes128 \
       unit-uart-flash \
       unit-aes256 unit-chacha20 unit-pci unit-pci-4gib unit-mock-state \
       unit-sectorflags unit-max-space \
       unit-image unit-image-hybrid unit-image-rsa unit-nvm unit-nvm-flagshome unit-enc-nvm \
       unit-enc-nvm-flagshome unit-delta unit-gzip unit-update-flash unit-update-flash-delta \
       unit-update-flash-hook \
       unit-update-flash-self-update \
       unit-update-flash-enc unit-update-flash-enc-full unit-update-ram unit-update-ram-uboot unit-update-ram-enc unit-update-ram-enc-nopart unit-update-ram-nofixed unit-update-ram-noramboot unit-update-flash-hwswap unit-pkcs11_store unit-psa_store unit-wolfhsm_flash_hal unit-disk \
       unit-update-disk unit-update-disk-fsp unit-update-disk-oob unit-update-disk-fit unit-multiboot unit-boot-x86-fsp unit-loader-tpm-init unit-qspi-flash unit-fwtpm-stub unit-tpm-rsa-exp \
       unit-image-nopart unit-image-sha384 unit-image-sha3-384 unit-image-dts \
       unit-image-dts-sha384 unit-image-dts-sha3-384 unit-store-sbrk \
       unit-tpm-blob unit-policy-create unit-policy-sign unit-rot-auth unit-sdhci-response-bits \
       unit-sdhci-disk-unaligned unit-sdhci-dma-error unit-cm4-sdhci unit-cm4-rauc-slot \
       unit-sign-encrypted-output \
       unit-sign-hybrid-keyload \
       unit-sign-header-failure \
       unit-keygen-xmss-params
TESTS+=unit-tpm-check-rot-auth
TESTS+=unit-tpm-api-names
TESTS+=unit-tpm-nsc-cert
TESTS+=unit-tpm-advio-zeroize
TESTS+=unit-tpm-mfgid-eh-zeroize
TESTS+=unit-pkcs11-nsc-zeroize
TESTS+=unit-pkcs11-pin-zeroize
TESTS+=unit-ubootenv
TESTS+=unit-diagnostics
TESTS+=unit-diagnostics-256
TESTS+=unit-fit-gzip unit-fit-nogzip
TESTS+=unit-fit-fpga
TESTS+=unit-mpusize
TESTS+=unit-flash-erase-h7
TESTS+=unit-flash-erase-wb
TESTS+=unit-flash-erase-l0
TESTS+=unit-flash-erase-g0
TESTS+=unit-flash-erase-c0
TESTS+=unit-flash-erase-u3
TESTS+=unit-flash-erase-mcxw
TESTS+=unit-otp-keystore
TESTS+=unit-otp-keystore-gen-zeroize
TESTS+=unit-x86-paging-oob
TESTS+=unit-ahci-unlock-panic
TESTS+=unit-ata-security-passphrase-zeroize
TESTS+=unit-fwtpm-nv-oob
TESTS+=unit-elf-bss-guard
TESTS+=unit-elf-entry-inplace
TESTS+=unit-elf-mmu-fail
TESTS+=unit-image-elf-scatter
TESTS+=unit-arm-tee-psa-ipc
TESTS+=unit-dice-token-size
TESTS+=unit-dice-token-nosign
TESTS+=unit-va416x0-fram
TESTS+=unit-flash-write-cc26x2
TESTS+=unit-p1021-fcm-bytes
TESTS+=unit-ls1028a-xspi-write
TESTS+=unit-zynq-erase-loop
TESTS+=unit-zynq-ext-write
TESTS+=unit-versal-qspi-dma
TESTS+=unit-versal-ext-write
TESTS+=unit-t10xx-qe-firmware
TESTS+=unit-t10xx-flash-status
TESTS+=unit-p1021-erase-advance
TESTS+=unit-p1021-read-badblock
TESTS+=unit-kontron-tgl-spi
TESTS+=unit-samr21-erase-advance
TESTS+=unit-hifive1-flash-write
TESTS+=unit-rp2350-flash-write
TESTS+=unit-fwtpm-rsp-overrun
TESTS+=unit-fwtpm-cmd-toctou
TESTS+=unit-fdt-memrsv-wrap
TESTS+=unit-aurix-erased-fill
TESTS+=unit-aurix-erased-fill-invert
TESTS+=unit-t2080-fman-loader
TESTS+=unit-ecc-raw-der
TESTS+=unit-stm32g4-write
TESTS+=unit-stm32l4-write
TESTS+=unit-stm32wb-write
TESTS+=unit-stm32l5-write
TESTS+=unit-stm32u5-write
TESTS+=unit-nvm-cache-scrub
TESTS+=unit-update-trigger-scrub
TESTS+=unit-sdhci-uhs-recover
TESTS+=unit-sdhci-wait-busy
TESTS+=unit-sdhci-acmd41-timeout
TESTS+=unit-ti-hercules-write
TESTS+=unit-p1021-qe-firmware
TESTS+=unit-t10xx-dts-memac
TESTS+=unit-fs-probe
TESTS+=unit-fs-malicious
TESTS+=unit-update-disk-fs unit-update-disk-fs-enc

# The filesystem interop tests build their fixtures with the host's real
# mkfs tools, so they are only added when those tools exist. The hostile
# input coverage lives in unit-fs-malicious, which needs no tools and is
# therefore unconditional.
HAVE_MKFS_VFAT := $(shell command -v mkfs.vfat 2>/dev/null)
HAVE_MCOPY     := $(shell command -v mcopy 2>/dev/null)
HAVE_MKE2FS    := $(shell command -v mke2fs 2>/dev/null)
ifneq ($(and $(HAVE_MKFS_VFAT),$(HAVE_MCOPY)),)
TESTS+=unit-fat32
else
$(warning Skipping unit-fat32: mkfs.vfat and/or mtools unavailable)
endif
ifneq ($(HAVE_MKE2FS),)
TESTS+=unit-ext4
else
$(warning Skipping unit-ext4: mke2fs unavailable)
endif

# The x86-64 EFI unit test needs the gnu-efi development headers (same
# dependency as the CMake x86_64_efi target). Probe and only add the test
# when they are installed, so a default "make" stays portable on hosts
# without them.
HAVE_GNU_EFI := $(wildcard /usr/include/efi/efi.h)
ENABLE_EFI_TESTS ?= $(if $(HAVE_GNU_EFI),1,0)
ifeq ($(ENABLE_EFI_TESTS),1)
TESTS+=unit-efi-x86-open-image
else
$(info Skipping x86-64 EFI unit test: gnu-efi headers not found (set ENABLE_EFI_TESTS=1 to force))
endif

TESTS+=unit-flash-write-mcxa
TESTS+=unit-flash-write-nrf52
TESTS+=unit-flash-write-samr21
TESTS+=unit-flash-write-same51
TESTS+=unit-imx-rt-cache-align
TESTS+=unit-xspi-tfd-index
TESTS+=unit-pic32-pfswap

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
TESTS+=unit-sama5d3-ext-read
else
$(info Skipping 32-bit x86 unit tests (linux-loader, sama5d3-ext-read): 'gcc -m32' unavailable (set ENABLE_32BIT_TESTS=1 to force))
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
	python3 unit-sign-delta-basehash-cleanup.py || exit 1
	python3 unit-delta-sector-align.py || exit 1
	python3 unit-elf-scatter-db-build.py || exit 1
	python3 unit-image-wolfhsm-client-build.py || exit 1
	python3 unit-x86-fsp-stage1auth-build.py || exit 1
	python3 unit-sign-custom-tlv-le.py || exit 1
	python3 unit-sign-custom-tlv-large.py || exit 1
	python3 unit-sign-custom-tlv-pubkey-der.py || exit 1
	python3 unit-sign-dts.py || exit 1


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
unit-parser-large-header:CFLAGS+=-DNVM_FLASH_WRITEONCE
unit-fdt:CFLAGS+=-DWOLFBOOT_FDT
unit-nvm:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS
unit-nvm-flagshome:CFLAGS+=-DNVM_FLASH_WRITEONCE -DMOCK_PARTITIONS -DFLAGS_HOME
unit-diagnostics:CFLAGS+=-DMOCK_PARTITIONS
unit-diagnostics-256:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_DIAGNOSTICS_RECORD_SIZE=32
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
# Intentionally misaligned config: the mock WOLFBOOT_SECTOR_SIZE (0x400) is
# not a multiple of DELTA_BLOCK_SIZE (1536). Must fail to build with the
# delta alignment #error. Used by unit-delta-sector-align.py only, so it is
# not part of $(TESTS).
unit-update-flash-delta-misalign:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT \
	-DDELTA_UPDATES -DDELTA_BLOCK_SIZE=1536 -D__WOLFBOOT \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
# Compile-only check for the DISABLE_BACKUP + WOLFBOOT_ELF_FLASH_SCATTER +
# EXT_FLASH combination: the ELF restore block in wolfBoot_update() must
# compile (it used to pass a struct where PART_IS_EXT expects a pointer).
# Used by unit-elf-scatter-db-build.py only, so it is not part of $(TESTS).
unit-update-flash-elf-scatter-db:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT -DPART_SWAP_EXT \
	-DWOLFBOOT_ELF_FLASH_SCATTER -DWOLFBOOT_ELF -DIMAGE_HEADER_SIZE=256 \
	-DDISABLE_BACKUP -D__WOLFBOOT \
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
# Same RAMBOOT config as unit-update-ram, plus WOLFBOOT_UBOOT_LEGACY to exercise
# the uImage ih_load payload relocation in wolfBoot_start().
unit-update-ram-uboot:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DPART_BOOT_EXT -DWOLFBOOT_DUALBOOT -DNO_XIP -DWOLFBOOT_UBOOT_LEGACY \
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
unit-update-disk-fs-enc:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED \
	-DWOLFBOOT_RAMBOOT_MAX_SIZE=0x1000 -DMAX_DISKS=1 \
	-DWOLFBOOT_DISK_FS -DWOLFBOOT_FAT32 -DWOLFBOOT_EXT4 \
	-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DHAVE_CHACHA \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT \
	-DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-disk-fs:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED \
	-DWOLFBOOT_RAMBOOT_MAX_SIZE=0x1000 -DMAX_DISKS=1 \
	-DWOLFBOOT_DISK_FS -DWOLFBOOT_FAT32 -DWOLFBOOT_EXT4 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT \
	-DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-disk-oob:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED \
	-DWOLFBOOT_RAMBOOT_MAX_SIZE=0x1000 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
# FIT (flattened uImage tree) exits of the encrypted disk loader. The panic hook
# is what lets the test observe the key material at the instant wolfBoot_panic()
# is entered, since on target that call never returns.
unit-update-disk-fit:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED -DWOLFBOOT_FDT \
	-DWOLFBOOT_HOOK_PANIC -DWOLFBOOT_RAMBOOT_MAX_SIZE=0x40 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
# Regression coverage for wolfBoot_check_flash_image_elf() (scattered-ELF
# integrity check). WOLFBOOT_NO_SIGN keeps this to the hashing path only (no
# signature verification is exercised by that function).
unit-image-elf-scatter:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DWOLFBOOT_ELF_FLASH_SCATTER -DWOLFBOOT_ELF \
	-DIMAGE_HEADER_SIZE=256
unit-string:CFLAGS+=-fno-builtin


WOLFCRYPT_CFLAGS+=-DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_SIGN_ECC256 -DHAVE_ECC_KEY_IMPORT -D__WOLFBOOT



../../include/target.h: FORCE
	cp -f target.h $@

unit-extflash.o: FORCE
	rm -f $@
	gcc -c -o $@ unit-extflash.c $(CFLAGS)

unit-parser: ../../include/target.h unit-parser.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-parser-large-header: ../../include/target.h unit-parser-large-header.c
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

unit-tpm-nsc-cert: ../../include/target.h unit-tpm-nsc-cert.c ../../src/string.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 -D__ARM_FEATURE_CMSE=3U -DCSME_NSE_API= \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

# The PKCS#11 NSC veneers are exercised here through C_CreateObject_nsc_call
# and C_DeriveKey_nsc_call only; --gc-sections drops the remaining veneers so
# just those two wolfPKCS11 entry points need a stub.
unit-pkcs11-nsc-zeroize: ../../include/target.h unit-pkcs11-nsc-zeroize.c
	gcc -o $@ unit-pkcs11-nsc-zeroize.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/misc.c \
		$(CFLAGS) -I$(WOLFBOOT_LIB_WOLFPKCS11) -DSECURE_PKCS11 \
		-DWOLFPKCS11_USER_SETTINGS -DWOLFCRYPT_SECURE_MODE \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

# The pin-zeroize test pulls libwolfboot.c into the test translation
# unit; --gc-sections drops the unused boot paths so only the crypto
# init/deinit dependency set must link.
unit-pkcs11-pin-zeroize: ../../include/target.h unit-pkcs11-pin-zeroize.c
	gcc -o $@ unit-pkcs11-pin-zeroize.c $(CFLAGS) -DMOCK_PARTITIONS \
		-DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH -DWOLFBOOT_HASH_SHA256 \
		-DPRINTF_ENABLED -DEXT_FLASH -I$(WOLFBOOT_LIB_WOLFPKCS11) \
		-DSECURE_PKCS11 -DWOLFPKCS11_USER_SETTINGS -ffunction-sections \
		-fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-fwtpm-stub: ../../include/target.h unit-fwtpm-stub.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS -ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections

unit-fwtpm-nv-oob: ../../include/target.h unit-fwtpm-nv-oob.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS $(LDFLAGS)

# unit-fwtpm-rsp-overrun: a response capacity below the 10-byte minimum
# response must not be overrun by the fwTPM processor (F-11043); the
# veneer processes into a staging buffer and copies only what fits.
unit-fwtpm-rsp-overrun: ../../include/target.h unit-fwtpm-rsp-overrun.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS $(LDFLAGS)

# unit-fwtpm-cmd-toctou: a midflight NS rewrite of the command buffer
# must not change what the processor executes (F-11044); the veneer
# stages the command in secure memory before processing.
unit-fwtpm-cmd-toctou: ../../include/target.h unit-fwtpm-cmd-toctou.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) \
		-DWOLFTPM_USER_SETTINGS $(LDFLAGS)

# unit-fdt-memrsv-wrap: layout validation in front of fdt_add_mem_rsv()
# (F-11045). Links the real parser rather than extracting one function.
unit-fdt-memrsv-wrap:CFLAGS+=-DWOLFBOOT_FDT
unit-fdt-memrsv-wrap: unit-fdt-memrsv-wrap.c ../../src/fdt.c
	gcc -o $@ unit-fdt-memrsv-wrap.c ../../src/fdt.c -I../../include \
		$(CFLAGS) $(LDFLAGS)

unit-tpm-blob: ../../include/target.h unit-tpm-blob.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFBOOT_TPM_SEAL -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-tpm-advio-zeroize: ../../include/target.h unit-tpm-advio-zeroize.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFTPM_ADV_IO \
		-DWOLFTPM_CHECK_WAIT_STATE -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-tpm-mfgid-eh-zeroize: ../../include/target.h unit-tpm-mfgid-eh-zeroize.c
	gcc -o $@ $^ $(CFLAGS) -I$(WOLFBOOT_LIB_WOLFTPM) -DWOLFBOOT_TPM \
		-DWOLFTPM_USER_SETTINGS -DWOLFTPM_MFG_IDENTITY \
		-DWOLFBOOT_TPM_MFG_AUTH_DERIVE -DWOLFBOOT_SIGN_RSA2048 \
		-DWOLFBOOT_HASH_SHA256 -D__ARM_FEATURE_CMSE=3U -DCSME_NSE_API= \
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

unit-sign-hybrid-keyload: ../../include/target.h unit-sign-hybrid-keyload.c \
		$(KEYTOOLS_SIGN_SRCS)
	gcc -o $@ $^ -I../keytools $(CFLAGS) -DML_DSA_LEVEL=2 -DDELTA_UPDATES \
		-D"LMS_LEVELS=1" -D"LMS_HEIGHT=10" -D"LMS_WINTERNITZ=8" \
		-DWOLFBOOT_XMSS_PARAMS=\"XMSS-SHA2_10_256\" \
		-ffunction-sections -fdata-sections \
		$(LDFLAGS) -Wl,--gc-sections

unit-sign-header-failure: ../../include/target.h unit-sign-header-failure.c \
		$(KEYTOOLS_SIGN_SRCS)
	gcc -o $@ $^ -I../keytools $(CFLAGS) -DML_DSA_LEVEL=2 -DDELTA_UPDATES \
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

DICE_TOKEN_TEST_CFLAGS=-I$(WOLFBOOT_LIB_WOLFCOSE)/include \
	-I$(WOLFBOOT_LIB_WOLFSSL) -DUNIT_TEST_AUTH -DWOLFBOOT_SIGN_ECC256 \
	-DWOLFCRYPT_SECURE_MODE -DHAVE_HKDF -DWOLFBOOT_NO_PARTITIONS \
	-DWOLFCOSE_LEAN -DWOLFCOSE_ENABLE_EXT_SIGN \
	-ffunction-sections -fdata-sections
unit-dice-token-size:CFLAGS+=$(DICE_TOKEN_TEST_CFLAGS)
unit-dice-token-nosign:CFLAGS+=$(DICE_TOKEN_TEST_CFLAGS) -DWOLFBOOT_NO_SIGN
DICE_TOKEN_TEST_SRC=unit-dice-token-size.c \
	$(WOLFBOOT_LIB_WOLFCOSE)/src/wolfcose.c \
	$(WOLFBOOT_LIB_WOLFCOSE)/src/wolfcose_cbor.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hash.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c
unit-dice-token-size unit-dice-token-nosign: ../../include/target.h \
	$(DICE_TOKEN_TEST_SRC)
	gcc -o $@ $(DICE_TOKEN_TEST_SRC) $(CFLAGS) $(LDFLAGS) \
		-Wl,--gc-sections

unit-va416x0-fram: unit-va416x0-fram.c ../../hal/va416x0.c
	gcc -o $@ unit-va416x0-fram.c $(CFLAGS) $(LDFLAGS)

unit-xspi-tfd-index: unit-xspi-tfd-index.c
	gcc -o $@ unit-xspi-tfd-index.c $(CFLAGS) -I../../hal $(LDFLAGS)

unit-pic32-pfswap: unit-pic32-pfswap.c
	gcc -o $@ unit-pic32-pfswap.c $(CFLAGS) $(LDFLAGS)

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

# unit-flash-erase-mcxw includes hal/mcxw.c directly (guarded to hal_flash_erase
# via WOLFBOOT_UNIT_TEST_FLASH_ERASE), so mcxw.c is not a separate input and the
# (not vendored) NXP MCUXpresso SDK headers are not needed. The erase command
# takes a uint32_t flash address as a pointer, which is only a narrowing cast on
# the 64-bit host.
unit-flash-erase-mcxw: unit-flash-erase-mcxw.c ../../hal/mcxw.c
	gcc -o $@ unit-flash-erase-mcxw.c -Wno-int-to-pointer-cast \
		$(CFLAGS) $(LDFLAGS)

# unit-otp-keystore includes src/flash_otp_keystore.c directly (guarded to its
# host-portable code via WOLFBOOT_UNIT_TEST_OTP_KEYSTORE), so it is not a
# separate input.
unit-otp-keystore: unit-otp-keystore.c ../../src/flash_otp_keystore.c
	gcc -o $@ unit-otp-keystore.c $(CFLAGS) $(LDFLAGS)

# unit-otp-keystore-gen-zeroize includes the host tool
# tools/keytools/otp/otp-keystore-gen.c directly (via #define main) and
# interposes read()/malloc()/free() (dlsym RTLD_NEXT, hence -ldl) to observe
# the OTP buffer and UDS stack array at free() time.
unit-otp-keystore-gen-zeroize: unit-otp-keystore-gen-zeroize.c \
		../keytools/otp/otp-keystore-gen.c unit-keystore.c
	gcc -o $@ unit-otp-keystore-gen-zeroize.c unit-keystore.c \
		-DFLASH_OTP_KEYSTORE $(CFLAGS) $(LDFLAGS) -ldl

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

unit-sdhci-dma-error: ../../include/target.h unit-sdhci-dma-error.c
	gcc -o $@ $^ $(CFLAGS) -ffunction-sections -fdata-sections $(LDFLAGS) \
		-Wl,--gc-sections

unit-cm4-sdhci: ../../include/target.h unit-cm4-sdhci.c
	gcc -o $@ $^ $(CFLAGS) -I../.. -ffunction-sections -fdata-sections $(LDFLAGS) \
		-Wl,--gc-sections

unit-cm4-rauc-slot: ../../include/target.h unit-cm4-rauc-slot.c ../../src/ubootenv.c
	gcc -o $@ unit-cm4-rauc-slot.c ../../src/ubootenv.c $(CFLAGS) -I../.. \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-ubootenv: unit-ubootenv.c ../../src/ubootenv.c
	gcc -o $@ $< $(CFLAGS) -I../.. $(LDFLAGS)

unit-aes128: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-aes256: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-chacha20: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-pci:  unit-pci.c ../../src/pci.c
	gcc -o $@ $< $(CFLAGS) -DWOLFBOOT_USE_PCI $(LDFLAGS)

# unit-pci-4gib reruns the same test file with the MMIO pool ending
# exactly at the 4 GiB boundary ([0xC0000000, 0x100000000)), which a
# 32-bit pool limit cannot represent. Only the pool-end test runs.
unit-pci-4gib:  unit-pci.c ../../src/pci.c
	gcc -o $@ $< $(CFLAGS) -DWOLFBOOT_USE_PCI \
	    -DUNIT_TEST_PCI_POOL_4GIB \
	    -DPCI_MMIO32_BASE=0xC0000000ULL \
	    -DPCI_MMIO32_LENGTH=0x40000000ULL \
	    -DPCI_MMIO32_PREFETCH_BASE=0x80000000ULL \
	    -DPCI_MMIO32_PREFETCH_LENGTH=0x40000000ULL \
	    $(LDFLAGS)

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

# Compile-only check for the STAGE1_AUTH variant of boot_x86_fsp.c. The real
# stage1 authentication build needs an i686 toolchain the unit test CI does
# not have, so this keeps the variant from silently rotting. Used by
# unit-x86-fsp-stage1auth-build.py only, so it is not part of $(TESTS).
unit-boot-x86-fsp-stage1auth: ../../include/target.h unit-boot-x86_fsp.c FORCE
	gcc -c -o /dev/null unit-boot-x86_fsp.c $(CFLAGS) \
		-DWOLFBOOT_LOAD_BASE=0x100000 -DWOLFBOOT_FSP -DUCODE0_ADDRESS=0 \
		-DSTAGE1_AUTH

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

# Exercises the raw-DTB authentication helper wolfBoot_verify_dts_digest()
# (Fenrir #7998). WOLFBOOT_FDT compiles the DTS helpers in image.c, which pull
# in fdt.c for wolfBoot_get_dts_size(). The sha384/sha3-384 variants cover the
# corresponding wolfBoot_hash_buffer() branches used by real MMU targets.
unit-image-dts: ../../include/target.h unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) ../../src/fdt.c \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DWOLFBOOT_FDT $(LDFLAGS)

unit-image-dts-sha384: ../../include/target.h unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) ../../src/fdt.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.c \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DWOLFBOOT_FDT -DUNIT_IMAGE_DTS_ONLY \
		-DWOLFBOOT_HASH_SHA384 $(LDFLAGS)

unit-image-dts-sha3-384: ../../include/target.h unit-image.c unit-common.c $(WOLFCRYPT_SRC)
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) ../../src/fdt.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.c \
		$(CFLAGS) $(WOLFCRYPT_CFLAGS) -DWOLFBOOT_FDT -DUNIT_IMAGE_DTS_ONLY \
		-DWOLFBOOT_HASH_SHA3_384 $(LDFLAGS)

unit-image-rsa: CFLAGS += -DWOLFBOOT_SIGN_RSA2048
unit-image-rsa: ../../include/target.h unit-image.c unit-common.c
	gcc -o $@ unit-image.c unit-common.c $(WOLFCRYPT_SRC) \
		$(CFLAGS) -D__WOLFBOOT $(LDFLAGS)

unit-nvm: ../../include/target.h unit-nvm.c
	gcc -o $@ unit-nvm.c $(CFLAGS) $(LDFLAGS)

unit-nvm-flagshome: ../../include/target.h unit-nvm.c
	gcc -o $@ unit-nvm.c $(CFLAGS) $(LDFLAGS)

unit-diagnostics: ../../include/target.h unit-diagnostics.c
	gcc -o $@ unit-diagnostics.c $(CFLAGS) $(LDFLAGS)

unit-diagnostics-256: ../../include/target.h unit-diagnostics.c
	gcc -o $@ unit-diagnostics.c $(CFLAGS) $(LDFLAGS)

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

unit-update-flash-delta-misalign: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c ../../src/delta.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-flash-elf-scatter-db: ../../include/target.h unit-update-flash.c FORCE
	gcc -c -o /dev/null unit-update-flash.c $(CFLAGS)

unit-image-wolfhsm-client-build:CFLAGS+=-I$(WOLFBOOT_LIB_WOLFHSM) \
	-DWOLFHSM_CFG_NO_SYS_TIME -DMOCK_PARTITIONS -DWOLFBOOT_HASH_SHA256 \
	-DWOLFBOOT_SIGN_ECC256 -DWOLFBOOT_ENABLE_WOLFHSM_CLIENT \
	-DIMAGE_HEADER_SIZE=256 -D__WOLFBOOT
unit-image-wolfhsm-client-build: ../../include/target.h ../../src/image.c FORCE
	gcc -c -o /dev/null ../../src/image.c $(CFLAGS)

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

# Same encrypted target without UNIT_TEST_FALLBACK_ONLY, so the full
# end-to-end suite (forward updates, rollback, ...) runs against the
# encrypted swap and its IV derivation.
unit-update-flash-enc-full:CFLAGS+=-DMOCK_PARTITIONS -DWOLFBOOT_NO_SIGN -DUNIT_TEST_AUTH \
	-DWOLFBOOT_HASH_SHA256 -DPRINTF_ENABLED -DEXT_FLASH -DPART_UPDATE_EXT \
	-DPART_SWAP_EXT -DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA -DHAVE_CHACHA \
	-DCUSTOM_ENCRYPT_KEY \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT -DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-flash-enc-full: ../../include/target.h unit-update-flash.c
	gcc -o $@ unit-update-flash.c ../../src/image.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c \
	$(CFLAGS) $(LDFLAGS)

unit-update-ram: ../../include/target.h unit-update-ram.c
	gcc -o $@ unit-update-ram.c ../../src/image.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

unit-update-ram-uboot: ../../include/target.h unit-update-ram-uboot.c
	gcc -o $@ unit-update-ram-uboot.c ../../src/image.c ../../src/gpt.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c $(CFLAGS) $(LDFLAGS)

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

# WOLFBOOT_FSP (x86) boot path of update_disk.c: the low-memory (tolum)
# size check must reject a slot and fall back to the other one, like every
# other per-slot rejection in the retry loop.
unit-update-disk-fsp:CFLAGS+=-DMOCK_PARTITIONS -DPRINTF_ENABLED -DWOLFBOOT_FSP \
	-DUCODE0_ADDRESS=0 -DWOLFBOOT_LOAD_BASE=0x100000 \
	-DWOLFBOOT_ORIGIN=MOCK_ADDRESS_BOOT \
	-DBOOTLOADER_PARTITION_SIZE=WOLFBOOT_PARTITION_SIZE
unit-update-disk-fsp: ../../include/target.h unit-update-disk-fsp.c
	gcc -o $@ unit-update-disk-fsp.c $(CFLAGS) $(LDFLAGS)

unit-update-disk-oob: ../../include/target.h unit-update-disk-oob.c
	gcc -o $@ unit-update-disk-oob.c $(CFLAGS) $(LDFLAGS)

# Disk loader reading its image from a file. Mocks only the raw block
# layer, so the real gpt/disk/disk_fs/fat32/ext4 stack runs underneath.
unit-update-disk-fs: ../../include/target.h unit-update-disk-fs.c
	gcc -o $@ unit-update-disk-fs.c $(CFLAGS) $(LDFLAGS)

# Same source with disk encryption compiled in. The mock cipher's keystream
# depends on the byte's position in the image, so this pins the fact that a
# file's byte stream lines up with the CTR counter exactly as a raw
# partition's does -- a wrong IV or offset would decrypt to garbage.
unit-update-disk-fs-enc: ../../include/target.h unit-update-disk-fs.c
	gcc -o $@ unit-update-disk-fs.c $(CFLAGS) $(LDFLAGS)

unit-update-disk-fit: ../../include/target.h unit-update-disk-fit.c
	gcc -o $@ unit-update-disk-fit.c $(CFLAGS) $(LDFLAGS)

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

# Backend-independent half of the read-only filesystem layer. Built with
# neither WOLFBOOT_FAT32 nor WOLFBOOT_EXT4, so it also pins the behaviour
# of a DISK_FS build with no parser selected: everything probes as raw.
unit-fs-probe: ../../include/target.h unit-fs-probe.c
	gcc -o $@ unit-fs-probe.c $(CFLAGS) -DWOLFBOOT_DISK_FS $(LDFLAGS)

# Hostile-input coverage for every parser bound. Builds all of its
# metadata in C, so unlike the interop suites it is never skipped.
unit-fs-malicious: ../../include/target.h unit-fs-malicious.c
	gcc -o $@ unit-fs-malicious.c $(CFLAGS) -DWOLFBOOT_DISK_FS \
		-DWOLFBOOT_FAT32 -DWOLFBOOT_EXT4 $(LDFLAGS)

# A conformant FAT32 volume needs at least 65525 clusters, so even with
# one 512-byte sector per cluster the smallest valid image is about 33 MB.
# That is far too large to embed as a C array the way gpt-sfdisk-test.h
# does, so the image stays a file and the test reads it through a
# file-backed mock disk_read().
fat32-test.img:
	rm -f $@ $@.tmp fat32-payload.bin fat32-nested.bin fat32-short.bin
	truncate -s 40M $@.tmp
	mkfs.vfat -F 32 -s 1 -S 512 -n WBTEST $@.tmp >/dev/null
	head -c 1048576 /dev/urandom > fat32-payload.bin
	head -c 4096 /dev/urandom > fat32-nested.bin
	head -c 777 /dev/urandom > fat32-short.bin
	MTOOLS_SKIP_CHECK=1 mmd -i $@.tmp ::/boot
	MTOOLS_SKIP_CHECK=1 mmd -i $@.tmp ::/boot/sub
	MTOOLS_SKIP_CHECK=1 mcopy -i $@.tmp fat32-payload.bin \
		::/boot/a-very-long-image-name-for-lfn.itb
	MTOOLS_SKIP_CHECK=1 mcopy -i $@.tmp fat32-nested.bin \
		::/boot/sub/nested.bin
	MTOOLS_SKIP_CHECK=1 mcopy -i $@.tmp fat32-short.bin ::/SHORT.BIN
	# 60 characters, so five LFN entries. Names of 53-64 characters fit
	# WOLFBOOT_FS_MAX_NAME but need the fifth entry, which is the band a
	# too-eager ordinal bound silently drops.
	MTOOLS_SKIP_CHECK=1 mcopy -i $@.tmp fat32-nested.bin \
		"::/boot/a-deliberately-long-filename-that-needs-five-lfn-entries.itb"
	mv $@.tmp $@

unit-fat32: ../../include/target.h unit-fat32.c fat32-test.img
	gcc -o $@ unit-fat32.c $(CFLAGS) -DWOLFBOOT_DISK_FS -DWOLFBOOT_FAT32 \
		$(LDFLAGS)

# Two ext4 fixtures. The first uses the smallest feature set the parser
# accepts; the second uses mke2fs's stock ext4 profile, which on a current
# e2fsprogs sets 64bit + metadata_csum + flex_bg -- that one is what proves
# a plain "mkfs.ext4" volume is readable. "mke2fs -d" populates from a
# directory with no root privileges and no loop mount (e2fsprogs >= 1.43).
ext4-fixtures.stamp:
	rm -rf .ext4-root ext4-min.img ext4-stock.img
	mkdir -p .ext4-root/boot/sub
	( head -c 4096 /dev/urandom; head -c 61440 /dev/zero; \
	  head -c 262144 /dev/urandom; head -c 131072 /dev/zero; \
	  head -c 589824 /dev/urandom ) > ext4-payload.bin
	head -c 4096 /dev/urandom > ext4-nested.bin
	cp ext4-payload.bin .ext4-root/boot/image.itb
	cp ext4-nested.bin .ext4-root/boot/sub/nested.bin
	mke2fs -q -F -t ext4 -O ^64bit,^metadata_csum -b 1024 -I 128 \
		-L WBEXT1 -d .ext4-root ext4-min.img 16384 2>/dev/null
	mke2fs -q -F -t ext4 -b 4096 -L WBEXT2 -d .ext4-root \
		ext4-stock.img 8192 2>/dev/null
	rm -rf .ext4-root
	touch $@

unit-ext4: ../../include/target.h unit-ext4.c ext4-fixtures.stamp
	gcc -o $@ unit-ext4.c $(CFLAGS) -DWOLFBOOT_DISK_FS -DWOLFBOOT_EXT4 \
		$(LDFLAGS)

unit-multiboot: unit-multiboot.c
	gcc -o $@ unit-multiboot.c $(CFLAGS) $(LDFLAGS)

unit-x86-paging-oob: ../../include/target.h unit-x86-paging-oob.c
	gcc -o $@ unit-x86-paging-oob.c $(CFLAGS) \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-ahci-unlock-panic: ../../include/target.h unit-ahci-unlock-panic.c
	gcc -o $@ unit-ahci-unlock-panic.c $(CFLAGS) \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

unit-ata-security-passphrase-zeroize: ../../include/target.h unit-ata-security-passphrase-zeroize.c
	gcc -o $@ unit-ata-security-passphrase-zeroize.c $(CFLAGS) \
		-ffunction-sections -fdata-sections $(LDFLAGS) -Wl,--gc-sections

# unit-flash-write-cc26x2 includes hal/cc26x2.c directly, with cc26x2_ti_stub/
# standing in for the (not vendored) TI CC26x2 SDK headers it includes. This
# is also the only build coverage hal/cc26x2.c has.
unit-flash-write-cc26x2: ../../include/target.h unit-flash-write-cc26x2.c ../../hal/cc26x2.c
	gcc -o $@ unit-flash-write-cc26x2.c -Icc26x2_ti_stub $(CFLAGS) $(LDFLAGS)

# unit-p1021-fcm-bytes drives the real FCM byte-copy helpers from
# hal/nxp_p1021.c (PowerPC). The P1021 FCM helpers are plain memory
# copies, so the HAL is compiled on the host: the PowerPC-asm accessors
# from nxp_ppc.h are shadowed inside the test, and nxp_p1021_host.c
# (generated below) is identical to hal/nxp_p1021.c except that the three
# bare __asm__ cache-barrier statements (DDR init / multi-core boot) are
# blanked out, as they cannot assemble on x86.
nxp_p1021_host.c: ../../hal/nxp_p1021.c
	sed -e 's/^[[:space:]]*__asm__[[:space:]]*__volatile__("[^"]*");[[:space:]]*$$//' $< > $@

unit-p1021-fcm-bytes: unit-p1021-fcm-bytes.c nxp_p1021_host.c
	gcc -o $@ unit-p1021-fcm-bytes.c -DTARGET_nxp_p1021 -I../../hal $(CFLAGS) $(LDFLAGS)

# unit-ls1028a-xspi-write drives the real XSPI write/erase path from
# hal/nxp_ls1028a.c. The driver is plain C register access apart from a
# few bare `asm volatile("...")` barrier statements (DDR init, delay), so
# nxp_ls1028a_host.c (generated below) is identical to the HAL file
# except those statements are blanked out: they cannot assemble on x86.
# The test rebases XSPI_BASE onto host memory and emulates the FlexSPI
# controller (including the NOR write-enable latch) at the
# XSPI_IPCMD_START() boundary.
nxp_ls1028a_host.c: ../../hal/nxp_ls1028a.c
	sed -e 's/^[[:space:]]*asm volatile("[^"]*");[[:space:]]*$$//' \
	    -e 's/\*(volatile uint32_t \*)(addr) = val;/xspi_emu_tfd_write(addr, val);/' \
	    $< > $@

unit-ls1028a-xspi-write: unit-ls1028a-xspi-write.c nxp_ls1028a_host.c
	gcc -o $@ unit-ls1028a-xspi-write.c -DTARGET_nxp_ls1028a -I../../hal $(CFLAGS) $(LDFLAGS)

# unit-zynq-erase-loop runs the real ext_flash_erase() from hal/zynq.c
# against emulated qspi_* functions. zynq.c needs the Xilinx SDK headers
# (board build tree) and cannot be compiled on the host, but the bug
# under test is entirely in this one function, so it is extracted
# verbatim by sed into zynq_erase_extract.h.
zynq_erase_extract.h: ../../hal/zynq.c
	sed -n '/^int RAMFUNCTION ext_flash_erase(/,/^}/p' $< > $@

unit-zynq-erase-loop: unit-zynq-erase-loop.c zynq_erase_extract.h
	gcc -o $@ unit-zynq-erase-loop.c $(CFLAGS) $(LDFLAGS)

# unit-zynq-ext-write runs the real ext_flash_write() from hal/zynq.c
# (page chunks crossed physical NOR page boundaries and the
# wrap clobbered the page start). Same extraction approach as the
# erase test; the emulated NOR models the wrap.
zynq_write_extract.h: ../../hal/zynq.c
	sed -n '/^int RAMFUNCTION ext_flash_write(/,/^}/p' $< > $@

unit-zynq-ext-write: unit-zynq-ext-write.c zynq_write_extract.h
	gcc -o $@ unit-zynq-ext-write.c $(CFLAGS) $(LDFLAGS)

# unit-sama5d3-ext-read runs the real ext_flash_read() from hal/sama5d3.c
# (the intra-page offset of the start address was never applied, sub-page
# reads were copied in 32-bit words, and a multi-page read ending
# mid-page wrote a full page past the caller's buffer). The function and
# the nand_flash geometry struct are extracted verbatim; the test provides
# host div_u()/mod() and emulated nand_read_page()/nand_check_bad_block()
# backed by a byte array.
sama5d3_read_extract.h: ../../hal/sama5d3.c
	sed -n '/^struct nand_flash {/,/^} nand_flash = { 0 };/p' $< > $@
	sed -n '/^int ext_flash_read(/,/^}/p' $< >> $@

unit-sama5d3-ext-read: unit-sama5d3-ext-read.c sama5d3_read_extract.h
	gcc -o $@ unit-sama5d3-ext-read.c $(CFLAGS) $(LDFLAGS)

# unit-versal-qspi-dma drives the real DMA RX path of qspi_transfer() in
# hal/versal.c (an unaligned read larger than the 4096-byte
# temp buffer copied the full requested length out of the buffer). The
# generated host copies differ from the real files only in:
#   - ARM asm statements blanked (cannot assemble on x86; the cache
#     maintenance helpers become no-ops, which is harmless because the
#     test emulates the DMA);
#   - VERSAL_QSPI_BASE rebased onto host memory (register offsets kept);
#   - the real qspi_dma_wait() renamed to qspi_dma_wait_hw() so the test
#     can provide a qspi_dma_wait() that performs the DMA move;
# The test is built with -DTARGET_versal -DEXT_FLASH -DARCH_AARCH64
# -DARCH_64BIT (the file is guarded by TARGET_versal, the QSPI section by
# EXT_FLASH, and the architecture check / hal.h prototypes by the ARCH
# defines).
# Only operand-less asm (barriers) may be deleted outright. An asm with
# an output operand must be replaced by an assignment, or the generated
# copy reads an uninitialised automatic -- undefined behaviour, and at
# -O0 a guaranteed infinite loop in the timer helpers.
versal_host.h: ../../hal/versal.h
	sed -E -e 's/0xF1030000UL/((uintptr_t)g_vqspi_regs)/' \
	    -e 's/__asm__ volatile\("mrs %0, CurrentEL" : "=r" \(el\)\);/el = 3UL << 2; \/* host stands in at EL3 *\//' \
	    -e 's/^[[:space:]]*__asm__[[:space:]]+(__)?(volatile__|volatile)\("[^"]*"\);[[:space:]]*//' \
	    -e 's/^[[:space:]]*__asm__[[:space:]]+(__)?(volatile__|volatile)\("[^"]*"[[:space:]]*:[[:space:]]*:[^;]*\);[[:space:]]*//' $< > $@
	test -s $@
	@! grep -q '__asm__' $@ || { echo "$@: un-handled asm (an output operand needs an explicit rewrite above)"; grep -n '__asm__' $@; false; }

# See versal_host.h above for why the two "mrs" reads are rewritten
# rather than deleted. The host counter advances one tick per read at a
# nominal 1 MHz, so hal_delay_us() terminates instead of spinning.
versal_host.c: ../../hal/versal.c versal_host.h
	sed -E -e 's|#include "hal/versal.h"|#include "versal_host.h"\nstatic uint64_t versal_host_ticks;|' \
	    -e 's/__asm__ volatile\("mrs %0, cntpct_el0" : "=r" \(cntpct\)\);/cntpct = ++versal_host_ticks;/' \
	    -e 's/__asm__ volatile\("mrs %0, cntfrq_el0" : "=r" \(cntfrq\)\);/cntfrq = 1000000ULL;/' \
	    -e 's/^static int qspi_dma_wait\(void\)$$/static int qspi_dma_wait_hw(void)/' \
	    -e 's/^[[:space:]]*__asm__[[:space:]]+(__)?(volatile__|volatile)\("[^"]*"\);[[:space:]]*//' \
	    -e 's/^[[:space:]]*__asm__[[:space:]]+(__)?(volatile__|volatile)\("[^"]*"[[:space:]]*:[[:space:]]*:[^;]*\);[[:space:]]*//' $< > $@
	test -s $@
	@! grep -q '__asm__' $@ || { echo "$@: un-handled asm (an output operand needs an explicit rewrite above)"; grep -n '__asm__' $@; false; }

unit-versal-qspi-dma: unit-versal-qspi-dma.c versal_host.c
	gcc -o $@ unit-versal-qspi-dma.c -DTARGET_versal -DEXT_FLASH -DARCH_AARCH64 -DARCH_64BIT $(CFLAGS) $(LDFLAGS)

# unit-versal-ext-write runs the real ext_flash_write() from
# hal/versal.c (page chunks crossed physical NOR page
# boundaries and the wrap clobbered the page start). The QspiDev_t
# struct and the function are extracted verbatim (the qspi_* helpers
# are file-static, so the test provides emulated ones); the emulated
# NOR models the wrap.
versal_qspidev_extract.h: ../../hal/versal.c
	sed -n '/^\/\* QSPI device structure \*\//,/^} QspiDev_t;/p' $< > $@

versal_ext_write_extract.h: ../../hal/versal.c
	sed -n '/^int ext_flash_write(/,/^}/p' $< > $@

unit-versal-ext-write: unit-versal-ext-write.c versal_qspidev_extract.h versal_ext_write_extract.h versal_host.h
	gcc -o $@ unit-versal-ext-write.c -DEXT_FLASH -DARCH_AARCH64 -DARCH_64BIT $(CFLAGS) $(LDFLAGS)

# unit-t10xx-qe-firmware runs the real qe_check_firmware() from
# hal/nxp_t10xx.c (the per-microcode code_offset was never
# bounded against the declared image, so a self-consistent blob could
# make the upload copy arbitrary memory into QE IRAM). The qe_*
# structures and the check function are extracted verbatim; the check
# path needs no register access. The four qe_* structs end where the
# check function's comment begins (a range cannot stop at the 4th
# '} QE_PACKED;').
t10xx_qe_firmware_extract.h: ../../hal/nxp_t10xx.c
	sed -n '/^\/\* Structure packing \*\//,/^\/\* Checks for valid QE firmware \*\//p' $< > $@
	sed -n '/^static int qe_check_firmware(/,/^}/p' $< >> $@

unit-t10xx-qe-firmware: unit-t10xx-qe-firmware.c t10xx_qe_firmware_extract.h
	gcc -o $@ unit-t10xx-qe-firmware.c -DWOLFBOOT_NO_PRINTF $(CFLAGS) $(LDFLAGS)

# unit-aurix-erased-fill runs the real cacheSector() and
# programBytesToErasedFlash() from hal/aurix_tc3xx.c (the
# uint32_t page buffers were filled with the byte-valued
# FLASH_BYTE_ERASED, so default-polarity builds got 0x000000FF words
# instead of the erased 0xFFFFFFFF). The tc3_flash_* API is emulated
# on a host flash array; the same source is built for both flag
# polarities and the assertions key off FLASH_BYTE_ERASED.
aurix_erased_extract.h: ../../hal/aurix_tc3xx.c
	sed -n '/^static void RAMFUNCTION cacheSector(uint32_t sectorAddress)/,/^}/p' $< > $@
	sed -n '/^static int RAMFUNCTION programBytesToErasedFlash/,/^}/p' $< >> $@

unit-aurix-erased-fill: unit-aurix-erased-fill.c aurix_erased_extract.h
	gcc -o $@ unit-aurix-erased-fill.c $(CFLAGS) $(LDFLAGS)

unit-aurix-erased-fill-invert: unit-aurix-erased-fill.c aurix_erased_extract.h
	gcc -o $@ unit-aurix-erased-fill.c -DWOLFBOOT_FLAGS_INVERT $(CFLAGS) $(LDFLAGS)

# unit-t2080-fman-loader runs the real hal_fman_init() from
# hal/nxp_t2080.c (the loader checked only the QEF magic and
# then copied attacker-shaped offsets/counts into FMan IRAM). The qe_*
# structures and the function are extracted verbatim; the test owns the
# NOR image (FMAN slot at a nonzero bank offset so the remaining-extent
# check is exercised) and the FMan IRAM registers. The four qe_*
# structs end where hal_fman_init() begins (a range cannot stop at
# the 4th '} QE_PACKED;'); sed '$d' trims the function signature.
t2080_fman_extract.h: ../../hal/nxp_t2080.c
	sed -n '/^\/\* Reuse QE firmware structures/,/^static int hal_fman_init(/p' $< | sed '$$d' > $@
	sed -n '/^static int hal_fman_init(void)/,/^}/p' $< >> $@

unit-t2080-fman-loader: unit-t2080-fman-loader.c t2080_fman_extract.h
	gcc -o $@ unit-t2080-fman-loader.c $(CFLAGS) $(LDFLAGS)

# unit-stm32g4-write runs the real hal_flash_write() from hal/stm32g4.c
# (F-11023: the double-word fast path was selected on "len - i > 3" but
# consumed eight bytes, so an aligned 4-7 byte tail over-read the
# caller's buffer and over-programmed flash). Same harness as the
# STM32L5/STM32U5 twins; the g4 helpers are static and un-prefixed.
stm32g4_write_extract.h: ../../hal/stm32g4.c
	sed -n '/^static RAMFUNCTION void flash_wait_complete/,/^}/p' $< > $@
	sed -n '/^static void RAMFUNCTION flash_clear_errors/,/^}/p' $< >> $@
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< >> $@

unit-stm32g4-write: unit-stm32g4-write.c stm32g4_write_extract.h
	gcc -o $@ unit-stm32g4-write.c $(CFLAGS) $(LDFLAGS)

# unit-stm32l4-write runs the real hal_flash_write() from hal/stm32l4.c
# (F-12062: the double-word fast path was selected on "len - i > 3" but
# consumed eight bytes, so an aligned 4-7 byte tail over-read the
# caller's buffer and over-programmed flash). Same harness as the
# STM32G4 twin (F-11023); the l4 helpers are static and un-prefixed.
stm32l4_write_extract.h: ../../hal/stm32l4.c
	sed -n '/^static RAMFUNCTION void flash_wait_complete/,/^}/p' $< > $@
	sed -n '/^static void RAMFUNCTION flash_clear_errors/,/^}/p' $< >> $@
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< >> $@

unit-stm32l4-write: unit-stm32l4-write.c stm32l4_write_extract.h
	gcc -o $@ unit-stm32l4-write.c $(CFLAGS) $(LDFLAGS)

# unit-stm32wb-write runs the real hal_flash_write() from hal/stm32wb.c
# (F-12063: the double-word fast path was selected on "len - i > 3" but
# consumed eight bytes, so an aligned 4-7 byte tail over-read the
# caller's buffer and over-programmed flash). Same harness as the
# STM32G4/STM32L4 twins; the wb helpers are static and un-prefixed.
stm32wb_write_extract.h: ../../hal/stm32wb.c
	sed -n '/^static RAMFUNCTION void flash_wait_complete/,/^}/p' $< > $@
	sed -n '/^static void RAMFUNCTION flash_clear_errors/,/^}/p' $< >> $@
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< >> $@

unit-stm32wb-write: unit-stm32wb-write.c stm32wb_write_extract.h
	gcc -o $@ unit-stm32wb-write.c $(CFLAGS) $(LDFLAGS)

# unit-t10xx-flash-status runs the real hal_flash_write()/hal_flash_erase()
# and hal_flash_status_wait() from hal/nxp_t10xx.c against a mock QPI
# status model (F-11033: a timed-out program/erase used to report
# success because the wait result was discarded).
t10xx_flash_status_extract.h: ../../hal/nxp_t10xx.c
	sed -n '/^static void hal_flash_unlock_sector/,/^}/p' $< > $@
	sed -n '/^static int hal_flash_status_wait/,/^}/p' $< >> $@
	sed -n '/^int hal_flash_write/,/^}/p' $< >> $@
	sed -n '/^int hal_flash_erase/,/^}/p' $< >> $@

unit-t10xx-flash-status: unit-t10xx-flash-status.c t10xx_flash_status_extract.h
	gcc -o $@ unit-t10xx-flash-status.c $(CFLAGS) $(LDFLAGS)

# unit-p1021-erase-advance runs the real ext_flash_erase() from
# hal/nxp_p1021.c against mocked ELBC access (F-11034: the loop never
# advanced the address, re-erasing the first block of the range).
p1021_erase_extract.h: ../../hal/nxp_p1021.c
	sed -n '/#define ELBC_BASE /p' $< > $@
	sed -n '/#define ELBC_MDR /p' $< >> $@
	sed -n '/#define ELBC_FIR /p' $< >> $@
	sed -n '/#define ELBC_FCR /p' $< >> $@
	sed -n '/#define ELBC_FBCR /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP(/p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_PA /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CM0 /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CM2 /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CW1 /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_RS /p' $< >> $@
	sed -n '/#define ELBC_FCR_CMD(/p' $< >> $@
	sed -n '/#define NAND_CMD_STATUS /p' $< >> $@
	sed -n '/#define NAND_CMD_BLOCK_ERASE1 /p' $< >> $@
	sed -n '/#define NAND_CMD_BLOCK_ERASE2 /p' $< >> $@
	sed -n '/#define FLASH_PAGE_SIZE /p' $< >> $@

p1021_erase_fn_extract.h: ../../hal/nxp_p1021.c
	sed -n '/^int ext_flash_erase/,/^}/p' $< > $@

unit-p1021-erase-advance: unit-p1021-erase-advance.c p1021_erase_extract.h \
	p1021_erase_fn_extract.h
	gcc -o $@ unit-p1021-erase-advance.c $(CFLAGS) $(LDFLAGS)

# unit-p1021-read-badblock runs the real ext_flash_read() from
# hal/nxp_p1021.c against a simulated NAND (F-12064: the bad-block
# marker counter was kept for the whole request, so only the first
# two pages read were checked, and a skip rewound the position but
# not the output pointer).
p1021_read_extract.h: ../../hal/nxp_p1021.c
	sed -n '/#define ELBC_BASE /p' $< > $@
	sed -n '/#define ELBC_MDR /p' $< >> $@
	sed -n '/#define ELBC_FIR /p' $< >> $@
	sed -n '/#define ELBC_FCR /p' $< >> $@
	sed -n '/#define ELBC_FBCR /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP(/p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_PA /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CA /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CM0 /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CM1 /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_CW0 /p' $< >> $@
	sed -n '/#define ELBC_FIR_OP_RBW /p' $< >> $@
	sed -n '/#define ELBC_FCR_CMD(/p' $< >> $@
	sed -n '/#define NAND_CMD_READA /p' $< >> $@
	sed -n '/#define NAND_CMD_READSTART /p' $< >> $@
	sed -n '/#define FLASH_PAGE_SIZE /p' $< >> $@

p1021_read_fn_extract.h: ../../hal/nxp_p1021.c
	sed -n '/^int ext_flash_read(uintptr_t address, uint8_t \*data, int len)$$/,/^}/p' $< > $@

unit-p1021-read-badblock: unit-p1021-read-badblock.c p1021_read_extract.h \
	p1021_read_fn_extract.h
	gcc -o $@ unit-p1021-read-badblock.c $(CFLAGS) $(LDFLAGS)

# unit-kontron-tgl-spi runs the real tgl_lock_bios_region() from
# hal/kontron_vx3060_s2.c against mocked PCI config space and an
# MMIO array at the SPI BAR address (F-12104: the lock was never
# applied - no hal_flash_protect() override - and targeted the
# wrong register space).
kontron_spi_extract.h: ../../hal/kontron_vx3060_s2.c
	sed -n '/^#define SPI_PCI_DEV/,/^#define SPI_FLOCKDN/p' $< > $@

kontron_spi_fn_extract.h: ../../hal/kontron_vx3060_s2.c
	sed -n '/^int tgl_lock_bios_region/,/^}/p' $< > $@
	sed -n '/^int hal_flash_protect/,/^}/p' $< >> $@

unit-kontron-tgl-spi: unit-kontron-tgl-spi.c kontron_spi_extract.h \
	kontron_spi_fn_extract.h
	gcc -o $@ unit-kontron-tgl-spi.c $(CFLAGS) $(LDFLAGS)

# unit-samr21-erase-advance runs the real hal_flash_erase() from
# hal/samr21.c against a host NVMCTRL register window (F-11036: the
# length decrement was the body of the NVMREADY wait and the address
# never advanced, so the loop re-erased the first page forever).
samr21_erase_extract.h: ../../hal/samr21.c
	sed -n '/#define FLASH_PAGESIZE /p' $< > $@
	sed -n '/#define FLASH_ROW_SIZE /p' $< >> $@
	sed -n '/#define NVMCTRLA_REG /p' $< >> $@
	sed -n '/#define NVMCTRL_INTFLAG /p' $< >> $@
	sed -n '/#define NVMCTRL_ADDR /p' $< >> $@
	sed -n '/#define NVMCMD_KEY /p' $< >> $@
	sed -n '/#define NVMCMD_ERASE /p' $< >> $@
	sed -n '/#define NVMCTRL_INTFLAG_NVMREADY /p' $< >> $@

samr21_erase_fn_extract.h: ../../hal/samr21.c
	sed -n '/^int RAMFUNCTION hal_flash_erase/,/^}/p' $< > $@

unit-samr21-erase-advance: unit-samr21-erase-advance.c samr21_erase_extract.h \
	samr21_erase_fn_extract.h
	gcc -o $@ unit-samr21-erase-advance.c $(CFLAGS) $(LDFLAGS)

# unit-hifive1-flash-write runs the real hal_flash_write() from
# hal/hifive1.c against a mock fespi model (F-11035: the final partial
# page of a multi-page write took the full-page branch, over-reading
# the input and over-programming flash).
hifive1_flash_write_extract.h: ../../hal/hifive1.c
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< > $@

unit-hifive1-flash-write: unit-hifive1-flash-write.c hifive1_flash_write_extract.h
	gcc -o $@ unit-hifive1-flash-write.c $(CFLAGS) $(LDFLAGS)

# unit-rp2350-flash-write runs the real hal_flash_write() from
# hal/rp2350.c against a mock flash_range_program() that enforces the
# ROM contract (F-12061: unaligned / non page-multiple writes from
# the partition-state path violate the 256-byte page requirement).
rp2350_flash_write_extract.h: ../../hal/rp2350.c
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< > $@

unit-rp2350-flash-write: unit-rp2350-flash-write.c rp2350_flash_write_extract.h
	gcc -o $@ unit-rp2350-flash-write.c $(CFLAGS) $(LDFLAGS)

# unit-ecc-raw-der runs the real wolfCrypt raw-to-DER conversion and
# verification (F-11024: the wolfHSM verify path in src/image.c passed
# minimal field sizes with field-start pointers to
# wc_ecc_rs_raw_to_sig, corrupting signatures with leading zeros). It
# links the same wolfSSL sources the sign tool uses.
unit-ecc-raw-der: unit-ecc-raw-der.c
	# WOLFSSL_NO_DER_TO_PEM: the test config's NO_CODING drops base64, which
	# DER-to-PEM (pulled in by WOLFSSL_KEY_GEN) needs; the test uses no PEM.
	gcc -o $@ unit-ecc-raw-der.c $(CFLAGS) -DWOLFCRYPT_TEST -DWOLFSSL_NO_DER_TO_PEM \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/asn.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ecc.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ed25519.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ed448.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_448.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_operations.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_448.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_operations.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hash.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/logging.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/rsa.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c64.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/tfm.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_port.c \
		$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wolfmath.c $(LDFLAGS)

# unit-stm32l5-write runs the real hal_flash_write() from hal/stm32l5.c
# (an 8-byte program unit was read whole even when len left a
# partial unit, over-reading the caller's buffer and writing the excess
# bytes to flash). The function and its wait/clear helpers are
# extracted verbatim; FLASH_SR/CR live on a host register file and the
# destination flash is a pre-filled host array, so the over-read is
# observable through a canary after the source buffer.
stm32l5_write_extract.h: ../../hal/stm32l5.c
	sed -n '/^void RAMFUNCTION hal_flash_wait_complete/,/^}/p' $< > $@
	sed -n '/^void RAMFUNCTION hal_flash_clear_errors/,/^}/p' $< >> $@
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< >> $@

unit-stm32l5-write: unit-stm32l5-write.c stm32l5_write_extract.h
	gcc -o $@ unit-stm32l5-write.c $(CFLAGS) $(LDFLAGS)

# unit-stm32u5-write is the 16-byte-unit twin of the STM32L5 test:
# the real hal_flash_write() and its wait/clear helpers from
# hal/stm32u5.c, FLASH_NS_SR/CR on a host register file, destination
# flash at a 32-bit host address, canary after the source buffer.
stm32u5_write_extract.h: ../../hal/stm32u5.c
	sed -n '/^void RAMFUNCTION hal_flash_wait_complete/,/^}/p' $< > $@
	sed -n '/^void RAMFUNCTION hal_flash_clear_errors/,/^}/p' $< >> $@
	sed -n '/^int RAMFUNCTION hal_flash_write/,/^}/p' $< >> $@

unit-stm32u5-write: unit-stm32u5-write.c stm32u5_write_extract.h
	gcc -o $@ unit-stm32u5-write.c $(CFLAGS) $(LDFLAGS)

# unit-nvm-cache-scrub runs the real trailer_write() and
# partition_magic_write() from src/libwolfboot.c (under
# NVM_FLASH_WRITEONCE the staged sector can hold the firmware
# key/nonce, and the buffer was never scrubbed on return). The test
# owns NVM_CACHE, stages a key-bearing sector image, and stubs the
# flash layer; after each call the whole buffer must be zeroed.
nvm_cache_scrub_extract.h: ../../src/libwolfboot.c
	sed -n '/^static void RAMFUNCTION nvm_cache_scrub(/,/^}/p' $< > $@
	sed -n '/^static int RAMFUNCTION trailer_write(/,/^}/p' $< >> $@
	sed -n '/^static int RAMFUNCTION partition_magic_write(/,/^}/p' $< >> $@

unit-nvm-cache-scrub: unit-nvm-cache-scrub.c nvm_cache_scrub_extract.h
	gcc -o $@ unit-nvm-cache-scrub.c $(CFLAGS) $(LDFLAGS)

# unit-update-trigger-scrub runs the real wolfBoot_update_trigger()
# from src/libwolfboot.c over the NVM_FLASH_WRITEONCE branch
# (F-11037: the staged trailer sector, which in EXT_ENCRYPTED builds
# carries the firmware key/nonce, was never scrubbed from NVM_CACHE).
# The function is extracted together with nvm_cache_scrub(); the build
# defines NVM_FLASH_WRITEONCE so the write-once branch compiles.
update_trigger_scrub_extract.h: ../../src/libwolfboot.c
	sed -n '/^static void RAMFUNCTION nvm_cache_scrub(/,/^}/p' $< > $@
	sed -n '/^void RAMFUNCTION wolfBoot_update_trigger(/,/^}/p' $< >> $@

unit-update-trigger-scrub: unit-update-trigger-scrub.c update_trigger_scrub_extract.h
	gcc -o $@ unit-update-trigger-scrub.c -DNVM_FLASH_WRITEONCE $(CFLAGS) $(LDFLAGS)

# unit-sdhci-uhs-recover drives disk_read()'s UHS recovery path from the
# real src/sdhci.c (any read error permanently switched the host
# to 1.8V signaling with no rollback). sdhci_host.c (generated below) is
# identical to the real file except the three `asm volatile` statements
# are blanked (they cannot assemble on x86) and the real sdhci_read() is
# renamed to sdhci_read_hw() so the test can provide a scripted one. The
# platform-provided sdhci_reg_read()/sdhci_reg_write() are backed by a
# host register array; hal_get_timer_us() and wolfBoot_printf() are
# stubbed in the test.

sdhci_host.c: ../../src/sdhci.c
	sed -E -e 's/^[[:space:]]*asm volatile\("[^"]*"[^;]*\);[[:space:]]*//' \
	    -e 's/^int sdhci_read\(/int sdhci_read_hw(/' \
	    -e 's/^void __attribute__\(\(weak\)\) sdhci_platform_wdt_pet\(void\)$$/void sdhci_platform_wdt_pet_unused(void)/' $< > $@

unit-sdhci-uhs-recover: unit-sdhci-uhs-recover.c sdhci_host.c
	gcc -o $@ unit-sdhci-uhs-recover.c -DDISK_SDCARD $(CFLAGS) $(LDFLAGS)

# unit-sdhci-wait-busy drives sdhci_wait_busy()'s DATA0/CMD13 loops
# from the real src/sdhci.c (neither loop had a timeout, so a
# stuck/removed card spun forever). The controller is scripted through
# the host register file. SDHCI_WAIT_BUSY_TIMEOUT_MS is deliberately
# NOT overridden: the test's timer stub steps 1ms per read so the
# shipped default is the value under test.
unit-sdhci-wait-busy: unit-sdhci-wait-busy.c sdhci_host.c
	gcc -o $@ unit-sdhci-wait-busy.c -DDISK_SDCARD -DWOLFBOOT_NO_PRINTF $(CFLAGS) $(LDFLAGS)

# unit-sdhci-acmd41-timeout drives sdcard_card_full_init()'s ACMD41 OCR
# readiness poll from the real src/sdhci.c (F-11032: the do/while had no
# bound, so a card answering ACMD41 without ever setting OCR ready held
# the boot forever). Same sdhci_host.c generation as the wait-busy test;
# the card model sets OCR ready after N scripted ACMD41 polls, the timer
# stub steps 10 ms per read, and a command-write cap turns the pre-fix
# infinite loop into an abort.
unit-sdhci-acmd41-timeout: unit-sdhci-acmd41-timeout.c sdhci_host.c
	gcc -o $@ unit-sdhci-acmd41-timeout.c -DDISK_SDCARD -DWOLFBOOT_NO_PRINTF $(CFLAGS) $(LDFLAGS)

# unit-ti-hercules-write runs the real hal_flash_write() staging logic
# from hal/ti_hercules.c (a short write crossing a block
# boundary overran the staging buffer and lost the next block's bytes).
# ti_hercules.c needs the TI FAPI vendor headers (board build tree) and
# cannot be compiled on the host, but the bug is entirely in this
# function, so the Makefile extracts hal_flash_unlock_helper(), the
# WRITE_BLOCK_SIZE define, and hal_flash_write() verbatim into
# ti_hercules_write_extract.h; the FAPI calls are emulated in the test,
# with Fapi_issueProgrammingCommand() performing the program into a host
# flash array.

ti_hercules_write_extract.h: ../../hal/ti_hercules.c
	sed -n '/^static inline int RAMFUNCTION hal_flash_unlock_helper/,/^}/p' $< > $@
	grep '^#define WRITE_BLOCK_SIZE' $< >> $@
	sed -n '/^int RAMFUNCTION hal_flash_write(/,/^}/p' $< >> $@

unit-ti-hercules-write: unit-ti-hercules-write.c ti_hercules_write_extract.h
	gcc -o $@ unit-ti-hercules-write.c $(CFLAGS) $(LDFLAGS)

# unit-p1021-qe-firmware drives the real qe_upload_firmware() from
# hal/nxp_p1021.c (microcode offsets/counts were
# never bounded to the 64 KiB buffer read from the update partition).
# Reuses the nxp_p1021_host.c generated for the FCM tests, with the
# PowerPC accessors shadowed the same way.

unit-p1021-qe-firmware: unit-p1021-qe-firmware.c nxp_p1021_host.c
	gcc -o $@ unit-p1021-qe-firmware.c -DTARGET_nxp_p1021 -I../../hal $(CFLAGS) $(LDFLAGS)

# unit-t10xx-dts-memac drives the real hal_dts_fixup() FDT memac loop
# (unvalidated cell-index into phydevs[5]) with hand-built
# DTBs. Extracts the function plus its liodn/qman static tables from
# hal/nxp_t10xx.c.
nxp_t10xx_fixup_extract.h: ../../hal/nxp_t10xx.c
	sed -n '/^struct liodn_id_table {/,/^};/p' $< > $@
	sed -n '/#define SET_LIODN(/,/reg_offset = (void\*)reg/p' $< >> $@
	sed -n '/^static const struct liodn_id_table liodn_tbl\[\] = {/,/^};/p' $< >> $@
	sed -n '/^struct qportal_info {/,/^};/p' $< >> $@
	sed -n '/#define SET_QP_INFO(/,/sdest = dest/p' $< >> $@
	sed -n '/^static const struct qportal_info qp_info\[QMAN_NUM_PORTALS\] = {/,/^};/p' $< >> $@
	grep -E '^#define FM1_(DTSEC[0-9]|10GEC1) ' $< >> $@
	sed -n '/^int hal_dts_fixup(/,/^}/p' $< >> $@

unit-t10xx-dts-memac: unit-t10xx-dts-memac.c nxp_t10xx_fixup_extract.h ../../src/fdt.c
	gcc -o $@ unit-t10xx-dts-memac.c $(CFLAGS) $(LDFLAGS)

# unit-efi-x86-open-image includes hal/x86_64_efi.c directly, with in-test
# host mocks standing in for the gnu-efi runtime (BS, LibFileInfo, the
# x86-64 efi_callN() trampolines). This is the only host build coverage
# hal/x86_64_efi.c has.
#
# -fshort-wchar matches the real x86_64_efi build (arch.mk), where the
# 16-bit wchar_t is what makes efi_main's L"..." literals valid CHAR16
# strings under gnu-efi 4.x (CHAR16 = uint16_t).
unit-efi-x86-open-image: unit-efi-x86-open-image.c ../../hal/x86_64_efi.c
	gcc -o $@ unit-efi-x86-open-image.c -I/usr/include/efi -I/usr/include/efi/x86_64 -DTARGET_X86_64_EFI -fshort-wchar $(CFLAGS) $(LDFLAGS)

# unit-flash-write-mcxa includes hal/mcxa.c directly, with mcxa_fsl_stub/
# standing in for the (not vendored) NXP MCUXpresso SDK headers it includes.
unit-flash-write-mcxa: unit-flash-write-mcxa.c ../../hal/mcxa.c
	gcc -o $@ unit-flash-write-mcxa.c -Imcxa_fsl_stub $(CFLAGS) $(LDFLAGS)

unit-flash-write-nrf52: unit-flash-write-nrf52.c ../../hal/nrf52.c
	gcc -o $@ unit-flash-write-nrf52.c -DTARGET_nrf52 -I../../hal $(CFLAGS) $(LDFLAGS)

unit-flash-write-samr21: unit-flash-write-samr21.c ../../hal/samr21.c
	gcc -o $@ unit-flash-write-samr21.c $(CFLAGS) $(LDFLAGS)

unit-flash-write-same51: unit-flash-write-same51.c ../../hal/same51.c
	gcc -o $@ unit-flash-write-same51.c $(CFLAGS) $(LDFLAGS)

# unit-imx-rt-cache-align only pulls in hal/imx_rt.h, which is dependency-free
# by design, avoiding the (not vendored) NXP MCUXpresso SDK headers that
# hal/imx_rt.c itself requires.
unit-imx-rt-cache-align: unit-imx-rt-cache-align.c ../../hal/imx_rt.h
	gcc -o $@ unit-imx-rt-cache-align.c $(CFLAGS) $(LDFLAGS)

unit-elf-bss-guard: unit-elf-bss-guard.c
	gcc -o $@ $< -I../../include -DWOLFBOOT_ELF -DARCH_FLASH_OFFSET=0 \
		-DWOLFBOOT_NO_PRINTF -g $(LDFLAGS)

unit-elf-entry-inplace: unit-elf-entry-inplace.c
	gcc -o $@ $< -I../../include -DWOLFBOOT_ELF -DARCH_FLASH_OFFSET=0 \
		-DWOLFBOOT_NO_PRINTF -g $(LDFLAGS)

# unit-elf-mmu-fail: a failing mmu_cb must abort the whole ELF load
# (F-11027: the failed mapping used to be skipped with continue, and
# the entry point was published for a partially loaded image).
unit-elf-mmu-fail: unit-elf-mmu-fail.c ../../src/elf.c
	gcc -o $@ unit-elf-mmu-fail.c -I../../include -DWOLFBOOT_ELF \
		-DARCH_FLASH_OFFSET=0 -DWOLFBOOT_NO_PRINTF -g $(LDFLAGS)

unit-image-elf-scatter: ../../include/target.h unit-image-elf-scatter.c
	gcc -o $@ unit-image-elf-scatter.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.c \
		$(CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)


covclean:
	rm -f *.gcov *.gcno *.gcda coverage.*
	rm -f unit-sign-encrypted-output-*

# Sources the extraction rules generate into this directory. Listed here
# so "clean" removes them and so there is one place that names them.
GENERATED_SRC:=aurix_erased_extract.h \
	hifive1_flash_write_extract.h nvm_cache_scrub_extract.h \
	nxp_ls1028a_host.c nxp_p1021_host.c nxp_t10xx_fixup_extract.h \
	p1021_erase_extract.h p1021_erase_fn_extract.h \
	p1021_read_extract.h p1021_read_fn_extract.h \
	kontron_spi_extract.h kontron_spi_fn_extract.h \
	rp2350_flash_write_extract.h \
	sdhci_host.c \
	stm32g4_write_extract.h stm32l4_write_extract.h stm32l5_write_extract.h \
	stm32u5_write_extract.h stm32wb_write_extract.h \
	t10xx_flash_status_extract.h t10xx_qe_firmware_extract.h \
	t2080_fman_extract.h \
	ti_hercules_write_extract.h versal_ext_write_extract.h versal_host.c \
	versal_host.h versal_qspidev_extract.h zynq_erase_extract.h \
	zynq_write_extract.h \
	fat32-test.img fat32-test.img.tmp fat32-payload.bin fat32-nested.bin \
	fat32-short.bin ext4-min.img ext4-stock.img ext4-payload.bin \
	ext4-nested.bin ext4-fixtures.stamp

# Tests that are only in $(TESTS) when their toolchain/headers are
# present; clean must remove them regardless, or a host that has since
# lost gnu-efi/multilib keeps a stale binary forever.
CONDITIONAL_TESTS:=unit-efi-x86-open-image unit-linux-loader-e820 \
	unit-linux-loader-syssize

clean: covclean
	rm -f $(TESTS) $(CONDITIONAL_TESTS) $(GENERATED_SRC) *.o *.gcno *.gcda \
		coverage.*

# The extraction rules below build one header with several appending
# commands. Without this, a failure partway through leaves a truncated
# file with a fresh mtime that the next make treats as up to date.
.DELETE_ON_ERROR:

.PHONY: FORCE
