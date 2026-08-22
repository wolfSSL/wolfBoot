# Host-side driver for the read-only FAT32 / ext4 layer.
#
#   make            build fs-test
#   make fixtures   generate assorted real filesystem images with mkfs
#   make check      build, generate fixtures, and verify every one of them
#   make bench      report open/read cost across the fixtures
#   make clean

CC      ?= gcc
CFLAGS  += -I. -I../../src -I../../include -Wall -Wextra -O2 -g
CFLAGS  += -D_FILE_OFFSET_BITS=64
CFLAGS  += -DWOLFBOOT_DISK_FS -DWOLFBOOT_FAT32 -DWOLFBOOT_EXT4
CFLAGS  += -DUNIT_TEST -DWOLFSSL_USER_SETTINGS

# Bigger metadata cache is a build-time knob; override to measure its effect:
#   make CACHE=4096 bench
ifdef CACHE
CFLAGS  += -DWOLFBOOT_FS_CACHE_SIZE=$(CACHE)
endif

ifdef DEBUG_FS
CFLAGS  += -DDEBUG_FS
endif

all: fs-test

fs-test: fs-test.c ../../src/fat32.c ../../src/ext4.c ../../src/disk_fs.c \
         ../../src/disk.c ../../src/gpt.c
	$(CC) -o $@ fs-test.c $(CFLAGS)

fixtures: mkfixtures.sh
	./mkfixtures.sh

check: fs-test fixtures
	./runtests.sh

bench: fs-test fixtures
	./runbench.sh

# clean removes only build output. The fixtures take minutes to generate,
# so they are removed by distclean instead -- rebuilding the binary at a
# different CACHE= size must not throw them away.
clean:
	rm -f fs-test

distclean: clean
	rm -rf fixtures

.PHONY: all fixtures check bench clean distclean
