# Settings
#
CROSS_COMPILE=
WOLFBOOT_ROOT=../../
CFLAGS:=-lwolfssl -DHAVE_ED25519 -DHAVE_ASN -DWOLFCRYPT_ONLY -DTFM_TIMING_RESISTANT -DNO_RSA -DECC_TIMING_RESISTANT -DWOLFSSL_DER_TO_PEM -I$(WOLFBOOT_ROOT)/include -I$(WOLFBOOT_ROOT)/lib/wolfssl
LDFLAGS:=-lwolfssl
LDFLAGS_EXTRA=
EXTRA_LDFLAGS=
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)gcc

EXT=


all: binaries

windows: binaries
	mv ed25519_keygen ed25519_keygen.exe
	mv ed25519_sign   ed25519_sign.exe

windows:CROSS_COMPILE=x86_64-w64-mingw32-
windows:EXT:=.exe
windows:WIN_OBJS+=/usr/x86_64-w64-mingw32/lib/libwolfssl-17.dll
windows:LDFLAGS=-Wl,-Bstatic,--whole-archive $(WIN_OBJS) -lgcc -Wl,--no-whole-archive
windows: all


binaries: ed25519_keygen$(EXT) ed25519_sign$(EXT)

ed25519_keygen: ed25519_keygen.o
	$(CC) -o $@ $^ $(LDFLAGS)

ed25519_sign: ed25519_sign.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f ed25519_keygen ed25519_sign *.o *.exe
