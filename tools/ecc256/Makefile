# Settings
#
CC=gcc
LD=gcc
CROSS_COMPILE=
WOLFBOOT_ROOT=../../
BOOTUTIL_ROOT=$(WOLFBOOT_ROOT)/lib/bootutil
CFLAGS:=-lwolfssl -DHAVE_ECC -DHAVE_ASN -DWOLFCRYPT_ONLY -DTFM_TIMING_RESISTANT -DNO_RSA -DECC_TIMING_RESISTANT -DWOLFSSL_DER_TO_PEM -DWOLFSSL_VALIDATE_ECC_IMPORT -DHAVE_ECC_KEY_EXPORT -I$(WOLFBOOT_ROOT)/include
LDFLAGS:=-lwolfssl
LDFLAGS_EXTRA=
EXTRA_LDFLAGS=

all: ecc256_keygen ecc256_sign



ecc256_keygen: ecc256_keygen.o
	gcc -o $@ $^ $(LDFLAGS)

ecc256_sign: ecc256_sign.o
	gcc -o $@ $^ $(LDFLAGS)

clean:
	rm -f ecc256_keygen ecc256_sign *.o
