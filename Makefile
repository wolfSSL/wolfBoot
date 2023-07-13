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
CFLAGS+=-Werror -Wextra
LSCRIPT:=config/target.ld
LSCRIPT_FLAGS:=
LDFLAGS:=
LD_START_GROUP:=-Wl,--start-group
LD_END_GROUP:=-Wl,--end-group
LSCRIPT_IN:=hal/$(TARGET).ld
V?=0
DEBUG?=0
DEBUG_UART?=0

OBJS:= \
	./hal/$(TARGET).o \
	./src/string.o \
	./src/image.o \
	./src/libwolfboot.o

ifeq ($(SIGN),NONE)
  PRIVATE_KEY=
else
  PRIVATE_KEY=wolfboot_signing_private_key.der
  OBJS+=./src/keystore.o
endif

WOLFCRYPT_OBJS:=
PUBLIC_KEY_OBJS:=
ifneq ("$(NO_LOADER)","1")
  OBJS+=./src/loader.o
endif

## Architecture/CPU configuration
include arch.mk

# Parse config options
include options.mk

OBJS+=$(WOLFCRYPT_OBJS)
OBJS+=$(PUBLIC_KEY_OBJS)
OBJS+=$(UPDATE_OBJS)

CFLAGS+= \
  -I"." -I"include/" -I"lib/wolfssl" \
  -D"WOLFSSL_USER_SETTINGS" \
  -D"WOLFTPM_USER_SETTINGS" \
  -D"PLATFORM_$(TARGET)"

# Setup default optimizations (for GCC)
ifeq ($(USE_GCC_HEADLESS),1)
  CFLAGS+=-Wall -Wextra -Wno-main -ffreestanding -Wno-unused -nostartfiles
  CFLAGS+=-ffunction-sections -fdata-sections -fomit-frame-pointer
  LDFLAGS+=-Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles
  # Not setting LDFLAGS directly since it is passed to the test-app
  LSCRIPT_FLAGS+=-T $(LSCRIPT)
endif

MAIN_TARGET=factory.bin
TARGET_H_TEMPLATE:=include/target.h.in

ifeq ($(TARGET),stm32l5)
	# Don't build a contiguous image
	MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),stm32u5)
	# Don't build a contiguous image
	MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ifeq ($(TARGET),x86_64_efi)
	MAIN_TARGET:=wolfboot.efi
endif

ifeq ($(FSP), 1)
	MAIN_TARGET:=wolfboot_stage1.bin
endif

ifeq ($(TARGET),library)
	CFLAGS+=-g
	MAIN_TARGET:=test-lib
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

ASFLAGS:=$(CFLAGS)
BOOTLOADER_PARTITION_SIZE?=$$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET)))

all: $(MAIN_TARGET)

stage1: stage1/loader_stage1.bin
stage1/loader_stage1.bin: FORCE
	@echo "\t[BIN] $@"
	$(Q)$(MAKE) -C $(dir $@) $(notdir $@)

test-lib: $(OBJS)
	$(Q)$(CC) $(CFLAGS) -o $@ $^

wolfboot.efi: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY)  -j .text -j .sdata -j .data \
					-j .dynamic -j .dynsym  -j .rel \
					-j .rela -j .reloc -j .eh_frame \
					--target=efi-app-x86_64 --subsystem=10 $^ $@
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.efi
	@echo

wolfboot.bin: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) --gap-fill $(FILL_BYTE) -O binary $^ $@
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.elf
	@echo

test-app/image.bin: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)"
	$(Q)$(SIZE) test-app/image.elf

standalone:
	$(Q)$(MAKE) -C test-app TARGET=$(TARGET) EXT_FLASH=$(EXT_FLASH) SPI_FLASH=$(SPI_FLASH) QSPI_FLASH=$(QSPI_FLASH) OCTOSPI_FLASH=$(OCTOSPI_FLASH) ARCH=$(ARCH) \
    NO_XIP=$(NO_XIP) V=$(V) RAM_CODE=$(RAM_CODE) WOLFBOOT_VERSION=$(WOLFBOOT_VERSION)\
	MCUXPRESSO=$(MCUXPRESSO) MCUXPRESSO_CPU=$(MCUXPRESSO_CPU) MCUXPRESSO_DRIVERS=$(MCUXPRESSO_DRIVERS) \
	MCUXPRESSO_CMSIS=$(MCUXPRESSO_CMSIS) NVM_FLASH_WRITEONCE=$(NVM_FLASH_WRITEONCE) \
	FREEDOM_E_SDK=$(FREEDOM_E_SDK) standalone
	$(Q)$(OBJCOPY) --gap-fill $(FILL_BYTE) -O binary test-app/image.elf standalone.bin
	$(Q)$(SIZE) test-app/image.elf

include tools/test.mk
include tools/test-enc.mk
include tools/test-delta.mk
include tools/test-renode.mk

PYTHON?=python3
keytools_check:
	$(Q)(test -x "$(KEYGEN_TOOL)") || \
	($(PYTHON) -c "import wolfcrypt"  > /dev/null 2>&1) || \
	 (echo "ERROR: Key tool unavailable '$(KEYGEN_TOOL)'.\n"\
		"Run 'make keytools' or install wolfcrypt 'pip3 install wolfcrypt'"  && false)


$(PRIVATE_KEY):
	$(Q)$(MAKE) keytools_check
	$(Q)(test $(SIGN) = NONE) || ($(KEYGEN_TOOL) $(KEYGEN_OPTIONS) -g $(PRIVATE_KEY)) || true
	$(Q)(test $(SIGN) = NONE) && (echo "// SIGN=NONE" >  src/keystore.c) || true

keytools:
	@$(MAKE) -C tools/keytools -s clean
	@$(MAKE) -C tools/keytools

test-app/image_v1_signed.bin: $(BOOT_IMG)
	@echo "\t[SIGN] $(BOOT_IMG)"
	$(Q)(test $(SIGN) = NONE) || $(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) 1
	$(Q)(test $(SIGN) = NONE) && $(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG) 1 || true

test-app/image.elf: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)" image.elf
	$(Q)$(SIZE) test-app/image.elf

internal_flash.dd: $(BINASSEMBLE) wolfboot.elf test-app/image_v1_signed.bin
	@echo "\t[MERGE] internal_flash.dd"
	$(Q)dd if=/dev/zero bs=1 count=$$(($(WOLFBOOT_SECTOR_SIZE))) > /tmp/swap
	$(Q)$(BINASSEMBLE) $@ 0 test-app/image_v1_signed.bin \
		$(WOLFBOOT_PARTITION_SIZE) /tmp/swap \
		$$(($(WOLFBOOT_PARTITION_SIZE)*2)) /tmp/swap

factory.bin: $(BINASSEMBLE) wolfboot.bin $(BOOT_IMG) $(PRIVATE_KEY) test-app/image_v1_signed.bin
	@echo "\t[MERGE] $@"
	$(Q)$(BINASSEMBLE) $@ \
		$(WOLFBOOT_ORIGIN) wolfboot.bin \
		$(WOLFBOOT_PARTITION_BOOT_ADDRESS) test-app/image_v1_signed.bin

factory_wstage1.bin: $(BINASSEMBLE) stage1/loader_stage1.bin wolfboot.bin $(BOOT_IMG) $(PRIVATE_KEY) test-app/image_v1_signed.bin
	@echo "\t[MERGE] $@"
	$(Q)$(BINASSEMBLE) $@ \
		$(WOLFBOOT_STAGE1_FLASH_ADDR) stage1/loader_stage1.bin \
		$(WOLFBOOT_ORIGIN) wolfboot.bin \
		$(WOLFBOOT_PARTITION_BOOT_ADDRESS) test-app/image_v1_signed.bin

# stage1 linker script embed wolfboot.bin inside stage1/loader_stage1.bin
wolfboot_stage1.bin: wolfboot.bin stage1/loader_stage1.bin
	$(Q) cp stage1/loader_stage1.bin wolfboot_stage1.bin

wolfboot.elf: include/target.h $(LSCRIPT) $(OBJS) $(BINASSEMBLE) FORCE
	$(Q)(test $(SIGN) = NONE) || (grep -q $(SIGN) src/keystore.c) || (echo "Key mismatch: please run 'make distclean' to remove all keys if you want to change algorithm" && false)
	@echo "\t[LD] $@"
	@echo $(OBJS)
	$(Q)$(LD) $(LDFLAGS) $(LSCRIPT_FLAGS) $(LD_START_GROUP) $(OBJS) $(LD_END_GROUP) -o $@

$(LSCRIPT): $(LSCRIPT_IN) FORCE
	@(test $(LSCRIPT_IN) != NONE) || (echo "Error: no linker script" \
		"configuration found. If you selected Encryption and RAM_CODE, then maybe" \
		"the encryption algorithm is not yet supported with bootloader updates." \
		&& false)
	@(test -r $(LSCRIPT_IN)) || (echo "Error: no RAM/ChaCha linker script found." \
		"If you selected Encryption and RAM_CODE, ensure that you have a" \
		"custom linker script (i.e. $(TARGET)_chacha_ram.ld). Please read " \
		"docs/encrypted_partitions.md for more information" && false)
	@cat $(LSCRIPT_IN) | \
		sed -e "s/@ARCH_FLASH_OFFSET@/$(ARCH_FLASH_OFFSET)/g" | \
		sed -e "s/@BOOTLOADER_PARTITION_SIZE@/$(BOOTLOADER_PARTITION_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_ORIGIN@/$(WOLFBOOT_ORIGIN)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$(WOLFBOOT_PARTITION_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_SIZE@/$(WOLFBOOT_STAGE1_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_LOAD_ADDR@/$(WOLFBOOT_STAGE1_LOAD_ADDR)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_FLASH_ADDR@/$(WOLFBOOT_STAGE1_FLASH_ADDR)/g" | \
		sed -e "s/@WOLFBOOT_STAGE1_BASE_ADDR@/$(WOLFBOOT_STAGE1_BASE_ADDR)/g" | \
		sed -e "s/@WOLFBOOT_LOAD_BASE@/$(WOLFBOOT_LOAD_BASE)/g" | \
		sed -e "s/@BOOTLOADER_START@/$(BOOTLOADER_START)/g" \
		> $@

hex: wolfboot.hex

%.hex:%.elf
	@echo "\t[ELF2HEX] $@"
	@$(OBJCOPY) -O ihex $^ $@

src/keystore.c: $(PRIVATE_KEY)

keys: $(PRIVATE_KEY)

clean:
	$(Q)rm -f src/*.o hal/*.o hal/spi/*.o lib/wolfssl/wolfcrypt/src/*.o test-app/*.o
	$(Q)rm -f *.bin *.elf wolfboot.map test-update.rom *.hex $(LSCRIPT)
	$(Q)rm -f src/x86/*.o $(MACHINE_OBJ) $(MAIN_TARGET)
	$(Q)rm -f lib/wolfTPM/src/*.o
	$(Q)$(MAKE) -C test-app -s clean
	$(Q)$(MAKE) -C tools/check_config -s clean
	$(Q)$(MAKE) -C stage1 -s clean

utilsclean: clean
	$(Q)$(MAKE) -C tools/keytools -s clean
	$(Q)$(MAKE) -C tools/delta -s clean
	$(Q)$(MAKE) -C tools/bin-assemble -s clean
	$(Q)$(MAKE) -C tools/elf-parser -s clean
	$(Q)$(MAKE) -C tools/check_config -s clean
	$(Q)$(MAKE) -C tools/test-expect-version -s clean
	$(Q)$(MAKE) -C tools/test-update-server -s clean
	$(Q)$(MAKE) -C tools/uart-flash-server -s clean
	$(Q)$(MAKE) -C tools/unit-tests -s clean

keysclean: clean
	$(Q)rm -f *.pem *.der tags ./src/*_pub_key.c ./src/keystore.c include/target.h

distclean: clean keysclean utilsclean
	$(Q)rm -f *.bin *.elf

include/target.h: $(TARGET_H_TEMPLATE) FORCE
	@cat $(TARGET_H_TEMPLATE) | \
	sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$(WOLFBOOT_PARTITION_SIZE)/g" | \
	sed -e "s/@WOLFBOOT_SECTOR_SIZE@/$(WOLFBOOT_SECTOR_SIZE)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_DTS_BOOT_ADDRESS@/$(WOLFBOOT_DTS_BOOT_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_DTS_UPDATE_ADDRESS@/$(WOLFBOOT_DTS_UPDATE_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_ADDRESS@/$(WOLFBOOT_LOAD_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_DTS_ADDRESS@/$(WOLFBOOT_LOAD_DTS_ADDRESS)/g" \
		> $@

delta: tools/delta/bmdiff

tools/delta/bmdiff: FORCE
	$(Q)$(MAKE) -C tools/delta

delta-test: FORCE
	$(Q)$(MAKE) -C tools/delta $@

elf-parser:
	@$(MAKE) -C tools/elf-parser -s clean
	@$(MAKE) -C tools/elf-parser

config: FORCE
	$(MAKE) -C config

check_config:
	$(MAKE) -C tools/check_config run

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

FORCE:

.PHONY: FORCE clean keytool_check
