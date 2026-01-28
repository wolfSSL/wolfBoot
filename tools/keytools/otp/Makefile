-include ../../../.config
-include ../../../tools/config.mk
-include ../../../options.mk
-include ../../../wcs/pkcs11.mk
V?=0
ifeq ($(V),0)
  Q=@
endif

WOLFBOOT_LIB_WOLFSSL?=../../../lib/wolfssl
OTP_GEN_DEFS=

TARGET?=none
ARCH?=ARM
CROSS_COMPILE?=arm-none-eabi-
CFLAGS+=-O0 -ggdb
CFLAGS+=-I. -I../../../ -I../../../include -I$(WOLFBOOT_LIB_WOLFSSL) \
    -I$(WOLFBOOT_LIB_WOLFSSL)/wolfssl
CFLAGS+=-I./wcs
CFLAGS+=-DFLASH_OTP_KEYSTORE -D__FLASH_OTP_PRIMER -DWOLFSSL_USER_SETTINGS -DWOLFCRYPT_SECURE_MODE
PRI_KS_OBJS+=startup.o otp-keystore-primer.o ../../../src/keystore.o

ifeq ($(HASH),SHA256)
    PRI_KS_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.o
endif
ifeq ($(HASH),SHA384)
    PRI_KS_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.o
endif
ifeq ($(HASH),SHA3)
    PRI_KS_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o
endif
LSCRIPT=target.ld
LDFLAGS+=$(CFLAGS) -T$(LSCRIPT) -lc -Wl,-Map=otp-keystore-primer.map

ifeq ($(TARGET),stm32h7)
    CFLAGS+=-DTARGET_stm32h7
    CFLAGS+=-mcpu=cortex-m7 -ffunction-sections -fdata-sections -fno-common -ffreestanding -nostartfiles
    PRI_KS_OBJS+=stm32h7.o
    OTP_GEN_DEFS+=-DTARGET_stm32h7
endif
ifeq ($(TARGET),stm32h5)
    CFLAGS+=-DTARGET_stm32h5
    CFLAGS+=-mcpu=cortex-m33 -ffunction-sections -fdata-sections -fno-common -ffreestanding -nostartfiles
    PRI_KS_OBJS+=stm32h5.o stm32_tz.o
    OTP_GEN_DEFS+=-DTARGET_stm32h5
endif
CC=$(CROSS_COMPILE)gcc
OBJCOPY?=$(CROSS_COMPILE)objcopy
SIZE?=$(CROSS_COMPILE)size

ifeq ($(ENABLE_OTP_WP),1)
	CFLAGS+=-DENABLE_OTP_WP
endif

all: otp-keystore-primer.bin otp-keystore-gen

otp-keystore-gen: otp-keystore-gen.c
	gcc -o $@ otp-keystore-gen.c ../../../src/keystore.c -I. -I../../../ \
        -I../../../include -I$(WOLFBOOT_LIB_WOLFSSL) \
        -I$(WOLFBOOT_LIB_WOLFSSL)/wolfssl -DFLASH_OTP_KEYSTORE $(OTP_GEN_DEFS)


otp-keystore-primer.bin: otp-keystore-primer.elf
	$(Q)$(OBJCOPY) -O binary $(^) $(@)

otp-keystore-primer.elf: $(PRI_KS_OBJS)
	$(Q)$(CC) -o otp-keystore-primer.elf $(LDFLAGS) $(CFLAGS) $(PRI_KS_OBJS)
	$(Q)$(SIZE) $(@)

%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

stm32h7.o: ../../../hal/stm32h7.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<


stm32h5.o: ../../../hal/stm32h5.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

stm32_tz.o: ../../../hal/stm32_tz.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<




clean:
	$(Q)rm -rf $(PRI_KS_OBJS) *.bin *.elf
