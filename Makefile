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
	./hal/hal.o \
	./hal/$(TARGET).o

ifeq ($(SIGN),NONE)
  PRIVATE_KEY=
else
  PRIVATE_KEY=wolfboot_signing_private_key.der
  ifeq ($(FLASH_OTP_KEYSTORE),1)
    OBJS+=./src/flash_otp_keystore.o
  else
    OBJS+=./src/keystore.o
  endif
endif

WOLFCRYPT_OBJS:=
SECURE_OBJS:=
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

CFLAGS+= \
  -I"." -I"include/" -I"lib/wolfssl" \
  -Wno-array-bounds \
  -D"WOLFSSL_USER_SETTINGS" \
  -D"WOLFTPM_USER_SETTINGS"

# Setup default optimizations (for GCC)
ifeq ($(USE_GCC_HEADLESS),1)
  CFLAGS+=-Wall -Wextra -Wno-main -ffreestanding -Wno-unused -nostartfiles
  CFLAGS+=-ffunction-sections -fdata-sections -fomit-frame-pointer
  LDFLAGS+=-Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles
  # Not setting LDFLAGS directly since it is passed to the test-app
  LSCRIPT_FLAGS+=-T $(LSCRIPT)
  OBJCOPY_FLAGS+=--gap-fill $(FILL_BYTE)
endif
ifeq ($(TARGET),ti_hercules)
  LSCRIPT_FLAGS+=--run_linker $(LSCRIPT)
endif


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
ifeq ($(TARGET),nxp_t1024)
    MAIN_TARGET:=factory_wstage1.bin
endif

ifeq ($(TARGET),sama5d3)
    MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif


ifeq ($(FLASH_OTP_KEYSTORE),1)
    MAIN_TARGET+=tools/keytools/otp/otp-keystore-primer.bin
endif

ifneq ($(SIGN_SECONDARY),)
  SECONDARY_PRIVATE_KEY=wolfboot_signing_second_private_key.der
  MAIN_TARGET+=$(SECONDARY_PRIVATE_KEY)
endif

ASFLAGS:=$(CFLAGS)

all: $(MAIN_TARGET)

stage1: stage1/loader_stage1.bin
stage1/loader_stage1.bin: wolfboot.elf
stage1/loader_stage1.bin: FORCE
	@echo "\t[BIN] $@"
	$(Q)$(MAKE) -C $(dir $@) $(notdir $@)

test-lib: include/target.h $(OBJS)
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJS)

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
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)"
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

keytools_check: keytools FORCE

$(PRIVATE_KEY):
	$(Q)$(MAKE) keytools_check
	$(Q)(test $(SIGN) = NONE) || ("$(KEYGEN_TOOL)" $(KEYGEN_OPTIONS) -g $(PRIVATE_KEY)) || true
	$(Q)(test $(SIGN) = NONE) && (echo "// SIGN=NONE" >  src/keystore.c) || true
	$(Q)(test "$(FLASH_OTP_KEYSTORE)" = "1") && (make -C tools/keytools/otp) || true

$(SECONDARY_PRIVATE_KEY):
	$(Q)$(MAKE) keytools_check
	$(Q)rm -f src/keystore.c
	$(Q)(test $(SIGN_SECONDARY) = NONE) || ("$(KEYGEN_TOOL)" \
		$(KEYGEN_OPTIONS) -i $(PRIVATE_KEY) $(SECONDARY_KEYGEN_OPTIONS) \
		-g $(SECONDARY_PRIVATE_KEY)) || true
	$(Q)(test "$(FLASH_OTP_KEYSTORE)" = "1") && (make -C tools/keytools/otp) || true

keytools: include/target.h
	@echo "Building key tools"
	@$(MAKE) -C tools/keytools -s clean
	@$(MAKE) -C tools/keytools -j

tpmtools: keys
	@echo "Building TPM tools"
	@$(MAKE) -C tools/tpm -s clean
	@$(MAKE) -C tools/tpm -j

swtpmtools:
	@echo "Building TPM tools"
	@$(MAKE) -C tools/tpm -s clean
	@$(MAKE) -C tools/tpm -j swtpm

test-app/image_v1_signed.bin: $(BOOT_IMG)
	@echo "\t[SIGN] $(BOOT_IMG)"
	@echo "\tSECONDARY_SIGN_OPTIONS=$(SECONDARY_SIGN_OPTIONS)"
	@echo "\tSECONDARY_PRIVATE_KEY=$(SECONDARY_PRIVATE_KEY)"

	$(Q)(test $(SIGN) = NONE) || "$(SIGN_TOOL)" $(SIGN_OPTIONS) \
		$(SECONDARY_SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) \
		$(SECONDARY_PRIVATE_KEY) 1 || true
	$(Q)(test $(SIGN) = NONE) && "$(SIGN_TOOL)" $(SIGN_OPTIONS) $(BOOT_IMG) 1 || true

test-app/image.elf: wolfboot.elf
	$(Q)$(MAKE) -C test-app WOLFBOOT_ROOT="$(WOLFBOOT_ROOT)" image.elf
	$(Q)$(SIZE) test-app/image.elf

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
wolfboot_stage1.bin: wolfboot.elf stage1/loader_stage1.bin
	$(Q) cp stage1/loader_stage1.bin wolfboot_stage1.bin

wolfboot.elf: include/target.h $(LSCRIPT) $(OBJS) $(BINASSEMBLE) FORCE
	$(Q)(test $(SIGN) = NONE) || (test $(FLASH_OTP_KEYSTORE) = 1) || (grep -q $(SIGN_ALG) src/keystore.c) || \
		(echo "Key mismatch: please run 'make distclean' to remove all keys if you want to change algorithm" && false)
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
		sed -e "s/@IMAGE_HEADER_SIZE@/$(IMAGE_HEADER_SIZE)/g" \
		> $@

hex: wolfboot.hex
srec: wolfboot.srec

%.hex:%.elf
	@echo "\t[ELF2HEX] $@"
	@$(OBJCOPY) -O ihex $^ $@

%.srec:%.elf
	@echo "\t[ELF2SREC] $@"
	@$(OBJCOPY) -O srec $^ $@

src/keystore.c: $(PRIVATE_KEY)

flash_keystore: src/flash_otp_keystore.o

src/flash_otp_keystore.o: $(PRIVATE_KEY) src/flash_otp_keystore.c
	$(Q)$(MAKE) src/keystore.c
	$(Q)$(CC) -c $(CFLAGS) src/flash_otp_keystore.c -o $(@)

keys: $(PRIVATE_KEY)

clean:
	$(Q)rm -f src/*.o hal/*.o hal/spi/*.o test-app/*.o src/x86/*.o
	$(Q)rm -f lib/wolfssl/wolfcrypt/src/*.o lib/wolfTPM/src/*.o lib/wolfTPM/hal/*.o
	$(Q)rm -f lib/wolfssl/wolfcrypt/src/port/Renesas/*.o
	$(Q)rm -f wolfboot.bin wolfboot.elf wolfboot.map test-update.rom wolfboot.hex
	$(Q)rm -f $(MACHINE_OBJ) $(MAIN_TARGET) $(LSCRIPT)
	$(Q)rm -f $(OBJS)
	$(Q)rm -f tools/keytools/otp/otp-keystore-gen
	$(Q)$(MAKE) -C test-app -s clean
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
	$(Q)$(MAKE) -C tools/keytools/otp -s clean

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
	cloc --force-lang-def cloc_lang_def.txt src/boot_x86_fsp.c src/boot_x86_fsp_payload.c src/boot_x86_fsp_start.S src/image.c src/keystore.c src/libwolfboot.c src/loader.c src/string.c src/update_disk.c src/x86/ahci.c src/x86/ata.c src/x86/common.c src/x86/gpt.c src/x86/hob.c src/pci.c src/x86/tgl_fsp.c hal/x86_fsp_tgl.c hal/x86_uart.c

cppcheck:
	cppcheck -f --enable=warning --enable=portability \
		--suppress="ctunullpointer" --suppress="nullPointer" \
		--suppress="objectIndex" --suppress="comparePointers" \
		--error-exitcode=89 --std=c89 src/*.c hal/*.c hal/spi/*.c hal/uart/*.c

otp: tools/keytools/otp/otp-keystore-primer.bin FORCE

otpgen:
	make -C tools/keytools/otp otp-keystore-gen

tools/keytools/otp/otp-keystore-primer.bin: FORCE
	make -C tools/keytools/otp clean
	make -C tools/keytools/otp

secondary: $(SECONDARY_PRIVATE_KEY)

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

FORCE:

.PHONY: FORCE clean keytool_check
