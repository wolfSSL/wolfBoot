## Measured boot requires TPM to be present
ifeq ($(MEASURED_BOOT),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_MEASURED_BOOT"
  CFLAGS+=-D"WOLFBOOT_MEASURED_PCR_A=$(MEASURED_PCR_A)"
endif

## DSA Settings

ifeq ($(SIGN),NONE)
  SIGN_OPTIONS+=--no-sign
  PRIVATE_KEY=
  STACK_USAGE=1180
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
  PRIVATE_KEY=ecc256.der
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
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=5008
  else
    STACK_USAGE=3896
  endif
  PUBLIC_KEY_OBJS=./src/ecc256_pub_key.o
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 256; echo $$?),0)
    IMAGE_HEADER_SIZE=256
  endif
endif

ifeq ($(SIGN),ECC384)
  KEYGEN_OPTIONS+=--ecc384
  SIGN_OPTIONS+=--ecc384
  PRIVATE_KEY=ecc384.der
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
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=6056
  else
    STACK_USAGE=5880
  endif
  PUBLIC_KEY_OBJS=./src/ecc384_pub_key.o
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),ECC521)
  KEYGEN_OPTIONS+=--ecc521
  SIGN_OPTIONS+=--ecc521
  PRIVATE_KEY=ecc521.der
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
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=7352
  else
    STACK_USAGE=3896
  endif
  PUBLIC_KEY_OBJS=./src/ecc521_pub_key.o
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),ED25519)
  KEYGEN_OPTIONS+=--ed25519
  SIGN_OPTIONS+=--ed25519
  PRIVATE_KEY=ed25519.der
  WOLFCRYPT_OBJS+= ./lib/wolfssl/wolfcrypt/src/sha512.o \
    ./lib/wolfssl/wolfcrypt/src/ed25519.o \
    ./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/fe_low_mem.o
  PUBLIC_KEY_OBJS=./src/ed25519_pub_key.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ED25519"
  STACK_USAGE?=1180
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 256; echo $$?),0)
    IMAGE_HEADER_SIZE=256
  endif
endif

ifeq ($(SIGN),ED448)
  KEYGEN_OPTIONS+=--ed448
  SIGN_OPTIONS+=--ed448
  PRIVATE_KEY=ed448.der
  WOLFCRYPT_OBJS+= ./lib/wolfssl/wolfcrypt/src/ed448.o \
    ./lib/wolfssl/wolfcrypt/src/ge_low_mem.o \
    ./lib/wolfssl/wolfcrypt/src/ge_448.o \
    ./lib/wolfssl/wolfcrypt/src/fe_448.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wolfmath.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o \
    ./lib/wolfssl/wolfcrypt/src/fe_low_mem.o
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    STACK_USAGE?=1024
  else
    STACK_USAGE?=4376
  endif

  ifneq ($(HASH),SHA3)
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  endif
  PUBLIC_KEY_OBJS=./src/ed448_pub_key.o
  CFLAGS+=-D"WOLFBOOT_SIGN_ED448"
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),RSA2048)
  KEYGEN_OPTIONS+=--rsa2048
  SIGN_OPTIONS+=--rsa2048
  PRIVATE_KEY=rsa2048.der
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  PUBLIC_KEY_OBJS=./src/rsa2048_pub_key.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA2048" $(RSA_EXTRA_CFLAGS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    ifneq ($(SPMATH),1)
      STACK_USAGE=5008
    else
      STACK_USAGE=4096
    endif
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=9096
  else ifneq ($(SPMATH),1)
    STACK_USAGE=35952
  else
    STACK_USAGE=12288
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),RSA4096)
  KEYGEN_OPTIONS+=--rsa4096
  SIGN_OPTIONS+=--rsa4096
  PRIVATE_KEY=rsa4096.der
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  PUBLIC_KEY_OBJS=./src/rsa4096_pub_key.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA4096" $(RSA_EXTRA_CFLAGS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    ifneq ($(SPMATH),1)
      STACK_USAGE=5888
    else
      STACK_USAGE=4096
    endif
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=10680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=69232
  else
    STACK_USAGE=18064
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 1024; echo $$?),0)
    IMAGE_HEADER_SIZE=1024
  endif
endif


ifeq ($(USE_GCC),1)
  CFLAGS+="-Wstack-usage=$(STACK_USAGE)"
endif

ifeq ($(RAM_CODE),1)
  CFLAGS+= -D"RAM_CODE"
endif

ifeq ($(FLAGS_HOME),1)
  CFLAGS+=-D"FLAGS_HOME=1"
endif

ifeq ($(FLAGS_INVERT),1)
  CFLAGS+=-D"WOLFBOOT_FLAGS_INVERT=1"
endif

ifeq ($(DUALBANK_SWAP),1)
  CFLAGS+=-D"DUALBANK_SWAP=1"
endif

ifeq ($(SPI_FLASH),1)
  EXT_FLASH=1
  CFLAGS+=-D"SPI_FLASH=1"
  OBJS+= src/spi_flash.o
  WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
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
  else ifeq ($(ENCRYPT_WITH_AES256),1)
    CFLAGS+=-DWOLFSSL_AES_COUNTER -DWOLFSSL_AES_DIRECT
    CFLAGS+=-DENCRYPT_WITH_AES256 -DWOLFSSL_AES_256
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/aes.o
  else
    ENCRYPT_WITH_CHACHA=1
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/chacha.o
    CFLAGS+=-DENCRYPT_WITH_CHACHA -DHAVE_CHACHA
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



ifeq ($(ALLOW_DOWNGRADE),1)
  CFLAGS+= -D"ALLOW_DOWNGRADE"
endif

ifeq ($(NVM_FLASH_WRITEONCE),1)
  CFLAGS+= -D"NVM_FLASH_WRITEONCE"
endif

ifeq ($(DISABLE_BACKUP),1)
  CFLAGS+= -D"DISABLE_BACKUP"
endif


ifeq ($(DEBUG),1)
  CFLAGS+=-O0 -g -ggdb3
else
  ifeq ($(OPTIMIZATION_LEVEL),)
    CFLAGS+=-Os
  else
    CFLAGS+=-O$(OPTIMIZATION_LEVEL)
  endif
endif

ifeq ($(V),0)
  Q=@
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

OBJS+=$(PUBLIC_KEY_OBJS)
OBJS+=$(UPDATE_OBJS)

ifeq ($(WOLFTPM),1)
  OBJS += lib/wolfTPM/src/tpm2.o \
    lib/wolfTPM/src/tpm2_packet.o \
    lib/wolfTPM/src/tpm2_tis.o \
    lib/wolfTPM/src/tpm2_wrap.o \
    lib/wolfTPM/src/tpm2_param_enc.o
  CFLAGS+=-D"WOLFBOOT_TPM" -D"SIZEOF_LONG=4" -Ilib/wolfTPM \
    -D"MAX_COMMAND_SIZE=1024" -D"MAX_RESPONSE_SIZE=1024" -D"WOLFTPM2_MAX_BUFFER=1500" \
    -D"MAX_SESSION_NUM=1" -D"MAX_DIGEST_BUFFER=973" \
    -D"WOLFTPM_SMALL_STACK"
  # Chip Type: WOLFTPM_SLB9670, WOLFTPM_ST33, WOLFTPM_MCHP
  CFLAGS+=-D"WOLFTPM_SLB9670"
  # Use TPM for hashing (slow)
  #CFLAGS+=-D"WOLFBOOT_HASH_TPM"
  ifneq ($(SPI_FLASH),1)
    WOLFCRYPT_OBJS+=hal/spi/spi_drv_$(SPI_TARGET).o
  endif
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/aes.o
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/hmac.o
  ifeq ($(DEBUG),1)
    CFLAGS+=-DWOLFBOOT_DEBUG_TPM=1
  endif
endif

## Hash settings
ifeq ($(HASH),SHA256)
  CFLAGS+=-D"WOLFBOOT_HASH_SHA256"
endif

ifeq ($(HASH),SHA3)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA3_384"
  SIGN_OPTIONS+=--sha3
endif

CFLAGS+=-DIMAGE_HEADER_SIZE=$(IMAGE_HEADER_SIZE)
OBJS+=$(WOLFCRYPT_OBJS)
