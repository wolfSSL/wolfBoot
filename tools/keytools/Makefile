# wolfBoot KeyGen and Signing Tool

CC      = gcc
WOLFDIR = ../../lib/wolfssl/
CFLAGS  = -Wall -Wextra -Werror
CFLAGS  += -I. -DWOLFSSL_USER_SETTINGS -I$(WOLFDIR) -I../../include

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

SRC+=../../src/delta.c

.PHONY: clean all

all: sign keygen

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

# build template
sign:
	@echo "Building signing tool"
	@$(CC) -o $@ $@.c $(SRC) $< $(CFLAGS)

keygen:
	@echo "Building keygen tool"
	@$(CC) -o $@ $@.c $(SRC) $< $(CFLAGS)

clean:
	rm -f sign keygen
