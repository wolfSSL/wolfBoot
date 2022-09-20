
WOLFDIR=../../lib/wolfssl/
WOLFBOOTDIR = ../..

CFLAGS= -DWOLFSSL_USER_SETTINGS  -I$(WOLFDIR) -I.

# Source files
SRC=$(WOLFDIR)wolfcrypt/src/asn.c \
	$(WOLFDIR)wolfcrypt/src/rsa.c \
	$(WOLFDIR)wolfcrypt/src/ecc.c \
	$(WOLFDIR)wolfcrypt/src/aes.c \
	$(WOLFDIR)wolfcrypt/src/sp_c32.c \
	$(WOLFDIR)wolfcrypt/src/sp_int.c \
	$(WOLFDIR)wolfcrypt/src/sha256.c \
	$(WOLFDIR)wolfcrypt/src/sha.c \
	$(WOLFDIR)wolfcrypt/src/hash.c \
	$(WOLFDIR)wolfcrypt/src/hmac.c \
	$(WOLFDIR)wolfcrypt/src/memory.c \
	$(WOLFDIR)wolfcrypt/src/random.c \
	$(WOLFDIR)wolfcrypt/src/wolfmath.c \
	$(WOLFDIR)wolfcrypt/src/logging.c \
	$(WOLFDIR)wolfcrypt/src/coding.c \
	$(WOLFDIR)wolfcrypt/src/pwdbased.c \
	$(WOLFDIR)wolfcrypt/src/wc_port.c \
	$(WOLFDIR)wolfcrypt/src/wc_encrypt.c \

all: der2raw

der2raw: $(SRC) der2raw.c
	$(CC) -o der2raw der2raw.c $(SRC) $(CFLAGS)

clean:
	rm *.o der2raw

