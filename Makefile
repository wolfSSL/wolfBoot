CROSS_COMPILE:=arm-none-eabi-
CC:=$(CROSS_COMPILE)gcc
LD:=$(CROSS_COMPILE)gcc


OBJCOPY:=$(CROSS_COMPILE)objcopy
SIZE:=$(CROSS_COMPILE)size
BOOT_IMG?=test-app/image.bin
BOOT0_OFFSET?=0x20000
SIGN?=ED25519
TARGET?=stm32f4
DEBUG?=0
VTOR?=1
SWAP?=1

LSCRIPT:=hal/$(TARGET).ld

OBJS:= \
./hal/$(TARGET).o \
./lib/bootutil/src/loader.o \
./lib/bootutil/src/image_validate.o \
./lib/bootutil/src/bootutil_misc.o \
./src/run.o \
./src/mem.o \
./src/keys.o \
./src/crypto.o \
./src/startup_bl.o \
./src/main.o \
./lib/wolfssl/wolfcrypt/src/sha256.o \
./lib/wolfssl/wolfcrypt/src/hash.o \
./lib/wolfssl/wolfcrypt/src/wolfmath.o \
./lib/wolfssl/wolfcrypt/src/fe_low_mem.o 

CFLAGS:=-mcpu=cortex-m3 -mthumb -Wall -Wno-main -Wstack-usage=1024 -ffreestanding -Wno-unused \
	-Ilib/bootutil/include -Iinclude/ -Ilib/wolfssl -nostartfiles \
	-DBOOT_MAX_IMG_SECTORS=256 -DWOLFBOOT_VALIDATE_SLOT0 -DWOLFBOOT_USE_FLASHAREA_GET_SECTORS \
	-nostdlib \
	-DWOLFSSL_USER_SETTINGS \
	-DPLATFORM_$(TARGET)

ifeq ($(SIGN),ED25519)
  OBJS+= ./lib/wolfssl/wolfcrypt/src/sha512.o \
	./lib/wolfssl/wolfcrypt/src/ed25519.o \
	./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./src/ed25519_pub_key.o
  CFLAGS+=-DBOOT_SIGN_ED25519
endif

ifeq ($(SIGN),EC256)
  OBJS+= ./ext/wolfssl/wolfcrypt/src/ecc.o
  CFLAGS+=-DBOOT_SIGN_EC256
endif

ifeq ($(DEBUG),1)
    CFLAGS+=-O0 -g -ggdb3 -DDEBUG=1
else
    CFLAGS+=-Os
endif

ifeq ($(VTOR),0)
    CFLAGS+=-DNO_VTOR
endif

ifeq ($(SWAP),0)
    CFLAGS+=-DWOLFBOOT_OVERWRITE_ONLY
endif


LDFLAGS:=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=wolfboot.map -ffreestanding -nostartfiles -mcpu=cortex-m3 -mthumb -nostdlib

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
	make -C test-app TARGET=$(TARGET)

tools/ed25519/ed25519_sign:
	make -C tools/ed25519

ed25519.der: tools/ed25519/ed25519_sign
	tools/ed25519/ed25519_keygen src/ed25519_pub_key.c


factory.bin: $(BOOT_IMG) wolfboot-align.bin tools/ed25519/ed25519_sign ed25519.der
	tools/ed25519/ed25519_sign $(BOOT_IMG) ed25519.der 1
	cat wolfboot-align.bin $(BOOT_IMG).v1.signed > $@

second.img: $(BOOT_IMG) wolfboot-align.bin tools/ed25519/ed25519_sign ed25519.der
	tools/ed25519/ed25519_sign $(BOOT_IMG) ed25519.der 1 65536
	tools/ed25519/ed25519_sign $(BOOT_IMG) ed25519.der 2
	cat wolfboot-align.bin $(BOOT_IMG).v1.signed $(BOOT_IMG).v2.signed > $@

wolfboot.elf: $(OBJS) $(LSCRIPT)
	grep stat $(OBJS)
	$(LD) $(LDFLAGS) -Wl,--start-group $(OBJS) -Wl,--end-group -o $@

src/ed25519_pub_key.c: ed25519.der

keys: ed25519.der

	
clean:
	rm -f *.bin *.elf $(OBJS) wolfboot.map *.bin  *.hex
	make -C test-app clean

distclean: clean
	make -C tools/ed25519 clean
	rm -f *.pem *.der tags ./src/ed25519_pub_key.c

