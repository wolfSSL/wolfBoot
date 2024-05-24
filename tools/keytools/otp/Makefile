-include ../../../.config
-include ../../../tools/config.mk
-include ../../../options.mk
-include ../../../wcs/pkcs11.mk
V?=0
ifeq ($(V),0)
  Q=@
endif

TARGET?=none
ARCH?=ARM
CROSS_COMPILE?=arm-none-eabi-
CFLAGS+=-O0 -ggdb
CFLAGS+=-I. -I../../../ -I../../../include
CFLAGS+=-I./wcs
CFLAGS+=-DFLASH_OTP_KEYSTORE -D__FLASH_OTP_PRIMER
PRI_KS_OBJS+=startup.o otp-keystore-primer.o ../../../src/keystore.o
LSCRIPT=target.ld
LDFLAGS+=$(CFLAGS) -T$(LSCRIPT) -lc -Wl,-Map=otp-keystore-primer.map

ifeq ($(TARGET),stm32h7)
    CFLAGS+=-DTARGET_stm32h7
    CFLAGS+=-mcpu=cortex-m7 -ffunction-sections -fdata-sections -fno-common -ffreestanding -nostartfiles
    PRI_KS_OBJS+=../../../hal/stm32h7.o
endif
ifeq ($(TARGET),stm32h5)
    CFLAGS+=-DTARGET_stm32h5
    CFLAGS+=-mcpu=cortex-m33 -ffunction-sections -fdata-sections -fno-common -ffreestanding -nostartfiles
    PRI_KS_OBJS+=../../../hal/stm32h5.o
endif
CC=$(CROSS_COMPILE)gcc
OBJCOPY?=$(CROSS_COMPILE)objcopy
SIZE?=$(CROSS_COMPILE)size

otp-keystore-primer.bin: otp-keystore-primer.elf
	$(Q)$(OBJCOPY) -O binary $(^) $(@)

otp-keystore-primer.elf: $(PRI_KS_OBJS)
	$(Q)$(CC) -o otp-keystore-primer.elf $(LDFLAGS) $(CFLAGS) $(PRI_KS_OBJS)
	$(Q)$(SIZE) $(@)

%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<


	

clean:
	$(Q)rm -rf $(PRI_KS_OBJS) *.bin *.elf
