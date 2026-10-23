# Makefile
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

CC=arm-none-eabi-gcc
OBJCOPY ?= arm-none-eabi-objcopy

MCUXPRESSO ?= $(abspath ../../../../NXP/mcuxpresso-sdk/mcuxsdk)
MCUXPRESSO_CMSIS ?= $(abspath ../../../../NXP/CMSIS_5/CMSIS)
MCUXPRESSO_DRIVERS ?= $(MCUXPRESSO)/devices/MCX/MCXN/MCXN947
MCUXPRESSO_CPU ?= MCXN947VDF_cm33_core0
ROMAPI_FLASH := $(MCUXPRESSO_DRIVERS)/drivers/romapi/flash

CFLAGS := -mcpu=cortex-m33 -mthumb -mcmse -Os -ffreestanding -fdata-sections -ffunction-sections -g -ggdb
CFLAGS += -I. -I../common -I../../../include -DEMU_MCXN947
CFLAGS += -DIMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE)
CFLAGS += -DCPU_$(MCUXPRESSO_CPU) -DNDEBUG -U__ARM_FEATURE_DSP
# ECC flash: a programmed phrase cannot be reprogrammed, as in the wolfBoot build.
CFLAGS += -DNVM_FLASH_WRITEONCE
ifneq ($(SECURE_HANDOFF_ADDRESS),)
  CFLAGS += -DEMU_SECURE_HANDOFF_ADDRESS=$(SECURE_HANDOFF_ADDRESS)
endif
CFLAGS += -I$(MCUXPRESSO_DRIVERS) -I$(MCUXPRESSO_DRIVERS)/drivers -I$(ROMAPI_FLASH)
CFLAGS += -I$(MCUXPRESSO_DRIVERS)/../periph -I$(MCUXPRESSO)/drivers/common
CFLAGS += -I$(MCUXPRESSO_CMSIS)/Include -I$(MCUXPRESSO_CMSIS)/Core/Include
LDFLAGS := -nostdlib -T target.ld -Wl,-gc-sections
LDLIBS := -Wl,--start-group -lc -lm -lgcc -lnosys -Wl,--end-group
# The MCXN TrustZone config hands off to a Secure payload
# (WOLFBOOT_SECURE_APP), so the test app runs in the secure world.
NSC_OBJ :=

VPATH := ../common

COMMON_SRCS := emu_ivt.c emu_startup.c emu_syscalls.c emu_update.c emu_hal.c
LIBWOLFBOOT_SRC := ../../../src/libwolfboot.c
SRCS := $(COMMON_SRCS) uart.c
OBJS := $(SRCS:.c=.o) libwolfboot.o fsl_flash.o $(NSC_OBJ)

all: app.bin

app.elf: $(OBJS) target.ld
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

app.bin: app.elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libwolfboot.o: $(LIBWOLFBOOT_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

fsl_flash.o: $(ROMAPI_FLASH)/src/fsl_flash.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRCS:.c=.o) libwolfboot.o fsl_flash.o app.elf app.bin

.PHONY: all clean
