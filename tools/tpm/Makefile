# wolfBoot TPM Tools

-include ../../.config

V?=0
ifeq ($(V),0)
  Q=@
endif

CC = gcc
LD = gcc
WOLFBOOTDIR = ../..
WOLFDIR = $(WOLFBOOTDIR)/lib/wolfssl/
WOLFTPMDIR = $(WOLFBOOTDIR)/lib/wolfTPM/
CFLAGS = -Wall -Wextra -Werror
CFLAGS += -DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS -DWOLFBOOT_TPM
CFLAGS += -I. -I$(WOLFDIR) -I$(WOLFTPMDIR) -I$(WOLFBOOTDIR)/include
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
	$(WOLFDIR)wolfcrypt/src/asn.o \
	$(WOLFDIR)wolfcrypt/src/aes.o \
	$(WOLFDIR)wolfcrypt/src/ecc.o \
	$(WOLFDIR)wolfcrypt/src/error.o \
	$(WOLFDIR)wolfcrypt/src/coding.o \
	$(WOLFDIR)wolfcrypt/src/hash.o \
	$(WOLFDIR)wolfcrypt/src/logging.o \
	$(WOLFDIR)wolfcrypt/src/memory.o \
	$(WOLFDIR)wolfcrypt/src/random.o \
	$(WOLFDIR)wolfcrypt/src/rsa.o \
	$(WOLFDIR)wolfcrypt/src/hmac.o \
	$(WOLFDIR)wolfcrypt/src/sp_int.o \
	$(WOLFDIR)wolfcrypt/src/sp_c32.o \
	$(WOLFDIR)wolfcrypt/src/sp_c64.o \
	$(WOLFDIR)wolfcrypt/src/sha256.o \
	$(WOLFDIR)wolfcrypt/src/sha512.o \
	$(WOLFDIR)wolfcrypt/src/tfm.o \
	$(WOLFDIR)wolfcrypt/src/wc_port.o \
	$(WOLFDIR)wolfcrypt/src/wolfmath.o \
	$(WOLFTPMDIR)src/tpm2_wrap.o \
	$(WOLFTPMDIR)src/tpm2.o \
	$(WOLFTPMDIR)src/tpm2_linux.o \
	$(WOLFTPMDIR)src/tpm2_packet.o \
	$(WOLFTPMDIR)src/tpm2_param_enc.o \
	$(WOLFTPMDIR)src/tpm2_swtpm.o \
	$(WOLFTPMDIR)src/tpm2_tis.o \
	$(WOLFTPMDIR)src/tpm2_winapi.o \
	$(WOLFTPMDIR)hal/tpm_io.o

OBJS_VIRT=$(addprefix $(OBJDIR), $(notdir $(OBJS_REAL)))
vpath %.c $(WOLFDIR)/wolfcrypt/src/
vpath %.c $(WOLFBOOTDIR)/src/
vpath %.c $(WOLFTPMDIR)/src/
vpath %.c $(WOLFTPMDIR)/hal/
vpath %.c $(WOLFTPMDIR)/examples/pcr
vpath %.c ./

.PHONY: clean all

all: rot policy_create pcr_extend pcr_read pcr_reset

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

swtpm:CFLAGS+=-DWOLFTPM_SWTPM
swtpm:all

# build objects
$(OBJDIR)/%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFBOOTDIR)/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFDIR)/wolfcrypt/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFTPMDIR)/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFTPMDIR)/hal/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

# build templates
rot: $(OBJS_VIRT) rot.o
	@echo "Building Root of Trust (ROT) tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LDFLAGS)

policy_create: $(OBJS_VIRT) policy_create.o
	@echo "Building Policy Creation Tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LDFLAGS)

pcr_extend: $(OBJS_VIRT) $(WOLFTPMDIR)/examples/pcr/extend.o
	@echo "Building PCR Extend Tool"
	$(Q)$(LD) -o $@ $(WOLFTPMDIR)/examples/pcr/extend.o $(OBJS_VIRT) $(LDFLAGS)

pcr_read: $(OBJS_VIRT) $(WOLFTPMDIR)/examples/pcr/read_pcr.o
	@echo "Building PCR Read Tool"
	$(Q)$(LD) -o $@ $(WOLFTPMDIR)/examples/pcr/read_pcr.o $(OBJS_VIRT) $(LDFLAGS)

pcr_reset: $(OBJS_VIRT) $(WOLFTPMDIR)/examples/pcr/reset.o
	@echo "Building PCR Reset Tool"
	$(Q)$(LD) -o $@ $(WOLFTPMDIR)/examples/pcr/reset.o $(OBJS_VIRT) $(LDFLAGS)

clean:
	rm -f rot policy_create pcr_extend pcr_read pcr_reset *.o
