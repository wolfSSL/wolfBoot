CROSS_COMPILE:=arm-none-eabi-
CC:=$(CROSS_COMPILE)gcc
LD:=$(CROSS_COMPILE)gcc
OBJS:=startup.o main.o timer.o led.o system.o
TARGET?=none
LSCRIPT:=app.ld
OBJCOPY:=$(CROSS_COMPILE)objcopy
CFLAGS:=-mcpu=cortex-m3 -mthumb -g -ggdb -Wall -Wno-main -Wstack-usage=200 -ffreestanding -Wno-unused -nostdlib -DPLATFORM_$(TARGET)
LDFLAGS:=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=image.map -nostdlib


image.bin: image.elf
	$(OBJCOPY) -O binary $^ $@

image.elf: $(OBJS) $(LSCRIPT)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

standalone:CFLAGS+=-DPLATFORM_stm32f4
standalone:LDFLAGS:=-T standalone.ld -Wl,-gc-sections -Wl,-Map=image.map -nostdlib


standalone: image.bin
	
startup.o: startup.c

main.o: main.c

clean:
	rm -f image.bin* image.elf *.o image.map tags
