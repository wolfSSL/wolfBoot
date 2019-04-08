## CPU Architecture selection via $ARCH

# check for FASTMATH or SP_MATH
ifeq ($(SPMATH),1)
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/sp_int.o
else
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/integer.o
endif

## ARM
ifeq ($(ARCH),ARM)
  CROSS_COMPILE:=arm-none-eabi-
  CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork -DARCH_ARM
  LDFLAGS+=-mthumb -mlittle-endian -mthumb-interwork
  OBJS+=src/boot_arm.o
  ARCH_FLASH_OFFSET=0x0

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
  CFLAGS+=-fno-builtin-printf -DUSE_PLIC -DUSE_M_TIME -g -march=rv32imac -mabi=ilp32 -mcmodel=medany -nostartfiles -DARCH_RISCV
  LDFLAGS+=-march=rv32imac -mabi=ilp32 -mcmodel=medany
  OBJS+=src/boot_riscv.o src/vector_riscv.o
  ARCH_FLASH_OFFSET=0x20400000
endif

## Toolchain setup
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)gcc
AS=$(CROSS_COMPILE)gcc
OBJCOPY:=$(CROSS_COMPILE)objcopy
SIZE:=$(CROSS_COMPILE)size
BOOT_IMG?=test-app/image.bin

## Target specific configuration
ifeq ($(TARGET),samr21)
  CORTEX_M0=1
endif

ifeq ($(TARGET),kinetis)
  CFLAGS+= -I$(KINETIS_DRIVERS)/drivers -I$(KINETIS_DRIVERS) -DCPU_$(KINETIS_CPU) -I$(KINETIS_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  OBJS+= $(KINETIS_DRIVERS)/drivers/fsl_clock.o $(KINETIS_DRIVERS)/drivers/fsl_ftfx_flash.o $(KINETIS_DRIVERS)/drivers/fsl_ftfx_cache.o $(KINETIS_DRIVERS)/drivers/fsl_ftfx_controller.o
  ## The following lines can be used to enable HW acceleration
  ##ifeq ($(KINETIS_CPU),MK82FN256VLL15)
  ##  ECC_EXTRA_CFLAGS+=-DFREESCALE_LTC_ECC -DFREESCALE_USE_LTC
  ##  ECC_EXTRA_OBJS+=./lib/wolfssl/wolfcrypt/src/port/nxp/ksdk_port.o $(KINETIS_DRIVERS)/drivers/fsl_ltc.o
  ##endif
endif
