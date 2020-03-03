## CPU Architecture selection via $ARCH

# check for FASTMATH or SP_MATH
ifeq ($(SPMATH),1)
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/sp_int.o
else
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/integer.o ./lib/wolfssl/wolfcrypt/src/tfm.o
endif

# Default flash offset
ARCH_FLASH_OFFSET=0x0

# Default SPI driver name
SPI_TARGET=$(TARGET)


## Hash settings
ifeq ($(HASH),SHA256)
  CFLAGS+=-DWOLFBOOT_HASH_SHA256
endif

ifeq ($(HASH),SHA3)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  CFLAGS+=-DWOLFBOOT_HASH_SHA3_384
  SIGN_OPTIONS+=--sha3
endif

# Include SHA256 module because it's implicitly needed by RSA
WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha256.o

## ARM
ifeq ($(ARCH),ARM)
  CROSS_COMPILE:=arm-none-eabi-
  CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork -DARCH_ARM
  LDFLAGS+=-mthumb -mlittle-endian -mthumb-interwork
  OBJS+=src/boot_arm.o

  ## Target specific configuration
  ifeq ($(TARGET),samr21)
    CORTEX_M0=1
  endif

  ifeq ($(TARGET),stm32l0)
    CORTEX_M0=1
  endif

  ifeq ($(TARGET),stm32g0)
    CORTEX_M0=1
  endif

  ifeq ($(TARGET),stm32f7)
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32h7)
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32wb)
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
  endif

  ## Cortex-M CPU
  ifeq ($(CORTEX_M0),1)
    CFLAGS+=-mcpu=cortex-m0
    LDFLAGS+=-mcpu=cortex-m0
    ifeq ($(SPMATH),1)
      MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
    endif
  else
    ifeq ($(NO_ASM),1)
      ifeq ($(SPMATH),1)
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
      endif
      CFLAGS+=-mcpu=cortex-m3
      LDFLAGS+=-mcpu=cortex-m3
    else
      CFLAGS+=-mcpu=cortex-m3 -fomit-frame-pointer
      LDFLAGS+=-mcpu=cortex-m3
      ifeq ($(SPMATH),1)
        CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
      endif
    endif
  endif
endif

## RISCV
ifeq ($(ARCH),RISCV)
  CROSS_COMPILE:=riscv32-unknown-elf-
  CFLAGS+=-fno-builtin-printf -DUSE_M_TIME -g -march=rv32imac -mabi=ilp32 -mcmodel=medany -nostartfiles -DARCH_RISCV
  LDFLAGS+=-march=rv32imac -mabi=ilp32 -mcmodel=medany
  MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o

  # Prune unused functions and data
  CFLAGS +=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  OBJS+=src/boot_riscv.o src/vector_riscv.o
  ARCH_FLASH_OFFSET=0x20010000
endif


ifeq ($(TARGET),kinetis)
  CFLAGS+= -I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_flash.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_cache.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_controller.o
  ## The following lines can be used to enable HW acceleration
  ifeq ($(MCUXPRESSO_CPU),MK82FN256VLL15)
    ifeq ($(PKA),1)
      PKA_EXTRA_CFLAGS+=-DFREESCALE_LTC_ECC -DFREESCALE_USE_LTC -DFREESCALE_LTC_TFM
      PKA_EXTRA_OBJS+=./lib/wolfssl/wolfcrypt/src/port/nxp/ksdk_port.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ltc.o
    endif
  endif
endif

ifeq ($(TARGET),lpc)
  CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_flashiap.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_power.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_reset.o
  OBJS+=$(MCUXPRESSO_DRIVERS)/mcuxpresso/libpower_softabi.a $(MCUXPRESSO_DRIVERS)/drivers/fsl_common.o
endif


CFLAGS+=-DARCH_FLASH_OFFSET=$(ARCH_FLASH_OFFSET)

## Toolchain setup
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)gcc
AS=$(CROSS_COMPILE)gcc
OBJCOPY:=$(CROSS_COMPILE)objcopy
SIZE:=$(CROSS_COMPILE)size
BOOT_IMG?=test-app/image.bin

ifeq ($(TARGET),stm32f4)
  SPI_TARGET=stm32
endif

ifeq ($(TARGET),stm32wb)
  SPI_TARGET=stm32
  ifneq ($(PKA),0)
    PKA_EXTRA_OBJS+= $(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pka.o  ./lib/wolfssl/wolfcrypt/src/port/st/stm32.o
    PKA_EXTRA_CFLAGS+=-DWOLFSSL_STM32_PKA -I$(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Inc \
        -Isrc -I$(STM32CUBE)/Drivers/BSP/P-NUCLEO-WB55.Nucleo/ -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32WBxx/Include \
        -I$(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Inc/ \
        -I$(STM32CUBE)/Drivers/CMSIS/Include \
		-Ihal \
	    -DSTM32WB55xx
  endif
endif
