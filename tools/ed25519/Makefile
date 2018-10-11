# Settings
#
BOOTUTIL_ROOT?=../../lib/bootutil


CFLAGS:=-lwolfssl -DHAVE_ED25519 -DHAVE_ASN -DWOLFCRYPT_ONLY -DTFM_TIMING_RESISTANT -DNO_RSA -DECC_TIMING_RESISTANT -DWOLFSSL_DER_TO_PEM -I$(BOOTUTIL_ROOT)/include

all: ed25519_keygen ed25519_sign



ed25519_keygen: ed25519_keygen.c

ed25519_sign: ed25519_sign.c

clean:
	@rm -f ed25519_keygen ed25519_sign
