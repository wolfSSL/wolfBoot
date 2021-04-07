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
CFLAGS:=-D__WOLFBOOT -DWOLFBOOT_VERSION=$(WOLFBOOT_VERSION)UL -ffunction-sections -fdata-sections
LSCRIPT:=config/target.ld
LDFLAGS:=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles
OBJS:= \
./hal/$(TARGET).o \
./src/loader.o \
./src/string.o \
./src/image.o \
./src/libwolfboot.o
WOLFCRYPT_OBJS:=
PUBLIC_KEY_OBJS:=
UPDATE_OBJS:=

## Architecture/CPU configuration
include arch.mk

# Parse config options
include options.mk

CFLAGS+=-Wall -Wextra -Wno-main -ffreestanding -Wno-unused \
  -I. -Iinclude/ -Ilib/wolfssl -nostartfiles \
  -DWOLFSSL_USER_SETTINGS \
  -DWOLFTPM_USER_SETTINGS \
  -DPLATFORM_$(TARGET)

MAIN_TARGET=factory.bin

ifeq ($(TARGET),stm32l5)
    # Don't build a contiguous image
	MAIN_TARGET:=wolfboot.bin test-app/image_v1_signed.bin
endif

ASFLAGS:=$(CFLAGS)

all: $(MAIN_TARGET)

wolfboot.bin: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) -O binary --gap-fill=255 $^ $@

align: wolfboot-align.bin

BOOTLOADER_PARTITION_SIZE=$$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET)))

wolfboot-align.bin: wolfboot.elf
	$(Q)$(OBJCOPY) -O binary --gap-fill=255 --pad-to=  wolfboot.elf $@
	echo $(BOOTLOADER_PARTITION_SIZE)
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.elf
	@echo

test-app/image.bin: wolfboot.bin
	$(Q)make -C test-app WOLFBOOT_ROOT=$(WOLFBOOT_ROOT)
	$(Q)rm -f src/*.o hal/*.o
	$(Q)$(SIZE) test-app/image.elf

standalone:
	$(Q)make -C test-app TARGET=$(TARGET) EXT_FLASH=$(EXT_FLASH) SPI_FLASH=$(SPI_FLASH) ARCH=$(ARCH) \
    NO_XIP=$(NO_XIP) V=$(V) RAM_CODE=$(RAM_CODE) WOLFBOOT_VERSION=$(WOLFBOOT_VERSION)\
	MCUXPRESSO=$(MCUXPRESSO) MCUXPRESSO_CPU=$(MCUXPRESSO_CPU) MCUXPRESSO_DRIVERS=$(MCUXPRESSO_DRIVERS) \
	MCUXPRESSO_CMSIS=$(MCUXPRESSO_CMSIS) NVM_FLASH_WRITEONCE=$(NVM_FLASH_WRITEONCE) \
	FREEDOM_E_SDK=$(FREEDOM_E_SDK) standalone
	$(Q)$(OBJCOPY) -O binary test-app/image.elf standalone.bin
	$(Q)$(SIZE) test-app/image.elf

include tools/test.mk
include tools/test-enc.mk

ed25519.der:
	$(Q)$(KEYGEN_TOOL) $(KEYGEN_OPTIONS) src/ed25519_pub_key.c
ecc256.der:
	$(Q)$(KEYGEN_TOOL) $(KEYGEN_OPTIONS) src/ecc256_pub_key.c
rsa2048.der:
	$(Q)$(KEYGEN_TOOL) $(KEYGEN_OPTIONS) src/rsa2048_pub_key.c
rsa4096.der:
	$(Q)$(KEYGEN_TOOL) $(KEYGEN_OPTIONS) src/rsa4096_pub_key.c

keytools:
	@make -C tools/keytools

test-app/image_v1_signed.bin: test-app/image.bin
	@echo "\t[SIGN] $(BOOT_IMG)"
	$(Q)$(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) 1

factory.bin: $(BOOT_IMG) wolfboot-align.bin $(PRIVATE_KEY) test-app/image_v1_signed.bin
	@echo "\t[MERGE] $@"
	$(Q)cat wolfboot-align.bin test-app/image_v1_signed.bin > $@

wolfboot.elf: include/target.h $(OBJS) $(LSCRIPT) FORCE
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) -Wl,--start-group $(OBJS) -Wl,--end-group -o $@

$(LSCRIPT): hal/$(TARGET).ld FORCE
	@cat hal/$(TARGET).ld | \
		sed -e "s/##WOLFBOOT_PARTITION_BOOT_ADDRESS##/$(BOOTLOADER_PARTITION_SIZE)/g" | \
		sed -e "s/##WOLFBOOT_ORIGIN##/$(WOLFBOOT_ORIGIN)/g" \
		> $@

hex: wolfboot.hex

%.hex:%.elf
	@echo "\t[ELF2HEX] $@"
	@$(OBJCOPY) -O ihex $^ $@

src/ed25519_pub_key.c: ed25519.der

src/ecc256_pub_key.c: ecc256.der

src/rsa2048_pub_key.c: rsa2048.der

src/rsa4096_pub_key.c: rsa4096.der

keys: $(PRIVATE_KEY)

clean:
	@find . -type f -name "*.o" | xargs rm -f
	@rm -f *.bin *.elf wolfboot.map *.bin  *.hex config/target.ld
	@make -C test-app clean
	@make -C tools/check_config clean

distclean: clean
	@rm -f *.pem *.der tags ./src/*_pub_key.c include/target.h
	@make -C tools/keytools clean

include/target.h: include/target.h.in FORCE
	@cat include/target.h.in | \
	sed -e "s/##WOLFBOOT_PARTITION_SIZE##/$(WOLFBOOT_PARTITION_SIZE)/g" | \
	sed -e "s/##WOLFBOOT_SECTOR_SIZE##/$(WOLFBOOT_SECTOR_SIZE)/g" | \
	sed -e "s/##WOLFBOOT_PARTITION_BOOT_ADDRESS##/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
	sed -e "s/##WOLFBOOT_PARTITION_UPDATE_ADDRESS##/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
	sed -e "s/##WOLFBOOT_PARTITION_SWAP_ADDRESS##/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" | \
	sed -e "s/##WOLFBOOT_DTS_BOOT_ADDRESS##/$(WOLFBOOT_DTS_BOOT_ADDRESS)/g" | \
	sed -e "s/##WOLFBOOT_DTS_UPDATE_ADDRESS##/$(WOLFBOOT_DTS_UPDATE_ADDRESS)/g" | \
	sed -e "s/##WOLFBOOT_LOAD_ADDRESS##/$(WOLFBOOT_LOAD_ADDRESS)/g" | \
	sed -e "s/##WOLFBOOT_LOAD_DTS_ADDRESS##/$(WOLFBOOT_LOAD_DTS_ADDRESS)/g" \
		> $@

config: FORCE
	make -C config

check_config:
	make -C tools/check_config


../src/libwolfboot.o: ../src/libwolfboot.c FORCE
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ ../src/libwolfboot.c

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

FORCE:

.PHONY: FORCE clean
