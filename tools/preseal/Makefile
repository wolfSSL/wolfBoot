-include ../../.config

CC      = gcc
WOLFBOOTDIR = ../..
WOLFDIR = $(WOLFBOOTDIR)/lib/wolfssl/
WOLFTPMDIR = $(WOLFBOOTDIR)/lib/wolfTPM/
CFLAGS  = -Wall -Wextra -Werror
CFLAGS  += -I. -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS -I$(WOLFDIR) -I$(WOLFTPMDIR)

DEBUG_FLAGS     = -g -DDEBUG -DDEBUG_SIGNTOOL -DDEBUG_WOLFSSL -DDEBUG_WOLFSSL_VERBOSE -fsanitize=address

ifeq ($(ARCH),sim)
CFLAGS+=-DPRESEAL_SIM
endif

ifneq ($(NO_PRESEAL_FILESYSTEM),)
CFLAGS+= \
	-DNO_PRESEAL_FILESYSTEM \
	-DPUBKEY=\"$(PUBKEY)\" \
	-DPOLICY_PUBKEY=\"$(POLICY_PUBKEY)\" \
	-DPOLICY_SIGNED=\"$(POLICY_SIGNED)\" \
	-DIMAGE_DIGEST=\"$(IMAGE_DIGEST)\" \
	-DSEAL_NV_INDEX=$(SEAL_NV_INDEX) \
	-DPOLICY_DIGEST_NV_INDEX=$(POLICY_DIGEST_NV_INDEX) \
	-DPCR_INDEX=$(PCR_INDEX)
endif

# Sources
SRC=$(WOLFDIR)wolfcrypt/src/asn.c \
	$(WOLFDIR)wolfcrypt/src/aes.c \
	$(WOLFDIR)wolfcrypt/src/ecc.c \
	$(WOLFDIR)wolfcrypt/src/coding.c \
	$(WOLFDIR)wolfcrypt/src/chacha.c \
	$(WOLFDIR)wolfcrypt/src/ed25519.c \
	$(WOLFDIR)wolfcrypt/src/ed448.c \
	$(WOLFDIR)wolfcrypt/src/fe_operations.c \
	$(WOLFDIR)wolfcrypt/src/ge_operations.c \
	$(WOLFDIR)wolfcrypt/src/fe_448.c \
	$(WOLFDIR)wolfcrypt/src/ge_448.c \
	$(WOLFDIR)wolfcrypt/src/hash.c \
	$(WOLFDIR)wolfcrypt/src/logging.c \
	$(WOLFDIR)wolfcrypt/src/memory.c \
	$(WOLFDIR)wolfcrypt/src/random.c \
	$(WOLFDIR)wolfcrypt/src/rsa.c \
	$(WOLFDIR)wolfcrypt/src/hmac.c \
	$(WOLFDIR)wolfcrypt/src/sp_int.c \
	$(WOLFDIR)wolfcrypt/src/sp_c32.c \
	$(WOLFDIR)wolfcrypt/src/sp_c64.c \
	$(WOLFDIR)wolfcrypt/src/sha3.c \
	$(WOLFDIR)wolfcrypt/src/sha256.c \
	$(WOLFDIR)wolfcrypt/src/sha512.c \
	$(WOLFDIR)wolfcrypt/src/tfm.c \
	$(WOLFDIR)wolfcrypt/src/wc_port.c \
	$(WOLFDIR)wolfcrypt/src/wolfmath.c \
	$(WOLFTPMDIR)src/tpm2_wrap.c \
	$(WOLFTPMDIR)src/tpm2.c \
	$(WOLFTPMDIR)src/tpm2_linux.c \
	$(WOLFTPMDIR)src/tpm2_packet.c \
	$(WOLFTPMDIR)src/tpm2_param_enc.c \
	$(WOLFTPMDIR)src/tpm2_swtpm.c \
	$(WOLFTPMDIR)src/tpm2_tis.c \
	$(WOLFTPMDIR)src/tpm2_winapi.c \

all: preseal hash

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

preseal:
	$(Q)$(CC) -o $@ $@.c $(SRC) $< $(CFLAGS)

hash:
	$(Q)$(CC) -o $@ $@.c $(SRC) $< $(CFLAGS)

clean:
	rm -f preseal
	rm -f hash
