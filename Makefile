## wolfBoot Makefile
#
# Configure by passing alternate values
# via environment variables.
#
# Configuration values: see tools/config.mk
-include .config
include tools/config.mk

## Initializers
WOLFBOOT_ROOT?=$(PWD)

# Resolve LIBERO_FPGA_CONFIG_DIR (MPFS DDR config header dir) to an absolute
# path here, where the working directory is the repo root, and export it.  The
# test-app is built by a sub-make with CWD=test-app/, so a relative -I added by
# arch.mk would resolve against test-app/ and miss the directory.  override is
# required because it is typically a command-line variable.
ifneq ($(LIBERO_FPGA_CONFIG_DIR),)
  override LIBERO_FPGA_CONFIG_DIR := $(abspath $(LIBERO_FPGA_CONFIG_DIR))
  export LIBERO_FPGA_CONFIG_DIR
endif

WOLFHSM_MICROCHIP_PIC32CZ ?= ..
override WOLFHSM_MICROCHIP_PIC32CZ := $(abspath $(WOLFHSM_MICROCHIP_PIC32CZ))
export WOLFHSM_MICROCHIP_PIC32CZ

CFLAGS:=-D"__WOLFBOOT"
# gcc/clang warning flags; the TI cl2000 driver (ARCH=C2000) rejects them.
ifneq ($(ARCH),C2000)
CFLAGS+=-Werror -Wextra -Wno-array-bounds
endif
LSCRIPT:=config/target.ld
LSCRIPT_FLAGS:=
LDFLAGS:=
SECURE_LDFLAGS:=
LD_START_GROUP:=-Wl,--start-group
LD_END_GROUP:=-Wl,--end-group
LSCRIPT_IN:=hal/$(TARGET).ld
L2SRAM_ADDR?=0xF8F00000
V?=0
DEBUG?=0
DEBUG_UART?=0
LIBS=
SIGN_ALG=
OBJCOPY_FLAGS=
BIG_ENDIAN?=0
USE_CLANG?=0
USE_ARMCLANG?=0
ifneq ($(filter 1,$(USE_CLANG) $(USE_ARMCLANG)),)
USE_GCC?=0
else
USE_GCC?=1
endif
USE_GCC_HEADLESS?=1
FLASH_OTP_KEYSTORE?=0
BOOTLOADER_PARTITION_SIZE?=$$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET)))

OBJS:= \
	./src/string.o \
	./src/image.o \
	./src/libwolfboot.o \
	./hal/hal.o

ifeq ($(USE_CLANG),1)
OBJS+=./src/clang_sections.o
endif

ifeq ($(WOLFCRYPT_TZ_PSA),1)
OBJS+=./src/dice/dice.o
endif

ifneq ($(TARGET),library)
  ifeq ($(WOLFHAL),1)
    OBJS+=./hal/wolfhal.o
  else
    OBJS+=./hal/$(TARGET).o
  endif
endif

# User-provided key configuration
# - USER_PRIVATE_KEY: Path to user's private key (DER format)
# - USER_PUBLIC_KEY: Path to user's public key (DER format)
# - USER_CERT_CHAIN: Path to user's certificate chain (DER format)
# All must be provided together, or none at all

# Validate USER_PRIVATE_KEY and USER_PUBLIC_KEY are used together
ifneq ($(USER_PRIVATE_KEY),)
  ifeq ($(USER_PUBLIC_KEY),)
    $(error USER_PRIVATE_KEY requires USER_PUBLIC_KEY to also be set)
  endif
  ifeq ($(wildcard $(USER_PRIVATE_KEY)),)
    $(error USER_PRIVATE_KEY file not found: $(USER_PRIVATE_KEY))
  endif
endif

ifneq ($(USER_PUBLIC_KEY),)
  ifeq ($(USER_PRIVATE_KEY),)
    $(error USER_PUBLIC_KEY requires USER_PRIVATE_KEY to also be set)
  endif
  ifeq ($(wildcard $(USER_PUBLIC_KEY)),)
    $(error USER_PUBLIC_KEY file not found: $(USER_PUBLIC_KEY))
  endif
endif

# Validate USER_CERT_CHAIN requires USER_PRIVATE_KEY and USER_PUBLIC_KEY
ifneq ($(USER_CERT_CHAIN),)
  ifeq ($(USER_PRIVATE_KEY),)
    $(error USER_CERT_CHAIN requires USER_PRIVATE_KEY to also be set)
  endif
  ifeq ($(USER_PUBLIC_KEY),)
    $(error USER_CERT_CHAIN requires USER_PUBLIC_KEY to also be set)
  endif
  ifeq ($(wildcard $(USER_CERT_CHAIN)),)
    $(error USER_CERT_CHAIN file not found: $(USER_CERT_CHAIN))
  endif
endif

# Validate USER_NVM_INIT if provided
# - USER_NVM_INIT: Path to user's NVM init file for wolfHSM NVM image generation
ifneq ($(USER_NVM_INIT),)
  ifeq ($(wildcard $(USER_NVM_INIT)),)
    $(error USER_NVM_INIT file not found: $(USER_NVM_INIT))
  endif
endif

# Helper variable to detect if user-provided keys are being used
# This is used to skip auto-generated NVM images when users provide their own keys
ifneq ($(USER_PRIVATE_KEY),)
  _USER_PROVIDED_KEYS:=1
else ifneq ($(USER_PUBLIC_KEY),)
  _USER_PROVIDED_KEYS:=1
else ifneq ($(USER_CERT_CHAIN),)
  _USER_PROVIDED_KEYS:=1
endif

# USER_NVM_INIT overrides default NVM_CONFIG when provided
ifneq ($(USER_NVM_INIT),)
  NVM_CONFIG:=$(USER_NVM_INIT)
endif

# Eliminates compilation and linkage of the built-in wolfBoot keystore
WOLFBOOT_NO_KEYSTORE :=
ifeq ($(WOLFHSM_CLIENT),1)
  WOLFBOOT_NO_KEYSTORE := 1
endif
ifeq ($(WOLFHSM_SERVER),1)
  ifneq ($(CERT_CHAIN_VERIFY),)
    WOLFBOOT_NO_KEYSTORE := 1
  endif
endif

ifeq ($(SIGN),NONE)
  PRIVATE_KEY=
else
  # Private Key selection logic:
  # 1. User-provided private keys take precedence (USER_PRIVATE_KEY)
  # 2. Otherwise, if CERT_CHAIN_VERIFY, use generated dummy cert chain leaf key
  # 3. Otherwise use standard generated private key
  ifneq ($(USER_PRIVATE_KEY),)
    PRIVATE_KEY=$(USER_PRIVATE_KEY)
  else
    ifneq ($(CERT_CHAIN_VERIFY),)
      # Auto-generate cert chain mode - use leaf key
      PRIVATE_KEY?=test-dummy-ca/leaf-prvkey.der
    else
      # No cert chain verification - standard single key mode
      PRIVATE_KEY?=wolfboot_signing_private_key.der
    endif
  endif
  ifeq ($(FLASH_OTP_KEYSTORE),1)
    OBJS+=./src/flash_otp_keystore.o
  else ifeq ($(WOLFBOOT_NO_KEYSTORE),1)
    CFLAGS+=-DWOLFBOOT_NO_KEYSTORE
    # No built-in keystore is compiled in, but firmware images must still be
    # signed (for test/factory builds). src/keystore.o normally triggers
    # generation of the signing key via 'src/keystore.c: $(PRIVATE_KEY)'.
    # Without it, tie the key to the bootloader build so the signing key is
    # still produced before any downstream signing step runs.
    WOLFBOOT_SIGN_KEY_DEP=$(PRIVATE_KEY)
  else
    OBJS+=./src/keystore.o
  endif
endif

WOLFCRYPT_OBJS:=
SECURE_OBJS:=
PUBLIC_KEY_OBJS:=
WOLFHSM_OBJS:=
ifneq ("$(NO_LOADER)","1")
  OBJS+=./src/loader.o
endif

## Library Path Configuration
# Default paths for wolf* submodules - can be overridden with absolute or relative paths
# Convert all paths to absolute paths for consistent handling across sub-makefiles
WOLFBOOT_LIB_WOLFSSL?=lib/wolfssl
WOLFBOOT_LIB_WOLFTPM?=lib/wolfTPM
WOLFBOOT_LIB_WOLFPKCS11?=lib/wolfPKCS11
WOLFBOOT_LIB_WOLFPSA?=lib/wolfPSA
WOLFBOOT_LIB_WOLFHSM?=lib/wolfHSM
WOLFBOOT_LIB_WOLFCOSE?=lib/wolfCOSE

# Convert to absolute paths using abspath function
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))
WOLFBOOT_LIB_WOLFTPM:=$(abspath $(WOLFBOOT_LIB_WOLFTPM))
WOLFBOOT_LIB_WOLFPKCS11:=$(abspath $(WOLFBOOT_LIB_WOLFPKCS11))
WOLFBOOT_LIB_WOLFPSA:=$(abspath $(WOLFBOOT_LIB_WOLFPSA))
WOLFBOOT_LIB_WOLFHSM:=$(abspath $(WOLFBOOT_LIB_WOLFHSM))
WOLFBOOT_LIB_WOLFCOSE:=$(abspath $(WOLFBOOT_LIB_WOLFCOSE))

# Export variables so they are available to sub-makefiles
export WOLFBOOT_LIB_WOLFSSL
export WOLFBOOT_LIB_WOLFTPM
export WOLFBOOT_LIB_WOLFPKCS11
export WOLFBOOT_LIB_WOLFPSA
export WOLFBOOT_LIB_WOLFHSM
export WOLFBOOT_LIB_WOLFCOSE

## Architecture/CPU configuration
include arch.mk

ifeq ($(WOLFHAL),1)
  ifeq ($(strip $(BOARD)),)
    $(error WOLFHAL=1 requires BOARD to be set, e.g. BOARD=stm32wb_nucleo)
  endif
  ifeq ($(wildcard hal/boards/$(BOARD)/board.mk),)
    $(error BOARD=$(BOARD) has no hal/boards/$(BOARD)/board.mk)
  endif
  # wolfHAL backend: hal/wolfhal.o replaces hal/$(TARGET).o above. The
  # board's board.c provides hal_init/hal_prepare_boot and the wolfHAL
  # device handles; board.mk pulls in chip drivers.
  OBJS+=./hal/boards/$(BOARD)/board.o
  include hal/boards/$(BOARD)/board.mk
endif

# Set only here, so that host-tool sub-makes which also include options.mk
# (tools/bin-assemble, tools/bin2hex) skip the checks that only make sense
# once arch.mk has run. Deliberately not exported.
WOLFBOOT_TARGET_BUILD=1

# Parse config options
include options.mk

OBJS+=$(WOLFCRYPT_OBJS)
OBJS+=$(PUBLIC_KEY_OBJS)
OBJS+=$(WOLFHSM_OBJS)

# Vendored wolfHSM sources: keep cosmetic unused-parameter warnings non-fatal
$(WOLFHSM_OBJS): CFLAGS += -Wno-error=unused-parameter

CFLAGS+= \
  -I"." -I"include/" -I"$(WOLFBOOT_LIB_WOLFSSL)" \
  -D"WOLFSSL_USER_SETTINGS" \
  -D"WOLFTPM_USER_SETTINGS"
# -Wno-array-bounds is a gcc/clang option; the TI cl2000 driver rejects it.
ifneq ($(ARCH),C2000)
CFLAGS+=-Wno-array-bounds
endif
CFLAGS+=$(WOLFPSA_CFLAGS)

# Setup default optimizations (for GCC)
ifeq ($(USE_GCC_HEADLESS),1)
  CFLAGS+=-Wall -Wextra -Wno-main -ffreestanding
  CFLAGS+=-ffunction-sections -fdata-sections -fomit-frame-pointer
  # Allow unused parameters and functions
  CFLAGS+=-Wno-unused-parameter -Wno-unused-function
  # Error on unused variables
  CFLAGS+=-Wunused-variable
  LDFLAGS+=-Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles
  # Not setting LDFLAGS directly since it is passed to the test-app
  LSCRIPT_FLAGS+=-T $(LSCRIPT)
  OBJCOPY_FLAGS+=--gap-fill $(FILL_BYTE)
endif

ifeq ($(TARGET),ti_hercules)
  LSCRIPT_FLAGS+=--run_linker $(LSCRIPT)
endif
ifeq ($(ARCH),C2000)
  # cl2000 enters link mode via -z (in LDFLAGS); the .cmd is a positional arg.
  LSCRIPT_FLAGS+=$(LSCRIPT)
endif
ifeq ($(ARCH),AURIX_TC3)
  ifneq ($(USE_GCC_HEADLESS),1)
    LSCRIPT_FLAGS+=-T $(LSCRIPT)
  endif
endif

## ARM Compiler for Embedded (USE_ARMCLANG=1): rebuild the link options from
## scratch and switch the linker script to a scatter file, to support armlink.
ifeq ($(USE_ARMCLANG),1)
  CFLAGS+=-D'END_STACK=Image$$$$ARM_LIB_STACK$$$$ZI$$$$Limit'
  ifeq ($(WOLFCRYPT_TZ),1)
    # Trap stubs for symbols referenced from wolfSSL code paths that are
    # dead in this configuration: GNU ld garbage-collects the referencing
    # sections, but armlink resolves all symbols before unused section
    # elimination
    OBJS+=tools/armclang/armclang_stubs.o
    CFLAGS+=-DARMCLANG_STUBS_DEAD_REFS
    ifeq ($(WOLFCRYPT_TZ_PKCS11),1)
      CFLAGS+=-D'_flash_keyvault=Image$$$$KEYVAULT$$$$Base'
      CFLAGS+=-D'_flash_keyvault_size=Image$$$$KEYVAULT$$$$ZI$$$$Length'
      CFLAGS+=-D'_start_heap=Image$$$$RAM_HEAP$$$$Base'
      CFLAGS+=-D'_heap_size=Image$$$$RAM_HEAP$$$$ZI$$$$Length'
      # ARM libc malloc needs the C-library init that never runs (entry is
      # isr_reset): provide a heap allocator instead
      CFLAGS+=-DARMCLANG_STUBS_MALLOC
    endif
  endif
  LDFLAGS:=$(ARMCLANG_LDFLAGS) --map --list=wolfboot.map
  LSCRIPT:=config/target.sct
  LSCRIPT_IN:=hal/$(TARGET).sct
  LSCRIPT_FLAGS:=--scatter=$(LSCRIPT)
  LD_START_GROUP:=
  LD_END_GROUP:=
  SECURE_LDFLAGS:=
  ifeq ($(TZEN),1)
    SECURE_LDFLAGS:=--import_cmse_lib_out=./src/wolfboot_tz_nsc.o
  endif
endif

# Environment variables for sign tool
SIGN_ENV=IMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE) \
		 WOLFBOOT_PARTITION_SIZE=$(WOLFBOOT_PARTITION_SIZE) \
		 WOLFBOOT_PARTITION_UPDATE_SIZE=$(WOLFBOOT_PARTITION_UPDATE_SIZE) \
		 WOLFBOOT_SECTOR_SIZE=$(WOLFBOOT_SECTOR_SIZE) \
		 NVM_FLASH_WRITEONCE=$(NVM_FLASH_WRITEONCE) \
		 ML_DSA_LEVEL=$(ML_DSA_LEVEL) \
		 IMAGE_SIGNATURE_SIZE=$(IMAGE_SIGNATURE_SIZE) \
		 LMS_LEVELS=$(LMS_LEVELS) \
		 LMS_HEIGHT=$(LMS_HEIGHT) \
		 LMS_WINTERNITZ=$(LMS_WINTERNITZ) \
		 XMSS_PARAMS=$(XMSS_PARAMS)


MAIN_TARGET=factory.bin
# PE/COFF output format for the wolfboot.efi objcopy rule. Overridden per
# target in arch.mk (e.g. pei-aarch64-little for aarch64_efi).
EFI_OBJCOPY_TARGET?=pei-x86-64
TARGET_H_TEMPLATE:=include/target.h.in

ifeq ($(TZEN),1)
ifeq ($(TARGET),stm32l5)
	# Don't build a contiguous image
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),stm32u5)
	# Don't build a contiguous image
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),stm32h5)
	# Don't build a contiguous image
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),stm32n6)
	# Don't build a contiguous image
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif
endif # TZEN=1

ifeq ($(TARGET),pic32cz)
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),x86_64_efi)
    MAIN_TARGET:=wolfboot.efi
endif

ifeq ($(TARGET),aarch64_efi)
    MAIN_TARGET:=wolfboot.efi
endif

ifeq ($(FSP), 1)
    MAIN_TARGET:=wolfboot_stage1.bin
endif

ifeq ($(TARGET),library)
    CFLAGS+=-g
    MAIN_TARGET:=libwolfboot.a
endif

ifeq ($(TARGET),library_fs)
    MAIN_TARGET:=libwolfboot.a
endif

ifeq ($(TARGET),raspi3)
    MAIN_TARGET:=wolfboot.bin
endif

# Tegra234 bare-metal BL33 boots from RAM (loaded by an earlier stage), so
# there is no contiguous flash factory.bin. Build the bootloader plus the
# signed payload, which the bundling scripts concatenate into the BL33 image.
ifeq ($(TARGET),tegra234)
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),cm4)
    MAIN_TARGET:=wolfboot.bin
endif

# i.MX95 M7 runs from ITCM (loaded by the Linux remoteproc driver); the payload
# lives in DDR at 0x80100000, so there is no contiguous flash image to assemble.
ifeq ($(TARGET),imx95_m7)
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),f28p55x)
    # C28x flash is word-addressed; DSLite loads the cl2000 .out (ELF) directly.
    # No objcopy / no flat .bin.
    MAIN_TARGET:=wolfboot.elf
endif

ifeq ($(TARGET),sim)
    CFLAGS+=-fno-pie
    LDFLAGS+=-no-pie
    MAIN_TARGET:=wolfboot.bin tools/bin-assemble/bin-assemble test-app/image_v1_signed.bin internal_flash.dd
endif

ifeq ($(TARGET),nxp_p1021)
    MAIN_TARGET:=factory_wstage1.bin
endif
ifeq ($(TARGET),nxp_t1024)
    MAIN_TARGET:=factory_wstage1.bin
endif
ifeq ($(TARGET),nxp_t1040)
    MAIN_TARGET:=factory_wstage1.bin
endif

ifeq ($(TARGET),sama5d3)
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),zynq7000)
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),stm32n6)
    # wolfBoot runs from SRAM, app from XIP on external NOR - no contiguous factory.bin
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),rp2350)
    MAIN_TARGET:=include/target.h keytools wolfboot_signing_private_key.der pico-sdk-info
endif


ifeq ($(FLASH_OTP_KEYSTORE),1)
    MAIN_TARGET+=tools/keytools/otp/otp-keystore-primer.bin
endif

ifneq ($(SIGN_SECONDARY),)
  SECONDARY_PRIVATE_KEY=wolfboot_signing_second_private_key.der
endif

ASFLAGS:=$(CFLAGS)

all: $(SECONDARY_PRIVATE_KEY) $(MAIN_TARGET)

stage1: stage1/loader_stage1.bin
stage1/loader_stage1.bin: wolfboot.elf
stage1/loader_stage1.bin: FORCE
	@echo "\t[BIN] $@"
	$(Q)$(MAKE) -C $(dir $@) $(notdir $@)

libwolfboot.a: include/target.h $(OBJS)
	@echo "\t[LIB] $@"
	$(Q)$(AR) rcs $@ $(OBJS)

test-lib: libwolfboot.a hal/library.o
	@echo "\t[BIN] $@"
	$(Q)$(CC) $(CFLAGS) -o $@ hal/library.o libwolfboot.a

lib-fs: libwolfboot.a hal/library_fs.o hal/filesystem.o
	@echo "\t[BIN] $@"
	$(Q)$(CC) $(CFLAGS) -o $@ hal/library_fs.o hal/filesystem.o libwolfboot.a

wolfboot.efi: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) -j .rodata -j .text -j .sdata -j .data \
					-j .dynamic -j .dynsym  -j .rel \
					-j .rela -j .reloc -j .eh_frame \
					-O $(EFI_OBJCOPY_TARGET) --subsystem=10 $^ $@
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.efi
	@echo

wolfboot.bin: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O binary $^ $@
ifeq ($(TARGET),nxp_lpc54s0xx)
	@echo "\t[LPC] enhanced boot block"
	$(Q)python3 tools/scripts/lpc54s0xx_patch_boot_block.py $@
endif
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.elf
	@echo

test-app/image.bin: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)" ELF_FLASH_SCATTER="$(ELF_FLASH_SCATTER)" LIBERO_FPGA_CONFIG_DIR="$(LIBERO_FPGA_CONFIG_DIR)" WOLFHSM_MICROCHIP_PIC32CZ="$(WOLFHSM_MICROCHIP_PIC32CZ)"
	$(Q)$(SIZE) test-app/image.elf

standalone:
	$(Q)$(MAKE) -C test-app TARGET=$(TARGET) EXT_FLASH=$(EXT_FLASH) SPI_FLASH=$(SPI_FLASH) QSPI_FLASH=$(QSPI_FLASH) OCTOSPI_FLASH=$(OCTOSPI_FLASH) ARCH=$(ARCH) \
    NO_XIP=$(NO_XIP) V=$(V) RAM_CODE=$(RAM_CODE) WOLFBOOT_VERSION=$(WOLFBOOT_VERSION)\
	MCUXPRESSO=$(MCUXPRESSO) MCUXPRESSO_CPU=$(MCUXPRESSO_CPU) MCUXPRESSO_DRIVERS=$(MCUXPRESSO_DRIVERS) \
	MCUXPRESSO_CMSIS=$(MCUXPRESSO_CMSIS) NVM_FLASH_WRITEONCE=$(NVM_FLASH_WRITEONCE) \
	FREEDOM_E_SDK=$(FREEDOM_E_SDK) standalone
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O binary test-app/image.elf standalone.bin
	$(Q)$(SIZE) test-app/image.elf


include tools/test.mk
include tools/test-enc.mk
include tools/test-delta.mk
include tools/test-renode.mk

hal/$(TARGET).o:

keytools_check: keytools

test-emu:
	$(MAKE) -C test-app/emu-test-apps WOLFBOOT_ROOT="$(CURDIR)" test-emu

# Generate the initial signing key (only if not using user-provided keys)
# - Creates wolfboot_signing_private_key.der when USER_PRIVATE_KEY is not set
# - If CERT_CHAIN_VERIFY is enabled and USER_CERT_CHAIN not provided, also generates cert chain with leaf key
wolfboot_signing_private_key.der:
ifeq ($(USER_PRIVATE_KEY),)
	$(Q)$(MAKE) keytools_check
	$(Q)(test $(SIGN) = NONE) || ($(SIGN_ENV) "$(KEYGEN_TOOL)" $(KEYGEN_OPTIONS) -g wolfboot_signing_private_key.der) || true
	$(Q)(test $(SIGN) = NONE) && (echo "// SIGN=NONE" >  src/keystore.c) || true
	$(Q)(test "$(FLASH_OTP_KEYSTORE)" = "1") && (make -C tools/keytools/otp) || true
	$(Q)(test $(SIGN) = NONE) || (test "$(CERT_CHAIN_VERIFY)" = "") || (test "$(USER_CERT_CHAIN)" != "") || (tools/scripts/sim-gen-dummy-chain.sh --leaf-algo $(CERT_CHAIN_GEN_LEAF_ALGO) --ca-algo $(CERT_CHAIN_GEN_CA_ALGO) --ca-hash $(CERT_CHAIN_GEN_CA_HASH) --leaf wolfboot_signing_private_key.der)
else
	@echo "Using user-provided private key: $(USER_PRIVATE_KEY)"
endif

# Auto-generate cert chain mode: Ensure leaf key exists after cert chain generation
# Only applies when CERT_CHAIN_VERIFY is enabled and USER_CERT_CHAIN not provided
# Skip this when using user-provided keys
ifeq ($(USER_PRIVATE_KEY),)
ifneq ($(CERT_CHAIN_VERIFY),)
ifeq ($(USER_CERT_CHAIN),)
$(PRIVATE_KEY): wolfboot_signing_private_key.der
	@test -f $(PRIVATE_KEY) || (echo "Error: $(PRIVATE_KEY) not found" && exit 1)
endif
endif
endif

$(SECONDARY_PRIVATE_KEY): $(PRIVATE_KEY) keystore.der
	$(Q)$(MAKE) keytools_check
	$(Q)rm -f src/keystore.c
	$(Q)dd if=keystore.der of=pubkey_1.der bs=1 skip=16
	$(Q)(test $(SIGN_SECONDARY) = NONE) || ($(SIGN_ENV) "$(KEYGEN_TOOL)" \
		$(KEYGEN_OPTIONS) -i pubkey_1.der $(SECONDARY_KEYGEN_OPTIONS) \
		-g $(SECONDARY_PRIVATE_KEY)) || true
	$(Q)(test "$(FLASH_OTP_KEYSTORE)" = "1") && (make -C tools/keytools/otp) || true

keytools:
	@echo "Building key tools"
	@$(MAKE) -C tools/keytools -j

squashelf:
	@echo "Building squashelf tool"
	@$(MAKE) -C tools/squashelf -j

squashelf_check: squashelf

tpmtools: include/target.h keys
	@echo "Building TPM tools"
	@$(MAKE) -C tools/tpm -s clean
	@$(MAKE) -C tools/tpm -j

swtpmtools: include/target.h
	@echo "Building TPM tools"
	@$(MAKE) -C tools/tpm -s clean
	@$(MAKE) -C tools/tpm -j swtpm

# Generate NVM image if either WOLFHSM_CLIENT or WOLFHSM_SERVER
ifeq ($(WOLFHSM_CLIENT),1)
    _DO_WH_NVMTOOL:=1
endif
ifeq ($(WOLFHSM_SERVER),1)
    _DO_WH_NVMTOOL:=1
endif
# Not every wolfHSM port provisions keys from a pre-built NVM image. A HAL may
# instead install the verification key into the server's key cache at boot (see
# hal_hsm_init_connect on pic32cz), which is the only option on targets whose
# server wipes its NVM partition on startup. Those set WOLFHSM_NVM_IMAGE=0.
WOLFHSM_NVM_IMAGE?=1
ifeq ($(WOLFHSM_NVM_IMAGE),0)
    _DO_WH_NVMTOOL:=
endif
# Disable NVM image generation if user-provided keys without explicit USER_NVM_INIT
# (providing USER_NVM_INIT allows users to supply keys and still generate a custom NVM image)
ifeq ($(_USER_PROVIDED_KEYS),1)
  ifeq ($(USER_NVM_INIT),)
    _DO_WH_NVMTOOL:=
  endif
endif
ifeq ($(_DO_WH_NVMTOOL),1)
whnvmtool:
	@echo "Building wolfHSM NVM tool"
	@$(MAKE) -C $(WOLFBOOT_LIB_WOLFHSM)/tools/whnvmtool

nvm-image: $(PRIVATE_KEY) whnvmtool
	@echo "Generating wolfHSM NVM image"
	$(Q)$(WOLFBOOT_LIB_WOLFHSM)/tools/whnvmtool/whnvmtool --image=$(WH_NVM_BIN) --size=$(WH_NVM_PART_SIZE) --invert-erased-byte $(NVM_CONFIG)
	@echo "Converting NVM image to Intel HEX format"
	$(Q)$(OBJCOPY) -I binary -O ihex --change-address $(WH_NVM_BASE_ADDRESS) $(WH_NVM_BIN) $(WH_NVM_HEX)
	@echo "NVM images generated: $(WH_NVM_BIN) and $(WH_NVM_HEX)"
endif

test-app/image_v1_signed.bin: $(BOOT_IMG) keytools_check
	@echo "\t[SIGN] $(BOOT_IMG)"
	@echo "\tSECONDARY_SIGN_OPTIONS=$(SECONDARY_SIGN_OPTIONS)"
	@echo "\tSECONDARY_PRIVATE_KEY=$(SECONDARY_PRIVATE_KEY)"
ifeq ($(STRIP_ELF),1)
	@echo "\t[STRIP] $(BOOT_IMG)"
	$(Q)$(OBJCOPY) --strip-debug $(BOOT_IMG) $(BOOT_IMG).stripped
	$(Q)(test $(SIGN) = NONE) || $(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) \
		$(SECONDARY_SIGN_OPTIONS) $(BOOT_IMG).stripped $(PRIVATE_KEY) \
		$(SECONDARY_PRIVATE_KEY) 1 || true
	$(Q)(test $(SIGN) = NONE) && $(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG).stripped 1 || true
	$(Q)mv test-app/image.elf_v1_signed.bin test-app/image_v1_signed.bin
else
	$(Q)(test $(SIGN) = NONE) || $(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) \
		$(SECONDARY_SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) \
		$(SECONDARY_PRIVATE_KEY) 1 || true
	$(Q)(test $(SIGN) = NONE) && $(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG) 1 || true
endif

test-app/image.elf: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)" ELF_FLASH_SCATTER="$(ELF_FLASH_SCATTER)" LIBERO_FPGA_CONFIG_DIR="$(LIBERO_FPGA_CONFIG_DIR)" WOLFHSM_MICROCHIP_PIC32CZ="$(WOLFHSM_MICROCHIP_PIC32CZ)" image.elf
	$(Q)$(SIZE) test-app/image.elf

ifeq ($(ELF_FLASH_SCATTER),1)
test-app/image.elf: squashelf
endif

assemble_internal_flash.dd: FORCE
	$(Q)$(BINASSEMBLE) internal_flash.dd \
		0 wolfboot.bin \
		$$(($(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET))) test-app/image_v1_signed.bin \
		$$(($(WOLFBOOT_PARTITION_UPDATE_ADDRESS)-$(ARCH_FLASH_OFFSET))) /tmp/swap \
		$(if $(filter 1,$(DISABLE_BACKUP)),,$$(($(WOLFBOOT_PARTITION_SWAP_ADDRESS)-$(ARCH_FLASH_OFFSET))) /tmp/swap) # omit swap partition if DISABLE_BACKUP=1

internal_flash.dd: $(BINASSEMBLE) wolfboot.bin $(BOOT_IMG) $(PRIVATE_KEY) test-app/image_v1_signed.bin
	@echo "\t[MERGE] internal_flash.dd"
	$(Q)dd if=/dev/zero bs=1 count=$$(($(WOLFBOOT_SECTOR_SIZE))) > /tmp/swap
	make assemble_internal_flash.dd

ifeq ($(_DO_WH_NVMTOOL),1)
factory.bin: $(BINASSEMBLE) wolfboot.bin $(BOOT_IMG) $(PRIVATE_KEY) test-app/image_v1_signed.bin nvm-image
else
factory.bin: $(BINASSEMBLE) wolfboot.bin $(BOOT_IMG) $(PRIVATE_KEY) test-app/image_v1_signed.bin
endif
	@echo "\t[MERGE] $@"
	$(Q)$(BINASSEMBLE) $@ \
		$(WOLFBOOT_ORIGIN) wolfboot.bin \
		$(WOLFBOOT_PARTITION_BOOT_ADDRESS) test-app/image_v1_signed.bin

factory.srec: factory.bin
	@echo "\t[BIN2SREC] $@"
	$(Q)$(OBJCOPY) -I binary -O srec --change-addresses=$(WOLFBOOT_ORIGIN) $< $@

factory_wstage1.bin: $(BINASSEMBLE) stage1/loader_stage1.bin wolfboot.bin $(BOOT_IMG) $(PRIVATE_KEY) test-app/image_v1_signed.bin
	@echo "\t[MERGE] $@"
	$(Q)$(BINASSEMBLE) $@ \
		$(WOLFBOOT_STAGE1_FLASH_ADDR) stage1/loader_stage1.bin \
		$(WOLFBOOT_ORIGIN) wolfboot.bin \
		$(WOLFBOOT_PARTITION_BOOT_ADDRESS) test-app/image_v1_signed.bin

# stage1 linker script embed wolfboot.bin inside stage1/loader_stage1.bin
wolfboot_stage1.bin: wolfboot.elf stage1/loader_stage1.bin
	$(Q) cp stage1/loader_stage1.bin wolfboot_stage1.bin

wolfboot.elf: include/target.h $(LSCRIPT) $(OBJS) $(BINASSEMBLE) $(WOLFBOOT_SIGN_KEY_DEP) FORCE
	$(Q)(test $(SIGN) = NONE) || (test $(FLASH_OTP_KEYSTORE) = 1) || (test "$(WOLFBOOT_NO_KEYSTORE)" = "1") || (grep -q $(SIGN_ALG) src/keystore.c) || \
		(echo "Key mismatch: please run 'make keysclean' to remove all keys if you want to change algorithm" && false)
	@echo "\t[LD] $@"
	@echo $(OBJS)
	$(Q)$(LD) $(LDFLAGS) $(LSCRIPT_FLAGS) $(SECURE_LDFLAGS) $(LD_START_GROUP) $(OBJS) $(LIBS) $(LD_END_GROUP) -o $@

$(LSCRIPT): $(LSCRIPT_IN) FORCE
	$(Q)(test $(LSCRIPT_IN) != NONE) || (echo "Error: no linker script" \
		"configuration found. If you selected Encryption and RAM_CODE, then maybe" \
		"the encryption algorithm is not yet supported with bootloader updates." \
		&& false)
	$(Q)(test -r $(LSCRIPT_IN)) || (echo "Error: no RAM/ChaCha linker script found." \
		"If you selected Encryption and RAM_CODE, ensure that you have a" \
		"custom linker script (i.e. $(TARGET)_chacha_ram.ld). Please read " \
		"docs/encrypted_partitions.md for more information" && false)
	$(Q)cat $(LSCRIPT_IN) | \
		sed -e "s/@ARCH_FLASH_OFFSET@/$(ARCH_FLASH_OFFSET)/g" | \
		sed -e "s/@BOOTLOADER_PARTITION_SIZE@/$(BOOTLOADER_PARTITION_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_ORIGIN@/$(WOLFBOOT_ORIGIN)/g" | \
		sed -e "s/@WOLFBOOT_KEYVAULT_ADDRESS@/$(WOLFBOOT_KEYVAULT_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_KEYVAULT_SIZE@/$(WOLFBOOT_KEYVAULT_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_NSC_ADDRESS@/$(WOLFBOOT_NSC_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_NSC_SIZE@/$(WOLFBOOT_NSC_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$(WOLFBOOT_PARTITION_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_SIZE@/$(WOLFBOOT_STAGE1_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_LOAD_ADDR@/$(WOLFBOOT_STAGE1_LOAD_ADDR)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_FLASH_ADDR@/$(WOLFBOOT_STAGE1_FLASH_ADDR)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_BASE_ADDR@/$(WOLFBOOT_STAGE1_BASE_ADDR)/g" | \
		sed -e "s/@WOLFBOOT_LOAD_BASE@/$(WOLFBOOT_LOAD_BASE)/g" | \
		sed -e "s/@BOOTLOADER_START@/$(BOOTLOADER_START)/g" | \
		sed -e "s/@IMAGE_HEADER_SIZE@/$(IMAGE_HEADER_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_LOAD_ADDRESS@/$(WOLFBOOT_LOAD_ADDRESS)/g" | \
		sed -e "s/@FSP_S_LOAD_BASE@/$(FSP_S_LOAD_BASE)/g" | \
		sed -e "s/@WOLFBOOT_L2LIM_SIZE@/$(WOLFBOOT_L2LIM_SIZE)/g" | \
		sed -e "s/@L2SRAM_ADDR@/$(L2SRAM_ADDR)/g" | \
		sed -e "s/@STACK_SIZE_PER_HART@/$(STACK_SIZE_PER_HART)/g" | \
		sed -e 's/@WOLFHAL_FLASH_EXCLUDE_TEXT@/$(WOLFHAL_FLASH_EXCLUDE_TEXT)/g' | \
		sed -e 's/@WOLFHAL_FLASH_EXCLUDE_RODATA@/$(WOLFHAL_FLASH_EXCLUDE_RODATA)/g' | \
		sed -e 's/@WOLFHAL_FLASH_RAM_SECTIONS@/$(WOLFHAL_FLASH_RAM_SECTIONS)/g' \
		> $@

hex: wolfboot.hex
srec: wolfboot.srec
factory-srec: factory.srec

%.hex:%.elf
	@echo "\t[ELF2HEX] $@"
	@$(OBJCOPY) -O ihex $^ $@

%.srec:%.elf
	@echo "\t[ELF2SREC] $@"
	@$(OBJCOPY) -O srec $^ $@

# Keystore generation: use user-provided public key if available
ifneq ($(USER_PUBLIC_KEY),)
src/keystore.c: $(USER_PUBLIC_KEY)
	@echo "Generating keystore from user-provided public key: $(USER_PUBLIC_KEY)"
	$(Q)$(MAKE) keytools_check
	$(Q)$(SIGN_ENV) "$(KEYGEN_TOOL)" $(KEYGEN_OPTIONS) --force -i $(USER_PUBLIC_KEY)
else
src/keystore.c: $(PRIVATE_KEY)
endif

flash_keystore: src/flash_otp_keystore.o

src/flash_otp_keystore.o: $(PRIVATE_KEY) src/flash_otp_keystore.c
	$(Q)$(MAKE) src/keystore.c
	$(Q)$(CC) -c $(CFLAGS) src/flash_otp_keystore.c -o $(@)

keys: $(PRIVATE_KEY)

clean:
	$(Q)rm -f src/*.o hal/*.o hal/spi/*.o hal/uart/*.o test-app/*.o src/x86/*.o
	$(Q)rm -f src/wolfboot_tz_nsc.o
	$(Q)rm -f *.asm  # TI cl2000 (ARCH=C2000) intermediate listings in repo root
	$(Q)rm -f $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/*.o $(WOLFBOOT_LIB_WOLFTPM)/src/*.o $(WOLFBOOT_LIB_WOLFTPM)/src/fwtpm/*.o $(WOLFBOOT_LIB_WOLFTPM)/hal/*.o $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/*.o
	$(Q)rm -f $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/Renesas/*.o
	$(Q)rm -f wolfboot.bin wolfboot.elf wolfboot.map test-update.rom wolfboot.hex wolfboot.srec factory.srec
	$(Q)rm -f $(MACHINE_OBJ) $(MAIN_TARGET) $(LSCRIPT)
	$(Q)rm -f $(OBJS)
	$(Q)rm -f tools/keytools/otp/otp-keystore-gen
	$(Q)rm -f .stack_usage
	$(Q)rm -f $(WH_NVM_BIN) $(WH_NVM_HEX)
	$(Q)rm -f test-lib
	$(Q)rm -f lib-fs
	$(Q)rm -f libwolfboot.a
	$(Q)$(MAKE) -C test-app clean V=$(V)
	$(Q)$(MAKE) -C tools/check_config -s clean
	$(Q)$(MAKE) -C stage1 -s clean

utilsclean: clean
	$(Q)$(MAKE) -C tools/keytools -s clean
	$(Q)$(MAKE) -C tools/delta -s clean
	$(Q)$(MAKE) -C tools/bin-assemble -s clean
	$(Q)$(MAKE) -C tools/elf-parser -s clean
	$(Q)$(MAKE) -C tools/fdt-parser -s clean
	$(Q)$(MAKE) -C tools/check_config -s clean
	$(Q)$(MAKE) -C tools/test-expect-version -s clean
	$(Q)$(MAKE) -C tools/test-update-server -s clean
	$(Q)$(MAKE) -C tools/uart-flash-server -s clean
	$(Q)$(MAKE) -C tools/unit-tests -s clean
	$(Q)if [ "$(WOLFHSM_CLIENT)" = "1" ]; then $(MAKE) -C $(WOLFBOOT_LIB_WOLFHSM)/tools/whnvmtool -s clean; fi
	$(Q)$(MAKE) -C tools/keytools/otp -s clean
	$(Q)$(MAKE) -C tools/squashelf -s clean

keysclean: clean
	$(Q)rm -f *.pem *.der tags ./src/*_pub_key.c ./src/keystore.c include/target.h
	$(Q)(test "$(CERT_CHAIN_VERIFY)" = "" || test "$(USER_CERT_CHAIN)" != "") || rm -rf test-dummy-ca || true

distclean: clean keysclean utilsclean
	$(Q)rm -f *.bin *.elf

include/target.h: $(TARGET_H_TEMPLATE) FORCE
	$(Q)cat $(TARGET_H_TEMPLATE) | \
	sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$(WOLFBOOT_PARTITION_SIZE)/g" | \
	sed -e "s|@WOLFBOOT_PARTITION_UPDATE_SIZE_DEFINE@|$(if $(strip $(WOLFBOOT_PARTITION_UPDATE_SIZE)),#define WOLFBOOT_PARTITION_UPDATE_SIZE    $(WOLFBOOT_PARTITION_UPDATE_SIZE),/* WOLFBOOT_PARTITION_UPDATE_SIZE not set */)|g" | \
	sed -e "s/@WOLFBOOT_SECTOR_SIZE@/$(WOLFBOOT_SECTOR_SIZE)/g" | \
	sed -e "s/@WOLFBOOT_NSC_ADDRESS@/$(WOLFBOOT_NSC_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_NSC_SIZE@/$(WOLFBOOT_NSC_SIZE)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_DTS_BOOT_ADDRESS@/$(WOLFBOOT_DTS_BOOT_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_DTS_UPDATE_ADDRESS@/$(WOLFBOOT_DTS_UPDATE_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_ADDRESS@/$(WOLFBOOT_LOAD_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_DTS_ADDRESS@/$(WOLFBOOT_LOAD_DTS_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_RAMDISK_ADDRESS@/$(WOLFBOOT_LOAD_RAMDISK_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_FPGA_ADDRESS@/$(WOLFBOOT_LOAD_FPGA_ADDRESS)/g" | \
	sed -e "s|@WOLFBOOT_RAMBOOT_MAX_SIZE_DEFINE@|$(if $(strip $(WOLFBOOT_RAMBOOT_MAX_SIZE)),#define WOLFBOOT_RAMBOOT_MAX_SIZE             $(WOLFBOOT_RAMBOOT_MAX_SIZE),/* WOLFBOOT_RAMBOOT_MAX_SIZE undefined */)|g" | \
	sed -e "s/@WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS@/$(WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS)/g" \
		> $@

delta: tools/delta/bmdiff

tools/delta/bmdiff: FORCE
	$(Q)$(MAKE) -C tools/delta

delta-test: FORCE
	$(Q)$(MAKE) -C tools/delta $@

elf-parser:
	@$(MAKE) -C tools/elf-parser -s clean
	@$(MAKE) -C tools/elf-parser

fdt-parser:
	@$(MAKE) -C tools/fdt-parser -s clean
	@$(MAKE) -C tools/fdt-parser

config: FORCE
	$(MAKE) -C config

check_config:
	$(MAKE) -C tools/check_config run

line-count:
	cloc --force-lang-def cloc_lang_def.txt src/boot_arm.c src/image.c src/libwolfboot.c src/loader.c src/update_flash.c

line-count-nrf52:
	cloc --force-lang-def cloc_lang_def.txt src/boot_arm.c src/image.c src/libwolfboot.c src/loader.c src/update_flash.c hal/nrf52.c

line-count-x86:
	cloc --force-lang-def cloc_lang_def.txt src/boot_x86_fsp.c src/boot_x86_fsp_payload.c src/boot_x86_fsp_start.S src/image.c src/keystore.c src/libwolfboot.c src/loader.c src/string.c src/update_disk.c src/gpt.c src/x86/ahci.c src/x86/ata.c src/x86/common.c src/disk.c src/x86/hob.c src/pci.c src/x86/tgl_fsp.c hal/x86_fsp_tgl.c hal/x86_uart.c

stack-usage: wolfboot.bin
	$(Q)echo $(STACK_USAGE) > .stack_usage

image-header-size: wolfboot.bin
	$(Q)echo $(IMAGE_HEADER_SIZE) > .image_header_size

## Target-specific flash targets
ifeq ($(TARGET),stm32n6)
flash: wolfboot.bin test-app/image_v1_signed.bin
	$(Q)tools/scripts/stm32n6_flash.sh --skip-build
endif


cppcheck:
	cppcheck -f --enable=warning --enable=portability \
		-Iinclude -I. \
		-D'XALIGNED(x)=' -D'TZ_SECURE()=0' -D'__has_attribute(x)=0' \
		--suppress="ctunullpointer" --suppress="nullPointer" \
		--suppress="objectIndex" --suppress="comparePointers" \
		--suppress="bufferAccessOutOfBounds" \
		--suppress="internalAstError" \
		--suppress="invalidPrintfArgType_s" \
		--suppress="invalidPrintfArgType_sint" \
		--suppress="invalidPrintfArgType_uint" \
		--suppress="invalidTestForOverflow" \
		--suppress="preprocessorErrorDirective" \
		--suppress="shiftTooManyBitsSigned" \
		--suppress="syntaxError" \
		--suppress="uninitvar" \
		--suppress="zerodiv" \
		--check-level=exhaustive \
		--error-exitcode=89 --std=c89 src/*.c hal/*.c hal/spi/*.c hal/uart/*.c

otp: tools/keytools/otp/otp-keystore-primer.bin FORCE

otpgen:
	make -C tools/keytools/otp otp-keystore-gen

tools/keytools/otp/otp-keystore-primer.bin: FORCE
	make -C tools/keytools/otp clean
	make -C tools/keytools/otp

secondary: $(SECONDARY_PRIVATE_KEY)

%.o:%.c
	@echo "\t[CC $(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

%.o:%.S
	@echo "\t[AS $(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

src/x86/fsp_s.o: $(FSP_S_BIN)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386 --rename-section .data=.fsp_s $^ $@

pico-sdk-info: FORCE
	@echo "To complete the build, check IDE/pico-sdk/rp2350"

## SBOM generation
# Usage: make sbom TARGET=<target> SIGN=<scheme> [HASH=SHA256] [EXT_FLASH=0]
#
# TARGET and SIGN select the build configuration.  They come from the same
# place as a normal build (command line, environment, or .config) and fall
# back to the tools/config.mk defaults (stm32f4 / ED25519) when unset.  The
# recipe echoes the effective target/sign so a default build is visible; pass
# them explicitly to get an SBOM that reflects your actual configuration.
#
# This is the plain-Make / arch.mk entry point.  It also covers every build
# that is really the Makefile with a vendor SDK bolted on via source/include
# paths (MCUXpresso, STM32Cube, PSoC6, Freedom-E-SDK, Vorago) and the IDE
# targets that also have an arch.mk path (TI Hercules, Renesas RX, Zynq).  It
# extracts the configuration-specific source list from OBJS (fully assembled by
# this point: core wolfBoot + wolfcrypt + HAL) and passes it, together with the
# build CFLAGS, to the vendored wolfGlass driver under tools/sbom/.
#
# wolfcrypt sources are compiled directly into the wolfBoot image.  They stay
# in the source-set hash, and are also declared as a wolfcrypt component so a
# CPE-driven scan can match the registered NVD product wolfssl:wolfcrypt.
#
# Optional make variables:
#   HOSTCC                 Host C compiler for macro capture (default: cc)
#   SBOM_GEN               Path to gen-sbom
#                          (default: tools/sbom/gen-sbom via driver discovery)
#   CRA_PYTHON             Python interpreter (default: python3)

HOSTCC?=cc
WOLFBOOT_VERSION:=$(shell sed -n \
    's/.*LIBWOLFBOOT_VERSION_STRING[[:space:]]*"\([^"]*\)".*/\1/p' \
    include/wolfboot/version.h)
SBOM_ROOT:=$(WOLFBOOT_ROOT)
SBOM_NAME:=wolfboot
# src/keystore.c carries the public keys that authorise a firmware update.  The
# build generates it from the signing key, so a fresh tree does not hold it yet.
# It is still a compiled source, and a bootloader SBOM that omits its own trust
# anchor describes the wrong image, so name it and depend on it below.
SBOM_GENERATED_SRCS=$(filter ./src/keystore.c src/keystore.c,\
  $(patsubst %.o,%.c,$(OBJS)))
# $(wildcard) cannot build this list.  GNU make 3.81 caches directory contents,
# so a source generated during the same run stays invisible for the rest of it;
# $(shell) re-reads the filesystem instead.  The generated sources go in
# unconditionally because sbom.mk expands this list through $(call) while
# reading the makefile, which is before any recipe can create them.  The driver
# opens the file afterwards, by which time the guard has made it exist.
SBOM_SRCS=$(sort $(SBOM_GENERATED_SRCS) $(shell for o in $(OBJS); do \
    for e in c S; do s="$${o%.o}.$$e"; [ -f "$$s" ] && printf '%s ' "$$s"; done; \
  done))
# A source that is absent must not be dropped quietly.  Filtering the list to
# what happens to be on disk shrinks the document with no diagnostic anywhere:
# a sim-tpm build without lib/wolfTPM checked out loses all eight tpm2*.c
# sources, emits nothing, and still validates.  An object that maps to no
# source is the signal, so compare the two lists and stop.
SBOM_SRCS_MISSING=$(filter-out $(basename $(SBOM_SRCS)),$(basename $(OBJS)))
SBOM_CFLAGS=$(CFLAGS)
# wolfBoot's wolfCrypt configuration is derived, not literal: include/user_settings.h
# turns WOLFBOOT_SIGN_ECC256 into HAVE_ECC, HAVE_ECC256, ECC_TIMING_RESISTANT and
# the rest. Capturing CFLAGS alone would record the -D set and none of what it
# selects, so the SBOM would report a bootloader with no signature algorithm.
SBOM_SETTINGS_H:=$(WOLFBOOT_LIB_WOLFSSL)/wolfssl/wolfcrypt/settings.h
SBOM_INCLUDE_DIRS:=$(WOLFBOOT_ROOT)/include $(WOLFBOOT_LIB_WOLFSSL)
# user_settings.h includes the generated target.h, so it must exist before the
# capture runs. It also carries the flash layout the SBOM records.
SBOM_PREREQS:=include/target.h sbom-check-sources
# Coat: wolfssl (TLS/library CPE) + wolfcrypt (crypto CPE). Sources remain in
# the merkle hash; the components give scanners resolvable identifiers.
SBOM_DEP_WOLFSSL?=yes
SBOM_DEP_WOLFCRYPT?=yes
SBOM_WOLFSSL_VERSION?=$(shell sed -n \
    's/.*LIBWOLFSSL_VERSION_STRING[[:space:]]*"\([^"]*\)".*/\1/p' \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfssl/version.h 2>/dev/null | head -1)
SBOM_VERSION=$(WOLFBOOT_VERSION)
SBOM_LICENSE_FILE=$(WOLFBOOT_ROOT)/LICENSE
# wolfBoot is a bootloader flashed as an image, not a library linked into one.
SBOM_COMPONENT_TYPE?=firmware
# LICENSE is the verbatim GPLv3, which says nothing about how wolfBoot licenses
# under it, so inference falls back to GPL-3.0-only and understates the grant.
# Every GPL-headered source says "either version 3 ... or (at your option) any
# later version".
SBOM_LICENSE_OVERRIDE?=GPL-3.0-or-later
# One version of wolfBoot has about 100 configurations, and each one is a
# different image with a different source set.  Name the document after the
# configuration so a second target does not overwrite the first.  gen-sbom
# folds the build configuration into the serialNumber and the SPDX
# documentNamespace too, so the documents stay distinct inside a scanner and
# not only on disk.
SBOM_CONFIG_TAG:=$(TARGET)$(if $(SIGN),-$(SIGN))$(if $(HASH),-$(HASH))
SBOM_CDX_OUT:=wolfboot-$(SBOM_CONFIG_TAG)-$(WOLFBOOT_VERSION).cdx.json
SBOM_SPDX_OUT:=wolfboot-$(SBOM_CONFIG_TAG)-$(WOLFBOOT_VERSION).spdx.json
SBOM_GEN?=

# Guards both SBOM targets: see SBOM_SRCS_MISSING above.  SBOM_ALLOW_MISSING=1
# accepts the partial document, and still lists what is absent.
sbom-check-sources: $(SBOM_GENERATED_SRCS)
	$(Q)if [ -n "$(strip $(SBOM_SRCS_MISSING))" ]; then \
	    echo "sbom: $(words $(SBOM_SRCS_MISSING)) object(s) map to no source on disk:" >&2; \
	    for o in $(SBOM_SRCS_MISSING); do echo "  $$o.c (or .S)" >&2; done; \
	    if [ "$(SBOM_ALLOW_MISSING)" = "1" ]; then \
	        echo "sbom: SBOM_ALLOW_MISSING=1 set; the SBOM will describe fewer" >&2; \
	        echo "sbom: sources than the image really holds." >&2; \
	    else \
	        echo "sbom: the SBOM would under-report the image.  A submodule is" >&2; \
	        echo "sbom: absent (git submodule update --init), a vendor SDK lives" >&2; \
	        echo "sbom: outside the tree, or the build generates that source and" >&2; \
	        echo "sbom: has not run yet.  Set SBOM_ALLOW_MISSING=1 to accept it." >&2; \
	        exit 1; \
	    fi; \
	fi

include tools/sbom/build/sbom.mk

## Per-HAL SBOM
# Emits a standalone SBOM whose component is the HAL layer for the selected
# TARGET (hal/hal.c, hal/$(TARGET).c, and any target flash/uart/board drivers),
# separate from the full bootloader SBOM.  Uses the same build config (CFLAGS)
# so the captured macros match the real build.  Run once per TARGET.
SBOM_HAL_NAME:=wolfboot-hal-$(TARGET)
SBOM_HAL_SRCS=$(filter hal/%,$(patsubst ./%,%,$(wildcard $(patsubst %.o,%.c,$(OBJS)) $(patsubst %.o,%.S,$(OBJS)))))
SBOM_HAL_CFLAGS=$(CFLAGS)
SBOM_HAL_SETTINGS_H:=$(SBOM_SETTINGS_H)
SBOM_HAL_INCLUDE_DIRS:=$(SBOM_INCLUDE_DIRS)
SBOM_HAL_PREREQS:=$(SBOM_PREREQS)
SBOM_HAL_VERSION:=$(WOLFBOOT_VERSION)
SBOM_HAL_LICENSE_FILE:=$(WOLFBOOT_ROOT)/LICENSE
# Same sources, same grant: without this the HAL SBOM would infer
# GPL-3.0-only and contradict the bootloader SBOM built from the same tree.
SBOM_HAL_LICENSE_OVERRIDE:=$(SBOM_LICENSE_OVERRIDE)
SBOM_HAL_CDX_OUT:=wolfboot-hal-$(TARGET)-$(WOLFBOOT_VERSION).cdx.json
SBOM_HAL_SPDX_OUT:=wolfboot-hal-$(TARGET)-$(WOLFBOOT_VERSION).spdx.json
SBOM_HAL_GEN:=$(SBOM_GEN)
$(eval $(call wolfglass_sbom_rule,sbom-hal,SBOM_HAL_))

FORCE:

.PHONY: FORCE clean keytool_check squashelf_check sbom sbom-hal sbom-check-sources
