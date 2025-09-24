# wolfBoot TPM Tools

-include ../../.config

V?=0
ifeq ($(V),0)
  Q=@
endif

# Default library paths (can be overridden)
WOLFBOOT_LIB_WOLFSSL?=../../lib/wolfssl
WOLFBOOT_LIB_WOLFTPM?=../../lib/wolfTPM

# Convert to absolute paths for standalone usage
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))
WOLFBOOT_LIB_WOLFTPM:=$(abspath $(WOLFBOOT_LIB_WOLFTPM))

CC = gcc
LD = gcc
WOLFBOOTDIR = ../..
CFLAGS = -Wall -Wextra -Werror -Wno-unused-function
CFLAGS += -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS -DWOLFBOOT_TPM -DHAVE_NETDB_H -DWOLFBOOT_SIGN_$(SIGN)
CFLAGS += -DXSTRTOL=strtol
CFLAGS += -I. -I$(WOLFBOOT_LIB_WOLFSSL) -I$(WOLFBOOT_LIB_WOLFTPM) -I$(WOLFBOOTDIR)/include
LDFLAGS =
OBJDIR = ./

# option variables
DEBUG_FLAGS     = -g -DDEBUG -DDEBUG_WOLFTPM -DDEBUG_WOLFSSL -DWOLFTPM_DEBUG_VERBOSE
SANITIZE_FLAGS  = -fsanitize=address
OPTIMIZE        = -O2

# Options
#CFLAGS+=$(DEBUG_FLAGS)
#CFLAGS+=$(SANITIZE_FLAGS)
CFLAGS+=$(OPTIMIZE)

ifeq ($(TARGET),sim)
	CFLAGS+=-D"WOLFTPM_SWTPM"
else
	CFLAGS+=-D"WOLFTPM_EXAMPLE_HAL"
endif

# Sources
OBJS_REAL=\
	$(WOLFBOOTDIR)/src/keystore.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/asn.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ecc.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/error.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hash.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/logging.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/rsa.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hmac.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c64.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/tfm.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_port.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wolfmath.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_wrap.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_linux.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_packet.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_param_enc.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_swtpm.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_tis.o \
	$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_winapi.o \
	$(WOLFBOOT_LIB_WOLFTPM)/hal/tpm_io.o

OBJS_VIRT=$(addprefix $(OBJDIR), $(notdir $(OBJS_REAL)))
vpath %.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/
vpath %.c $(WOLFBOOTDIR)/src/
vpath %.c $(WOLFBOOT_LIB_WOLFTPM)/src/
vpath %.c $(WOLFBOOT_LIB_WOLFTPM)/hal/
vpath %.c $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr
vpath %.c ./

.PHONY: clean all

all: rot policy_create pcr_extend pcr_read pcr_reset policy_sign

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

swtpm:CFLAGS+=-DWOLFTPM_SWTPM
swtpm:all

# build objects
$(OBJDIR)/%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFBOOTDIR)/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFBOOT_LIB_WOLFTPM)/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFBOOT_LIB_WOLFTPM)/hal/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

# build templates
rot: $(OBJS_VIRT) rot.o
	@echo "Building Root of Trust (ROT) tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LDFLAGS)

policy_create: $(OBJS_VIRT) policy_create.o
	@echo "Building Policy Creation Tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LDFLAGS)

policy_sign: $(OBJS_VIRT) policy_sign.o
	@echo "Building Policy Sign Tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LDFLAGS)

pcr_extend: $(OBJS_VIRT) $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/extend.o
	@echo "Building PCR Extend Tool"
	$(Q)$(LD) -o $@ $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/extend.o $(OBJS_VIRT) $(LDFLAGS)

pcr_read: $(OBJS_VIRT) $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/read_pcr.o
	@echo "Building PCR Read Tool"
	$(Q)$(LD) -o $@ $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/read_pcr.o $(OBJS_VIRT) $(LDFLAGS)

pcr_reset: $(OBJS_VIRT) $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/reset.o
	@echo "Building PCR Reset Tool"
	$(Q)$(LD) -o $@ $(WOLFBOOT_LIB_WOLFTPM)/examples/pcr/reset.o $(OBJS_VIRT) $(LDFLAGS)

clean:
	rm -f rot policy_create pcr_extend pcr_read pcr_reset *.o
