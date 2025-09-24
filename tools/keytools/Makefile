# wolfBoot KeyGen and Signing Tool

-include ../../.config

V?=0
ifeq ($(V),0)
  Q=@
endif

# Default library paths (can be overridden)
WOLFBOOT_LIB_WOLFSSL?=../../lib/wolfssl

# Convert to absolute paths for standalone usage
WOLFBOOT_LIB_WOLFSSL:=$(abspath $(WOLFBOOT_LIB_WOLFSSL))

CC      = gcc
LD      = gcc
WOLFBOOTDIR = ../..
CFLAGS  = -Wall -Wextra -Werror
CFLAGS  += -I. -DWOLFSSL_USER_SETTINGS -I$(WOLFBOOT_LIB_WOLFSSL) -I$(WOLFBOOTDIR)/include
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
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/asn.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ecc.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ed25519.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ed448.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_operations.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_operations.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_448.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_448.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hash.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/logging.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/rsa.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c64.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/tfm.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_port.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wolfmath.o

OBJS_REAL+=\
	$(WOLFBOOTDIR)/src/delta.o

OBJS_REAL+=\
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_lms.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_lms_impl.o

OBJS_REAL+=\
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_xmss.o \
	$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_xmss_impl.o
OBJS_REAL+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/dilithium.o

OBJS_VIRT=$(addprefix $(OBJDIR), $(notdir $(OBJS_REAL)))
vpath %.c $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/
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
$(OBJDIR)/%.o: $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/%.c
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

