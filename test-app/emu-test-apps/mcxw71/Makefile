CC=arm-none-eabi-gcc
OBJCOPY ?= arm-none-eabi-objcopy

CFLAGS := -mcpu=cortex-m33 -mthumb -mcmse -Os -ffreestanding -fdata-sections -ffunction-sections -g -ggdb
MCXW71_SDK ?= ../../../../MCXW71
CFLAGS += -I. -I../common -I../../../include
CFLAGS += -DCPU_MCXW716CMFPA
CFLAGS += -I$(MCXW71_SDK)/devices/MCXW716C -I$(MCXW71_SDK)/devices/MCXW716C/periph2
CFLAGS += -I$(MCXW71_SDK)/CMSIS/Core/Include
LDFLAGS := -nostdlib -T target.ld -Wl,-gc-sections
LDLIBS := -Wl,--start-group -lc -lm -lgcc -lnosys -Wl,--end-group

VPATH := ../common

COMMON_SRCS := emu_ivt.c emu_startup.c emu_syscalls.c emu_update.c emu_hal.c
LIBWOLFBOOT_SRC := ../../../src/libwolfboot.c
SRCS := $(COMMON_SRCS) uart.c
OBJS := $(SRCS:.c=.o) libwolfboot.o

all: app.bin

app.elf: $(OBJS) target.ld
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

app.bin: app.elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libwolfboot.o: $(LIBWOLFBOOT_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) app.elf app.bin

.PHONY: all clean
