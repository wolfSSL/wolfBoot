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
CFLAGS:=-D"__WOLFBOOT"
CFLAGS+=-Werror -Wextra -Wno-array-bounds
LSCRIPT:=config/target.ld
LSCRIPT_FLAGS:=
LDFLAGS:=
SECURE_LDFLAGS:=
LD_START_GROUP:=-Wl,--start-group
LD_END_GROUP:=-Wl,--end-group
LSCRIPT_IN:=hal/$(TARGET).ld
V?=0
DEBUG?=0
DEBUG_UART?=0
LIBS=
SIGN_ALG=
OBJCOPY_FLAGS=
BIG_ENDIAN?=0
USE_GCC?=1
USE_GCC_HEADLESS?=1
FLASH_OTP_KEYSTORE?=0
BOOTLOADER_PARTITION_SIZE?=$$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET)))

OBJS:= \
	./src/string.o \
	./src/image.o \
	./src/libwolfboot.o \
	./hal/hal.o

ifeq ($(WOLFCRYPT_TZ_PSA),1)
OBJS+=./src/dice/dice.o
endif

ifneq ($(TARGET),library)
	OBJS+=./hal/$(TARGET).o
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

# Convert to absolute paths using abspath function
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))
WOLFBOOT_LIB_WOLFTPM:=$(abspath $(WOLFBOOT_LIB_WOLFTPM))
WOLFBOOT_LIB_WOLFPKCS11:=$(abspath $(WOLFBOOT_LIB_WOLFPKCS11))
WOLFBOOT_LIB_WOLFPSA:=$(abspath $(WOLFBOOT_LIB_WOLFPSA))
WOLFBOOT_LIB_WOLFHSM:=$(abspath $(WOLFBOOT_LIB_WOLFHSM))

# Export variables so they are available to sub-makefiles
export WOLFBOOT_LIB_WOLFSSL
export WOLFBOOT_LIB_WOLFTPM
export WOLFBOOT_LIB_WOLFPKCS11
export WOLFBOOT_LIB_WOLFPSA
export WOLFBOOT_LIB_WOLFHSM

## Architecture/CPU configuration
include arch.mk

# Parse config options
include options.mk

OBJS+=$(WOLFCRYPT_OBJS)
OBJS+=$(PUBLIC_KEY_OBJS)
OBJS+=$(WOLFHSM_OBJS)

CFLAGS+= \
  -I"." -I"include/" -I"$(WOLFBOOT_LIB_WOLFSSL)" \
  -Wno-array-bounds \
  -D"WOLFSSL_USER_SETTINGS" \
  -D"WOLFTPM_USER_SETTINGS"
CFLAGS+=$(WOLFPSA_CFLAGS)

# Setup default optimizations (for GCC)
ifeq ($(USE_GCC_HEADLESS),1)
  CFLAGS+=-Wall -Wextra -Wno-main -ffreestanding -nostartfiles
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
ifeq ($(ARCH),AURIX_TC3)
  ifneq ($(USE_GCC_HEADLESS),1)
    LSCRIPT_FLAGS+=-T $(LSCRIPT)
  endif
endif

# Environment variables for sign tool
SIGN_ENV=IMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE) \
		 WOLFBOOT_SECTOR_SIZE=$(WOLFBOOT_SECTOR_SIZE) \
		 ML_DSA_LEVEL=$(ML_DSA_LEVEL) \
		 IMAGE_SIGNATURE_SIZE=$(IMAGE_SIGNATURE_SIZE) \
		 LMS_LEVELS=$(LMS_LEVELS) \
		 LMS_HEIGHT=$(LMS_HEIGHT) \
		 LMS_WINTERNITZ=$(LMS_WINTERNITZ) \
		 XMSS_PARAMS=$(XMSS_PARAMS)


MAIN_TARGET=factory.bin
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
endif # TZEN=1

ifeq ($(TARGET),x86_64_efi)
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

ifeq ($(TARGET),sim)
    MAIN_TARGET:=wolfboot.bin tools/bin-assemble/bin-assemble test-app/image_v1_signed.bin internal_flash.dd
endif

ifeq ($(TARGET),nxp_p1021)
    MAIN_TARGET:=factory_wstage1.bin
endif
ifeq ($(TARGET),nxp_t1024)
    MAIN_TARGET:=factory_wstage1.bin
endif

ifeq ($(TARGET),sama5d3)
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
					--target=efi-app-x86_64 --subsystem=10 $^ $@
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.efi
	@echo

wolfboot.bin: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O binary $^ $@
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.elf
	@echo

test-app/image.bin: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)" ELF_FLASH_SCATTER="$(ELF_FLASH_SCATTER)"
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
	$(Q)(test $(SIGN) = NONE) || (test "$(CERT_CHAIN_VERIFY)" = "") || (test "$(USER_CERT_CHAIN)" != "") || (tools/scripts/sim-gen-dummy-chain.sh --algo $(CERT_CHAIN_GEN_ALGO) --leaf wolfboot_signing_private_key.der)
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

	$(Q)(test $(SIGN) = NONE) || $(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) \
		$(SECONDARY_SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) \
		$(SECONDARY_PRIVATE_KEY) 1 || true
	$(Q)(test $(SIGN) = NONE) && $(SIGN_ENV) $(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG) 1 || true

test-app/image.elf: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)" ELF_FLASH_SCATTER="$(ELF_FLASH_SCATTER)" image.elf
	$(Q)$(SIZE) test-app/image.elf

ifeq ($(ELF_FLASH_SCATTER),1)
test-app/image.elf: squashelf
endif

assemble_internal_flash.dd: FORCE
	$(Q)$(BINASSEMBLE) internal_flash.dd \
		0 wolfboot.bin \
		$$(($(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET))) test-app/image_v1_signed.bin \
		$$(($(WOLFBOOT_PARTITION_UPDATE_ADDRESS)-$(ARCH_FLASH_OFFSET))) /tmp/swap \
		$$(($(WOLFBOOT_PARTITION_SWAP_ADDRESS)-$(ARCH_FLASH_OFFSET))) /tmp/swap

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

wolfboot.elf: include/target.h $(LSCRIPT) $(OBJS) $(BINASSEMBLE) FORCE
	$(Q)(test $(SIGN) = NONE) || (test $(FLASH_OTP_KEYSTORE) = 1) || (grep -q $(SIGN_ALG) src/keystore.c) || \
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
		sed -e "s/@FSP_S_LOAD_BASE@/$(FSP_S_LOAD_BASE)/g" \
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
	$(Q)rm -f src/*.o hal/*.o hal/spi/*.o test-app/*.o src/x86/*.o
	$(Q)rm -f src/wc_secure_calls.o
	$(Q)rm -f $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/*.o $(WOLFBOOT_LIB_WOLFTPM)/src/*.o $(WOLFBOOT_LIB_WOLFTPM)/hal/*.o $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/*.o
	$(Q)rm -f $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/Renesas/*.o
	$(Q)rm -f wolfboot.bin wolfboot.elf wolfboot.map test-update.rom wolfboot.hex wolfboot.srec factory.srec
	$(Q)rm -f $(MACHINE_OBJ) $(MAIN_TARGET) $(LSCRIPT)
	$(Q)rm -f $(OBJS)
	$(Q)rm -f tools/keytools/otp/otp-keystore-gen
	$(Q)rm -f .stack_usage
	$(Q)rm -f $(WH_NVM_BIN) $(WH_NVM_HEX)
	$(Q)rm -f test-lib
	$(Q)rm -f lib-fs
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


cppcheck:
	cppcheck -f --enable=warning --enable=portability \
		--suppress="ctunullpointer" --suppress="nullPointer" \
		--suppress="objectIndex" --suppress="comparePointers" \
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

FORCE:

.PHONY: FORCE clean keytool_check squashelf_check
