# wolfBoot Signing Tool

TARGET  = sign
CC      = gcc
WOLFDIR = ../../lib/wolfssl/
CFLAGS  = -Wall -I. -DWOLFSSL_USER_SETTINGS -I$(WOLFDIR)

# option variables
DEBUG_FLAGS     = -g -DDEBUG
OPTIMIZE        = -Os

# Options
#CFLAGS+=$(DEBUG_FLAGS)
CFLAGS+=$(OPTIMIZE)

# Sources
SRC=sign.c \
	$(WOLFDIR)wolfcrypt/src/asn.c \
	$(WOLFDIR)wolfcrypt/src/ecc.c \
	$(WOLFDIR)wolfcrypt/src/coding.c \
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

.PHONY: clean all

all: $(TARGET)

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

# build template
$(TARGET):
	@echo "Building C signing tool"
	@$(CC) -o $(TARGET) $(SRC) $< $(CFLAGS)

clean:
	rm -f $(TARGET)
