-include ../../../.config
-include ../../../tools/config.mk
-include ../../../options.mk
-include ../../../wcs/pkcs11.mk

TARGET?=none
ARCH?=ARM
CROSS_COMPILE?=arm-none-eabi-
CFLAGS+=-O0 -ggdb
CFLAGS+=-I. -I../../../ -I../../../include
CFLAGS+=-I./wcs
CFLAGS+=-DFLASH_OTP_ROT -D__FLASH_OTP_PRIMER
OBJS+=startup.o otp-keystore-primer.o ../../../src/keystore.o
LSCRIPT=target.ld
LDFLAGS+=$(CFLAGS) -T$(LSCRIPT) -lc -Wl,-Map=otp-keystore-primer.map

ifeq ($(TARGET),stm32h7)
    CFLAGS+=-DTARGET_stm32h7
    CFLAGS+=-mcpu=cortex-m7 -ffunction-sections -fdata-sections -fno-common -ffreestanding -nostartfiles
    OBJS+=../../../hal/stm32h7.o
endif
ifeq ($(TARGET),stm32h5)
    CFLAGS+=-DTARGET_stm32h5
    CFLAGS+=-mcpu=cortex-m33 -ffunction-sections -fdata-sections -fno-common -ffreestanding -nostartfiles
    OBJS+=../../../hal/stm32h5.o
endif
CC=$(CROSS_COMPILE)gcc
OBJCOPY?=$(CROSS_COMPILE)objcopy
SIZE?=$(CROSS_COMPILE)size


otp-keystore-primer.bin: otp-keystore-primer.elf
	@$(OBJCOPY) -O binary $(^) $(@)

otp-keystore-primer.elf: $(OBJS)
	@$(CC) -o otp-keystore-primer.elf $(LDFLAGS) $(CFLAGS) $(OBJS)
	@$(SIZE) $(@)
	

clean:
	@rm -rf $(OBJS) *.bin *.elf
