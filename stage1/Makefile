## wolfBoot Makefile for stage 1 loader
#
# Configure by passing alternate values
# via environment variables.
#
# Configuration values: see tools/config.mk
-include ../.config
include ../tools/config.mk

## Initializers
WOLFBOOT_ROOT?=$(PWD)
CFLAGS:=-D"__WOLFBOOT"
CFLAGS+=-Werror -Wextra -nostdlib
LDFLAGS:=
LSCRIPT:=loader_stage1.ld
LSCRIPT_IN:=../hal/$(TARGET)_stage1.ld
LSCRIPT_FLAGS:=
LD_START_GROUP:=-Wl,--start-group
LD_END_GROUP:=-Wl,--end-group
V?=0
DEBUG?=0
DEBUG_UART?=0
USE_GCC?=1
USE_GCC_HEADLESS?=1

ifeq ($(ARCH),PPC)
OBJS:= \
	./hal/$(TARGET).o \
	./src/string.o \
	loader_stage1.o
endif

WOLFCRYPT_OBJS:=
PUBLIC_KEY_OBJS:=
# make mk scripts aware that we are building stage1
STAGE1=1
# Forcefully disable TPM support in first stage loader for PPC
ifeq ($(ARCH),PPC)
	WOLFTPM=0
	WOLFBOOT_TPM_KEYSTORE=0
	RAM_CODE=0
	UPDATE_OBJS:=
endif
ELF=0
MULTIBOOT2=0
LINUX_PAYLOAD=0


## Architecture/CPU configuration
include ../arch.mk

# Parse config options
include ../options.mk

ifeq ($(WOLFTPM),1)
  LS1_OBJS += \
    tpm_io.o \
    tpm2.o \
    tpm2_packet.o \
    tpm2_tis.o \
    tpm2_wrap.o \
    tpm2_param_enc.o \
    image.o
endif

CFLAGS+= \
  -I".." -I"../include/" -I"../lib/wolfssl" \
  -I"../lib/wolfTPM" \
  -D"WOLFSSL_USER_SETTINGS" \
  -D"WOLFTPM_USER_SETTINGS"

# Do not change platform auth in stage 1
CFLAGS+=-D"WOLFBOOT_TPM_NO_CHG_PLAT_AUTH"

# Setup default optimizations (for GCC)
ifeq ($(USE_GCC_HEADLESS),1)
  CFLAGS+=-Wall -Wextra -Wno-main -Wno-unused
  CFLAGS+=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,-gc-sections -Wl,-Map=loader_stage1.map
  LSCRIPT_FLAGS+=-T $(LSCRIPT)
  OBJCOPY_FLAGS+=--gap-fill $(FILL_BYTE)
endif

MAIN_TARGET=loader_stage1.bin
TARGET_H_TEMPLATE:=../include/target.h.in

ASFLAGS:=$(CFLAGS)
BOOTLOADER_PARTITION_SIZE?=$$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET)))

ifeq ($(WOLFBOOT_STAGE1_SIZE),)
  # default is end of 4KB region (0x0FFC)
  WOLFBOOT_STAGE1_SIZE=0x1000
endif

CFLAGS+=\
	-DBUILD_LOADER_STAGE1 \
	-DWOLFBOOT_STAGE1_SIZE=$(WOLFBOOT_STAGE1_SIZE) \
	-DWOLFBOOT_STAGE1_LOAD_ADDR=$(WOLFBOOT_STAGE1_LOAD_ADDR) \
	-DWOLFBOOT_STAGE1_FLASH_ADDR=$(WOLFBOOT_STAGE1_FLASH_ADDR) \
	-DWOLFBOOT_STAGE1_BASE_ADDR=$(WOLFBOOT_STAGE1_BASE_ADDR)

# For printf support (disable NO_PRINTF_UART) and increase WOLFBOOT_STAGE1_SIZE
ifeq ($(ARCH),PPC)
  CFLAGS+=-DNO_PRINTF_UART
  ifeq ($(NO_XIP),1)
    # Use PIC (Position Independent Code) for first stage loader
    CFLAGS+=-fPIC
   endif
endif


BUILD_DIR=.
LS1_OBJS=$(addprefix $(BUILD_DIR)/, $(notdir $(OBJS)))
vpath %.c ../src
vpath %.c ../hal
vpath %.c ../lib/wolfssl/wolfcrypt/src ../lib/wolfTPM/src
vpath %.c ../src/x86
vpath %.S ../src

# x86-64 FSP targets
ifeq ($(FSP), 1)
include ./x86_fsp.mk
endif

all: $(MAIN_TARGET)

loader_stage1.bin: loader_stage1.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O binary $^ $@
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) loader_stage1.elf
	@echo

loader_stage1.elf: ../include/target.h $(LSCRIPT) $(LS1_OBJS)
	@echo "\t[LD] $@"
	@echo $(LS1_OBJS)
	$(Q)$(LD) $(LDFLAGS) $(LSCRIPT_FLAGS) $(LD_START_GROUP) $(LS1_OBJS) $(LD_END_GROUP) -o $@

stage1: loader_stage1.bin
loader_stage1: loader_stage1.bin

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
		sed -e "s/@IMAGE_HEADER_SIZE@/$(IMAGE_HEADER_SIZE)/g"  \
		> $@

%.hex:%.elf
	@echo "\t[ELF2HEX] $@"
	$(Q)@$(OBJCOPY) -O ihex $^ $@

../include/target.h: $(TARGET_H_TEMPLATE) FORCE
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

$(BUILD_DIR)/%.o: %.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<
$(BUILD_DIR)/%.o: ../src/%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<
$(BUILD_DIR)/%.o: ../hal/%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<
$(BUILD_DIR)/%.o: ../lib/wolfssl/wolfcrypt/src/%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<

$(BUILD_DIR)/%.o: %.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<
$(BUILD_DIR)/%.o: ../src/%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<
$(BUILD_DIR)/%.o: ../hal/%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $<

clean:
	$(Q)rm -f *.o
	$(Q)rm -f *.bin
	$(Q)rm -f loader_stage1.bin loader_stage1.elf *.map $(LSCRIPT)

FORCE:

.PHONY: FORCE clean
