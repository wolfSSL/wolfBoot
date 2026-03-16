WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/asn.o
USE_GCC?=1
WOLFBOOT_TEST_FILLER?=0
WOLFBOOT_TIME_TEST?=0

# Support for Built-in ROT into OTP flash memory
ifeq ($(FLASH_OTP_KEYSTORE),1)
    CFLAGS+=-D"FLASH_OTP_KEYSTORE"
endif

ifeq ($(WOLFBOOT_TEST_FILLER),1)
    CFLAGS+=-D"WOLFBOOT_TEST_FILLER"
endif

ifeq ($(WOLFBOOT_TIME_TEST),1)
    CFLAGS+=-D"WOLFBOOT_TIME_TEST"
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

ifeq ($(WOLFBOOT_ATTESTATION_IAK),1)
  CFLAGS+=-D"WOLFBOOT_ATTESTATION_IAK"
endif

ifeq ($(WOLFBOOT_UDS_UID_FALLBACK_FORTEST),1)
  CFLAGS+=-D"WOLFBOOT_UDS_UID_FALLBACK_FORTEST"
endif

ifeq ($(WOLFBOOT_UDS_OBKEYS),1)
  ifneq ($(TARGET),stm32h5)
    $(error WOLFBOOT_UDS_OBKEYS is only supported on STM32H5 targets)
  endif
  CFLAGS+=-D"WOLFBOOT_UDS_OBKEYS"
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

## Persist wolfBoot self header at fixed address
## Invariants and defaults are enforced in wolfboot.h
ifeq ($(WOLFBOOT_SELF_HEADER),1)
  CFLAGS+=-DWOLFBOOT_SELF_HEADER
  ifeq ($(WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS),)
    $(error WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS must be set when WOLFBOOT_SELF_HEADER=1)
  endif
  ifneq ($(WOLFBOOT_SELF_HEADER_SIZE),)
    CFLAGS+=-D"WOLFBOOT_SELF_HEADER_SIZE=$(WOLFBOOT_SELF_HEADER_SIZE)"
  endif
  ifeq ($(SELF_HEADER_EXT),1)
    CFLAGS+=-DWOLFBOOT_SELF_HEADER_EXT
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


ECC_OBJS= \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ecc.o

ED25519_OBJS=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ed25519.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_low_mem.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_low_mem.o

ED448_OBJS=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ed448.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_low_mem.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ge_448.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_448.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/fe_low_mem.o

RSA_OBJS=\
    $(RSA_EXTRA_OBJS) \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/rsa.o

LMS_OBJS=\
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_lms.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_lms_impl.o

LMS_EXTRA=\
    -D"WOLFSSL_HAVE_LMS" \
    -D"WOLFSSL_WC_LMS" -D"WOLFSSL_WC_LMS_SMALL" \
    -D"WOLFSSL_LMS_MAX_LEVELS=$(LMS_LEVELS)" \
    -D"WOLFSSL_LMS_MAX_HEIGHT=$(LMS_HEIGHT)" \
    -D"LMS_LEVELS=$(LMS_LEVELS)" -D"LMS_HEIGHT=$(LMS_HEIGHT)" \
    -D"LMS_WINTERNITZ=$(LMS_WINTERNITZ)" \
    -D"LMS_IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"WOLFSSL_LMS_VERIFY_ONLY"

XMSS_OBJS=\
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_xmss.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_xmss_impl.o

XMSS_EXTRA=\
    -D"WOLFSSL_HAVE_XMSS" \
    -D"WOLFSSL_WC_XMSS" -D"WOLFSSL_WC_XMSS_SMALL" \
    -DWOLFBOOT_XMSS_PARAMS=\"$(XMSS_PARAMS)\"  \
    -D"XMSS_IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"WOLFSSL_XMSS_VERIFY_ONLY" -D"WOLFSSL_XMSS_MAX_HEIGHT=32"

ML_DSA_OBJS=\
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/dilithium.o

ML_DSA_EXTRA=\
    -D"ML_DSA_IMAGE_SIGNATURE_SIZE"=$(IMAGE_SIGNATURE_SIZE) \
    -D"ML_DSA_LEVEL"=$(ML_DSA_LEVEL)

ifeq ($(SIGN),ECC256)
  KEYGEN_OPTIONS+=--ecc256
  SIGN_OPTIONS+=--ecc256
  WOLFCRYPT_OBJS+=$(ECC_OBJS)
  WOLFCRYPT_OBJS+=$(MATH_OBJS)
  CFLAGS+=-D"WOLFBOOT_SIGN_ECC256"
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
       STACK_USAGE=4096
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=7616
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
  WOLFCRYPT_OBJS+=$(ECC_OBJS)
  WOLFCRYPT_OBJS+=$(MATH_OBJS)
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
  CFLAGS+=-D"WOLFBOOT_SIGN_ECC521"
  WOLFCRYPT_OBJS+=$(ECC_OBJS)
  WOLFCRYPT_OBJS+=$(MATH_OBJS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
       STACK_USAGE=4096
  else
    ifeq ($(WOLFTPM),1)
      STACK_USAGE=6680
    else
      ifneq ($(SPMATH),1)
        STACK_USAGE=11256
      else
        STACK_USAGE=8480
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
  WOLFCRYPT_OBJS+=$(ED25519_OBJS)
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
  WOLFCRYPT_OBJS+= $(ED448_OBJS)
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
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o
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
  WOLFCRYPT_OBJS+= $(RSA_OBJS)
  WOLFCRYPT_OBJS+=$(MATH_OBJS)
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
  WOLFCRYPT_OBJS+= $(RSA_OBJS)
  WOLFCRYPT_OBJS+=$(MATH_OBJS)
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
  WOLFCRYPT_OBJS+= $(RSA_OBJS)
  WOLFCRYPT_OBJS+=$(MATH_OBJS)
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

ifeq ($(SIGN),LMS)
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
  WOLFCRYPT_OBJS+= $(LMS_OBJS)
  CFLAGS+=-D"WOLFBOOT_SIGN_LMS" $(LMS_EXTRA)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with LMS not supported)
  else
    STACK_USAGE=1320
  endif
endif

ifeq ($(SIGN),XMSS)
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
  WOLFCRYPT_OBJS+=$(XMSS_OBJS)
  CFLAGS+=-D"WOLFBOOT_SIGN_XMSS"
  CFLAGS+=$(XMSS_EXTRA)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with XMSS not supported)
  else
    STACK_USAGE=9352
  endif
endif

ifeq ($(SIGN),ML_DSA)
  # Use wolfcrypt ML-DSA dilithium implementation.
  KEYGEN_OPTIONS+=--ml_dsa
  SIGN_OPTIONS+=--ml_dsa
  WOLFCRYPT_OBJS+= $(ML_DSA_OBJS)
  CFLAGS+=-D"WOLFBOOT_SIGN_ML_DSA" $(ML_DSA_EXTRA)
  ifneq ($(HASH),SHA3)
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o
  endif

  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    $(error WOLFBOOT_SMALL_STACK with ML-DSA not supported yet)
  else
    STACK_USAGE=25000
  endif
endif

ifneq ($(SIGN_SECONDARY),)
  LOWERCASE_SECONDARY=$(shell echo $(SIGN_SECONDARY) | tr '[:upper:]' '[:lower:]')
  SECONDARY_KEYGEN_OPTIONS=--$(LOWERCASE_SECONDARY)
  SECONDARY_SIGN_OPTIONS=--$(LOWERCASE_SECONDARY)
  CFLAGS+=-DSIGN_HYBRID
  CFLAGS+=-DWOLFBOOT_SIGN_SECONDARY_$(SIGN_SECONDARY)
  ifeq ($(SIGN_SECONDARY),RSA2048)
    WOLFCRYPT_OBJS+=$(RSA_OBJS)
    WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),RSA3072)
    WOLFCRYPT_OBJS+=$(RSA_OBJS)
    WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),RSA4096)
    WOLFCRYPT_OBJS+=$(RSA_OBJS)
    WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),ECC256)
    WOLFCRYPT_OBJS+=$(ECC_OBJS)
    WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),ECC384)
    WOLFCRYPT_OBJS+=$(ECC_OBJS)
    WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),ECC521)
    WOLFCRYPT_OBJS+=$(ECC_OBJS)
    WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),ED25519)
    WOLFCRYPT_OBJS+=$(ED25519_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),ED448)
    WOLFCRYPT_OBJS+=$(ED448_OBJS)
  endif
  ifeq ($(SIGN_SECONDARY),LMS)
    WOLFCRYPT_OBJS+=$(LMS_OBJS)
    CFLAGS+=-D"WOLFBOOT_SIGN_LMS" $(LMS_EXTRA)
  endif
  ifeq ($(SIGN_SECONDARY),XMSS)
    WOLFCRYPT_OBJS+= $(XMSS_OBJS)
    CFLAGS+=-D"WOLFBOOT_SIGN_XMSS" $(XMSS_EXTRA)
  endif
  ifeq ($(SIGN_SECONDARY),ML_DSA)
    WOLFCRYPT_OBJS+= $(ML_DSA_OBJS)
    ifneq ($(HASH),SHA3)
      WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o
    endif
    CFLAGS+=-D"WOLFBOOT_SIGN_ML_DSA" $(ML_DSA_EXTRA)
  endif
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

# SD Card support (Cadence SDHCI controller)
ifeq ($(DISK_SDCARD),1)
  CFLAGS+=-D"DISK_SDCARD=1"
endif

# eMMC support (Cadence SDHCI controller)
ifeq ($(DISK_EMMC),1)
  CFLAGS+=-D"DISK_EMMC=1"
endif

# Add SDHCI driver if SD card or eMMC is enabled (only add once)
ifneq ($(filter 1,$(DISK_SDCARD) $(DISK_EMMC)),)
  OBJS+= src/sdhci.o
endif

ifeq ($(UART_FLASH),1)
  EXT_FLASH=1
endif

ifeq ($(ENCRYPT),1)
  CFLAGS+=-D"EXT_ENCRYPTED=1"
  ifeq ($(ENCRYPT_PKCS11),1)
    CFLAGS+=-DENCRYPT_PKCS11 -D'ENCRYPT_PKCS11_PIN=$(ENCRYPT_PKCS11_PIN)'
    ifeq ($(ENCRYPT_PKCS11_KEY_ID_SIZE),)
      ENCRYPT_PKCS11_KEY_ID_SIZE=4
    endif
    CFLAGS+=-DENCRYPT_PKCS11_KEY_ID_SIZE=$(ENCRYPT_PKCS11_KEY_ID_SIZE)
    ifeq ($(ENCRYPT_PKCS11_MECHANISM),)
      # No mechanism defined; assume AES-CTR
      CFLAGS+=-DENCRYPT_PKCS11_MECHANISM=0x00001086UL
      CFLAGS+=-DENCRYPT_PKCS11_BLOCK_SIZE=16
      CFLAGS+=-DENCRYPT_PKCS11_NONCE_SIZE=16
    else
      CFLAGS+=-DENCRYPT_PKCS11_MECHANISM=$(ENCRYPT_PKCS11_MECHANISM)
      CFLAGS+=-DENCRYPT_PKCS11_BLOCK_SIZE=$(ENCRYPT_PKCS11_BLOCK_SIZE)
      CFLAGS+=-DENCRYPT_PKCS11_NONCE_SIZE=$(ENCRYPT_PKCS11_NONCE_SIZE)
    endif
  else
    ifeq ($(ENCRYPT_WITH_AES128),1)
      CFLAGS+=-DWOLFSSL_AES_COUNTER -DWOLFSSL_AES_DIRECT
      CFLAGS+=-DENCRYPT_WITH_AES128 -DWOLFSSL_AES_128
      WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o
    else
      ifeq ($(ENCRYPT_WITH_AES256),1)
        CFLAGS+=-DWOLFSSL_AES_COUNTER -DWOLFSSL_AES_DIRECT
        CFLAGS+=-DENCRYPT_WITH_AES256 -DWOLFSSL_AES_256
        WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o
      else
        ENCRYPT_WITH_CHACHA=1
        WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.o
        CFLAGS+=-DENCRYPT_WITH_CHACHA -DHAVE_CHACHA
      endif
    endif
  endif
  ifeq ($(CUSTOM_ENCRYPT_KEY),1)
    CFLAGS+=-D"CUSTOM_ENCRYPT_KEY"
  endif
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+= -D"EXT_FLASH=1" -D"PART_UPDATE_EXT=1"
  ifeq ($(NO_SWAP_EXT),)
    CFLAGS+= -D"PART_SWAP_EXT=1"
  endif
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

ifeq ($(DEBUG_UART),1)
  ifeq ($(strip $(UART_TARGET)),)
  else
    UART_DRV_OBJ:=hal/uart/uart_drv_$(UART_TARGET).o
    ifneq ($(wildcard $(UART_DRV_OBJ)),)
      CFLAGS+=-DDEBUG_UART
      ifneq ($(findstring $(UART_DRV_OBJ),$(OBJS)),$(UART_DRV_OBJ))
        OBJS+=$(UART_DRV_OBJ)
      endif
    endif
  endif
endif
ifeq ($(NO_QNX),1)
  CFLAGS+=-D"NO_QNX"
endif
ifeq ($(SKIP_GIC_INIT),1)
  CFLAGS+=-D"SKIP_GIC_INIT"
endif

# Boot Exception Level (mutually exclusive)
ifeq ($(BOOT_EL1),1)
  ifeq ($(BOOT_EL2),1)
    $(error BOOT_EL1 and BOOT_EL2 are mutually exclusive. Choose one.)
  endif
  CFLAGS+=-D"BOOT_EL1"
endif

ifeq ($(BOOT_EL2),1)
  CFLAGS+=-D"BOOT_EL2"
endif

ifeq ($(BOOT_BENCHMARK),1)
  CFLAGS+=-D"BOOT_BENCHMARK"
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
  CFLAGS+=-g -DDEBUG_SYMBOLS
  ifeq ($(USE_GCC),1)
    CFLAGS+=-ggdb3
  else ifneq ($(ARCH),AURIX_TC3)
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
  ifeq ($(WOLFCRYPT_TZ_PSA),1)
    $(error WOLFCRYPT_TZ_PKCS11 and WOLFCRYPT_TZ_PSA are mutually exclusive)
  endif
endif

ifeq ($(WOLFCRYPT_TZ_PKCS11),1)
  CFLAGS+=-DSECURE_PKCS11
  CFLAGS+=-DWOLFSSL_PKCS11_RW_TOKENS
  CFLAGS+=-DCK_CALLABLE="__attribute__((cmse_nonsecure_entry))"
  CFLAGS+=-I$(WOLFBOOT_LIB_WOLFPKCS11)
  CFLAGS+=-DWP11_HASH_PIN_COST=3
  LDFLAGS+=--specs=nano.specs
  WOLFCRYPT_OBJS+=src/pkcs11_store.o
  WOLFCRYPT_OBJS+=src/pkcs11_callable.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/pwdbased.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hmac.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/dh.o
  ifeq ($(findstring random.o,$(WOLFCRYPT_OBJS)),)
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o
  endif
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFPKCS11)/src/crypto.o \
        $(WOLFBOOT_LIB_WOLFPKCS11)/src/internal.o \
        $(WOLFBOOT_LIB_WOLFPKCS11)/src/slot.o \
        $(WOLFBOOT_LIB_WOLFPKCS11)/src/wolfpkcs11.o
  STACK_USAGE=16688
  ifeq ($(ENCRYPT_WITH_AES128)$(ENCRYPT_WITH_AES256),)
      WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o
  endif
  ifeq ($(findstring RSA,$(SIGN)),)
  ifeq ($(findstring RSA,$(SIGN_SECONDARY)),)
      WOLFCRYPT_OBJS+=$(RSA_OBJS)
  endif
  endif
  ifeq ($(findstring ECC,$(SIGN)),)
  ifeq ($(findstring ECC,$(SIGN_SECONDARY)),)
      WOLFCRYPT_OBJS+=$(ECC_OBJS)
  endif
  endif
  ifeq ($(findstring ECC,$(SIGN)),)
  ifeq ($(findstring ECC,$(SIGN_SECONDARY)),)
  ifeq ($(findstring RSA,$(SIGN)),)
  ifeq ($(findstring RSA,$(SIGN_SECONDARY)),)
	  WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  endif
  endif
  endif
endif

ifeq ($(WOLFCRYPT_TZ_PSA),1)
  CFLAGS+=-DWOLFCRYPT_TZ_PSA
  CFLAGS+=-DWOLFCRYPT_SECURE_MODE
  CFLAGS+=-DWOLFSSL_PSA_ENGINE
  CFLAGS+=-DWOLFPSA_CUSTOM_STORE
  CFLAGS+=-DNO_DES3 -DNO_DES3_TLS_SUITES
  WOLFPSA_CFLAGS+=-I$(WOLFBOOT_LIB_WOLFPSA)
  WOLFPSA_CFLAGS+=-I$(WOLFBOOT_LIB_WOLFPSA)/wolfpsa
  LDFLAGS+=--specs=nano.specs
  WOLFCRYPT_OBJS+=src/psa_store.o
  WOLFCRYPT_OBJS+=src/arm_tee_psa_veneer.o
  WOLFCRYPT_OBJS+=src/arm_tee_psa_ipc.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/pwdbased.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hmac.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/dh.o
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/chacha.o
  ifeq ($(findstring random.o,$(WOLFCRYPT_OBJS)),)
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o
  endif
  WOLFPSA_SRCS := $(filter-out $(WOLFBOOT_LIB_WOLFPSA)/src/psa_store_posix.c, \
    $(wildcard $(WOLFBOOT_LIB_WOLFPSA)/src/*.c))
  WOLFPSA_OBJS := $(patsubst %.c,%.o,$(WOLFPSA_SRCS))
  WOLFCRYPT_OBJS+=$(WOLFPSA_OBJS)
  STACK_USAGE=16688
  ifneq ($(ENCRYPT),1)
      WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o
  endif
  ifeq ($(findstring RSA,$(SIGN)),)
  ifeq ($(findstring RSA,$(SIGN_SECONDARY)),)
      WOLFCRYPT_OBJS+=$(RSA_OBJS)
  endif
  endif
  ifeq ($(findstring ECC,$(SIGN)),)
  ifeq ($(findstring ECC,$(SIGN_SECONDARY)),)
      WOLFCRYPT_OBJS+=$(ECC_OBJS)
  endif
  endif
  ifeq ($(findstring ECC,$(SIGN)),)
  ifeq ($(findstring ECC,$(SIGN_SECONDARY)),)
  ifeq ($(findstring RSA,$(SIGN)),)
  ifeq ($(findstring RSA,$(SIGN_SECONDARY)),)
	  WOLFCRYPT_OBJS+=$(MATH_OBJS)
  endif
  endif
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
    $(WOLFBOOT_LIB_WOLFTPM)/src/tpm2.o \
    $(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_packet.o \
    $(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_tis.o \
    $(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_wrap.o \
    $(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_param_enc.o
  CFLAGS+=-I$(WOLFBOOT_LIB_WOLFTPM)
  CFLAGS+=-D"WOLFBOOT_TPM"
  CFLAGS+=-D"WOLFTPM_SMALL_STACK"
  CFLAGS+=-D"WOLFTPM_AUTODETECT"
  ifneq ($(SPI_FLASH),1)
    # don't use spi if we're using simulator
    ifeq ($(TARGET),sim)
      SIM_TPM=1
    endif
    ifeq ($(SIM_TPM),1)
      CFLAGS+=-DWOLFTPM_SWTPM -DTPM_TIMEOUT_TRIES=0 -DHAVE_NETDB_H -DHAVE_UNISTD_H
      OBJS+=$(WOLFBOOT_LIB_WOLFTPM)/src/tpm2_swtpm.o
    else
      # Use memory-mapped WOLFTPM on x86-64
       ifeq ($(ARCH),x86_64)
          CFLAGS+=-DWOLFTPM_MMIO -DWOLFTPM_EXAMPLE_HAL -DWOLFTPM_INCLUDE_IO_FILE
          OBJS+=$(WOLFBOOT_LIB_WOLFTPM)/hal/tpm_io_mmio.o
        # By default, on other architectures, provide SPI driver
        else
          WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
        endif
    endif
  endif
  ifneq ($(WOLFCRYPT_TZ_PKCS11),1)
  ifneq ($(WOLFCRYPT_TZ_PSA),1)
    ifneq ($(ENCRYPT),1)
      WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o
    endif
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hmac.o
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o
  endif
  endif
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
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.o
  endif
endif

ifeq ($(WOLFBOOT_NO_PARTITIONS),1)
  CFLAGS+=-D"WOLFBOOT_NO_PARTITIONS"
endif

ifeq ($(HASH),SHA3)
  ifeq ($(HASH_HAL),)
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o
  endif
  CFLAGS+=-D"WOLFBOOT_HASH_SHA3_384"
  SIGN_OPTIONS+=--sha3
endif

CFLAGS+=-DIMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE)
OBJS+=$(SECURE_OBJS)

# check if both encryption and self update are on
#
ifeq ($(RAM_CODE),1)
  ifeq ($(ENCRYPT),1)
    ifeq ($(ENCRYPT_WITH_CHACHA),1)
       LSCRIPT_IN=hal/$(TARGET)_chacha_ram.ld
    endif
  endif
  ifeq ($(ARCH),ARM)
    CFLAGS+=-mlong-calls
  endif
endif

# Support external encryption cache
#
ifeq ($(ENCRYPT),1)
  ifeq ($(ENCRYPT_CACHE),1)
	CFLAGS+=-D"WOLFBOOT_ENCRYPT_CACHE=$(ENCRYPT_CACHE)"
  endif
endif

# support for elf32 or elf64 loader
ifeq ($(ELF),1)
  CFLAGS+=-DWOLFBOOT_ELF
  OBJS += src/elf.o

  ifneq ($(DEBUG_ELF),)
    CFLAGS+=-DDEBUG_ELF=$(DEBUG_ELF)
  endif
  ifeq ($(ELF_FLASH_SCATTER),1)
    CFLAGS+=-D"WOLFBOOT_ELF_FLASH_SCATTER=1"
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
  OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.o
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
OBJS+=$(OBJS_EXTRA)

ifeq ($(USE_GCC_HEADLESS),1)
  ifneq ($(ARCH),RENESAS_RX)
    ifneq ($(ARCH),AURIX_TC3)
      CFLAGS+="-Wstack-usage=$(STACK_USAGE)"
    endif
  endif
endif

ifeq ($(SIGN_ALG),)
  SIGN_ALG=$(SIGN)
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

# wolfHSM client options
ifeq ($(WOLFHSM_CLIENT),1)
  WOLFCRYPT_OBJS += \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/cryptocb.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.o

  ifeq ($(SIGN),ML_DSA)
    WOLFCRYPT_OBJS += $(MATH_OBJS)
    # Dilithium asn.c decode/encode requires mp_xxx functions
    WOLFCRYPT_OBJS += \
        $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o

    # Large enough to handle the largest Dilithium key/signature
    CFLAGS += -DWOLFHSM_CFG_COMM_DATA_LEN=5000
  endif

  WOLFHSM_OBJS += \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_client.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_client_nvm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_client_cryptocb.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_client_crypto.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_client_dma.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_crypto.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_dma.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_utils.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_comm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_comm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_nvm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_customcb.o
  #includes
  CFLAGS += -I"$(WOLFBOOT_LIB_WOLFHSM)"
  # defines
  CFLAGS += -DWOLFBOOT_ENABLE_WOLFHSM_CLIENT -DWOLFHSM_CFG_ENABLE_CLIENT
  # Make sure we export generated public keys so they can be used to load into
  # HSM out-of-band
  KEYGEN_OPTIONS += --exportpubkey --der

  # Default to using public keys on the HSM
  ifneq ($(WOLFHSM_CLIENT_LOCAL_KEYS),1)
    KEYGEN_OPTIONS += --nolocalkeys
    CFLAGS += -DWOLFBOOT_USE_WOLFHSM_PUBKEY_ID
    # big enough for cert chain
    CFLAGS += -DWOLFHSM_CFG_COMM_DATA_LEN=5000
  endif

  # Ensure wolfHSM is configured to use certificate manager if we are
  # doing cert chain verification
  ifneq ($(CERT_CHAIN_VERIFY),)
    WOLFHSM_OBJS += \
      $(WOLFBOOT_LIB_WOLFHSM)/src/wh_client_cert.o \
      $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_cert.o
    CFLAGS += -DWOLFHSM_CFG_CERTIFICATE_MANAGER
  endif
endif

# wolfHSM server options
ifeq ($(WOLFHSM_SERVER),1)
  WOLFCRYPT_OBJS += \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/cryptocb.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.o \
    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o

  ifeq ($(SIGN),ML_DSA)
    WOLFCRYPT_OBJS += $(MATH_OBJS)
    # Large enough to handle the largest Dilithium key/signature
    CFLAGS += -DWOLFHSM_CFG_COMM_DATA_LEN=5000
  endif

  WOLFHSM_OBJS += \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_utils.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_comm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_nvm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_nvm_flash.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_keyid.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_flash_unit.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_crypto.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server_nvm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server_crypto.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server_counter.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server_keystore.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server_customcb.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_customcb.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_keystore.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_crypto.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_counter.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_nvm.o \
    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_comm.o

  #includes
  CFLAGS += -I"$(WOLFBOOT_LIB_WOLFHSM)"
  # defines'
  CFLAGS += -DWOLFBOOT_ENABLE_WOLFHSM_SERVER -DWOLFHSM_CFG_ENABLE_SERVER

  # Ensure wolfHSM is configured to use certificate manager if we are
  # doing cert chain verification
  ifneq ($(CERT_CHAIN_VERIFY),)
    CFLAGS += -I"$(WOLFBOOT_LIB_WOLFSSL)"
    WOLFCRYPT_OBJS += \
      $(WOLFBOOT_LIB_WOLFSSL)/src/internal.o \
      $(WOLFBOOT_LIB_WOLFSSL)/src/ssl.o \
      $(WOLFBOOT_LIB_WOLFSSL)/src/ssl_certman.o

    WOLFHSM_OBJS += \
      $(WOLFBOOT_LIB_WOLFHSM)/src/wh_message_cert.o \
      $(WOLFBOOT_LIB_WOLFHSM)/src/wh_server_cert.o
    CFLAGS += -DWOLFHSM_CFG_CERTIFICATE_MANAGER
  endif
endif

# Cert chain verification options
ifneq ($(CERT_CHAIN_VERIFY),)
  CFLAGS += -DWOLFBOOT_CERT_CHAIN_VERIFY
  # export the private key in DER format so it can be used with certificates
  KEYGEN_OPTIONS += --der

  # User-provided cert chain takes precedence
  ifneq ($(USER_CERT_CHAIN),)
    CERT_CHAIN_FILE = $(USER_CERT_CHAIN)
  else
    # Auto-generate dummy cert chain (when USER_CERT_CHAIN not provided)
    CERT_CHAIN_FILE = test-dummy-ca/raw-chain.der

    # Set appropriate cert gen algo based on signature algorithm
    ifeq ($(SIGN),ECC256)
      CERT_CHAIN_GEN_ALGO+=ecc256
    endif
    ifeq ($(SIGN),RSA2048)
      CERT_CHAIN_GEN_ALGO+=rsa2048
    endif
    ifeq ($(SIGN),RSA4096)
      CERT_CHAIN_GEN_ALGO+=rsa4096
      # Reasonably large default
      CFLAGS += -DWOLFHSM_CFG_MAX_CERT_SIZE=4096
    endif
  endif
  SIGN_OPTIONS += --cert-chain $(CERT_CHAIN_FILE)
endif

# Clock Speed (Hz)
ifneq ($(CLOCK_SPEED),)
	CFLAGS += -DCLOCK_SPEED=$(CLOCK_SPEED)
endif

# STM32F4 clock options
ifneq ($(STM32_PLLM),)
	CFLAGS += -DSTM32_PLLM=$(STM32_PLLM)
endif
ifneq ($(STM32_PLLN),)
	CFLAGS += -DSTM32_PLLN=$(STM32_PLLN)
endif
ifneq ($(STM32_PLLP),)
	CFLAGS += -DSTM32_PLLP=$(STM32_PLLP)
endif
ifneq ($(STM32_PLLQ),)
	CFLAGS += -DSTM32_PLLQ=$(STM32_PLLQ)
endif

# STM32 UART options
ifeq ($(USE_UART1),1)
	CFLAGS += -DUSE_UART1=1
endif

ifeq ($(USE_UART3),1)
	CFLAGS += -DUSE_UART3=1
endif

ifneq ($(WOLFBOOT_PARTITION_FILENAME),)
	CFLAGS += -DWOLFBOOT_PARTITION_FILENAME=$(WOLFBOOT_PARTITION_FILENAME)
endif

# Clock Restore Option (default on)
ifneq ($(WOLFBOOT_RESTORE_CLOCK),0)
	CFLAGS += -DWOLFBOOT_RESTORE_CLOCK
endif

ifeq ($(TZEN),1)
  CFLAGS+=-DTZEN
endif
