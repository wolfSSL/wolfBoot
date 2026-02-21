## CPU Architecture selection via $ARCH

# check for math library
ifeq ($(SPMATH),1)
  # SP Math
  MATH_OBJS:=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.o
else
  ifeq ($(SPMATHALL),1)
    # SP Math all
    CFLAGS+=-DWOLFSSL_SP_MATH_ALL
    MATH_OBJS:=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.o
  else
    # Fastmath
    CFLAGS+=-DUSE_FAST_MATH
    MATH_OBJS:=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/integer.o $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/tfm.o
  endif
endif

# Default flash offset
ARCH_FLASH_OFFSET?=0x0

# Default SPI driver name
SPI_TARGET=$(TARGET)

# Default UART driver name
UART_TARGET=$(TARGET)

# Include some modules by default
WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.o \
                $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hash.o \
                $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.o \
                $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_port.o \
                $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wolfmath.o


ifeq ($(ARCH),x86_64)
  CFLAGS+=-DARCH_x86_64 -DFAST_MEMCPY
  ifeq ($(FORCE_32BIT),1)
    NO_ASM=1
    CFLAGS+=-DFORCE_32BIT
  endif
  ifeq ($(SPMATH),1)
    ifeq ($(NO_ASM),1)
      ifeq ($(FORCE_32BIT),1)
        MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
        CFLAGS+=-DWOLFSSL_SP_DIV_WORD_HALF
      else
        MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c64.o
      endif
    else
      MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_x86_64.o
    endif
  endif
  ifeq ($(TARGET),x86_64_efi)
     OBJS+=src/boot_x86_64.o
    ifeq ($(DEBUG),1)
      CFLAGS+=-DWOLFBOOT_DEBUG_EFI=1
    endif
  endif
endif

## ARM Cortex-A
ifeq ($(ARCH),AARCH64)
  CROSS_COMPILE?=aarch64-none-elf-
  CFLAGS+=-DARCH_AARCH64 -DFAST_MEMCPY
  OBJS+=src/boot_aarch64.o src/boot_aarch64_start.o

  ifeq ($(TARGET),zynq)
    ARCH_FLAGS=-march=armv8-a+crypto
    CFLAGS+=$(ARCH_FLAGS) -DCORTEX_A53
    CFLAGS+=-DNO_QNX
    # Support detection and skip of U-Boot legacy header */
    CFLAGS+=-DWOLFBOOT_UBOOT_LEGACY
    CFLAGS+=-DWOLFBOOT_DUALBOOT

    ifeq ($(HW_SHA3),1)
      # Use HAL for hash (see zynqmp.c)
      HASH_HAL=1
      CFLAGS+=-DWOLFBOOT_ZYNQMP_CSU
    endif
  endif

  ifeq ($(TARGET),versal)
    # AMD Versal ACAP (VMK180) - Dual Cortex-A72
    ARCH_FLAGS=-mcpu=cortex-a72+crypto -march=armv8-a+crypto -mtune=cortex-a72
    CFLAGS+=$(ARCH_FLAGS) -DCORTEX_A72
    CFLAGS+=-DWOLFBOOT_DUALBOOT
    # Support detection and skip of U-Boot legacy header
    CFLAGS+=-DWOLFBOOT_UBOOT_LEGACY
    # PLM owns RVBAR on Versal in JTAG boot; skip RVBAR writes
    CFLAGS+=-DSKIP_RVBAR=1
    # Disable SDMA for multi-block transfers - use PIO instead.
    # The Versal Arasan SDHCI controller does not restart SDMA after
    # boundary crossings via SRS22/SRS23 writes (Cadence-specific behavior).
    CFLAGS_EXTRA+=-DSDHCI_SDMA_DISABLED
  endif

  ifeq ($(TARGET),nxp_ls1028a)
    ARCH_FLAGS=-mcpu=cortex-a72+crypto -march=armv8-a+crypto -mtune=cortex-a72
    CFLAGS+=$(ARCH_FLAGS) -DCORTEX_A72

    CFLAGS +=-ffunction-sections -fdata-sections
    LDFLAGS+=-Wl,--gc-sections

    ifeq ($(DEBUG_UART),0)
      CFLAGS+=-fno-builtin-printf
    endif

    SPI_TARGET=nxp
  endif

  # Default ARM ASM setting for unrecognized AARCH64 targets
  ifeq ($(filter zynq versal nxp_ls1028a,$(TARGET)),)
    NO_ARM_ASM?=1
  endif

  ifeq ($(SPMATH),1)
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_arm64.o
  endif
  ifneq ($(NO_ARM_ASM),1)
    ARCH_FLAGS=-mstrict-align
    CFLAGS+=$(ARCH_FLAGS) -DWOLFSSL_ARMASM -DWOLFSSL_ARMASM_INLINE -DWC_HASH_DATA_ALIGNMENT=8 -DWOLFSSL_AARCH64_PRIVILEGE_MODE
    WOLFCRYPT_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/cpuid.o \
                      $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/armv8-sha512-asm_c.o \
                      $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/armv8-sha3-asm_c.o \
                      $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/armv8-aes-asm_c.o \
                      $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/armv8-sha256-asm_c.o
  endif
endif

## ARM Cortex-M
ifeq ($(ARCH),ARM)
  CROSS_COMPILE?=arm-none-eabi-
  CFLAGS+=-DARCH_ARM
  CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork
  LDFLAGS+=-mthumb -mlittle-endian -mthumb-interwork

  ## Target specific configuration
  ifeq ($(TARGET),samr21)
    CORTEX_M0=1
  endif
  ifeq ($(TARGET),imx_rt)
    CORTEX_M7=1
  endif
  ifeq ($(TARGET),stm32l0)
    CORTEX_M0=1
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32c0)
    CORTEX_M0=1
    ARCH_FLASH_OFFSET=0x08000000
  endif

  ifeq ($(TARGET),stm32g0)
    CORTEX_M0=1
    ARCH_FLASH_OFFSET=0x08000000
  endif

  ifeq ($(TARGET),stm32f1)
    CORTEX_M3=1
    NO_ARM_ASM=1
    ARCH_FLASH_OFFSET=0x08000000
  endif

  ifeq ($(TARGET),stm32f4)
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),pic32cz)
    ARCH_FLASH_OFFSET=0x08000000
    CORTEX_M7=1
    OBJS+=hal/pic32c.o
  endif

  ifeq ($(TARGET),pic32ck)
    ARCH_FLASH_OFFSET=0x08000000
    CORTEX_M33=1
    OBJS+=hal/pic32c.o
  endif

  ifeq ($(TARGET),stm32l4)
    SPI_TARGET=stm32
    ARCH_FLASH_OFFSET=0x08000000
    OBJS+=$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.o
    OBJS+=$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.o
    CFLAGS+=-DSTM32L4A6xx -DUSE_HAL_DRIVER -Isrc -Ihal \
      -I$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Inc/ \
      -I$(STM32CUBE)/Drivers/BSP/STM32L4xx_Nucleo_144/ \
      -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32L4xx/Include/ \
      -I$(STM32CUBE)/Drivers/CMSIS/Include/
  endif

  ifeq ($(TARGET),stm32f7)
    CORTEX_M7=1
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32h7)
    CORTEX_M7=1
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32wb)
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
    ifneq ($(PKA),0)
      PKA_EXTRA_OBJS+= $(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pka.o $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/st/stm32.o
      PKA_EXTRA_CFLAGS+=-DWOLFSSL_STM32WB -DWOLFSSL_STM32_PKA -DWOLFSSL_STM32_CUBEMX -DNO_STM32_HASH -DSTM32WB55xx
      PKA_EXTRA_CFLAGS+=-Isrc -Ihal \
          -I$(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Inc \
          -I$(STM32CUBE)/Drivers/BSP/P-NUCLEO-WB55.Nucleo/ \
          -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32WBxx/Include \
          -I$(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Inc \
          -I$(STM32CUBE)/Drivers/CMSIS/Include
    endif
  endif


  ifeq ($(TARGET),stm32l5)
    CORTEX_M33=1
    CFLAGS+=-Ihal
    ARCH_FLASH_OFFSET=0x08000000
    ifeq ($(TZEN),1)
      WOLFBOOT_ORIGIN=0x0C000000
    else
      WOLFBOOT_ORIGIN=0x08000000
      LSCRIPT_IN=hal/$(TARGET)-ns.ld
    endif
  endif

  ifeq ($(TARGET),stm32u5)
    CORTEX_M33=1
    CFLAGS+=-Ihal
    ARCH_FLASH_OFFSET=0x08000000
    ifeq ($(TZEN),1)
      WOLFBOOT_ORIGIN=0x0C000000
    else
      WOLFBOOT_ORIGIN=0x08000000
      LSCRIPT_IN=hal/$(TARGET)-ns.ld
    endif
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32h5)
    CORTEX_M33=1
    CFLAGS+=-Ihal
    ARCH_FLASH_OFFSET=0x08000000
    ifeq ($(TZEN),1)
      WOLFBOOT_ORIGIN=0x0C000000
    else
      WOLFBOOT_ORIGIN=0x08000000
      LSCRIPT_IN=hal/$(TARGET)-ns.ld
    endif
    SPI_TARGET=stm32
    ifneq ($(DEBUG),0)
        CFLAGS+=-DPKCS11_SMALL
    endif

  endif

  ifeq ($(TARGET),rp2350)
    CORTEX_M33=1
    CFLAGS+=-Ihal
    ARCH_FLASH_OFFSET=0x10000000
    WOLFBOOT_ORIGIN=0x10000000
    ifeq ($(TZEN),1)
      LSCRIPT_IN=hal/$(TARGET).ld
    else
      LSCRIPT_IN=hal/$(TARGET)-ns.ld
    endif
    SPI_TARGET=raspberrypi_pico
    CFLAGS+=-DPICO_SDK_PATH=$(PICO_SDK_PATH)
    CFLAGS+=-I$(PICO_SDK_PATH)/src/common/pico_stdlib_headers/include
  endif

  ifeq ($(TARGET),sama5d3)
     CORTEX_A5=1
     UPDATE_OBJS:=src/update_ram.o
     CFLAGS+=-DWOLFBOOT_DUALBOOT -DEXT_FLASH -DNAND_FLASH -fno-builtin -ffreestanding
     CFLAGS+=-DWOLFBOOT_USE_STDLIBC
  endif

  ifeq ($(TARGET),va416x0)
    CFLAGS+=-I$(WOLFBOOT_ROOT)/hal/vorago/ \
            -I$(VORAGO_SDK_DIR)/common/drivers/hdr/ \
            -I$(VORAGO_SDK_DIR)/common/mcu/hdr/ \
            -I$(VORAGO_SDK_DIR)/common/utils/hdr/
    SDK_OBJS=$(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal.o \
             $(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal_spi.o \
             $(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal_clkgen.o \
             $(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal_ioconfig.o \
             $(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal_irqrouter.o \
             $(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal_uart.o \
             $(VORAGO_SDK_DIR)/common/drivers/src/va416xx_hal_timer.o \
             $(VORAGO_SDK_DIR)/common/mcu/src/system_va416xx.o
    ifeq ($(USE_HAL_SPI_FRAM),1)
      SDK_OBJS+=$(VORAGO_SDK_DIR)/common/utils/src/spi_fram.o
      CFLAGS+=-DUSE_HAL_SPI_FRAM
    endif
    OBJS+=$(SDK_OBJS)
  endif

## Cortex CPU

ifeq ($(CORTEX_A5),1)
  FPU=-mfpu=vfp4-d16
  CFLAGS+=-mcpu=cortex-a5  -mtune=cortex-a5 -static -z noexecstack \
		  -mno-unaligned-access
  LDLAGS+=-mcpu=cortex-a5 -mtune=cortex-a5  -mtune=cortex-a5 -static \
          -z noexecstack  -Ttext 0x300000
  # Cortex-A uses boot_arm32.o
  OBJS+=src/boot_arm32.o src/boot_arm32_start.o
  ifeq ($(NO_ASM),1)
    MATH_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
  else
    MATH_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_arm32.o
    ifneq ($(NO_ARM_ASM),1)
      OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/armv8-32-sha256-asm.o
      OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/armv8-32-sha256-asm_c.o
      CFLAGS+=-DWOLFSSL_SP_ARM32_ASM -DWOLFSSL_ARMASM -DWOLFSSL_ARMASM_NO_HW_CRYPTO \
              -DWOLFSSL_ARM_ARCH=7 -DWOLFSSL_ARMASM_INLINE -DWOLFSSL_ARMASM_NO_NEON
    endif
  endif
else
  # All others use boot_arm.o
  OBJS+=src/boot_arm.o
  ifneq ($(NO_ARM_ASM),1)
    CORTEXM_ARM_EXTRA_OBJS= \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-aes-asm.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-aes-asm_c.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-sha256-asm.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-sha256-asm_c.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-sha512-asm.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-sha512-asm_c.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-sha3-asm.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-sha3-asm_c.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-chacha-asm.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/arm/thumb2-chacha-asm_c.o

    CORTEXM_ARM_EXTRA_CFLAGS+=-DWOLFSSL_ARMASM -DWOLFSSL_ARMASM_NO_HW_CRYPTO \
                              -DWOLFSSL_ARMASM_NO_NEON -DWOLFSSL_ARMASM_THUMB2
  endif
  ifeq ($(CORTEX_M33),1)
    CFLAGS+=-mcpu=cortex-m33 -DCORTEX_M33
    LDFLAGS+=-mcpu=cortex-m33
    ifeq ($(TZEN),1)
      ifneq (,$(findstring stm32,$(TARGET)))
        OBJS+=hal/stm32_tz.o
      endif
      CFLAGS+=-mcmse
      ifeq ($(WOLFCRYPT_TZ),1)
        CORTEXM_ARM_EXTRA_OBJS=
        CORTEXM_ARM_EXTRA_CFLAGS=
        SECURE_OBJS+=./src/wc_callable.o
        WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o
        CFLAGS+=-DWOLFCRYPT_SECURE_MODE
        SECURE_LDFLAGS+=-Wl,--cmse-implib -Wl,--out-implib=./src/wc_secure_calls.o
      endif
    endif # TZEN=1
      ifeq ($(SPMATH),1)
        ifeq ($(NO_ASM),1)
          MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
        else
          CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
          MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_cortexm.o
          CFLAGS+=$(CORTEXM_ARM_EXTRA_CFLAGS) -DWOLFSSL_ARM_ARCH=8
          OBJS+=$(CORTEXM_ARM_EXTRA_OBJS)
        endif
      endif
    else
    ifeq ($(CORTEX_M7),1)
      CFLAGS+=-mcpu=cortex-m7
      LDFLAGS+=-mcpu=cortex-m7
      ifeq ($(SPMATH),1)
        ifeq ($(NO_ASM),1)
          MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
        else
          CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
          MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_cortexm.o
          CFLAGS+=$(CORTEXM_ARM_EXTRA_CFLAGS) -DWOLFSSL_ARM_ARCH=7
          OBJS+=$(CORTEXM_ARM_EXTRA_OBJS)
        endif
       endif
    else
    ifeq ($(CORTEX_M0),1)
        CFLAGS+=-mcpu=cortex-m0
        LDFLAGS+=-mcpu=cortex-m0
        ifeq ($(SPMATH),1)
          ifeq ($(NO_ASM),1)
            MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
          else
            CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_THUMB_ASM
            MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_armthumb.o
            # No ARMASM support available for ARMv6-M.
          endif
        endif
      else
        ifeq ($(CORTEX_M3),1)
          CFLAGS+=-mcpu=cortex-m3
          LDFLAGS+=-mcpu=cortex-m3
          ifeq ($(NO_ASM),1)
            ifeq ($(SPMATH),1)
              MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
            endif
          else
            ifeq ($(SPMATH),1)
              CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM -DWOLFSSL_SP_NO_UMAAL
              MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_cortexm.o
              CFLAGS+=$(CORTEXM_ARM_EXTRA_CFLAGS) -DWOLFSSL_ARM_ARCH=7
              OBJS+=$(CORTEXM_ARM_EXTRA_OBJS)
            endif
          endif
      else
        # default Cortex M4
        CFLAGS+=-mcpu=cortex-m4
        LDFLAGS+=-mcpu=cortex-m4
        ifeq ($(NO_ASM),1)
          ifeq ($(SPMATH),1)
            MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
          endif
        else
          CFLAGS+=-fomit-frame-pointer # required with debug builds only
          ifeq ($(SPMATH),1)
            CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
            MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_cortexm.o
            CFLAGS+=$(CORTEXM_ARM_EXTRA_CFLAGS) -DWOLFSSL_ARM_ARCH=7
            OBJS+=$(CORTEXM_ARM_EXTRA_OBJS)
          endif
        endif
      endif
    endif
  endif
endif
endif
endif


## Renesas RX
ifeq ($(ARCH),RENESAS_RX)
  RX_GCC_PATH?=~/toolchains/gcc_8.3.0.202311_rx_elf
  CROSS_COMPILE?=$(RX_GCC_PATH)/bin/rx-elf-

  ## Toolchain setup
  ifeq ($(USE_GCC),0)
    CC=$(CROSS_COMPILE)gcc
    # Must use LD directly (gcc link calls LD with sysroot and is not supported)
    LD=$(CROSS_COMPILE)ld
    AS=$(CROSS_COMPILE)gcc
    AR=$(CROSS_COMPILE)ar
    OBJCOPY?=$(CROSS_COMPILE)objcopy
    SIZE=$(CROSS_COMPILE)size

    # Override flags
    USE_GCC_HEADLESS=0
    LD_START_GROUP=--start-group
    LD_END_GROUP=--end-group
    CFLAGS+=-Wall -Wextra -ffreestanding -Wno-unused -nostartfiles -fno-common
    CFLAGS+=-ffunction-sections -fdata-sections
    CFLAGS+=-B$(dir $(CROSS_COMPILE))
    LDFLAGS+=-gc-sections -Map=wolfboot.map
    LDFLAGS+=-T $(LSCRIPT) -L$(dir $(CROSS_COMPILE))../lib
    LIBS+=-lgcc
  endif

  # Renesas specific files
  OBJS+=src/boot_renesas.o src/boot_renesas_start.o hal/renesas-rx.o
  ifeq ($(SPMATH),1)
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
  endif

  # RX parts support big or little endian data depending on MDE register
  CFLAGS+=-fomit-frame-pointer -nofpu
  ifeq ($(BIG_ENDIAN),1)
    CFLAGS+=-mbig-endian-data
    ifeq ($(USE_GCC),1)
      LDFLAGS+=-Wl,--oformat=elf32-rx-be
      CFLAGS+=-misa=v2
    else
      LDFLAGS+=--oformat=elf32-rx-be
    endif
  else
    CFLAGS+=-mlittle-endian-data
    ifeq ($(USE_GCC),1)
      LDFLAGS+=-Wl,--oformat=elf32-rx-le
      CFLAGS+=-misa=v2
    else
      LDFLAGS+=--oformat=elf32-rx-le
    endif
  endif

  ifeq ($(PKA),1)
    CFLAGS+=-DWOLFBOOT_RENESAS_TSIP
    RX_DRIVER_PATH?=./lib

    OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/cryptocb.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/Renesas/renesas_common.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/Renesas/renesas_tsip_util.o \
          $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/Renesas/renesas_tsip_aes.o

    # RX TSIP uses pre-compiled .a library by default
    ifneq ($(RX_TSIP_SRC),1)
      ifeq ($(TARGET),rx65n)
        ifeq ($(BIG_ENDIAN),1)
          LIBS+=$(RX_DRIVER_PATH)/r_tsip_rx/lib/gcc/libr_tsip_rx65n_big.a
        else
          LIBS+=$(RX_DRIVER_PATH)/r_tsip_rx/lib/gcc/libr_tsip_rx65n_little.a
        endif
      endif
      ifeq ($(TARGET),rx72n)
        ifeq ($(BIG_ENDIAN),1)
          LIBS+=$(RX_DRIVER_PATH)/r_tsip_rx/lib/gcc/libr_tsip_rx72m_rx72n_rx66n_big.a
        else
          LIBS+=$(RX_DRIVER_PATH)/r_tsip_rx/lib/gcc/libr_tsip_rx72m_rx72n_rx66n_little.a
        endif
      endif
    else
      CFLAGS+=-DRX_TSIP_SRC
      ifeq ($(TARGET),rx65n)
        RX_TSIP_SRC_PATH?=$(RX_DRIVER_PATH)/r_tsip_rx/src/targets/rx65n
      endif
      ifeq ($(TARGET),rx72n)
        RX_TSIP_SRC_PATH?=$(RX_DRIVER_PATH)/r_tsip_rx/src/targets/rx72m_rx72n_rx66n
      endif
      # Use RX_TSIP_SRC if building TSIP sources directly
      OBJS+=$(RX_TSIP_SRC_PATH)/r_tsip_rx.o \
            $(RX_TSIP_SRC_PATH)/r_tsip_rx_private.o \
            $(RX_TSIP_SRC_PATH)/r_tsip_aes_rx.o \
            $(RX_TSIP_SRC_PATH)/r_tsip_hash_rx.o \
            $(RX_TSIP_SRC_PATH)/r_tsip_ecc_rx.o \
            $(RX_TSIP_SRC_PATH)/ip/s_flash.o \
            $(patsubst %.c,%.o,$(wildcard $(RX_TSIP_SRC_PATH)/ip/r_tsip_rx_p*.c)) \
            $(patsubst %.c,%.o,$(wildcard $(RX_TSIP_SRC_PATH)/ip/r_tsip_rx_subprc*.c)) \
            $(patsubst %.c,%.o,$(wildcard $(RX_TSIP_SRC_PATH)/ip/r_tsip_rx_function*.c))

      # Ignore sign compare warnings in TSIP RX driver
      CFLAGS+=-Wno-error=sign-compare
    endif

    OBJS+=$(RX_DRIVER_PATH)/r_bsp/mcu/all/r_bsp_cpu.o \
          $(RX_DRIVER_PATH)/r_bsp/mcu/all/r_bsp_interrupts.o \
          $(RX_DRIVER_PATH)/r_bsp/mcu/all/r_rx_intrinsic_functions.o
    ifeq ($(TARGET),rx65n)
      OBJS+=$(RX_DRIVER_PATH)/r_bsp/mcu/rx65n/mcu_interrupts.o
    endif
    ifeq ($(TARGET),rx72n)
      OBJS+=$(RX_DRIVER_PATH)/r_bsp/mcu/rx72n/mcu_interrupts.o
    endif

    CFLAGS+=-Ihal -I$(WOLFBOOT_LIB_WOLFSSL) \
            -I$(RX_DRIVER_PATH)/r_bsp \
            -I$(RX_DRIVER_PATH)/r_config \
            -I$(RX_DRIVER_PATH)/r_tsip_rx \
            -I$(RX_DRIVER_PATH)/r_tsip_rx/src
  endif
endif


## RISCV (32-bit)
ifeq ($(ARCH),RISCV)
  CROSS_COMPILE?=riscv32-unknown-elf-
  ARCH_FLAGS=-march=rv32imac -mabi=ilp32 -mcmodel=medany
  CFLAGS+=-fno-builtin-printf -DUSE_M_TIME -g -nostartfiles -DARCH_RISCV
  CFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=$(ARCH_FLAGS)
  MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o

  # Prune unused functions and data
  CFLAGS +=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  # Unified RISC-V boot code (32/64-bit via __riscv_xlen)
  OBJS+=src/boot_riscv_start.o src/boot_riscv.o src/vector_riscv.o
  ARCH_FLASH_OFFSET=0x20010000
endif

## RISCV64 (64-bit)
ifeq ($(ARCH),RISCV64)
  CROSS_COMPILE?=riscv64-unknown-elf-
  CFLAGS+=-DMMU -DWOLFBOOT_DUALBOOT

  # If SD card or eMMC is enabled use update_disk loader with GPT support
  ifneq ($(filter 1,$(DISK_SDCARD) $(DISK_EMMC)),)
    CFLAGS+=-DWOLFBOOT_UPDATE_DISK -DMAX_DISKS=1
    UPDATE_OBJS:=src/update_disk.o
    OBJS += src/gpt.o
    OBJS += src/disk.o
  else
    # Use RAM-based update path for non-memory-mapped flash (SC SPI)
    # Images are loaded into RAM before execution
    UPDATE_OBJS?=src/update_ram.o
  endif

  ARCH_FLAGS=-march=rv64imafd -mabi=lp64d -mcmodel=medany
  CFLAGS+=-fno-builtin-printf -DUSE_M_TIME -g -nostartfiles -DARCH_RISCV -DARCH_RISCV64
  CFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=$(ARCH_FLAGS)

  # Prune unused functions and data
  CFLAGS +=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  # Unified RISC-V boot code (32/64-bit via __riscv_xlen)
  OBJS+=src/boot_riscv_start.o src/boot_riscv.o src/vector_riscv.o

  CFLAGS+=-DWOLFBOOT_FDT
  OBJS+=src/fdt.o

  ifeq ($(SPMATH),1)
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c64.o
  endif

  ifneq ($(NO_ASM),1)
    CFLAGS+=-DWOLFSSL_RISCV_ASM
    WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/riscv/riscv-64-sha256.o \
                    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/riscv/riscv-64-sha512.o \
                    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/riscv/riscv-64-sha3.o \
                    $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/riscv/riscv-64-aes.o
  endif
endif

# powerpc
ifeq ($(ARCH),PPC)
  CROSS_COMPILE?=powerpc-linux-gnu-
  LDFLAGS+=-Wl,--build-id=none
  CFLAGS+=-DARCH_PPC -DFAST_MEMCPY -ffreestanding -fno-tree-loop-distribute-patterns

  ifeq ($(DEBUG_UART),0)
    CFLAGS+=-fno-builtin-printf
  endif

  # Target-specific CPU flags
  ifeq ($(TARGET),nxp_t2080)
    CFLAGS+=-mcpu=e6500 -mno-altivec -mbss-plt
  else ifeq ($(TARGET),nxp_t1024)
    CFLAGS+=-mcpu=e5500
  endif

  # Prune unused functions and data
  CFLAGS+=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  OBJS+=src/boot_ppc_start.o src/boot_ppc.o

  ifeq ($(SPMATH),1)
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
  endif

  ifneq ($(NO_ASM),1)
    # Use the SHA256 and SP math all assembly accelerations
    CFLAGS+=-DWOLFSSL_SP_PPC
    CFLAGS+=-DWOLFSSL_PPC32_ASM -DWOLFSSL_PPC32_ASM_INLINE
    #CFLAGS+=-DWOLFSSL_PPC32_ASM_SMALL
    MATH_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/ppc32/ppc32-sha256-asm_c.o
  endif
endif

ifeq ($(TARGET),kinetis)
  CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS) \
      -I$(MCUXPRESSO_DRIVERS)/drivers \
      -I$(MCUXPRESSO)/drivers \
      -I$(MCUXPRESSO)/drivers/common \
      -I$(MCUXPRESSO_CMSIS)/Include \
      -I$(MCUXPRESSO_CMSIS)/Core/Include
  CFLAGS+=\
      -DCPU_$(MCUXPRESSO_CPU) -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -DNVM_FLASH_WRITEONCE=1
  OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o

  ifeq ($(MCUXSDK),1)
    CFLAGS+=\
      -I$(MCUXPRESSO)/drivers/flash \
      -I$(MCUXPRESSO)/drivers/sysmpu \
      -I$(MCUXPRESSO)/drivers/ltc \
      -I$(MCUXPRESSO)/drivers/port \
      -I$(MCUXPRESSO)/drivers/gpio

    OBJS+=\
      $(MCUXPRESSO)/drivers/flash/fsl_ftfx_flash.o \
      $(MCUXPRESSO)/drivers/flash/fsl_ftfx_cache.o \
      $(MCUXPRESSO)/drivers/flash/fsl_ftfx_controller.o
  else
    OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_flash.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_cache.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_controller.o
  endif

  ## The following lines can be used to enable HW acceleration
  ifeq ($(MCUXPRESSO_CPU),MK82FN256VLL15)
    ifeq ($(PKA),1)
      PKA_EXTRA_CFLAGS+=-DFREESCALE_USE_LTC
      PKA_EXTRA_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/nxp/ksdk_port.o
      ifeq ($(MCUXSDK),1)
        PKA_EXTRA_OBJS+=$(MCUXPRESSO)/drivers/ltc/fsl_ltc.o
      else
        PKA_EXTRA_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_ltc.o
      endif
    endif
  endif
endif

ifeq ($(TARGET),mcxa)
  CORTEX_M33=1
  CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS) \
      -I$(MCUXPRESSO_DRIVERS)/drivers \
      -I$(MCUXPRESSO_DRIVERS)/drivers/common \
      -I$(MCUXPRESSO_DRIVERS)/drivers/romapi \
      -I$(MCUXPRESSO_DRIVERS)/../periph \
      -I$(MCUXPRESSO)/drivers \
      -I$(MCUXPRESSO)/drivers/common \
      -I$(MCUXPRESSO_CMSIS)/Include \
      -I$(MCUXPRESSO_CMSIS)/Core/Include \
      -I$(MCUXPRESSO)/drivers/flash \
      -I$(MCUXPRESSO)/drivers/mcx_spc \
      -I$(MCUXPRESSO)/drivers/sysmpu \
      -I$(MCUXPRESSO)/drivers/ltc \
      -I$(MCUXPRESSO)/drivers/port \
      -I$(MCUXPRESSO)/drivers/gpio
  CFLAGS+=-DCPU_$(MCUXPRESSO_CPU) -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  CFLAGS+=-DWOLFSSL_SP_NO_UMAAL
  CFLAGS+=-Wno-old-style-declaration
  CFLAGS+=-mcpu=cortex-m33 -DCORTEX_M33 -U__ARM_FEATURE_DSP
  LDFLAGS+=-mcpu=cortex-m33
  OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO)/drivers/mcx_spc/fsl_spc.o \
      $(MCUXPRESSO_PROJECT_TEMPLATE)/clock_config.o
endif

ifeq ($(TARGET),mcxw)
  CORTEX_M33=1
  ifneq ($(TZEN),1)
    LSCRIPT_IN=hal/$(TARGET)-ns.ld
  endif
  CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS) \
      -I$(MCUXPRESSO_DRIVERS)/drivers \
      -I$(MCUXPRESSO_DRIVERS)/drivers/romapi \
      -I$(MCUXPRESSO_DRIVERS)/../periph2 \
      -I$(MCUXPRESSO)/drivers \
      -I$(MCUXPRESSO)/drivers/flash_k4 \
      -I$(MCUXPRESSO)/drivers/ccm32k \
      -I$(MCUXPRESSO)/drivers/common \
      -I$(MCUXPRESSO_CMSIS)/Include \
      -I$(MCUXPRESSO_CMSIS)/Core/Include \
      -I$(MCUXPRESSO)/drivers/flash \
      -I$(MCUXPRESSO)/drivers/spc \
      -I$(MCUXPRESSO)/drivers/sysmpu \
      -I$(MCUXPRESSO)/drivers/ltc \
      -I$(MCUXPRESSO)/drivers/port \
      -I$(MCUXPRESSO)/drivers/gpio
  CFLAGS+=-DCPU_$(MCUXPRESSO_CPU) -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  CFLAGS+=-DWOLFSSL_SP_NO_UMAAL
  CFLAGS+=-Wno-old-style-declaration
  CFLAGS+=-mcpu=cortex-m33 -DCORTEX_M33 -U__ARM_FEATURE_DSP
  LDFLAGS+=-mcpu=cortex-m33
  OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO)/drivers/spc/fsl_spc.o \
      $(MCUXPRESSO_PROJECT_TEMPLATE)/clock_config.o \
      $(MCUXPRESSO)/drivers/ccm32k/fsl_ccm32k.o \
      $(MCUXPRESSO_DRIVERS)/drivers/romapi/fsl_romapi.o
endif

ifeq ($(TARGET),mcxn)
  CORTEX_M33=1
  ifneq ($(TZEN),1)
    LSCRIPT_IN=hal/$(TARGET)-ns.ld
  endif
  CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS) \
      -I$(MCUXPRESSO_DRIVERS)/drivers \
      -I$(MCUXPRESSO_DRIVERS)/drivers/romapi/flash \
      -I$(MCUXPRESSO_DRIVERS)/../periph \
      -I$(MCUXPRESSO) \
      -I$(MCUXPRESSO)/drivers \
      -I$(MCUXPRESSO)/drivers/gpio \
      -I$(MCUXPRESSO)/drivers/port \
      -I$(MCUXPRESSO)/drivers/common \
      -I$(MCUXPRESSO)/drivers/lpflexcomm \
      -I$(MCUXPRESSO)/drivers/lpuart \
      -I$(MCUXPRESSO_PROJECT_TEMPLATE) \
      -I$(MCUXPRESSO_CMSIS)/Include \
      -I$(MCUXPRESSO_CMSIS)/Core/Include
  CFLAGS+=-DCPU_$(MCUXPRESSO_CPU) -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  CFLAGS+=-Wno-old-style-declaration
  CFLAGS+=-mcpu=cortex-m33 -DCORTEX_M33 -U__ARM_FEATURE_DSP
  LDFLAGS+=-mcpu=cortex-m33
  OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO_PROJECT_TEMPLATE)/clock_config.o \
      $(MCUXPRESSO_DRIVERS)/drivers/romapi/flash/src/fsl_flash.o

  ifeq ($(DEBUG_UART),1)
    OBJS+=\
      $(MCUXPRESSO)/drivers/lpflexcomm/fsl_lpflexcomm.o \
      $(MCUXPRESSO)/drivers/lpuart/fsl_lpuart.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_reset.o
  endif
endif

ifeq ($(TARGET),nrf5340)
  ifneq ($(TZEN), 1)
    LSCRIPT_IN=hal/$(TARGET)-ns.ld
  endif
endif

ifeq ($(TARGET),nrf5340_net)
  # Net core doesn't support DSP and FP
  CFLAGS+=-mcpu=cortex-m33+nodsp+nofp
  LDFLAGS+=-mcpu=cortex-m33+nodsp+nofp
endif

ifeq ($(TARGET),imx_rt)
  CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS) \
      -I$(MCUXPRESSO_DRIVERS)/drivers \
      -I$(MCUXPRESSO)/drivers \
      -I$(MCUXPRESSO)/drivers/cache/armv7-m7 \
      -I$(MCUXPRESSO)/drivers/common \
      -I$(MCUXPRESSO)/drivers/flexspi \
      -I$(MCUXPRESSO)/drivers/lpuart \
      -I$(MCUXPRESSO)/drivers/igpio \
      -I$(MCUXPRESSO)/components/uart \
      -I$(MCUXPRESSO)/components/flash/nor \
      -I$(MCUXPRESSO)/components/flash/nor/flexspi \
      -I$(MCUXPRESSO)/components/serial_manager \
      -I$(MCUXPRESSO_DRIVERS)/project_template \
      -I$(MCUXPRESSO_CMSIS)/Include \
      -I$(MCUXPRESSO_CMSIS)/Core/Include
  CFLAGS+=\
      -DCPU_$(MCUXPRESSO_CPU) \
      -DDEBUG_CONSOLE_ASSERT_DISABLE=1 \
      -DXIP_EXTERNAL_FLASH=1 -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -DPRINTF_ADVANCED_ENABLE=1 \
      -DSCANF_ADVANCED_ENABLE=1 -DSERIAL_PORT_TYPE_UART=1 -DNDEBUG=1

  ifeq ($(MCUXSDK),1)
    CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS)/../periph \
      -I$(MCUXPRESSO)/components/debug_console \
      -I$(MCUXPRESSO)/components/debug_console/config \
      -I$(MCUXPRESSO)/components/lists \
      -I$(MCUXPRESSO)/components/str
    CFLAGS+=-DDCB=CoreDebug -DDCB_DEMCR_TRCENA_Msk=CoreDebug_DEMCR_TRCENA_Msk
    OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO)/drivers/flexspi/fsl_flexspi.o \
      $(MCUXPRESSO)/drivers/cache/armv7-m7/fsl_cache.o
    ifeq ($(DEBUG_UART),1)
      OBJS+= $(MCUXPRESSO)/drivers/lpuart/fsl_lpuart.o
    endif
  else
    CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS)/utilities/str \
      -I$(MCUXPRESSO_DRIVERS)/utilities/debug_console
    OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexspi.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_cache.o
    ifeq ($(DEBUG_UART),1)
      OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_lpuart.o
    endif
  endif

  ifeq ($(TARGET_IMX_HAB),1)
      LSCRIPT_IN:=hal/$(TARGET)_hab.ld
  else
      LSCRIPT_IN:=hal/$(TARGET).ld
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1064DVL6A)
    ARCH_FLASH_OFFSET=0x70000000
    ifeq ($(MCUXSDK),1)
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1064/xip/
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1064
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1064/project_template
    else
      CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1064/xip/
    endif
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1062DVL6A)
    ARCH_FLASH_OFFSET=0x60000000
    ifeq ($(MCUXSDK),1)
      # Use evkbmimxrt1060 because evkmimxrt1060 is not supported by the SDK
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/xip/
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/project_template
      CFLAGS+=-I$(MCUXPRESSO)/devices/RT/RT1050/MIMXRT1052
    else
      CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1060/xip/
    endif
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1062DVL6B)
    ARCH_FLASH_OFFSET=0x60000000
    ifeq ($(MCUXSDK),1)
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/xip/
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/project_template
      CFLAGS+=-I$(MCUXPRESSO)/devices/RT/RT1050/MIMXRT1052
    else
      CFLAGS+=-I$(MCUXPRESSO)/boards/evkbmimxrt1060/xip/
    endif
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1061CVJ5B)
    ARCH_FLASH_OFFSET=0x60000000
    ifeq ($(MCUXSDK),1)
      # Use evkbmimxrt1060 because evkmimxrt1060 is not supported by the SDK
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/xip/
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/project_template
      CFLAGS+=-I$(MCUXPRESSO)/devices/RT/RT1050/MIMXRT1052
    else
      CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1060/xip/
    endif
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1052DVJ6B)
    ARCH_FLASH_OFFSET=0x60000000
    ifeq ($(MCUXSDK),1)
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbimxrt1050/xip/
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbimxrt1050
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbimxrt1050/project_template
    else
      CFLAGS+=-I$(MCUXPRESSO)/boards/evkbimxrt1050/xip/
    endif
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1042XJM5B)
    ARCH_FLASH_OFFSET=0x60000000
    ifeq ($(MCUXSDK),1)
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1040/xip/
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1040
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1040/project_template
      CFLAGS+=-I$(MCUXPRESSO)/devices/RT/RT1050/MIMXRT1052
    else
      CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1040/xip/
    endif
  endif

  ifeq ($(PKA),1)
    ifeq ($(MCUXSDK),1)
      PKA_EXTRA_OBJS+= $(MCUXPRESSO)/drivers/dcp/fsl_dcp.o
    else
      PKA_EXTRA_OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_dcp.o
    endif
    PKA_EXTRA_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/nxp/dcp_port.o
    PKA_EXTRA_CFLAGS+=\
        -DWOLFSSL_IMXRT_DCP \
        -I$(MCUXPRESSO)/drivers/cache/armv7-m7 \
        -I$(MCUXPRESSO)/drivers/dcp
  endif
endif

# ARM Big Endian
ifeq ($(ARCH),ARM_BE)
  OBJS+=src/boot_arm.o
  ifeq ($(SPMATH),1)
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
  endif
endif

ifeq ($(TARGET),nxp_t1024)
  # Power PC big endian
  ARCH_FLAGS=-mhard-float -mcpu=e5500
  CFLAGS+=$(ARCH_FLAGS)
  BIG_ENDIAN=1
  CFLAGS+=-DMMU -DWOLFBOOT_FDT -DWOLFBOOT_DUALBOOT
  CFLAGS+=-pipe # use pipes instead of temp files
  CFLAGS+=-feliminate-unused-debug-types
  LDFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=-Wl,--hash-style=both # generate both sysv and gnu symbol hash table
  LDFLAGS+=-Wl,--as-needed # remove weak functions not used
  OBJS+=src/boot_ppc_mp.o # support for spin table
  OBJS+=src/fdt.o
  OBJS+=src/pci.o
  CFLAGS+=-DWOLFBOOT_USE_PCI
  UPDATE_OBJS:=src/update_ram.o

  SPI_TARGET=nxp
  OPTIMIZATION_LEVEL=0 # using default -Os causes issues with alignment
endif

ifeq ($(TARGET),nxp_t2080)
  # Power PC big endian
  ARCH_FLAGS=-mhard-float -mcpu=e6500
  CFLAGS+=$(ARCH_FLAGS)
  BIG_ENDIAN=1
  CFLAGS+=-DMMU -DWOLFBOOT_FDT -DWOLFBOOT_DUALBOOT
  CFLAGS+=-pipe # use pipes instead of temp files
  CFLAGS+=-feliminate-unused-debug-types
  LDFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=-Wl,--hash-style=both # generate both sysv and gnu symbol hash table
  LDFLAGS+=-Wl,--as-needed # remove weak functions not used
  OBJS+=src/boot_ppc_mp.o # support for spin table
  UPDATE_OBJS:=src/update_ram.o
  OBJS+=src/fdt.o
endif

ifeq ($(TARGET),nxp_p1021)
  # Power PC big endian
  ARCH_FLAGS=-m32 -mhard-float -mcpu=e500mc
  ARCH_FLAGS+=-fno-builtin -ffreestanding -nostartfiles
  CFLAGS+=$(ARCH_FLAGS)
  BIG_ENDIAN=1
  CFLAGS+=-DWOLFBOOT_DUALBOOT
  CFLAGS+=-pipe # use pipes instead of temp files
  LDFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=-Wl,--as-needed # remove weak functions not used
  OBJS+=src/boot_ppc_mp.o # support for spin table
  UPDATE_OBJS:=src/update_ram.o

  # Use PPC stdlib for memcpy, etc.
  #CFLAGS+=-DWOLFBOOT_USE_STDLIBC

  SPI_TARGET=nxp
endif

ifeq ($(TARGET),ti_hercules)
  # HALCoGen Source and Include?
  CORTEX_R5=1
  CFLAGS+=-D"CORTEX_R5" -D"NVM_FLASH_WRITEONCE" -D"FLASHBUFFER_SIZE=32"
  BIG_ENDIAN=1
  STACK_USAGE=0
  USE_GCC=0
  USE_GCC_HEADLESS=0

  ifeq ($(CCS_ROOT),)
    $(error "CCS_ROOT must be defined to root of tools")
  endif
  CROSS_COMPILE?=$(CCS_ROOT)/bin/

  CC=$(CROSS_COMPILE)armcl
  LD=$(CROSS_COMPILE)armcl
  AS=$(CROSS_COMPILE)armasm
  AR=$(CROSS_COMPILE)armcl -ar
  OBJCOPY=$(CROSS_COMPILE)armobjcopy
  SIZE=$(CROSS_COMPILE)armsize
  OUTPUT_FLAG=--output_file

  F021_DIR?=c:\\ti\\Hercules\\F021\ Flash\ API\\02.01.01

  ARCH_FLAGS=-mv7R5 --code_state=32 --float_support=VFPv3D16 --enum_type=packed --abi=eabi -I"$(CCS_ROOT)/include" -I"$(F021_DIR)/include"
  CFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=$(ARCH_FLAGS) -i"$(CCS_ROOT)/lib" -i"$(F021_DIR)" -z --be32 --map_file=wolfboot.map --reread_libs --diag_wrap=off --display_error_number --warn_sections --heap_size=0 --stack_size=0x800 --ram_model
  LD_START_GROUP= #--start-group
  LD_END_GROUP= -llibc.a -l"$(F021_DIR)/F021_API_CortexR4_BE_L2FMC_V3D16.lib"

  OPTIMIZATION_LEVEL=2
endif

ifeq ($(TARGET),lpc)
  ifeq ($(MCUXSDK),1)
    # Some targets in the SDK use drivers from a different target
    MCUXPRESSO_DRIVERS_SHARED?=$(MCUXPRESSO_DRIVERS)
    ifneq (,$(filter LPC54628%,$(MCUXPRESSO_CPU)))
      MCUXPRESSO_DRIVERS_SHARED=$(MCUXPRESSO)/devices/LPC/LPC54000/LPC54628
    else ifneq (,$(filter LPC54605% LPC54606% LPC54607% LPC54608% LPC54616% LPC54618%,$(MCUXPRESSO_CPU)))
      MCUXPRESSO_DRIVERS_SHARED=$(MCUXPRESSO)/devices/LPC/LPC54000/LPC54608
    else ifneq (,$(filter LPC54018M% LPC54S018M%,$(MCUXPRESSO_CPU)))
      MCUXPRESSO_DRIVERS_SHARED=$(MCUXPRESSO)/devices/LPC/LPC54000/LPC54S018M
    else ifneq (,$(filter LPC54005% LPC54016% LPC54018% LPC54S005% LPC54S016% LPC54S018%,$(MCUXPRESSO_CPU)))
      MCUXPRESSO_DRIVERS_SHARED=$(MCUXPRESSO)/devices/LPC/LPC54000/LPC54S018
    endif
    CFLAGS+=\
        -I$(MCUXPRESSO_DRIVERS) \
        -I$(MCUXPRESSO_DRIVERS_SHARED)/drivers \
        -I$(MCUXPRESSO_DRIVERS)/../periph \
        -I$(MCUXPRESSO)/drivers \
        -I$(MCUXPRESSO)/drivers/common \
        -I$(MCUXPRESSO)/drivers/flashiap \
        -I$(MCUXPRESSO_CMSIS)/Include \
        -I$(MCUXPRESSO_CMSIS)/Core/Include
    CFLAGS+=\
        -DCPU_$(MCUXPRESSO_CPU) -DDEBUG_CONSOLE_ASSERT_DISABLE=1
    CFLAGS+=-DDCB=CoreDebug -DDCB_DEMCR_TRCENA_Msk=CoreDebug_DEMCR_TRCENA_Msk
    OBJS+=\
        $(MCUXPRESSO_DRIVERS_SHARED)/drivers/fsl_clock.o \
        $(MCUXPRESSO_DRIVERS_SHARED)/drivers/fsl_power.o \
        $(MCUXPRESSO_DRIVERS_SHARED)/drivers/fsl_reset.o
    CFLAGS+=\
      -I$(MCUXPRESSO)/drivers/flashiap \
      -I$(MCUXPRESSO)/drivers/flexcomm
    OBJS+=\
      $(MCUXPRESSO)/drivers/common/fsl_common.o \
      $(MCUXPRESSO)/drivers/common/fsl_common_arm.o \
      $(MCUXPRESSO)/drivers/flashiap/fsl_flashiap.o \
      $(MCUXPRESSO)/drivers/flexcomm/usart/fsl_usart.o \
      $(MCUXPRESSO)/drivers/flexcomm/fsl_flexcomm.o
  else
    CFLAGS+=\
        -I$(MCUXPRESSO_DRIVERS) \
        -I$(MCUXPRESSO_DRIVERS)/drivers \
        -I$(MCUXPRESSO)/drivers \
        -I$(MCUXPRESSO)/drivers/common \
        -I$(MCUXPRESSO_CMSIS)/Include \
        -I$(MCUXPRESSO_CMSIS)/Core/Include
    CFLAGS+=\
        -DCPU_$(MCUXPRESSO_CPU) -DDEBUG_CONSOLE_ASSERT_DISABLE=1
    OBJS+=\
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_power.o \
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_reset.o
    LIBS+=\
        $(MCUXPRESSO_DRIVERS)/mcuxpresso/libpower_softabi.a
    OBJS+=\
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_common.o \
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_flashiap.o \
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_usart.o \
        $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexcomm.o
  endif
endif

ifeq ($(TARGET),psoc6)
    CORTEX_M0=1
    OBJS+=\
        $(CYPRESS_PDL)/drivers/source/cy_flash.o \
        $(CYPRESS_PDL)/drivers/source/cy_ipc_pipe.o \
        $(CYPRESS_PDL)/drivers/source/cy_ipc_sema.o \
        $(CYPRESS_PDL)/drivers/source/cy_ipc_drv.o \
        $(CYPRESS_PDL)/drivers/source/cy_device.o \
        $(CYPRESS_PDL)/drivers/source/cy_sysclk.o \
        $(CYPRESS_PDL)/drivers/source/cy_sysint.o \
        $(CYPRESS_PDL)/drivers/source/cy_syslib.o \
        $(CYPRESS_PDL)/drivers/source/cy_ble_clk.o \
        $(CYPRESS_PDL)/drivers/source/cy_wdt.o \
        $(CYPRESS_PDL)/drivers/source/TOOLCHAIN_GCC_ARM/cy_syslib_gcc.o \
        $(CYPRESS_PDL)/devices/templates/COMPONENT_MTB/COMPONENT_CM0P/system_psoc6_cm0plus.o
    PSOC6_CRYPTO_OBJS=\
        $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/cypress/psoc6_crypto.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_vu.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_ecc_domain_params.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_ecc_nist_p.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_ecc_ecdsa.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_sha_v2.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_sha_v1.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_mem_v2.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_mem_v1.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_hw.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto_core_hw_v1.o \
        $(CYPRESS_PDL)/drivers/source/cy_crypto.o

    CFLAGS+=\
        -I$(CYPRESS_PDL)/devices/include \
        -I$(CYPRESS_PDL)/drivers/include \
        -I$(CYPRESS_PDL)/cmsis/include \
        -I$(CYPRESS_TARGET_LIB) \
        -I$(CYPRESS_CORE_LIB)/include \
        -I$(CYPRESS_PDL)/devices/include/ip \
        -I$(CYPRESS_PDL)/devices/templates/COMPONENT_MTB \
        -DCY8C624ABZI_D44
    ARCH_FLASH_OFFSET=0x10000000
    ifneq ($(PSOC6_CRYPTO),0)
        CFLAGS+=-DWOLFSSL_PSOC6_CRYPTO
        OBJS+=$(PSOC6_CRYPTO_OBJS)
    endif
endif

ifeq ($(USE_GCC),1)
  ## Toolchain setup
  CC=$(CROSS_COMPILE)gcc
  LD=$(CROSS_COMPILE)gcc
  AS=$(CROSS_COMPILE)gcc
  AR=$(CROSS_COMPILE)ar
  OBJCOPY?=$(CROSS_COMPILE)objcopy
  SIZE=$(CROSS_COMPILE)size
endif
OUTPUT_FLAG?=-o

ifeq ($(filter $(TARGET),x86_fsp_qemu kontron_vx3060_s2),$(TARGET))
  FSP=1
  CFLAGS+=-ffunction-sections -fdata-sections -ffreestanding -nostdlib -static
  # some std libc have headers that bring in extra symbols used in
  # FORTIFY_SOURCE realated checks. Use -U_FORTIFY_SOURCE to avoid that.
  CFLAGS+=-U_FORTIFY_SOURCE
  ifeq ($(TARGET), kontron_vx3060_s2)
    FSP_TGL=1
    CFLAGS+=-DWOLFBOOT_TGL=1
  endif
endif

ifeq ($(TARGET),x86_fsp_qemu)
    OBJS+=src/x86/qemu_fsp.o
endif

# x86-64 FSP targets
ifeq ("${FSP}", "1")
  CFLAGS+=-DWOLFBOOT_FSP=1
  USE_GCC_HEADLESS=0
  LD_START_GROUP =
  LD_END_GROUP =
  LD := ld
  # load to address in RAM after wolfBoot (aligned to 16 bytes)
  CFLAGS+=-DWOLFBOOT_NO_LOAD_ADDRESS
  ifeq ($(filter-out $(STAGE1),1),)
    # building stage1
    ifeq ($(FSP_TGL), 1)
      LSCRIPT_IN = ../hal/x86_fsp_tgl_stage1.ld
    else
      LSCRIPT_IN = ../hal/$(TARGET)_stage1.ld
    endif
    # using ../wolfboot.map as stage1 is built from stage1 sub-directory
    LDFLAGS = --defsym main=0x`nm ../wolfboot.elf | grep -w main | awk '{print $$1}'` \
              --defsym wb_start_bss=0x`nm ../wolfboot.elf | grep -w _start_bss | awk '{print $$1}'` \
              --defsym wb_end_bss=0x`nm ../wolfboot.elf | grep -w _end_bss | awk '{print $$1}'` \
              --defsym _stage2_params=0x`nm ../wolfboot.elf | grep -w _stage2_params | awk '{print $$1}'`
    LDFLAGS +=  --gc-sections --entry=reset_vector -T $(LSCRIPT) -m elf_i386  -Map=loader_stage1.map
    OBJS += src/boot_x86_fsp.o
    OBJS += src/boot_x86_fsp_start.o
    OBJS += src/fsp_m.o
    OBJS += src/fsp_t.o
    OBJS += src/wolfboot_raw.o
    OBJS += src/x86/common.o
    OBJS += src/x86/hob.o
    OBJS += src/pci.o
    CFLAGS+=-DWOLFBOOT_USE_PCI
    OBJS += hal/x86_uart.o
    OBJS += src/string.o
    OBJS += src/stage2_params.o
    OBJS += src/x86/fsp.o
    ifeq ($(filter-out $(STAGE1_AUTH),1),)
      OBJS += src/libwolfboot.o
      OBJS += src/image.o
      OBJS += src/keystore.o
      OBJS += src/sig_wolfboot_raw.o
      ifeq ($(TARGET), kontron_vx3060_s2)
        OBJS += hal/kontron_vx3060_s2_loader.o
      endif
      OBJS += $(WOLFCRYPT_OBJS)
      CFLAGS+=-DSTAGE1_AUTH
    endif

    CFLAGS += -fno-stack-protector -m32 -fno-PIC -fno-pie -mno-mmx -mno-sse -DDEBUG_UART
    CFLAGS += -DFSP_M_BASE=$(FSP_M_BASE)
    ifeq ($(FSP_TGL), 1)
      OBJS+=src/x86/tgl_fsp.o
      OBJS+=src/ucode0.o
      CFLAGS += -DUCODE0_ADDRESS=$(UCODE0_BASE)
    endif
    ifeq ($(TARGET),x86_fsp_qemu)
      OBJS += hal/x86_fsp_qemu_loader.o
    endif
  else
    # building wolfBoot
    ifeq ($(FSP_TGL), 1)
      LSCRIPT_IN = hal/x86_fsp_tgl.ld.in
    else
      LSCRIPT_IN = hal/$(TARGET).ld.in
    endif
    LDFLAGS = --gc-sections --entry=main  -T $(LSCRIPT) -Map=wolfboot.map
    CFLAGS += -fno-stack-protector -fno-PIC -fno-pie -mno-mmx -mno-sse -Os -DDEBUG_UART
    CFLAGS += -DFSP_M_BASE=$(FSP_M_BASE)
    OBJS += hal/x86_fsp_tgl.o
    OBJS += hal/x86_uart.o
    OBJS += src/boot_x86_fsp_payload.o
    OBJS += src/x86/common.o
    OBJS += src/x86/hob.o
    OBJS += src/pci.o
    CFLAGS+=-DWOLFBOOT_USE_PCI
    OBJS += src/x86/ahci.o
    OBJS += src/x86/ata.o
    OBJS += src/gpt.o
    OBJS += src/disk.o
    OBJS += src/x86/mptable.o
    OBJS += src/stage2_params.o
    OBJS += src/x86/exceptions.o
    OBJS += src/x86/gdt.o
    OBJS += src/x86/fsp.o
    OBJS += src/x86/fsp_s.o
    UPDATE_OBJS := src/update_disk.o
    CFLAGS+=-DWOLFBOOT_UPDATE_DISK
    ifeq ($(64BIT),1)
      LDFLAGS += -m elf_x86_64 --oformat elf64-x86-64
      CFLAGS += -m64
    else
      CFLAGS += -m32
      LDFLAGS += -m elf_i386 --oformat elf32-i386
    endif
    ifeq ($(FSP_TGL), 1)
      OBJS+=src/x86/tgl_fsp.o
    endif
  endif
  ifeq ($(64BIT),1)
    OBJS += src/x86/paging.o
  endif
endif

ifneq ($(CROSS_COMPILE_PATH),)
  # optional path for cross compiler includes
  CFLAGS+=-I$(CROSS_COMPILE_PATH)/usr/include --sysroot=$(CROSS_COMPILE_PATH)
  LDFLAGS+=-L$(CROSS_COMPILE_PATH)/usr/lib --sysroot=$(CROSS_COMPILE_PATH)
endif


ifeq ($(TARGET),x86_64_efi)
  USE_GCC_HEADLESS=0
  GNU_EFI_LIB_PATH?=/usr/lib
  GNU_EFI_CRT0=$(GNU_EFI_LIB_PATH)/crt0-efi-x86_64.o
  GNU_EFI_LSCRIPT=$(GNU_EFI_LIB_PATH)/elf_x86_64_efi.lds
  CFLAGS += -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
            -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
  CFLAGS += -I/usr/include/efi -I/usr/include/efi/x86_64 \
            -DTARGET_X86_64_EFI  -DWOLFBOOT_DUALBOOT
  # avoid using of fixed LOAD_ADDRESS, uefi target uses dynamic location
  CFLAGS += -DWOLFBOOT_NO_LOAD_ADDRESS
  LDFLAGS = -shared -Bsymbolic -L/usr/lib -T$(GNU_EFI_LSCRIPT)
  LD_START_GROUP = $(GNU_EFI_CRT0)
  LD_END_GROUP = -lgnuefi -lefi
  LD = ld
  UPDATE_OBJS:=src/update_ram.o
endif

ifeq ($(ARCH),sim)
  USE_GCC_HEADLESS=0
  LD = gcc
  ifneq ($(TARGET),library)
  ifneq ($(TARGET),library_fs)
    UPDATE_OBJS:=src/update_flash.o
  endif
  endif
  ifeq ($(TARGET),library_fs)
    UPDATE_OBJS += hal/filesystem.o
  endif
  LD_START_GROUP=
  LD_END_GROUP=
  BOOT_IMG=test-app/image.elf
  CFLAGS+=-DARCH_SIM
  ifneq ($(ELF_FLASH_SCATTER),1)
    CFLAGS+=-DWOLFBOOT_USE_STDLIBC
  endif
  ifeq ($(FORCE_32BIT),1)
    CFLAGS+=-m32
    LDFLAGS+=-m32
  endif
  ifeq ($(SPMATH),1)
    MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
    CFLAGS+=-DWOLFSSL_SP_DIV_WORD_HALF
  endif
  ifeq ($(WOLFHSM_CLIENT),1)
    WOLFHSM_OBJS += $(WOLFBOOT_LIB_WOLFHSM)/port/posix/posix_transport_tcp.o
  endif
  ifeq ($(WOLFHSM_SERVER),1)
    WOLFHSM_OBJS += $(WOLFBOOT_LIB_WOLFHSM)/port/posix/posix_flash_file.o \
                    $(WOLFBOOT_LIB_WOLFHSM)/src/wh_transport_mem.o

  endif
  # wolfHSM NVM image generation support for simulator
  # User must provide NVM_CONFIG for their specific setup
  ifneq ($(filter 1,$(WOLFHSM_CLIENT) $(WOLFHSM_SERVER)),)
    WH_NVM_BIN ?= whNvmImage.bin
    WH_NVM_HEX ?= whNvmImage.hex
    WH_NVM_PART_SIZE ?= 16384 # must match partition size in hal/sim.c
    WH_NVM_BASE_ADDRESS ?= 0x0

    CFLAGS += -DWOLFHSM_CFG_NO_SYS_TIME
  endif
endif

# Infineon AURIX Tricore
ifeq ($(ARCH), AURIX_TC3)
  # TC3xx specific
  ifeq ($(TARGET), aurix_tc3xx)
    USE_GCC?=1
    ARCH_FLASH_OFFSET=0x00000000

    CFLAGS += -I$(TC3_DIR) -Ihal

    CFLAGS += -Werror
    CFLAGS += -Wall -Wdiv-by-zero -Warray-bounds -Wformat -Wformat-security \
              -Wignored-qualifiers -Wno-implicit-function-declaration \
              -Wno-type-limits -Wno-unused-variable -Wno-unused-parameter \
              -Wno-missing-braces -fno-common -pipe \
              -ffunction-sections -fdata-sections -fmessage-length=0 \
              -std=gnu99 -DPART_BOOT_EXT -DPART_UPDATE_EXT -DPART_SWAP_EXT \
              -DHAVE_TC3XX -DWOLFBOOT_LOADER_MAIN


    # Makefile shennanigans for "if (WOLFHSM_CLIENT==1 || WOLFHSM_SERVER==1)"
    ifneq ($(filter 1,$(WOLFHSM_CLIENT) $(WOLFHSM_SERVER)),)
      # Common wolfHSM port files
      CFLAGS += -I$(WOLFHSM_INFINEON_TC3XX)/port -DWOLFHSM_CFG_DMA \
                -DWOLFHSM_CFG_NO_SYS_TIME
      OBJS += $(WOLFHSM_INFINEON_TC3XX)/port/tchsm_common.o \
              $(WOLFHSM_INFINEON_TC3XX)/port/tchsm_hsmhost.o
      # General wolfHSM files
      OBJS += $(WOLFBOOT_LIB_WOLFHSM)/src/wh_transport_mem.o

      # NVM image generation variables
      WH_NVM_BIN ?= whNvmImage.bin
      WH_NVM_HEX ?= whNvmImage.hex
      WH_NVM_PART_SIZE ?= 0x8000
      # Default to base of HSM DFLASH1
      WH_NVM_BASE_ADDRESS ?= 0xAFC00000

      # Select config file based on certificate chain verification
      # Use ?= to allow user override via command line (e.g., for offline cert chain)
      ifneq ($(CERT_CHAIN_VERIFY),)
        NVM_CONFIG ?= tools/scripts/tc3xx/wolfBoot-wolfHSM-dummy-certchain.nvminit
      else
        NVM_CONFIG ?= tools/scripts/tc3xx/wolfBoot-wolfHSM-keys.nvminit
      endif
    endif

    # Set BOOT_IMG to the ELF file instead of default bin when ELF_FLASH_SCATTER is enabled
    ifeq ($(ELF_FLASH_SCATTER),1)
      BOOT_IMG=test-app/image.elf
    endif

    ifeq ($(AURIX_TC3_HSM),1)
      # HSM compiler flags, build options, source code, etc
      ifeq ($(USE_GCC),1)
        # Just arm-none-eabi-gcc for now
        CROSS_COMPILE?=arm-none-eabi-
      else
      endif

      # Compiler flags
      ifeq ($(NO_ASM),1)
        ifeq ($(SPMATH),1)
          MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o
        endif
      else
        ifeq ($(SPMATH),1)
          CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM -DWOLFSSL_SP_NO_UMAAL
          MATH_OBJS += $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_cortexm.o
          CFLAGS+=$(CORTEXM_ARM_EXTRA_CFLAGS) -DWOLFSSL_ARM_ARCH=7
        endif
      endif

      CFLAGS += -march=armv7-m -mcpu=cortex-m3 -mthumb -mlittle-endian \
                -fno-builtin -DWOLFBOOT_AURIX_TC3XX_HSM

      # Temporary fix masking wolfCrypt unused function warning with RSA_LOW_MEM
      CFLAGS += -Wno-unused-function

      LDFLAGS += -march=armv7-m -mcpu=cortex-m3 -mthumb -mlittle-endian -g \
                --specs=nano.specs -Wl,--gc-sections -static -Wl,--cref -Wl,-n \
                -ffunction-sections -fdata-sections \
                -nostartfiles \
                -Wl,-Map="wolfboot.map" \
                -Wl,-L$(TC3_DIR)/tc3

      LSCRIPT_IN=hal/$(TARGET)_hsm.ld

      # wolfHSM port server-specific files
      ifeq ($(WOLFHSM_SERVER),1)
        USE_GCC_HEADLESS=0

        CFLAGS += -I$(WOLFHSM_INFINEON_TC3XX)/port/server

        OBJS += $(WOLFHSM_INFINEON_TC3XX)/port/server/port_halflash_df1.o \
          $(WOLFHSM_INFINEON_TC3XX)/port/server/io.o \
          $(WOLFHSM_INFINEON_TC3XX)/port/server/sysmem.o \
          $(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_hh_hsm.o \
          $(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_utils.o

        # SW only for now, as we dont have the right protection macros
        #$(WOLFHSM_INFINEON_TC3XX)/port/server/ccb_hsm.o \
        #$(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_hash.o \
        #$(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_aes.o \
        #$(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_cmac.o \
        #$(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_pk.o \
        #$(WOLFHSM_INFINEON_TC3XX)/port/server/tchsm_trng.o
      endif

      # HSM BSP specific object files
      OBJS += $(TC3_DIR)/src/tc3_clock.o \
              $(TC3_DIR)/src/tc3_flash.o \
              $(TC3_DIR)/src/tc3_gpio.o \
              $(TC3_DIR)/src/tc3_uart.o \
              $(TC3_DIR)/src/tc3.o \
              $(TC3_DIR)/src/tc3arm.o \
              $(TC3_DIR)/src/tc3arm_crt.o \
              $(TC3_DIR)/../tc3arm_bootloader/tc3arm_bootloader.o

    else
      # Tricore compiler settings
      ifeq ($(USE_GCC),1)
        HT_ROOT?=/opt/hightec/gnutri_v4.9.4.1-11fcedf-lin64
        CROSS_COMPILE?=$(HT_ROOT)/bin/tricore-
      else
        HT_ROOT?=~/HighTec/toolchains/tricore/v9.1.2
        CROSS_COMPILE?=$(HT_ROOT)/bin
        CC=$(CROSS_COMPILE)/clang
        LD=$(CROSS_COMPILE)/clang
        AS=$(CROSS_COMPILE)/clang
        AR=$(CROSS_COMPILE)/llvm-ar
        OBJCOPY=tricore-objcopy
        SIZE=$(CROSS_COMPILE)/llvm-size
      endif

      # No asm for you!
      MATH_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o

      # Arch settings for tricore
      ifeq ($(USE_GCC),1)
        CFLAGS+= -fshort-double -mtc162 -fstrict-volatile-bitfields -fno-builtin \
                 -fno-strict-aliasing
      else
        CFLAGS+= --target=tricore -march=tc162
      endif

      DEBUG_AFLAGS= -Wa,--gdwarf-2

      # Linker flags
      ifeq ($(USE_GCC),1)
        LDFLAGS+= -fshort-double -mtc162 -nostartfiles -Wl,--extmap="a"
      else
        LDFLAGS+= --target=tricore -march=tc162 -Wl,--entry=tc3tc_start
      endif

      LDFLAGS+= -Wl,--gc-sections -Wl,--cref -Wl,-n \
                -ffunction-sections -fdata-sections \
                -Wl,-Map="wolfboot.map" \
                -Wl,-L$(TC3_DIR)/tc3

      # Tricore BSP layer (replace with only tricore specific stuff)
      OBJS += $(TC3_DIR)/src/tc3_clock.o \
              $(TC3_DIR)/src/tc3_flash.o \
              $(TC3_DIR)/src/tc3_gpio.o \
              $(TC3_DIR)/src/tc3_uart.o \
              $(TC3_DIR)/src/tc3.o \
              $(TC3_DIR)/src/tc3tc_isr.o \
              $(TC3_DIR)/src/tc3tc_traps.o \
              $(TC3_DIR)/src/tc3tc.o \
              $(TC3_DIR)/src/tc3tc_crt.o \
              $(TC3_DIR)/../tc3tc_bootloader/tc3tc_bootloader.o

      ifeq ($(WOLFHSM_CLIENT),1)
        CFLAGS += -I$(WOLFHSM_INFINEON_TC3XX)/port/client
        OBJS += $(WOLFHSM_INFINEON_TC3XX)/port/client/hsm_ipc.o \
                $(WOLFHSM_INFINEON_TC3XX)/port/client/io.o \
                $(WOLFHSM_INFINEON_TC3XX)/port/client/tchsm_hh_host.o
      endif

    endif # !AURIX_TC3_HSM
  endif

  # TC4xx specific
  ifeq ($(TARGET), aurix_tc4xx)
    # Coming soon ;-)
  endif
endif

CFLAGS+=-DARCH_FLASH_OFFSET=$(ARCH_FLASH_OFFSET)
BOOT_IMG?=test-app/image.bin

# When ELF loading is enabled, sign the ELF file (not the flat binary)
ifeq ($(ELF),1)
  BOOT_IMG=test-app/image.elf
endif

## Update mechanism
ifeq ($(ARCH),AARCH64)
  CFLAGS+=-DMMU -DWOLFBOOT_FDT -DWOLFBOOT_DUALBOOT
  OBJS+=src/fdt.o
  ifneq ($(filter 1,$(DISK_SDCARD) $(DISK_EMMC)),)
    # Disk-based boot (SD card or eMMC)
    CFLAGS+=-DWOLFBOOT_UPDATE_DISK
    ifeq ($(MAX_DISKS),)
      MAX_DISKS=1
    endif
    CFLAGS+=-DMAX_DISKS=$(MAX_DISKS)
    UPDATE_OBJS:=src/update_disk.o
    OBJS+=src/gpt.o
    OBJS+=src/disk.o
  else
    # RAM-based boot from external flash (default)
    UPDATE_OBJS:=src/update_ram.o
  endif
else
  ifeq ($(DUALBANK_SWAP),1)
    CFLAGS+=-DWOLFBOOT_DUALBOOT
    UPDATE_OBJS:=src/update_flash_hwswap.o
  endif
endif

## For library target disable partitions
ifeq ($(TARGET),library)
  WOLFBOOT_NO_PARTITIONS=1
  NO_LOADER=1
endif

ifeq ($(TARGET),library_fs)
  EXT_FLASH=1
  # Force all partitions to be marked as external
  NO_XIP=1
  NO_SWAP_EXT=
  NO_LOADER=1
  USE_GCC_HEADLESS=0
  CFLAGS+=-DWOLFBOOT_USE_STDLIBC
endif


## Set default update object
ifneq ($(WOLFBOOT_NO_PARTITIONS),1)
  ifeq ($(UPDATE_OBJS),)
    UPDATE_OBJS:=./src/update_flash.o
  endif
endif

## wolfBoot origin
ifeq ($(WOLFBOOT_ORIGIN),)
  WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)
endif
CFLAGS+=-DWOLFBOOT_ORIGIN=$(WOLFBOOT_ORIGIN)
CFLAGS+=-DBOOTLOADER_PARTITION_SIZE=$(BOOTLOADER_PARTITION_SIZE)

## Debug
WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/logging.o

# Debug UART
ifeq ($(DEBUG_UART),1)
  CFLAGS+=-DDEBUG_UART

  # If this target has a UART driver, add it to the OBJS
  ifneq (,$(wildcard hal/uart/uart_drv_$(TARGET).c))
    OBJS+=hal/uart/uart_drv_$(TARGET).o
  endif
endif

ifeq ($(NXP_CUSTOM_DCD),1)
  CFLAGS+=-DNXP_CUSTOM_DCD
  OBJS+=$(NXP_CUSTOM_DCD_OBJS)
endif

ifeq ($(BIG_ENDIAN),1)
  CFLAGS+=-D"BIG_ENDIAN_ORDER"
endif

CFLAGS+=-DWOLFBOOT_ARCH_$(ARCH)
CFLAGS+=-DTARGET_$(TARGET)
