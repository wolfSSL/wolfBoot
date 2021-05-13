## Measured boot requires TPM to be present
ifeq ($(MEASURED_BOOT),1)
  WOLFTPM:=1
  CFLAGS+=-D"WOLFBOOT_MEASURED_BOOT"
  CFLAGS+=-D"WOLFBOOT_MEASURED_PCR_A=$(MEASURED_PCR_A)"
endif

## DSA Settings
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
  ifeq ($(WOLFTPM),0)
    CFLAGS+=-Wstack-usage=3888
  else
    #CFLAGS+=-Wstack-usage=6680
  endif
  PUBLIC_KEY_OBJS=./src/ecc256_pub_key.o
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
  CFLAGS+=-D"WOLFBOOT_SIGN_ED25519" -Wstack-usage=1024
endif

ifeq ($(SIGN),RSA2048)
  KEYGEN_OPTIONS+=--rsa2048
  SIGN_OPTIONS+=--rsa2048
  PRIVATE_KEY=rsa2048.der
  IMAGE_HEADER_SIZE=512
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  PUBLIC_KEY_OBJS=./src/rsa2048_pub_key.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA2048" -D"XMALLOC_USER" $(RSA_EXTRA_CFLAGS) \
		  -D"IMAGE_HEADER_SIZE=512"
  ifeq ($(WOLFTPM),0)
    CFLAGS+=-Wstack-usage=12288
  else
    CFLAGS+=-Wstack-usage=8320
  endif
endif

ifeq ($(SIGN),RSA4096)
  KEYGEN_OPTIONS+=--rsa4096
  SIGN_OPTIONS+=--rsa4096
  PRIVATE_KEY=rsa4096.der
  IMAGE_HEADER_SIZE=1024
  WOLFCRYPT_OBJS+= \
    $(RSA_EXTRA_OBJS) \
    $(MATH_OBJS) \
    ./lib/wolfssl/wolfcrypt/src/rsa.o \
    ./lib/wolfssl/wolfcrypt/src/asn.o \
    ./lib/wolfssl/wolfcrypt/src/hash.o \
    ./lib/wolfssl/wolfcrypt/src/wc_port.o
  PUBLIC_KEY_OBJS=./src/rsa4096_pub_key.o
  CFLAGS+=-D"WOLFBOOT_SIGN_RSA4096" -D"XMALLOC_USER" $(RSA_EXTRA_CFLAGS) \
		  -D"IMAGE_HEADER_SIZE=1024"
  ifeq ($(WOLFTPM),0)
    CFLAGS+=-Wstack-usage=18064
  else
    CFLAGS+=-Wstack-usage=10680
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
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/chacha.o
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
  CFLAGS+=-O0 -g -ggdb3 -D"DEBUG=1"
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

OBJS+=$(WOLFCRYPT_OBJS)
