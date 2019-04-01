TARGET?=none
ARCH?=ARM

CFLAGS:=-g -ggdb -Wall -Wstack-usage=200 -ffreestanding -Wno-unused -DPLATFORM_$(TARGET) -I../include -nostartfiles

APP_OBJS:=main.o led.o system.o timer.o ../hal/$(TARGET).o ../src/libwolfboot.o
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

LSCRIPT:=$(ARCH).ld
LDFLAGS:=$(CFLAGS) -T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=image.map

ifeq ($(EXT_FLASH),1)
  CFLAGS+=-DEXT_FLASH=1 -DPART_UPDATE_EXT=1
endif

ifeq ($(SPI_FLASH),1)
  CFLAGS+=-DSPI_FLASH
  APP_OBJS+=../hal/spi/spi_drv_$(TARGET).o ../src/spi_flash.o
endif

image.bin: image.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) -O binary $^ $@

image.elf: $(APP_OBJS) $(LSCRIPT)
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) $(APP_OBJS) -o $@

standalone:CFLAGS+=-DPLATFORM_stm32f4
standalone:LDFLAGS:=-T standalone.ld -Wl,-gc-sections -Wl,-Map=image.map -nostdlib

standalone: image.bin

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

clean:
	@rm -f image.bin* image.elf *.o image.map tags
