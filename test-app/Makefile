-include ../.config
-include ../tools/config.mk
TARGET?=none
ARCH?=ARM
MCUXPRESSO_CMSIS?=$(MCUXPRESSO)/CMSIS

ifeq ($(SIGN),RSA2048)
  IMAGE_HEADER_SIZE:=512
endif

ifeq ($(SIGN),RSA4096)
  IMAGE_HEADER_SIZE:=1024
endif
ifeq ($(HASH),SHA256)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha256.o
  CFLAGS+=-DWOLFBOOT_HASH_SHA256
endif

ifeq ($(HASH),SHA3_384)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  CFLAGS+=-DWOLFBOOT_HASH_SHA3_384
endif

CFLAGS:=-g -ggdb -Wall -Wstack-usage=1024 -ffreestanding -Wno-unused -DPLATFORM_$(TARGET) -I../include -nostartfiles

APP_OBJS:=app_$(TARGET).o led.o system.o timer.o ../hal/$(TARGET).o ../src/libwolfboot.o
include ../arch.mk

ifeq ($(ARCH),RISCV)
  APP_OBJS+=startup_riscv.o vector_riscv.o
endif

ifeq ($(ARCH),ARM)
  APP_OBJS+=startup_arm.o
endif

ifeq ($(V),0)
  Q=@
endif

ENTRY_POINT=`cat .entry-point-address`
LSCRIPT:=../config/target-app.ld
LSCRIPT_TEMPLATE:=$(ARCH).ld
ifeq ($(TARGET),stm32f7)
  LSCRIPT_TEMPLATE=ARM-stm32f7.ld
  CFLAGS+=-DDUALBANK_SWAP
endif
LDFLAGS:=$(CFLAGS) -T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=image.map

ifeq ($(EXT_FLASH),1)
  CFLAGS+=-DEXT_FLASH=1 -DPART_UPDATE_EXT=1
endif

ifeq ($(SPI_FLASH),1)
  CFLAGS+=-DSPI_FLASH
  APP_OBJS+=../hal/spi/spi_drv_$(SPI_TARGET).o ../src/spi_flash.o
endif

ifeq ($(TARGET),kinetis)
  CFLAGS+= -I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -DNVM_FLASH_WRITEONCE=1
  APP_OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_flash.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_cache.o \
  $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_controller.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_gpio.o
endif

ifeq ($(TARGET),stm32g0)
  CFLAGS+=-DNVM_FLASH_WRITEONCE=1
endif

ifeq ($(TARGET),hifive1.freedom)
  CFLAGS+=-I$(FREEDOM_E_SDK)/freedom-metal/ -D__METAL_MACHINE_HEADER=\"$(FREEDOM_E_SDK)/bsp/sifive-hifive1/metal.h\"
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/clock.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/led.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/cache.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/cpu.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/gpio.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/interrupt.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/uart.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/tty.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/spi.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/shutdown.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/timer.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,hfrosc.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,hfxosc.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,pll.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,prci.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,spi0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,uart0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,global-external-interrupts0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,local-external-interrupts0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,gpio0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,gpio-leds.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,clint0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,plic0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,cpu.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/fixed-clock.o
endif

ifeq ($(TARGET),hifive1)
  CFLAGS+=-DRAMFUNCTION='__attribute__((used,section(".ramcode")))'
  APP_OBJS+=hifive1_write_page.o
endif

standalone:CFLAGS+=-DTEST_APP_STANDALONE
standalone:LDFLAGS:=$(CFLAGS) -T standalone.ld -Wl,-gc-sections -Wl,-Map=image.map

image.bin: image.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) -O binary $^ $@

image.elf: $(APP_OBJS) $(LSCRIPT)
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) $(APP_OBJS) -o $@


standalone: image.bin

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

clean:
	@rm -f *.bin *.elf tags *.o $(LSCRIPT)

$(LSCRIPT): $(LSCRIPT_TEMPLATE) FORCE
	@printf "%d" $(WOLFBOOT_PARTITION_BOOT_ADDRESS) > .wolfboot-offset
	@printf "%d" $(WOLFBOOT_PARTITION_SIZE) > .partition-size
	@printf "%d" $(IMAGE_HEADER_SIZE) > .header-size
	@expr `cat .wolfboot-offset` + `cat .header-size` > .entry-point
	@printf "0x%X" `cat .entry-point` > .entry-point
	@expr `cat .partition-size` - `cat .header-size` > .app-size
	@printf "0x%X" `cat .app-size` > .app-size
	@ cat $(LSCRIPT_TEMPLATE) | \
		sed -e "s/##WOLFBOOT_TEST_APP_SIZE##/`cat .app-size`/g" | \
		sed -e "s/##WOLFBOOT_TEST_APP_ADDRESS##/`cat .entry-point`/g" \
		> $(@)
	@rm -f .app-size .entry-point .wolfboot-offset .partition-size .header-size

.PHONY: FORCE clean
