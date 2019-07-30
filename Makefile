## wolfBoot Makefile
#
# Configure by passing alternate values
# via environment variables.
#
# Default values:
ARCH?=ARM
TARGET?=stm32f4
SIGN?=ED25519
KINETIS?=$(HOME)/src/FRDM-K64F
KINETIS_CPU=MK64FN1M0VLL12
KINETIS_DRIVERS?=$(KINETIS)/devices/MK64F12
KINETIS_CMSIS?=$(KINETIS)/CMSIS
FREEDOM_E_SDK?=$(HOME)/src/freedom-e-sdk
DEBUG?=0
VTOR?=1
CORTEX_M0?=0
NO_ASM?=0
EXT_FLASH?=0
SPI_FLASH?=0
ALLOW_DOWNGRADE?=0
NVM_FLASH_WRITEONCE?=0
WOLFBOOT_VERSION?=0
V?=0
SPMATH?=1
RAM_CODE?=0
DUALBANK_SWAP=0



## Initializers
CFLAGS:=-D__WOLFBOOT  -DWOLFBOOT_VERSION=$(WOLFBOOT_VERSION)UL
LSCRIPT:=hal/$(TARGET).ld
LDFLAGS:=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles
OBJS:= \
./hal/$(TARGET).o \
./src/loader.o \
./src/string.o \
./src/image.o \
./src/libwolfboot.o \
./lib/wolfssl/wolfcrypt/src/sha256.o \
./lib/wolfssl/wolfcrypt/src/hash.o \
./lib/wolfssl/wolfcrypt/src/wolfmath.o \
./lib/wolfssl/wolfcrypt/src/fe_low_mem.o

## Architecture/CPU configuration
include arch.mk


## DSA Settings

ifeq ($(SIGN),ECC256)
  KEYGEN_OPTIONS=--ecc256
  SIGN_OPTIONS=--ecc256
  PRIVATE_KEY=ecc256.der
  OBJS+= \
    $(ECC_EXTRA_OBJS) \
    $(MATH_OBJS) \
	./lib/wolfssl/wolfcrypt/src/ecc.o \
	./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
	./lib/wolfssl/wolfcrypt/src/memory.o \
	./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./src/ecc256_pub_key.o \
    ./src/xmalloc.o
  CFLAGS+=-DWOLFBOOT_SIGN_ECC256 -DXMALLOC_USER $(ECC_EXTRA_CFLAGS)
else
  KEYGEN_OPTIONS=--ed25519
  SIGN_OPTIONS=--ed25519
  PRIVATE_KEY=ed25519.der
  OBJS+= ./lib/wolfssl/wolfcrypt/src/sha512.o \
	./lib/wolfssl/wolfcrypt/src/ed25519.o \
	./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./src/ed25519_pub_key.o
  CFLAGS+=-DWOLFBOOT_SIGN_ED25519 -nostdlib -DWOLFSSL_STATIC_MEMORY
  LDFLAGS+=-nostdlib
endif


CFLAGS+=-Wall -Wextra -Wno-main -Wstack-usage=1024 -ffreestanding -Wno-unused \
	-I. -Iinclude/ -Ilib/wolfssl -nostartfiles \
	-DWOLFSSL_USER_SETTINGS \
	-DPLATFORM_$(TARGET)

ifeq ($(RAM_CODE),1)
   CFLAGS+= -DRAM_CODE
endif

ifeq ($(DUALBANK_SWAP),1)
   CFLAGS+= -DDUALBANK_SWAP
endif

ifeq ($(SPI_FLASH),1)
   EXT_FLASH=1
   CFLAGS+= -DSPI_FLASH=1
   OBJS+= src/spi_flash.o hal/spi/spi_drv_$(TARGET).o
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+= -DEXT_FLASH=1 -DPART_UPDATE_EXT=1 -DPART_SWAP_EXT=1
endif

ifeq ($(ALLOW_DOWNGRADE),1)
  CFLAGS+= -DALLOW_DOWNGRADE
endif

ifeq ($(NVM_FLASH_WRITEONCE),1)
  CFLAGS+= -DNVM_FLASH_WRITEONCE
endif



ifeq ($(DEBUG),1)
    CFLAGS+=-O0 -g -ggdb3 -DDEBUG=1
else
    CFLAGS+=-Os
endif

ifeq ($(V),0)
  Q=@
endif

ifeq ($(VTOR),0)
    CFLAGS+=-DNO_VTOR
endif

ASFLAGS:=$(CFLAGS)

all: factory.bin

wolfboot.bin: wolfboot.elf
	@echo "\t[BIN] $@"	
	$(Q)$(OBJCOPY) -O binary $^ $@

wolfboot.hex: wolfboot.elf
	@echo "\t[HEX] $@"	
	$(Q)$(OBJCOPY) -O ihex $^ $@

align: wolfboot-align.bin

wolfboot-align.bin: wolfboot.bin
	@cat include/target.h | grep WOLFBOOT_PARTITION_BOOT_ADDRESS | tr -d "\n\r" | sed -e "s/.*[ ]//g" > .wolfboot-offset
	@printf "%d" `cat .wolfboot-offset` > .wolfboot-offset
	@printf "%d" $(ARCH_FLASH_OFFSET) > .wolfboot-arch-offset
	@expr `cat .wolfboot-offset` - `cat .wolfboot-arch-offset` > .wolfboot-partition-size
	@dd if=/dev/zero bs=`cat .wolfboot-partition-size` count=1 2>/dev/null | tr "\000" "\377" > $(@)
	@#rm -f .wolfboot-partition-size .wolfboot-offset .wolfboot-arch-offset
	@dd if=$^ of=$(@) conv=notrunc 2>/dev/null
	@echo
	@echo "\t[SIZE]"
	@$(SIZE) wolfboot.elf
	@echo

test-app/image.bin:
	@make -C test-app TARGET=$(TARGET) EXT_FLASH=$(EXT_FLASH) SPI_FLASH=$(SPI_FLASH) ARCH=$(ARCH) \
    V=$(V) RAM_CODE=$(RAM_CODE) WOLFBOOT_VERSION=$(WOLFBOOT_VERSION)\
	KINETIS=$(KINETIS) KINETIS_CPU=$(KINETIS_CPU) KINETIS_DRIVERS=$(KINETIS_DRIVERS) \
	KINETIS_CMSIS=$(KINETIS_CMSIS) NVM_FLASH_WRITEONCE=$(NVM_FLASH_WRITEONCE) \
	FREEDOM_E_SDK=$(FREEDOM_E_SDK)
	@rm -f src/*.o hal/*.o
	@$(SIZE) test-app/image.elf

include tools/test.mk

ed25519.der:
	@python3 tools/keytools/keygen.py $(KEYGEN_OPTIONS) src/ed25519_pub_key.c

ecc256.der:
	@python3 tools/keytools/keygen.py $(KEYGEN_OPTIONS) src/ecc256_pub_key.c

factory.bin: $(BOOT_IMG) wolfboot-align.bin $(PRIVATE_KEY)
	@echo "\t[SIGN] $(BOOT_IMG)"
	$(Q)python3 tools/keytools/sign.py $(SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) 1
	@echo "\t[MERGE] $@"
	@cat wolfboot-align.bin test-app/image_v1_signed.bin > $@

wolfboot.elf: $(OBJS) $(LSCRIPT)
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) -Wl,--start-group $(OBJS) -Wl,--end-group -o $@

src/ed25519_pub_key.c: ed25519.der

src/ecc256_pub_key.c: ecc256.der

keys: $(PRIVATE_KEY)
	
clean:
	@find . -type f -name "*.o" | xargs rm -f
	@rm -f *.bin *.elf wolfboot.map *.bin  *.hex
	@make -C test-app clean

distclean: clean
	@rm -f *.pem *.der tags ./src/ed25519_pub_key.c ./src/ecc256_pub_key.c


%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

FORCE: 

