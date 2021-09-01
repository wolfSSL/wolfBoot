# wolfBoot KeyGen and Signing Tool

-include ../../.config

V?=0
ifeq ($(V),0)
  Q=@
endif

CC      = gcc
WOLFBOOTDIR = ../..
WOLFDIR = $(WOLFBOOTDIR)/lib/wolfssl/
CFLAGS  = -Wall -Wextra -Werror
CFLAGS  += -I. -DWOLFSSL_USER_SETTINGS -I$(WOLFDIR) -I$(WOLFBOOTDIR)/include

# option variables
DEBUG_FLAGS     = -g -DDEBUG -DDEBUG_SIGNTOOL -DDEBUG_WOLFSSL -DDEBUG_WOLFSSL_VERBOSE
OPTIMIZE        = -Os

# Options
#CFLAGS+=$(DEBUG_FLAGS)
CFLAGS+=$(OPTIMIZE)

# Sources
SRC=$(WOLFDIR)wolfcrypt/src/asn.c \
	$(WOLFDIR)wolfcrypt/src/ecc.c \
	$(WOLFDIR)wolfcrypt/src/coding.c \
	$(WOLFDIR)wolfcrypt/src/chacha.c \
	$(WOLFDIR)wolfcrypt/src/ed25519.c \
	$(WOLFDIR)wolfcrypt/src/fe_operations.c \
	$(WOLFDIR)wolfcrypt/src/ge_operations.c \
	$(WOLFDIR)wolfcrypt/src/hash.c \
	$(WOLFDIR)wolfcrypt/src/logging.c \
	$(WOLFDIR)wolfcrypt/src/memory.c \
	$(WOLFDIR)wolfcrypt/src/random.c \
	$(WOLFDIR)wolfcrypt/src/rsa.c \
	$(WOLFDIR)wolfcrypt/src/sp_int.c \
	$(WOLFDIR)wolfcrypt/src/sp_c32.c \
	$(WOLFDIR)wolfcrypt/src/sp_c64.c \
	$(WOLFDIR)wolfcrypt/src/sha3.c \
	$(WOLFDIR)wolfcrypt/src/sha256.c \
	$(WOLFDIR)wolfcrypt/src/sha512.c \
	$(WOLFDIR)wolfcrypt/src/tfm.c \
	$(WOLFDIR)wolfcrypt/src/wc_port.c \
	$(WOLFDIR)wolfcrypt/src/wolfmath.c

SRC+=$(WOLFBOOTDIR)/src/delta.c

.PHONY: clean all

all: $(WOLFBOOTDIR)/include/target.h sign keygen

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

# Target.h is required for key tools
$(WOLFBOOTDIR)/include/target.h: $(WOLFBOOTDIR)/include/target.h.in
	@cat $(WOLFBOOTDIR)/include/target.h.in | \
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

# build template
sign:
	@echo "Building signing tool"
	$(Q)$(CC) -o $@ $@.c $(SRC) $< $(CFLAGS)

keygen:
	@echo "Building keygen tool"
	$(Q)$(CC) -o $@ $@.c $(SRC) $< $(CFLAGS)

clean:
	rm -f sign keygen
