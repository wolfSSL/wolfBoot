## Measured boot requires TPM to be present
ifeq ($(MEASURED_BOOT),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_MEASURED_BOOT"
  CFLAGS+=-D"WOLFBOOT_MEASURED_PCR_A=$(MEASURED_PCR_A)"
endif

## DSA Settings

ifeq ($(SIGN),NONE)
  SIGN_OPTIONS+=--no-sign
  STACK_USAGE=1216
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
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=5008
  else
    STACK_USAGE=3960
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
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=11248
  else
    STACK_USAGE=5880
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
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=6680
  else ifneq ($(SPMATH),1)
    STACK_USAGE=7352
  else
    STACK_USAGE=3896
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
    STACK_USAGE?=1180
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
  else ifeq ($(WOLFBOOT_SMALL_STACK),1)
    STACK_USAGE?=1024
  else
    STACK_USAGE?=4376
  endif


  ifneq ($(HASH),SHA3)
    WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  endif
  CFLAGS+=-D"WOLFBOOT_SIGN_ED448"
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),RSA2048)
  KEYGEN_OPTIONS+=--rsa2048
  SIGN_OPTIONS+=--rsa2048
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
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

ifeq ($(SIGN),RSA3072)
  KEYGEN_OPTIONS+=--rsa3072
  SIGN_OPTIONS+=--rsa3072
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA3072" $(RSA_EXTRA_CFLAGS)
  ifeq ($(WOLFBOOT_SMALL_STACK),1)
    ifneq ($(SPMATH),1)
      STACK_USAGE=5008
    else
      STACK_USAGE=4096
    endif
  else ifeq ($(WOLFTPM),1)
    STACK_USAGE=9096
  else ifneq ($(SPMATH),1)
    STACK_USAGE=52592
  else
    STACK_USAGE=12288
  endif
  ifneq ($(HASH),SHA256)
    IMAGE_HEADER_SIZE=1024
  endif
  ifeq ($(shell test $(IMAGE_HEADER_SIZE) -lt 512; echo $$?),0)
    IMAGE_HEADER_SIZE=512
  endif
endif

ifeq ($(SIGN),RSA4096)
  KEYGEN_OPTIONS+=--rsa4096
  SIGN_OPTIONS+=--rsa4096
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
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


ifeq ($(USE_GCC_HEADLESS),1)
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
OBJS+=$(WOLFCRYPT_OBJS)
