WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/asn.o
USE_GCC?=1

# Support for Built-in ROT into OTP flash memory
ifeq ($(FLASH_OTP_KEYSTORE),1)
    CFLAGS+=-D"FLASH_OTP_KEYSTORE"
endif

# Support for TPM signature verification
ifeq ($(WOLFBOOT_TPM_VERIFY),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_TPM_VERIFY"
endif

## Measured boot requires TPM to be present
ifeq ($(MEASURED_BOOT),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_MEASURED_BOOT"
  CFLAGS+=-D"WOLFBOOT_MEASURED_PCR_A=$(MEASURED_PCR_A)"
endif

## TPM keystore
ifeq ($(WOLFBOOT_TPM_KEYSTORE),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_TPM_KEYSTORE"
  ifneq ($(WOLFBOOT_TPM_KEYSTORE_AUTH),)
    CFLAGS+=-DWOLFBOOT_TPM_KEYSTORE_AUTH='"$(WOLFBOOT_TPM_KEYSTORE_AUTH)"'
  endif
  ifneq ($(WOLFBOOT_TPM_KEYSTORE_NV_BASE),)
    CFLAGS+=-D"WOLFBOOT_TPM_KEYSTORE_NV_BASE=$(WOLFBOOT_TPM_KEYSTORE_NV_BASE)"
  endif
endif

## Sealing a secret into the TPM
ifeq ($(WOLFBOOT_TPM_SEAL),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_TPM_SEAL"
  ifneq ($(WOLFBOOT_TPM_SEAL_AUTH),)
    CFLAGS+=-DWOLFBOOT_TPM_SEAL_AUTH='"$(WOLFBOOT_TPM_SEAL_AUTH)"'
  endif
  ifneq ($(WOLFBOOT_TPM_SEAL_NV_BASE),)
    CFLAGS+=-D"WOLFBOOT_TPM_SEAL_NV_BASE=$(WOLFBOOT_TPM_SEAL_NV_BASE)"
  endif
  ifneq ($(WOLFBOOT_TPM_SEAL_KEY_ID),)
    CFLAGS+=-D"WOLFBOOT_TPM_SEAL_KEY_ID=$(WOLFBOOT_TPM_SEAL_KEY_ID)"
  endif
  ifneq ($(POLICY_FILE),)
    SIGN_OPTIONS+=--policy $(POLICY_FILE)
  endif
endif

## DSA Settings
ifeq ($(SIGN),NONE)
  SIGN_OPTIONS+=--no-sign
  ifeq ($(HASH),SHA384)
    STACK_USAGE=3760
  else
    STACK_USAGE=1216
  endif

  CFLAGS+=-DWOLFBOOT_NO_SIGN
endif

ifeq ($(IMAGE_HEADER_SIZE),)
  IMAGE_HEADER_SIZE=256
endif

ifeq ($(WOLFBOOT_SMALL_STACK),1)
  CFLAGS+=-D"WOLFBOOT_SMALL_STACK" -D"XMALLOC_USER"
  STACK_USAGE=4096
  OBJS+=./src/xmalloc.o
endif

ifeq ($(SIGN),ECC256)
  KEYGEN_OPTIONS+=--ecc256
  SIGN_OPTIONS+=--ecc256
  WOLFCRYPT_OBJS+= \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/ecc.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ECC256"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
       STACK_USAGE=4096
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=6680
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=5264
      else
        STACK_USAGE=7632
      endif
    endif
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 256; echo $$?),0)
    IMAGE_HEADER_SIZE=256
  endif
endif

ifeq ($(SIGN),ECC384)
  KEYGEN_OPTIONS+=--ecc384
  SIGN_OPTIONS+=--ecc384
  WOLFCRYPT_OBJS+= \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/ecc.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ECC384"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
       STACK_USAGE=5880
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=6680
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=11248
      else
        STACK_USAGE=11216
      endif
    endif
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),ECC521)
  KEYGEN_OPTIONS+=--ecc521
  SIGN_OPTIONS+=--ecc521
  WOLFCRYPT_OBJS+= \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/ecc.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ECC521"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
       STACK_USAGE=4096
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=6680
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=11256
      else
        STACK_USAGE=8288
      endif
    endif
  endif

  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),ED25519)
  KEYGEN_OPTIONS+=--ed25519
  SIGN_OPTIONS+=--ed25519
  WOLFCRYPT_OBJS+= ./lib/wolfssl/wolfcrypt/src/sha512.o \
    ./lib/wolfssl/wolfcrypt/src/ed25519.o \
    ./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/fe_low_mem.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ED25519"
  ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else
    STACK_USAGE?=5000
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 256; echo $$?),0)
    IMAGE_HEADER_SIZE=256
  endif
endif

ifeq ($(SIGN),ED448)
  KEYGEN_OPTIONS+=--ed448
  SIGN_OPTIONS+=--ed448
  WOLFCRYPT_OBJS+= ./lib/wolfssl/wolfcrypt/src/ed448.o \
    ./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./lib/wolfssl/wolfcrypt/src/ge_448.o \
    ./lib/wolfssl/wolfcrypt/src/fe_448.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/fe_low_mem.o
  ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else
    ifeq ($(WOLFBOOT_SMALL_STACK),1)
      STACK_USAGE?=1024
    else
      STACK_USAGE?=4578
    endif
  endif


  ifneq ($(HASH),SHA3)
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  endif
  CFLAGS+=-D"WOLFBOOT_SIGN_ED448"
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifneq ($(findstring RSA2048,$(SIGN)),)
  KEYGEN_OPTIONS+=--rsa2048
  ifeq ($(SIGN),RSA2048ENC)
    SIGN_OPTIONS+=--rsa2048enc
  else
    SIGN_OPTIONS+=--rsa2048
  endif
  SIGN_ALG=RSA2048 # helps keystore.c check
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA2048" $(RSA_EXTRA_CFLAGS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    ifneq ($(SPMATH),1)
      STACK_USAGE=5008
    else
      STACK_USAGE=4096
    endif
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=9096
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=35952
      else
        STACK_USAGE=17568
      endif
    endif
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifneq ($(findstring RSA3072,$(SIGN)),)
  KEYGEN_OPTIONS+=--rsa3072
  ifeq ($(SIGN),RSA3072ENC)
    SIGN_OPTIONS+=--rsa3072enc
  else
    SIGN_OPTIONS+=--rsa3072
  endif
  SIGN_ALG=RSA3072 # helps keystore.c check
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA3072" $(RSA_EXTRA_CFLAGS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    ifneq ($(SPMATH),1)
      STACK_USAGE=5008
    else
      STACK_USAGE=4364
    endif
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=9096
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=52592
      else
        STACK_USAGE=12288
      endif
    endif
  endif
  ifneq ($(HASH),SHA256)
    IMAGE_HEADER_SIZE=1024
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifneq ($(findstring RSA4096,$(SIGN)),)
  SIGN:=RSA4096
  KEYGEN_OPTIONS+=--rsa4096
  ifeq ($(SIGN),RSA4096ENC)
    SIGN_OPTIONS+=--rsa4096enc
  else
    SIGN_OPTIONS+=--rsa4096
  endif
  SIGN_ALG=RSA4096 # helps keystore.c check
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA4096" $(RSA_EXTRA_CFLAGS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    ifneq ($(SPMATH),1)
      STACK_USAGE=5888
    else
      STACK_USAGE=5768
    endif
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=10680
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=69232
      else
        STACK_USAGE=18064
      endif
    endif
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 1024; echo $$?),0)
    IMAGE_HEADER_SIZE=1024
  endif
endif

ifneq (,$(filter $(SIGN), LMS ext_LMS))
  # For LMS the signature size is a function of the LMS parameters.
  # All five of these parms must be set in the LMS .config file:
  #   LMS_LEVELS, LMS_HEIGHT, LMS_WINTERNITZ, IMAGE_SIGNATURE_SIZE,
  #   IMAGE_HEADER_SIZE

  ifndef LMS_LEVELS
    $(error LMS_LEVELS not set)
  endif

  ifndef LMS_HEIGHT
    $(error LMS_HEIGHT not set)
  endif

  ifndef LMS_WINTERNITZ
    $(error LMS_WINTERNITZ not set)
  endif

  ifndef IMAGE_SIGNATURE_SIZE
    $(error IMAGE_SIGNATURE_SIZE not set)
  endif

  ifndef IMAGE_HEADER_SIZE
    $(error IMAGE_HEADER_SIZE not set)
  endif
endif

ifeq ($(SIGN),LMS)
  KEYGEN_OPTIONS+=--lms
  SIGN_OPTIONS+=--lms
  WOLFCRYPT_OBJS+= \
    ./lib/wolfssl/wolfcrypt/src/wc_lms.o \
    ./lib/wolfssl/wolfcrypt/src/wc_lms_impl.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_LMS" -D"WOLFSSL_HAVE_LMS" \
    -D"WOLFSSL_WC_LMS" -D"WOLFSSL_WC_LMS_SMALL" \
    -D"WOLFSSL_LMS_MAX_LEVELS=$(LMS_LEVELS)" \
    -D"WOLFSSL_LMS_MAX_HEIGHT=$(LMS_HEIGHT)" \
    -D"LMS_LEVELS=$(LMS_LEVELS)" -D"LMS_HEIGHT=$(LMS_HEIGHT)" \
    -D"LMS_WINTERNITZ=$(LMS_WINTERNITZ)" \
    -D"IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"WOLFSSL_LMS_VERIFY_ONLY"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with LMS not supported)
  else
    STACK_USAGE=1320
  endif
endif

ifeq ($(SIGN),ext_LMS)
  LMSDIR = lib/hash-sigs
  KEYGEN_OPTIONS+=--lms
  SIGN_OPTIONS+=--lms
  WOLFCRYPT_OBJS+= \
    ./$(LMSDIR)/src/hss_verify.o \
    ./$(LMSDIR)/src/hss_verify_inc.o \
    ./$(LMSDIR)/src/hss_common.o \
    ./$(LMSDIR)/src/hss_thread_single.o \
    ./$(LMSDIR)/src/hss_zeroize.o \
    ./$(LMSDIR)/src/lm_common.o \
    ./$(LMSDIR)/src/lm_ots_common.o \
    ./$(LMSDIR)/src/lm_ots_verify.o \
    ./$(LMSDIR)/src/lm_verify.o \
    ./$(LMSDIR)/src/endian.o \
    ./$(LMSDIR)/src/hash.o \
    ./$(LMSDIR)/src/sha256.o \
    ./lib/wolfssl/wolfcrypt/src/ext_lms.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_LMS" -D"WOLFSSL_HAVE_LMS" -D"HAVE_LIBLMS" \
    -D"LMS_LEVELS=$(LMS_LEVELS)" -D"LMS_HEIGHT=$(LMS_HEIGHT)" \
    -D"LMS_WINTERNITZ=$(LMS_WINTERNITZ)" -I$(LMSDIR)/src \
    -D"IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"WOLFSSL_LMS_VERIFY_ONLY"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with LMS not supported)
  else
    STACK_USAGE=1024
  endif
endif

ifneq (,$(filter $(SIGN), XMSS ext_XMSS))
  ifndef XMSS_PARAMS
    $(error XMSS_PARAMS not set)
  endif

  ifndef IMAGE_SIGNATURE_SIZE
    $(error IMAGE_SIGNATURE_SIZE not set)
  endif

  ifndef IMAGE_HEADER_SIZE
    $(error IMAGE_HEADER_SIZE not set)
  endif
endif

ifeq ($(SIGN),XMSS)
  # Use wc_xmss implementation.
  KEYGEN_OPTIONS+=--xmss
  SIGN_OPTIONS+=--xmss
  WOLFCRYPT_OBJS+= \
    ./lib/wolfssl/wolfcrypt/src/wc_xmss.o \
    ./lib/wolfssl/wolfcrypt/src/wc_xmss_impl.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_XMSS" -D"WOLFSSL_HAVE_XMSS" \
    -D"WOLFSSL_WC_XMSS" -D"WOLFSSL_WC_XMSS_SMALL" \
    -DWOLFBOOT_XMSS_PARAMS=\"$(XMSS_PARAMS)\"  \
    -D"IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"WOLFSSL_XMSS_VERIFY_ONLY" -D"WOLFSSL_XMSS_MAX_HEIGHT=32"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with XMSS not supported)
  else
    STACK_USAGE=2720
  endif
endif

ifeq ($(SIGN),ext_XMSS)
  # Use ext_xmss implementation.
  XMSSDIR = lib/xmss
  KEYGEN_OPTIONS+=--xmss
  SIGN_OPTIONS+=--xmss
  WOLFCRYPT_OBJS+= \
    ./$(XMSSDIR)/params.o \
    ./$(XMSSDIR)/thash.o \
    ./$(XMSSDIR)/hash_address.o \
    ./$(XMSSDIR)/wots.o \
    ./$(XMSSDIR)/xmss.o \
    ./$(XMSSDIR)/xmss_core_fast.o \
    ./$(XMSSDIR)/xmss_commons.o \
    ./$(XMSSDIR)/utils.o \
    ./lib/wolfssl/wolfcrypt/src/ext_xmss.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_XMSS" -D"WOLFSSL_HAVE_XMSS" -D"HAVE_LIBXMSS" \
    -DWOLFBOOT_XMSS_PARAMS=\"$(XMSS_PARAMS)\" -I$(XMSSDIR) \
    -D"IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"WOLFSSL_XMSS_VERIFY_ONLY" -D"XMSS_VERIFY_ONLY"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with XMSS not supported)
  else
    STACK_USAGE=2712
  endif
endif

# Only needed if using 3rd party integration. This can be
# removed if ext_lms and ext_xmss are deprecated.
ifneq (,$(filter $(SIGN), ext_LMS ext_XMSS))
  CFLAGS  +=-DWOLFSSL_EXPERIMENTAL_SETTINGS
endif

ifeq ($(SIGN),ML_DSA)
  # Use wolfcrypt ML-DSA dilithium implementation.
  KEYGEN_OPTIONS+=--ml_dsa
  SIGN_OPTIONS+=--ml_dsa
  WOLFCRYPT_OBJS+= \
    ./lib/wolfssl/wolfcrypt/src/dilithium.o \
    ./lib/wolfssl/wolfcrypt/src/memory.o \
    ./lib/wolfssl/wolfcrypt/src/sha3.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ML_DSA" \
          -D"IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
          -D"ML_DSA_LEVEL"=$(ML_DSA_LEVEL)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with ML-DSA not supported yet)
  else
    STACK_USAGE=19544
  endif
endif

ifneq ($(SIGN_SECONDARY),)
  LOWERCASE_SECONDARY=$(shell echo $(SIGN_SECONDARY) | tr '[:upper:]' '[:lower:]')
  SECONDARY_KEYGEN_OPTIONS=--$(LOWERCASE_SECONDARY)
  SECONDARY_SIGN_OPTIONS=--$(LOWERCASE_SECONDARY)
  CFLAGS+=-DSIGN_HYBRID
  CFLAGS+=-DSIGN_SECONDARY_$(SIGN_SECONDARY)
endif

ifeq ($(RAM_CODE),1)
  CFLAGS+= -D"RAM_CODE"
endif

ifeq ($(FLAGS_HOME),1)
  CFLAGS+=-D"FLAGS_HOME=1"
endif

ifeq ($(FLAGS_INVERT),1)
  CFLAGS+=-D"WOLFBOOT_FLAGS_INVERT=1"
  FILL_BYTE?=0x00
else
  FILL_BYTE?=0xFF
endif
CFLAGS+=-D"FILL_BYTE=$(FILL_BYTE)"


ifeq ($(DUALBANK_SWAP),1)
  CFLAGS+=-D"DUALBANK_SWAP=1"
endif

ifeq ($(SPI_FLASH),1)
  EXT_FLASH=1
  CFLAGS+=-D"SPI_FLASH=1"
  OBJS+= src/spi_flash.o
  ifeq ($(ARCH),RENESAS_RX)
    WOLFCRYPT_OBJS+=hal/spi/spi_drv_renesas_rx.o
  else
    WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
  endif
endif

ifeq ($(OCTOSPI_FLASH),1)
  EXT_FLASH=1
  QSPI_FLASH=1
  CFLAGS+=-D"OCTOSPI_FLASH=1"
endif

ifeq ($(QSPI_FLASH),1)
  EXT_FLASH=1
  CFLAGS+=-D"QSPI_FLASH=1"
  OBJS+= src/qspi_flash.o
  ifeq ($(ARCH),RENESAS_RX)
    WOLFCRYPT_OBJS+=hal/spi/spi_drv_renesas_rx.o
  else
    WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
  endif
endif

ifeq ($(UART_FLASH),1)
  EXT_FLASH=1
endif

ifeq ($(ENCRYPT),1)
  CFLAGS+=-D"EXT_ENCRYPTED=1"
  ifeq ($(ENCRYPT_WITH_AES128),1)
    CFLAGS+=-DWOLFSSL_AES_COUNTER -DWOLFSSL_AES_DIRECT
    CFLAGS+=-DENCRYPT_WITH_AES128 -DWOLFSSL_AES_128
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/aes.o
  else
    ifeq ($(ENCRYPT_WITH_AES256),1)
      CFLAGS+=-DWOLFSSL_AES_COUNTER -DWOLFSSL_AES_DIRECT
      CFLAGS+=-DENCRYPT_WITH_AES256 -DWOLFSSL_AES_256
      WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/aes.o
    else
      ENCRYPT_WITH_CHACHA=1
      WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/chacha.o
      CFLAGS+=-DENCRYPT_WITH_CHACHA -DHAVE_CHACHA
    endif
  endif
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+= -D"EXT_FLASH=1" -D"PART_UPDATE_EXT=1" -D"PART_SWAP_EXT=1"
  ifeq ($(NO_XIP),1)
    CFLAGS+=-D"PART_BOOT_EXT=1"
  endif
  ifeq ($(UART_FLASH),1)
    CFLAGS+=-D"UART_FLASH=1"
    OBJS+=src/uart_flash.o
    WOLFCRYPT_OBJS+=hal/uart/uart_drv_$(UART_TARGET).o
  endif
endif

ifeq ($(NO_XIP),1)
  CFLAGS+=-D"NO_XIP"
endif


ifeq ($(ALLOW_DOWNGRADE),1)
  CFLAGS+= -D"ALLOW_DOWNGRADE"
endif

ifeq ($(NVM_FLASH_WRITEONCE),1)
  CFLAGS+= -D"NVM_FLASH_WRITEONCE"
endif

ifeq ($(DISABLE_BACKUP),1)
  CFLAGS+= -D"DISABLE_BACKUP"
endif

DEBUG_SYMBOLS?=0
ifeq ($(DEBUG),1)
  CFLAGS+=-O0 -D"DEBUG"
  DEBUG_SYMBOLS=1
else
  ifeq ($(OPTIMIZATION_LEVEL),)
    CFLAGS+=-Os
  else
    CFLAGS+=-O$(OPTIMIZATION_LEVEL)
  endif
endif

# allow elf inclusion of debug symbols even with optimizations enabled
# make DEBUG_SYMBOLS=1
ifeq ($(DEBUG_SYMBOLS),1)
  CFLAGS+=-g
  ifeq ($(USE_GCC),1)
    CFLAGS+=-ggdb3
  else
    CFLAGS+=-gstabs
  endif
endif


Q?=@
ifeq ($(V),1)
  Q=
endif

ifeq ($(NO_MPU),1)
  CFLAGS+=-D"WOLFBOOT_NO_MPU"
endif

ifeq ($(VTOR),0)
  CFLAGS+=-D"NO_VTOR"
endif

ifeq ($(PKA),1)
  OBJS += $(PKA_EXTRA_OBJS)
  CFLAGS+=$(PKA_EXTRA_CFLAGS)
endif

ifneq ($(WOLFBOOT_VERSION),0)
  ifneq ($(WOLFBOOT_VERSION),)
    CFLAGS+=-DWOLFBOOT_VERSION=$(WOLFBOOT_VERSION)
  endif
endif

ifeq ($(DELTA_UPDATES),1)
  OBJS += src/delta.o
  CFLAGS+=-DDELTA_UPDATES
  ifneq ($(DELTA_BLOCK_SIZE),)
    CFLAGS+=-DDELTA_BLOCK_SIZE=$(DELTA_BLOCK_SIZE)
  endif
endif

ifeq ($(ARMORED),1)
  CFLAGS+=-DWOLFBOOT_ARMORED
endif

ifeq ($(WOLFBOOT_HUGE_STACK),1)
  CFLAGS+=-DWOLFBOOT_HUGE_STACK
endif

ifeq ($(WOLFCRYPT_TZ_PKCS11),1)
  CFLAGS+=-DSECURE_PKCS11
  CFLAGS+=-DWOLFSSL_PKCS11_RW_TOKENS
  CFLAGS+=-DCK_CALLABLE="__attribute__((cmse_nonsecure_entry))"
  CFLAGS+=-Ilib/wolfPKCS11
  CFLAGS+=-DWP11_HASH_PIN_COST=3
  OBJS+=src/pkcs11_store.o
  OBJS+=src/pkcs11_callable.o
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/pwdbased.o
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/hmac.o
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/dh.o
  WOLFCRYPT_OBJS+=./lib/wolfPKCS11/src/crypto.o \
		./lib/wolfPKCS11/src/internal.o \
		./lib/wolfPKCS11/src/slot.o \
		./lib/wolfPKCS11/src/wolfpkcs11.o
  STACK_USAGE=16688
  ifneq ($(ENCRYPT),1)
      WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/aes.o
  endif
  ifeq ($(findstring RSA,$(SIGN)),)
      WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/rsa.o
  endif
  ifeq ($(findstring ECC,$(SIGN)),)
      WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/ecc.o
  endif
  ifeq ($(findstring RSA,$(SIGN)),)
    ifeq ($(findstring ECC,$(SIGN)),)
      WOLFCRYPT_OBJS+=$(MATH_OBJS) ./lib/wolfssl/wolfcrypt/src/wolfmath.o
    endif
  endif
endif

OBJS+=$(PUBLIC_KEY_OBJS)
ifneq ($(STAGE1),1)
  OBJS+=$(UPDATE_OBJS)
endif

ifeq ($(WOLFTPM),1)
  OBJS+=\
    ./src/tpm.o \
    lib/wolfTPM/src/tpm2.o \
    lib/wolfTPM/src/tpm2_packet.o \
    lib/wolfTPM/src/tpm2_tis.o \
    lib/wolfTPM/src/tpm2_wrap.o \
    lib/wolfTPM/src/tpm2_param_enc.o
  CFLAGS+=-Ilib/wolfTPM
  CFLAGS+=-D"WOLFBOOT_TPM"
  CFLAGS+=-D"WOLFTPM_SMALL_STACK"
  CFLAGS+=-D"WOLFTPM_AUTODETECT"
  ifneq ($(SPI_FLASH),1)
    # don't use spi if we're using simulator
    ifeq ($(TARGET),sim)
      SIM_TPM=1
    endif
    ifeq ($(SIM_TPM),1)
      CFLAGS+=-DWOLFTPM_SWTPM -DTPM_TIMEOUT_TRIES=0 -DHAVE_NETDB_H
      OBJS+=./lib/wolfTPM/src/tpm2_swtpm.o
    else
      # Use memory-mapped WOLFTPM on x86-64
       ifeq ($(ARCH),x86_64)
          CFLAGS+=-DWOLFTPM_MMIO -DWOLFTPM_EXAMPLE_HAL -DWOLFTPM_INCLUDE_IO_FILE
          OBJS+=./lib/wolfTPM/hal/tpm_io_mmio.o
        # By default, on other architectures, provide SPI driver
        else
          WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
        endif
    endif
  endif
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/aes.o
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/hmac.o
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/random.o
  ifeq ($(DEBUG),1)
    CFLAGS+=-DWOLFBOOT_DEBUG_TPM=1
  endif
endif

## Hash settings
ifeq ($(HASH),SHA256)
  CFLAGS+=-D"WOLFBOOT_HASH_SHA256"
endif

ifeq ($(HASH),SHA384)
  CFLAGS+=-D"WOLFBOOT_HASH_SHA384"
  SIGN_OPTIONS+=--sha384
  ifneq ($(SIGN),ED25519)
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha512.o
  endif
endif

ifeq ($(WOLFBOOT_NO_PARTITIONS),1)
  CFLAGS+=-D"WOLFBOOT_NO_PARTITIONS"
endif

ifeq ($(HASH),SHA3)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA3_384"
  SIGN_OPTIONS+=--sha3
endif

CFLAGS+=-DIMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE)
OBJS+=$(SECURE_OBJS)

# check if both encryption and self update are on
#
ifeq ($(RAM_CODE),1)
  ifeq ($(ENCRYPT),1)
    ifneq ($(ENCRYPT_WITH_CHACHA),1)
       LSCRIPT_IN=NONE
    else
       LSCRIPT_IN=hal/$(TARGET)_chacha_ram.ld
    endif
  endif
endif

# support for elf32 or elf64 loader
ifeq ($(ELF),1)
  CFLAGS+=-DWOLFBOOT_ELF
  OBJS += src/elf.o

  ifneq ($(DEBUG_ELF),)
    CFLAGS+=-DDEBUG_ELF=$(DEBUG_ELF)
  endif
endif

ifeq ($(MULTIBOOT2),1)
  CFLAGS+=-DWOLFBOOT_MULTIBOOT2
  OBJS += src/multiboot.o
endif

ifeq ($(LINUX_PAYLOAD),1)
  CFLAGS+=-DWOLFBOOT_LINUX_PAYLOAD
  ifeq ($(ARCH),x86_64)
    OBJS+=src/x86/linux_loader.o
  endif
endif

ifeq ($(64BIT),1)
  CFLAGS+=-DWOLFBOOT_64BIT
endif

ifeq ($(WOLFBOOT_UNIVERSAL_KEYSTORE),1)
  CFLAGS+=-DWOLFBOOT_UNIVERSAL_KEYSTORE
endif

ifeq ($(DISK_LOCK),1)
  CFLAGS+=-DWOLFBOOT_ATA_DISK_LOCK
  ifneq ($(DISK_LOCK_PASSWORD),)
    CFLAGS+=-DWOLFBOOT_ATA_DISK_LOCK_PASSWORD=\"$(DISK_LOCK_PASSWORD)\"
  endif
  OBJS+=./lib/wolfssl/wolfcrypt/src/coding.o
endif

ifeq ($(FSP), 1)
  X86_FSP_OPTIONS := \
    X86_UART_BASE \
    X86_UART_REG_WIDTH \
    X86_UART_MMIO \
    PCH_HAS_PCR \
    PCI_USE_ECAM \
    PCH_PCR_BASE \
    PCI_ECAM_BASE \
    WOLFBOOT_LOAD_BASE \
    FSP_S_LOAD_BASE

    # set CFLAGS defines for each x86_fsp option
    $(foreach option,$(X86_FSP_OPTIONS),$(if $($(option)), $(eval CFLAGS += -D$(option)=$($(option)))))
endif

ifeq ($(FLASH_MULTI_SECTOR_ERASE),1)
    CFLAGS+=-DWOLFBOOT_FLASH_MULTI_SECTOR_ERASE
endif

CFLAGS+=$(CFLAGS_EXTRA)
OBJS:=$(OBJS_EXTRA) $(OBJS)

ifeq ($(USE_GCC_HEADLESS),1)
  ifneq ($(ARCH),RENESAS_RX)
    CFLAGS+="-Wstack-usage=$(STACK_USAGE)"
  endif
endif

ifeq ($(SIGN_ALG),)
  SIGN_ALG=$(SIGN)
endif

ifeq ($(SIGN_ALG),ext_XMSS)
  SIGN_ALG=XMSS
endif

ifeq ($(SIGN_ALG),ext_LMS)
  SIGN_ALG=LMS
endif

ifneq ($(KEYVAULT_OBJ_SIZE),)
  CFLAGS+=-DKEYVAULT_OBJ_SIZE=$(KEYVAULT_OBJ_SIZE)
endif

ifneq ($(KEYVAULT_MAX_ITEMS),)
  CFLAGS+=-DKEYVAULT_MAX_ITEMS=$(KEYVAULT_MAX_ITEMS)
endif

# Support for using a custom partition ID
ifneq ($(WOLFBOOT_PART_ID),)
  CFLAGS+=-DHDR_IMG_TYPE_APP=$(WOLFBOOT_PART_ID)
  SIGN_OPTIONS+=--id $(WOLFBOOT_PART_ID)
endif
