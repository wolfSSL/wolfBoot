# Settings
#
CC=gcc
LD=gcc
CROSS_COMPILE=
WOLFBOOT_ROOT=../../
BOOTUTIL_ROOT=$(WOLFBOOT_ROOT)/lib/bootutil
CFLAGS:=-lwolfssl -DHAVE_ED25519 -DHAVE_ASN -DWOLFCRYPT_ONLY -DTFM_TIMING_RESISTANT -DNO_RSA -DECC_TIMING_RESISTANT -DWOLFSSL_DER_TO_PEM -I$(WOLFBOOT_ROOT)/include
LDFLAGS:=-lwolfssl
LDFLAGS_EXTRA=
EXTRA_LDFLAGS=

all: ed25519_keygen ed25519_sign



ed25519_keygen: ed25519_keygen.o
	gcc -o $@ $^ $(LDFLAGS)

ed25519_sign: ed25519_sign.o
	gcc -o $@ $^ $(LDFLAGS)

clean:
	@rm -f ed25519_keygen ed25519_sign
