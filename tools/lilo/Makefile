# wolfBoot lilo: installs the x86_64_lilo target on a legacy MBR disk.
#
#   make            build lilo
#   make clean

CC      ?= gcc
CFLAGS  += -I. -I../../src -I../../include -Wall -Wextra -O2 -g
CFLAGS  += -D_FILE_OFFSET_BITS=64
CFLAGS  += -DWOLFBOOT_DISK_FS -DWOLFBOOT_FAT32 -DWOLFBOOT_NO_PRINTF
CFLAGS  += -DWOLFSSL_USER_SETTINGS

all: lilo

lilo: lilo.c ../../include/lilo_map.h ../../src/fat32.c ../../src/disk_fs.c \
      ../../src/disk.c ../../src/gpt.c
	$(CC) -o $@ lilo.c $(CFLAGS)

clean:
	rm -f lilo

.PHONY: all clean
