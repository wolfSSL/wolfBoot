## wolfBoot Makefile
#
# Configure by passing alternate values
# via environment variables.
#
# Configuration values: see tools/config.mk
-include .config
include tools/config.mk

## Initializers
WOLFBOOT_ROOT=$(PWD)
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


ifeq ($(SIGN),RSA4096)
  SPMATH=0
endif

## Architecture/CPU configuration
include arch.mk


## DSA Settings
ifeq ($(SIGN),ECC256)
  KEYGEN_OPTIONS+=--ecc256
  SIGN_OPTIONS+=--ecc256
  PRIVATE_KEY=ecc256.der
  WOLFCRYPT_OBJS+= \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/ecc.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./src/xmalloc_ecc.o
  CFLAGS+=-DWOLFBOOT_SIGN_ECC256 -DXMALLOC_USER
  ifeq ($(WOLFTPM),0)
    CFLAGS+=-Wstack-usage=1024
  else
    CFLAGS+=-Wstack-usage=6680
  endif
  PUBLIC_KEY_OBJS=./src/ecc256_pub_key.o
endif

ifeq ($(SIGN),ED25519)
  KEYGEN_OPTIONS+=--ed25519
  SIGN_OPTIONS+=--ed25519
  PRIVATE_KEY=ed25519.der
  WOLFCRYPT_OBJS+= ./lib/wolfssl/wolfcrypt/src/sha512.o \
    ./lib/wolfssl/wolfcrypt/src/ed25519.o \
    ./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/fe_low_mem.o
  PUBLIC_KEY_OBJS=./src/ed25519_pub_key.o
  CFLAGS+=-DWOLFBOOT_SIGN_ED25519 -Wstack-usage=1024
endif

ifeq ($(SIGN),RSA2048)
  KEYGEN_OPTIONS+=--rsa2048
  SIGN_OPTIONS+=--rsa2048
  PRIVATE_KEY=rsa2048.der
  IMAGE_HEADER_SIZE=512
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./src/xmalloc_rsa.o
  PUBLIC_KEY_OBJS=./src/rsa2048_pub_key.o
  CFLAGS+=-DWOLFBOOT_SIGN_RSA2048 -DXMALLOC_USER $(RSA_EXTRA_CFLAGS) \
		  -DIMAGE_HEADER_SIZE=512
  ifeq ($(WOLFTPM),0)
    CFLAGS+=-Wstack-usage=12288
  else
    CFLAGS+=-Wstack-usage=8320
  endif
endif

ifeq ($(SIGN),RSA4096)
  KEYGEN_OPTIONS+=--rsa4096
  SIGN_OPTIONS+=--rsa4096
  PRIVATE_KEY=rsa4096.der
  IMAGE_HEADER_SIZE=1024
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./src/xmalloc_rsa.o
  PUBLIC_KEY_OBJS=./src/rsa4096_pub_key.o
  CFLAGS+=-DWOLFBOOT_SIGN_RSA4096 -DXMALLOC_USER $(RSA_EXTRA_CFLAGS) \
		  -DIMAGE_HEADER_SIZE=1024
  ifeq ($(WOLFTPM),0)
    CFLAGS+=-Wstack-usage=12288
  else
    CFLAGS+=-Wstack-usage=10680
  endif
endif


CFLAGS+=-Wall -Wextra -Wno-main -ffreestanding -Wno-unused \
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
  OBJS+= src/spi_flash.o
  WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
endif

ifeq ($(UART_FLASH),1)
  EXT_FLASH=1
endif

ifeq ($(ENCRYPT),1)
  CFLAGS+=-DEXT_ENCRYPTED=1
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/chacha.o
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+= -DEXT_FLASH=1 -DPART_UPDATE_EXT=1 -DPART_SWAP_EXT=1
  ifeq ($(NO_XIP),1)
    CFLAGS+=-DPART_BOOT_EXT=1
  endif
  ifeq ($(UART_FLASH),1)
    CFLAGS+=-DUART_FLASH=1
    OBJS+=src/uart_flash.o
    WOLFCRYPT_OBJS+=hal/uart/uart_drv_$(UART_TARGET).o
  endif
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

ifeq ($(NO_MPU),1)
  CFLAGS+=-DWOLFBOOT_NO_MPU
endif

ifeq ($(VTOR),0)
  CFLAGS+=-DNO_VTOR
endif

ifeq ($(PKA),1)
  OBJS += $(PKA_EXTRA_OBJS)
  CFLAGS+=$(PKA_EXTRA_CFLAGS)
endif

OBJS+=$(PUBLIC_KEY_OBJS)
OBJS+=$(UPDATE_OBJS)

ifeq ($(WOLFTPM),1)
  OBJS += lib/wolfTPM/src/tpm2.o \
    lib/wolfTPM/src/tpm2_packet.o \
    lib/wolfTPM/src/tpm2_tis.o \
    lib/wolfTPM/src/tpm2_wrap.o
  CFLAGS+=-DWOLFBOOT_TPM -DSIZEOF_LONG=4 -Ilib/wolfTPM \
    -DMAX_COMMAND_SIZE=1024 -DMAX_RESPONSE_SIZE=1024 -DWOLFTPM2_MAX_BUFFER=1500 \
    -DMAX_SESSION_NUM=1 -DMAX_DIGEST_BUFFER=973 \
    -DWOLFTPM_SMALL_STACK
  # Chip Type: WOLFTPM_SLB9670, WOLFTPM_ST33, WOLFTPM_MCHP
  CFLAGS+=-DWOLFTPM_SLB9670
  # Use TPM for hashing (slow)
  #CFLAGS+=-DWOLFBOOT_HASH_TPM
  ifneq ($(SPI_FLASH),1)
    WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
  endif
endif
OBJS+=$(WOLFCRYPT_OBJS)


ASFLAGS:=$(CFLAGS)

all: factory.bin

wolfboot.bin: wolfboot.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) -O binary $^ $@

align: wolfboot-align.bin

.bootloader-partition-size:
	$(Q)printf "%d" $(WOLFBOOT_PARTITION_BOOT_ADDRESS) > .wolfboot-offset
	$(Q)printf "%d" $(ARCH_FLASH_OFFSET) > .wolfboot-arch-offset
	$(Q)expr `cat .wolfboot-offset` - `cat .wolfboot-arch-offset` > .bootloader-partition-size
	$(Q)rm -f .wolfboot-offset .wolfboot-arch-offset

wolfboot-align.bin: .bootloader-partition-size wolfboot.bin
	$(Q)dd if=/dev/zero bs=`cat .bootloader-partition-size` count=1 2>/dev/null | tr "\000" "\377" > $(@)
	$(Q)dd if=wolfboot.bin of=$(@) conv=notrunc 2>/dev/null
	@echo
	@echo "\t[SIZE]"
	$(Q)$(SIZE) wolfboot.elf
	@echo

test-app/image.bin: wolfboot-align.bin
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

factory.bin: $(BOOT_IMG) wolfboot-align.bin $(PRIVATE_KEY)
	@echo "\t[SIGN] $(BOOT_IMG)"
	$(Q)$(SIGN_TOOL) $(SIGN_OPTIONS) $(BOOT_IMG) $(PRIVATE_KEY) 1
	@echo "\t[MERGE] $@"
	$(Q)cat wolfboot-align.bin test-app/image_v1_signed.bin > $@

wolfboot.elf: include/target.h $(OBJS) $(LSCRIPT) FORCE
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) -Wl,--start-group $(OBJS) -Wl,--end-group -o $@

$(LSCRIPT): hal/$(TARGET).ld .bootloader-partition-size FORCE
	@cat hal/$(TARGET).ld | \
		sed -e "s/##WOLFBOOT_PARTITION_BOOT_ADDRESS##/`cat .bootloader-partition-size`/g" \
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
	@rm -f *.bin *.elf wolfboot.map *.bin  *.hex config/target.ld .bootloader-partition-size
	@make -C test-app clean

distclean: clean
	@rm -f *.pem *.der tags ./src/ed25519_pub_key.c ./src/ecc256_pub_key.c ./src/rsa2048_pub_key.c include/target.h
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
