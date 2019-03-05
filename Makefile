CROSS_COMPILE:=arm-none-eabi-
CC:=$(CROSS_COMPILE)gcc
LD:=$(CROSS_COMPILE)gcc
AS:=$(CROSS_COMPILE)gcc
OBJCOPY:=$(CROSS_COMPILE)objcopy
SIZE:=$(CROSS_COMPILE)size
BOOT0_OFFSET?=`cat include/target.h |grep WOLFBOOT_PARTITION_BOOT_ADDRESS | sed -e "s/.*[ ]//g"`
BOOT_IMG?=test-app/image.bin
SIGN?=ED25519
TARGET?=stm32f4
KINETIS?=$(HOME)/src/FRDM-K64F/devices/MK64F12
KINETIS_CMSIS?=$(KINETIS)/../../CMSIS
DEBUG?=0
VTOR?=1
SWAP?=1
CORTEX_M0?=0
NO_ASM?=0
EXT_FLASH?=0
ALLOW_DOWNGRADE?=0

LSCRIPT:=hal/$(TARGET).ld

OBJS:= \
./hal/$(TARGET).o \
./src/loader.o \
./src/string.o \
./src/crypto.o \
./src/wolfboot.o \
./src/image.o \
./src/libwolfboot.o \
./lib/wolfssl/wolfcrypt/src/sha256.o \
./lib/wolfssl/wolfcrypt/src/hash.o \
./lib/wolfssl/wolfcrypt/src/wolfmath.o \
./lib/wolfssl/wolfcrypt/src/fe_low_mem.o

## Target specific configuration
ifeq ($(TARGET),samr21)
  CORTEX_M0=1
endif

## Signature
ifeq ($(SIGN),ECC256)
  KEYGEN_TOOL=tools/ecc256/ecc256_keygen
  SIGN_TOOL=tools/ecc256/ecc256_sign
  PRIVATE_KEY=ecc256.der
else
  KEYGEN_TOOL=tools/ed25519/ed25519_keygen
  SIGN_TOOL=tools/ed25519/ed25519_sign
  PRIVATE_KEY=ed25519.der
endif

MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/sp_int.o

ifeq ($(CORTEX_M0),1)
  CFLAGS:=-mcpu=cortex-m0
  MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
else
  ifeq ($(NO_ASM),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
    CFLAGS:=-mcpu=cortex-m3
  else
    CFLAGS:=-mcpu=cortex-m3 -DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM -fomit-frame-pointer
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
  endif
endif

ifeq ($(FASTMATH),1)
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/integer.o
  CFLAGS+=-DUSE_FAST_MATH
endif

CFLAGS+=-mthumb -Wall -Wextra -Wno-main -Wstack-usage=1024 -ffreestanding -Wno-unused \
	-Ilib/bootutil/include -Iinclude/ -Ilib/wolfssl -nostartfiles \
	-DWOLFSSL_USER_SETTINGS \
	-mthumb -mlittle-endian -mthumb-interwork \
	-DPLATFORM_$(TARGET)

ifeq ($(TARGET),kinetis)
  CFLAGS+=-I$(KINETIS)/drivers -I$(KINETIS) -DCPU_MK64FN1M0VLL12 -I$(KINETIS_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  OBJS+=$(KINETIS)/drivers/fsl_clock.o $(KINETIS)/drivers/fsl_ftfx_flash.o $(KINETIS)/drivers/fsl_ftfx_cache.o $(KINETIS)/drivers/fsl_ftfx_controller.o
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+=-DEXT_FLASH=1 -DPART_UPDATE_EXT=1 -DPART_SWAP_EXT=1
endif

ifeq ($(ALLOW_DOWNGRADE),1)
  CFLAGS+=-DALLOW_DOWNGRADE
endif

LDFLAGS:=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles -mcpu=cortex-m3 -mthumb
ASFLAGS:=$(CFLAGS)

ifeq ($(SIGN),ED25519)
  OBJS+= ./lib/wolfssl/wolfcrypt/src/sha512.o \
	./lib/wolfssl/wolfcrypt/src/ed25519.o \
	./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./src/ed25519_pub_key.o
  CFLAGS+=-DWOLFBOOT_SIGN_ED25519 -nostdlib -DWOLFSSL_STATIC_MEMORY
  LDFLAGS+=-nostdlib
endif

ifeq ($(SIGN),ECC256)
  OBJS+= \
    $(MATH_OBJS) \
	./lib/wolfssl/wolfcrypt/src/ecc.o \
	./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
	./lib/wolfssl/wolfcrypt/src/memory.o \
	./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./src/ecc256_pub_key.o \
    ./src/xmalloc.o
  CFLAGS+=-DWOLFBOOT_SIGN_ECC256 -DXMALLOC_USER
endif

ifeq ($(DEBUG),1)
    CFLAGS+=-O0 -g -ggdb3 -DDEBUG=1
else
    CFLAGS+=-Os
endif

ifeq ($(VTOR),0)
    CFLAGS+=-DNO_VTOR
endif


all: factory.bin


wolfboot.bin: wolfboot.elf
	$(OBJCOPY) -O binary $^ $@
	$(SIZE) wolfboot.elf

wolfboot.hex: wolfboot.elf
	$(OBJCOPY) -O ihex $^ $@

align: wolfboot-align.bin

wolfboot-align.bin: wolfboot.elf
	$(OBJCOPY) -O binary $^ $@ --pad-to=$(BOOT0_OFFSET) --gap-fill=255
	$(SIZE) wolfboot.elf

test-app/image.bin:
	make -C test-app TARGET=$(TARGET) EXT_FLASH=$(EXT_FLASH)

tools/ed25519/ed25519_sign:
	make -C tools/ed25519

tools/ecc256/ecc256_sign:
	make -C tools/ecc256

ed25519.der: tools/ed25519/ed25519_sign
	tools/ed25519/ed25519_keygen src/ed25519_pub_key.c

ecc256.der: tools/ecc256/ecc256_sign
	tools/ecc256/ecc256_keygen src/ecc256_pub_key.c

factory.bin: $(BOOT_IMG) wolfboot-align.bin $(SIGN_TOOL) $(PRIVATE_KEY)
	$(SIGN_TOOL) $(BOOT_IMG) $(PRIVATE_KEY) 1
	cat wolfboot-align.bin $(BOOT_IMG).v1.signed > $@

second.img: $(BOOT_IMG) wolfboot-align.bin $(SIGN_TOOL) $(PRIVATE_KEY)
	$(SIGN_TOOL) $(BOOT_IMG) $(PRIVATE_KEY) 1 65536
	$(SIGN_TOOL) $(BOOT_IMG) $(PRIVATE_KEY) 2
	cat wolfboot-align.bin $(BOOT_IMG).v1.signed $(BOOT_IMG).v2.signed > $@

wolfboot.elf: $(OBJS) $(LSCRIPT)
	grep stat $(OBJS)
	$(LD) $(LDFLAGS) -Wl,--start-group $(OBJS) -Wl,--end-group -o $@

src/ed25519_pub_key.c: ed25519.der

src/ecc256_pub_key.c: ecc256.der

keys: $(PRIVATE_KEY)
	
clean:
	rm -f *.bin *.elf $(OBJS) wolfboot.map *.bin  *.hex hal/*.o
	make -C test-app clean

distclean: clean
	make -C tools/ed25519 clean
	make -C tools/ecc256 clean
	rm -f *.pem *.der tags ./src/ed25519_pub_key.c ./src/ecc256_pub_key.c

