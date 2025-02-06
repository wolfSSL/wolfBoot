# wolfBoot KeyGen and Signing Tool

-include ../../.config

V?=0
ifeq ($(V),0)
  Q=@
endif

CC      = gcc
LD      = gcc
USE_GCC = 1
WOLFBOOTDIR = ../..
WOLFDIR = $(WOLFBOOTDIR)/lib/wolfssl
CFLAGS  = -Wall -Wextra -Werror
CFLAGS  += -I. -DWOLFSSL_USER_SETTINGS -I$(WOLFDIR) -I$(WOLFBOOTDIR)/include
LDFLAGS =
OBJDIR = ./
LIBS =

ML_DSA_LEVEL?=2
CFLAGS+=-DML_DSA_LEVEL=$(ML_DSA_LEVEL)

LMS_LEVELS?=1
LMS_HEIGHT?=10
LMS_WINTERNITZ?=8

# Common to wc_lms and ext_lms.
CFLAGS +=-D"LMS_LEVELS=$(LMS_LEVELS)" \
		 -D"LMS_HEIGHT=$(LMS_HEIGHT)" \
		 -D"LMS_WINTERNITZ=$(LMS_WINTERNITZ)"

# XMSS flags
XMSS_PARAMS?='XMSS-SHA2_10_256'
CFLAGS +=-D"IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
		 -DWOLFBOOT_XMSS_PARAMS=\"$(XMSS_PARAMS)\"

# When WOLFBOOT_UNIVERSAL_KEYSTORE is defined, pad store_sizes in keystore.der
ifeq ($(WOLFBOOT_UNIVERSAL_KEYSTORE),1)
  CFLAGS+=-DWOLFBOOT_UNIVERSAL_KEYSTORE
endif

# option variables
DEBUG_FLAGS     = -g -DDEBUG -DDEBUG_SIGNTOOL -DDEBUG_WOLFSSL -DDEBUG_WOLFSSL_VERBOSE
SANITIZE_FLAGS  = -fsanitize=address
OPTIMIZE        = -O2

# Options
ifeq ($(DEBUG),1)
  CFLAGS+=$(DEBUG_FLAGS)
else
  #CFLAGS+=$(SANITIZE_FLAGS)
  CFLAGS+=$(OPTIMIZE)
endif

CFLAGS+=-DDELTA_UPDATES

ifneq ($(RENESAS_KEY),)
   CFLAGS+=-DRENESAS_KEY=$(RENESAS_KEY)

   ifeq ($(RENESAS_KEY),1)
      CFLAGS+=-DWOLFBOOT_RENESAS_SCEPROTECT
   else ifeq ($(RENESAS_KEY),2)
      CFLAGS+=-DWOLFBOOT_RENESAS_TSIP
   else ifeq ($(RENESAS_KEY),3)
      CFLAGS+=-DWOLFBOOT_RENESAS_RSIP
   endif
endif

# Sources
OBJS_REAL=\
	$(WOLFDIR)/wolfcrypt/src/asn.o \
	$(WOLFDIR)/wolfcrypt/src/aes.o \
	$(WOLFDIR)/wolfcrypt/src/ecc.o \
	$(WOLFDIR)/wolfcrypt/src/coding.o \
	$(WOLFDIR)/wolfcrypt/src/chacha.o \
	$(WOLFDIR)/wolfcrypt/src/ed25519.o \
	$(WOLFDIR)/wolfcrypt/src/ed448.o \
	$(WOLFDIR)/wolfcrypt/src/fe_operations.o \
	$(WOLFDIR)/wolfcrypt/src/ge_operations.o \
	$(WOLFDIR)/wolfcrypt/src/fe_448.o \
	$(WOLFDIR)/wolfcrypt/src/ge_448.o \
	$(WOLFDIR)/wolfcrypt/src/hash.o \
	$(WOLFDIR)/wolfcrypt/src/logging.o \
	$(WOLFDIR)/wolfcrypt/src/memory.o \
	$(WOLFDIR)/wolfcrypt/src/random.o \
	$(WOLFDIR)/wolfcrypt/src/rsa.o \
	$(WOLFDIR)/wolfcrypt/src/sp_int.o \
	$(WOLFDIR)/wolfcrypt/src/sp_c32.o \
	$(WOLFDIR)/wolfcrypt/src/sp_c64.o \
	$(WOLFDIR)/wolfcrypt/src/sha3.o \
	$(WOLFDIR)/wolfcrypt/src/sha256.o \
	$(WOLFDIR)/wolfcrypt/src/sha512.o \
	$(WOLFDIR)/wolfcrypt/src/tfm.o \
	$(WOLFDIR)/wolfcrypt/src/wc_port.o \
	$(WOLFDIR)/wolfcrypt/src/wolfmath.o

OBJS_REAL+=\
	$(WOLFBOOTDIR)/src/delta.o

OBJS_REAL+=\
	$(WOLFDIR)/wolfcrypt/src/wc_lms.o \
	$(WOLFDIR)/wolfcrypt/src/wc_lms_impl.o

OBJS_REAL+=\
	$(WOLFDIR)/wolfcrypt/src/wc_xmss.o \
	$(WOLFDIR)/wolfcrypt/src/wc_xmss_impl.o
OBJS_REAL+=$(WOLFDIR)/wolfcrypt/src/dilithium.o

OBJS_VIRT=$(addprefix $(OBJDIR), $(notdir $(OBJS_REAL)))
vpath %.c $(WOLFDIR)/wolfcrypt/src/
vpath %.c $(WOLFBOOTDIR)/src/
vpath %.c ./

.PHONY: clean all

all: sign keygen

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

# build objects
$(OBJDIR)/%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFBOOTDIR)/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: $(WOLFDIR)/wolfcrypt/src/%.c
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

# build templates
sign: $(OBJS_VIRT) $(LIBS) sign.o
	@echo "Building signing tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LIBS) $(LDFLAGS)

keygen: $(OBJS_VIRT) $(LIBS) keygen.o
	@echo "Building keygen tool"
	$(Q)$(LD) -o $@ $@.o $(OBJS_VIRT) $(LIBS) $(LDFLAGS)

clean:
	rm -f sign keygen *.o

