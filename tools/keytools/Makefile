# wolfBoot KeyGen and Signing Tool

-include ../../.config

V?=0
ifeq ($(V),0)
  Q=@
endif

CC      = gcc
LD      = gcc
WOLFBOOTDIR = ../..
WOLFDIR = $(WOLFBOOTDIR)/lib/wolfssl
CFLAGS  = -Wall -Wextra -Werror
CFLAGS  += -I. -DWOLFSSL_USER_SETTINGS -I$(WOLFDIR) -I$(WOLFBOOTDIR)/include -DWOLFBOOT_KEYTOOLS
LDFLAGS =
OBJDIR = ./
LIBS =

ifeq ($(SIGN),LMS)
  LMSDIR = $(WOLFBOOTDIR)/lib/hash-sigs
  LIBS += $(LMSDIR)/lib/hss_lib.a
  CFLAGS  +=-DWOLFBOOT_SIGN_LMS -DWOLFSSL_HAVE_LMS -DHAVE_LIBLMS -I$(LMSDIR)/src \
            -D"LMS_LEVELS=$(LMS_LEVELS)" -D"LMS_HEIGHT=$(LMS_HEIGHT)" \
            -D"LMS_WINTERNITZ=$(LMS_WINTERNITZ)"
endif

# option variables
DEBUG_FLAGS     = -g -DDEBUG -DDEBUG_SIGNTOOL -DDEBUG_WOLFSSL -DDEBUG_WOLFSSL_VERBOSE -fsanitize=address
OPTIMIZE        = -O2

# Options
#CFLAGS+=$(DEBUG_FLAGS)
CFLAGS+=$(OPTIMIZE)

ifeq ($(IMAGE_HEADER_SIZE),)
  IMAGE_HEADER_SIZE=256
endif

CFLAGS+=-DIMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE)
CFLAGS+=-DDELTA_UPDATES

ifeq ($(RENESAS_KEY),1)
   CFLAGS+=-DWOLFBOOT_RENESAS_SCEPROTECT
else ifeq ($(RENESAS_KEY),2)
   CFLAGS+=-DWOLFBOOT_RENESAS_TSIP
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
	$(WOLFDIR)/wolfcrypt/src/wolfmath.o \
	$(WOLFDIR)/wolfcrypt/src/ext_lms.o

OBJS_REAL+=\
	$(WOLFBOOTDIR)/src/delta.o

OBJS_VIRT=$(addprefix $(OBJDIR), $(notdir $(OBJS_REAL)))
vpath %.c $(WOLFDIR)/wolfcrypt/src/
vpath %.c $(WOLFBOOTDIR)/src/
vpath %.c ./

.PHONY: clean all

all: $(WOLFBOOTDIR)/include/target.h sign keygen

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

# Target.h is required for key tools
$(WOLFBOOTDIR)/include/target.h: $(WOLFBOOTDIR)/include/target.h.in
	@cat $(WOLFBOOTDIR)/include/target.h.in | \
	sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$(WOLFBOOT_PARTITION_SIZE)/g" | \
	sed -e "s/@WOLFBOOT_SECTOR_SIZE@/$(WOLFBOOT_SECTOR_SIZE)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_DTS_BOOT_ADDRESS@/$(WOLFBOOT_DTS_BOOT_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_DTS_UPDATE_ADDRESS@/$(WOLFBOOT_DTS_UPDATE_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_ADDRESS@/$(WOLFBOOT_LOAD_ADDRESS)/g" | \
	sed -e "s/@WOLFBOOT_LOAD_DTS_ADDRESS@/$(WOLFBOOT_LOAD_DTS_ADDRESS)/g" \
		> $@

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

# The final make clean is to ensure a subsequent LMS wolfboot
# hash-sigs build is separate from keytools.
$(LMSDIR)/lib/hss_lib.a:
	@echo "Building hss_lib.a"
	$(Q)@$(MAKE) -C $(LMSDIR)/src/ -s clean
	$(Q)@$(MAKE) -C $(LMSDIR)/src/ hss_lib.a
	$(Q)cp $(LMSDIR)/src/hss_lib.a $(LMSDIR)/lib/
	$(Q)@$(MAKE) -C $(LMSDIR)/src/ -s clean
