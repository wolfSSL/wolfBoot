## CPU Architecture selection via $ARCH

# check for math library
ifeq ($(SPMATH),1)
  # SP Math
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/sp_int.o
else
  ifeq ($(SPMATHALL),1)
    # SP Math all
    CFLAGS+=-DWOLFSSL_SP_MATH_ALL
    MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/sp_int.o
  else
    # Fastmath
    CFLAGS+=-DUSE_FAST_MATH
    MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/integer.o ./lib/wolfssl/wolfcrypt/src/tfm.o
  endif
endif

# Default flash offset
ARCH_FLASH_OFFSET?=0x0

# Default SPI driver name
SPI_TARGET=$(TARGET)

# Default UART driver name
UART_TARGET=$(TARGET)

# Include SHA256 module because it's implicitly needed by RSA
WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha256.o

ifeq ($(ARCH),x86_64)
  CFLAGS+=-DARCH_x86_64
  ifeq ($(FORCE_32BIT),1)
    NO_ASM=1
    CFLAGS+=-DFORCE_32BIT
  endif
  ifeq ($(SPMATH),1)
    ifeq ($(NO_ASM),1)
      ifeq ($(FORCE_32BIT),1)
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
        CFLAGS+=-DWOLFSSL_SP_DIV_WORD_HALF
      else
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c64.o
      endif
    else
      MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_x86_64.o
    endif
  endif
  ifeq ($(TARGET),x86_64_efi)
     OBJS+=src/boot_x86_64.o
    ifeq ($(DEBUG),1)
      CFLAGS+=-DWOLFBOOT_DEBUG_EFI=1
    endif
  endif
endif

## ARM
ifeq ($(ARCH),AARCH64)
  CROSS_COMPILE?=aarch64-none-elf-
  CFLAGS+=-DARCH_AARCH64 -march=armv8-a
  OBJS+=src/boot_aarch64.o src/boot_aarch64_start.o
  CFLAGS+=-DNO_QNX
  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_arm64.o
  endif
endif

ifeq ($(ARCH),ARM)
  CROSS_COMPILE?=arm-none-eabi-
  CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork -DARCH_ARM
  LDFLAGS+=-mthumb -mlittle-endian -mthumb-interwork
  OBJS+=src/boot_arm.o

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

  ifeq ($(TARGET),stm32f4)
    ARCH_FLASH_OFFSET=0x08000000
    SPI_TARGET=stm32
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
      PKA_EXTRA_OBJS+= $(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pka.o  ./lib/wolfssl/wolfcrypt/src/port/st/stm32.o
      PKA_EXTRA_CFLAGS+=-DWOLFSSL_STM32_PKA -I$(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Inc \
          -Isrc -I$(STM32CUBE)/Drivers/BSP/P-NUCLEO-WB55.Nucleo/ -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32WBxx/Include \
          -I$(STM32CUBE)/Drivers/STM32WBxx_HAL_Driver/Inc/ \
          -I$(STM32CUBE)/Drivers/CMSIS/Include \
          -Ihal \
          -DSTM32WB55xx
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
    endif
    ifneq ($(TZEN),1)
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
    endif
    SPI_TARGET=stm32
  endif

  ## Cortex-M CPU
ifeq ($(CORTEX_M33),1)
  CFLAGS+=-mcpu=cortex-m33 -DCORTEX_M33
  LDFLAGS+=-mcpu=cortex-m33
  ifeq ($(TZEN),1)
    OBJS+=hal/stm32_tz.o
    CFLAGS+=-mcmse
    ifeq ($(WOLFCRYPT_TZ),1)
      SECURE_OBJS+=./src/wc_callable.o
      SECURE_OBJS+=./lib/wolfssl/wolfcrypt/src/random.o
      CFLAGS+=-DWOLFCRYPT_SECURE_MODE
      SECURE_LDFLAGS+=-Wl,--cmse-implib -Wl,--out-implib=./src/wc_secure_calls.o
    endif
  endif # TZEN=1
  ifeq ($(NO_ASM),1)
    ifeq ($(SPMATH),1)
      ifeq ($(NO_ASM),1)
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
      else
        CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
      endif
    endif
  else
    ifeq ($(SPMATH),1)
      CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
      MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
    endif
  endif
else
  ifeq ($(CORTEX_M7),1)
    CFLAGS+=-mcpu=cortex-m7
    LDFLAGS+=-mcpu=cortex-m7
    ifeq ($(SPMATH),1)
      ifeq ($(NO_ASM),1)
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
      else
        CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
        MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
      endif
    endif
  else
    ifeq ($(CORTEX_M0),1)
      CFLAGS+=-mcpu=cortex-m0
      LDFLAGS+=-mcpu=cortex-m0
      ifeq ($(SPMATH),1)
        ifeq ($(NO_ASM),1)
          MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
        else
          CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_THUMB_ASM
          MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_armthumb.o
        endif
      endif
    else
      ifeq ($(CORTEX_M3),1)

        CFLAGS+=-mcpu=cortex-m3
        LDFLAGS+=-mcpu=cortex-m3
        ifeq ($(NO_ASM),1)
          ifeq ($(SPMATH),1)
            MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
          endif
        else
          ifeq ($(SPMATH),1)
            CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM -DWOLFSSL_SP_NO_UMAAL
            MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
          endif
        endif
    else
      # default Cortex M4
      CFLAGS+=-mcpu=cortex-m4
      LDFLAGS+=-mcpu=cortex-m4
      ifeq ($(NO_ASM),1)
        ifeq ($(SPMATH),1)
          MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
        endif
      else
        CFLAGS+=-fomit-frame-pointer # required with debug builds only
        ifeq ($(SPMATH),1)
          CFLAGS+=-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM
          MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_cortexm.o
        endif
      endif
    endif
  endif
endif
endif
endif

ifeq ($(TZEN),1)
  CFLAGS+=-DTZEN
endif

## RISCV
ifeq ($(ARCH),RISCV)
  CROSS_COMPILE?=riscv32-unknown-elf-
  CFLAGS+=-fno-builtin-printf -DUSE_M_TIME -g -march=rv32imac -mabi=ilp32 -mcmodel=medany -nostartfiles -DARCH_RISCV
  LDFLAGS+=-march=rv32imac -mabi=ilp32 -mcmodel=medany
  MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o

  # Prune unused functions and data
  CFLAGS +=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  OBJS+=src/boot_riscv.o src/vector_riscv.o
  ARCH_FLASH_OFFSET=0x20010000
endif

# powerpc
ifeq ($(ARCH),PPC)
  CROSS_COMPILE?=powerpc-linux-gnu-
  LDFLAGS+=-Wl,--build-id=none
  CFLAGS+=-DARCH_PPC

  ifeq ($(DEBUG_UART),0)
    CFLAGS+=-fno-builtin-printf
  endif

  # Prune unused functions and data
  CFLAGS+=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  OBJS+=src/boot_ppc_start.o src/boot_ppc.o
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
      PKA_EXTRA_OBJS+=./lib/wolfssl/wolfcrypt/src/port/nxp/ksdk_port.o
      ifeq ($(MCUXSDK),1)
        PKA_EXTRA_OBJS+=$(MCUXPRESSO)/drivers/ltc/fsl_ltc.o
      else
        PKA_EXTRA_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_ltc.o
      endif
    endif
  endif
endif

ifeq ($(TARGET),imx_rt)
  CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS) \
      -I$(MCUXPRESSO_DRIVERS)/drivers \
      -I$(MCUXPRESSO)/drivers \
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
      -I$(MCUXPRESSO)/utilities/str \
      -I$(MCUXPRESSO)/utilities/debug_console
    OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO)/drivers/flexspi/fsl_flexspi.o
    ifeq ($(DEBUG_UART),1)
      OBJS+= $(MCUXPRESSO)/drivers/lpuart/fsl_lpuart.o
    endif
  else
    CFLAGS+=\
      -I$(MCUXPRESSO_DRIVERS)/utilities/str \
      -I$(MCUXPRESSO_DRIVERS)/utilities/debug_console
    OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexspi.o
    ifeq ($(DEBUG_UART),1)
      OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_lpuart.o
    endif
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1064DVL6A)
    ARCH_FLASH_OFFSET=0x70000000
    CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1064/xip/
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1062DVL6A)
    ARCH_FLASH_OFFSET=0x60000000
    CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1060/xip/
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1052DVJ6B)
    ARCH_FLASH_OFFSET=0x60000000
    CFLAGS+=-I$(MCUXPRESSO)/boards/evkbimxrt1050/xip/
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1042XJM5B)
    ARCH_FLASH_OFFSET=0x60000000
    CFLAGS+=-I$(MCUXPRESSO)/boards/evkmimxrt1040/xip/
  endif

  ifeq ($(PKA),1)
    ifeq ($(MCUXSDK),1)
      PKA_EXTRA_OBJS+= $(MCUXPRESSO)/drivers/fsl_dcp.o
    else
      PKA_EXTRA_OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_dcp.o
    endif
    PKA_EXTRA_OBJS+=./lib/wolfssl/wolfcrypt/src/port/nxp/dcp_port.o
    PKA_EXTRA_CFLAGS+=-DWOLFSSL_IMXRT_DCP
  endif
endif

# ARM Big Endian
ifeq ($(ARCH),ARM_BE)
  OBJS+=src/boot_arm.o
  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
  endif
endif

ifeq ($(TARGET),nxp_t1024)
  # Power PC big endian
  ARCH_FLAGS=-mhard-float -mcpu=e5500
  CFLAGS+=$(ARCH_FLAGS) -DBIG_ENDIAN_ORDER
  CFLAGS+=-DMMU -DWOLFBOOT_DUALBOOT
  CFLAGS+=-pipe # use pipes instead of temp files
  CFLAGS+=-feliminate-unused-debug-types
  LDFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=-Wl,--hash-style=both # generate both sysv and gnu symbol hash table
  LDFLAGS+=-Wl,--as-needed # remove weak functions not used
  OBJS+=src/boot_ppc_mp.o # support for spin table
  OBJS+=src/fdt.o
  UPDATE_OBJS:=src/update_ram.o
  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
  else
    # Use the SP math all assembly accelerations
    CFLAGS+=-DWOLFSSL_SP_PPC
  endif
  SPI_TARGET=nxp
  OPTIMIZATION_LEVEL=0 # using default -Os causes issues with alignment
endif

ifeq ($(TARGET),nxp_t2080)
  # Power PC big endian
  ARCH_FLAGS=-mhard-float -mcpu=e6500
  CFLAGS+=$(ARCH_FLAGS) -DBIG_ENDIAN_ORDER
  CFLAGS+=-DMMU -DWOLFBOOT_DUALBOOT
  CFLAGS+=-pipe # use pipes instead of temp files
  CFLAGS+=-feliminate-unused-debug-types
  LDFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=-Wl,--hash-style=both # generate both sysv and gnu symbol hash table
  LDFLAGS+=-Wl,--as-needed # remove weak functions not used
  UPDATE_OBJS:=src/update_ram.o
  OBJS+=src/fdt.o
  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
  else
    # Use the SP math all assembly accelerations
    CFLAGS+=-DWOLFSSL_SP_PPC
  endif
endif

ifeq ($(TARGET),nxp_p1021)
  # Power PC big endian
  ARCH_FLAGS=-m32 -mhard-float -mcpu=e500mc
  ARCH_FLAGS+=-fno-builtin -ffreestanding -nostartfiles
  CFLAGS+=$(ARCH_FLAGS) -DBIG_ENDIAN_ORDER
  CFLAGS+=-DWOLFBOOT_DUALBOOT
  CFLAGS+=-pipe # use pipes instead of temp files
  LDFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=-Wl,--as-needed # remove weak functions not used
  OBJS+=src/boot_ppc_mp.o # support for spin table
  UPDATE_OBJS:=src/update_ram.o

  # Use PPC stdlib for memcpy, etc.
  #CFLAGS+=-DWOLFBOOT_USE_STDLIBC

  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
  else
    # Use the SP math all assembly accelerations
    CFLAGS+=-DWOLFSSL_SP_PPC
  endif
  SPI_TARGET=nxp
endif

ifeq ($(TARGET),zynq)
  # Support detection and skip of U-Boot legecy header */
  CFLAGS+=-DWOLFBOOT_UBOOT_LEGACY
endif

ifeq ($(TARGET),ti_hercules)
  # HALCoGen Source and Include?
  CORTEX_R5=1
  CFLAGS+=-D"CORTEX_R5" -D"BIG_ENDIAN_ORDER" -D"NVM_FLASH_WRITEONCE" -D"FLASHBUFFER_SIZE=32"
  STACK_USAGE=0
  USE_GCC=0

  ifeq ($(CCS_ROOT),)
    $(error "CCS_ROOT must be defined to root of tools")
  endif
  CROSS_COMPILE?=$(CCS_ROOT)/bin/

  CC=$(CROSS_COMPILE)armcl
  LD=$(CROSS_COMPILE)armcl
  AS=$(CROSS_COMPILE)armasm
  OBJCOPY=$(CROSS_COMPILE)armobjcopy
  SIZE=$(CROSS_COMPILE)armsize
  OUTPUT_FLAG=--output_file

  F021_DIR?=c:\\ti\\Hercules\\F021\ Flash\ API\\02.01.01

  ARCH_FLAGS=-mv7R5 --code_state=32 --float_support=VFPv3D16 --enum_type=packed --abi=eabi -I"$(CCS_ROOT)/include" -I"$(F021_DIR)/include"
  CFLAGS+=$(ARCH_FLAGS)
  LDFLAGS+=$(ARCH_FLAGS) -i"$(CCS_ROOT)/lib" -i"$(F021_DIR)" -z --be32 --map_file=wolfboot.map --reread_libs --diag_wrap=off --display_error_number --warn_sections --heap_size=0 --stack_size=0x800 --ram_model
  LD_START_GROUP= #--start-group
  LD_END_GROUP= -llibc.a -l"$(F021_DIR)\\F021_API_CortexR4_BE_L2FMC_V3D16.lib" $(LSCRIPT)

  OPTIMIZATION_LEVEL=2
endif

ifeq ($(TARGET),lpc)
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
  ifeq ($(MCUXSDK),1)
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
        ./lib/wolfssl/wolfcrypt/src/port/cypress/psoc6_crypto.o \
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
        -I$(CYPRESS_PDL)/drivers/include \
        -I$(CYPRESS_PDL)/devices/include \
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



USE_GCC?=1
USE_GCC_HEADLESS?=1
ifeq ($(USE_GCC),1)
  ## Toolchain setup
  CC=$(CROSS_COMPILE)gcc
  LD=$(CROSS_COMPILE)gcc
  AS=$(CROSS_COMPILE)gcc
  OBJCOPY?=$(CROSS_COMPILE)objcopy
  SIZE=$(CROSS_COMPILE)size
  OUTPUT_FLAG=-o
endif

ifeq ($(filter $(TARGET),x86_fsp_qemu kontron_vx3060_s2),$(TARGET))
  FSP=1
  CFLAGS+=-DWOLFBOOT_FSP=1
  ifeq ($(TARGET), kontron_vx3060_s2)
    FSP_TGL=1
    CFLAGS+=-DWOLFBOOT_TGL=1
  endif
endif

ifeq ($(TARGET),x86_fsp_qemu)
  ifeq ($(filter-out $(STAGE1),1),)
    OBJS+=src/x86/qemu_fsp.o
  endif
endif

# x86-64 FSP targets
ifeq ("${FSP}", "1")
  USE_GCC_HEADLESS=0
  LD_START_GROUP =
  LD_END_GROUP =
  LD := ld
  ifeq ($(filter-out $(STAGE1),1),)
    # building stage1
    ifeq ($(FSP_TGL), 1)
      LSCRIPT_IN = ../hal/x86_fsp_tgl_stage1.ld
    else
      LSCRIPT_IN = ../hal/$(TARGET)_stage1.ld
    endif
    # using ../wolfboot.map as stage1 is built from stage1 sub-directory
    LDFLAGS = --defsym main=`grep main ../wolfboot.map | awk '{print $$1}'` \
              --defsym wb_start_bss=`grep _start_bss ../wolfboot.map | awk '{print $$1}'` \
              --defsym wb_end_bss=`grep _end_bss ../wolfboot.map | awk '{print $$1}'` \
              --defsym _stage2_params=`grep _stage2_params ../wolfboot.map | awk '{print $$1}'`
    LDFLAGS +=  --no-gc-sections --print-gc-sections -T $(LSCRIPT) -m elf_i386  -Map=loader_stage1.map
    OBJS += src/boot_x86_fsp.o
    OBJS += src/boot_x86_fsp_start.o
    OBJS += src/fsp_m.o
    OBJS += src/fsp_s.o
    OBJS += src/fsp_t.o
    OBJS += src/wolfboot_raw.o
    OBJS += src/x86/common.o
    OBJS += src/x86/hob.o
    OBJS += src/pci.o
    OBJS += hal/x86_uart.o
    OBJS += src/string.o
    ifeq ($(filter-out $(STAGE1_AUTH),1),)
      OBJS += src/libwolfboot.o
      OBJS += src/image.o
      OBJS += src/keystore.o
      OBJS += src/sig_wolfboot_raw.o
      OBJS += src/sig_fsp_s.o
      ifeq ($(TARGET), kontron_vx3060_s2)
        OBJS += hal/kontron_vx3060_s2_loader.o
      endif
      OBJS += $(WOLFCRYPT_OBJS)
      CFLAGS+=-DSTAGE1_AUTH
    endif

    CFLAGS += -fno-stack-protector -m32 -fno-PIC -fno-pie -mno-mmx -mno-sse -DDEBUG_UART
    ifeq ($(FSP_TGL), 1)
      OBJS+=src/x86/tgl_fsp.o
      OBJS+=src/fsp_tgl_s_upd.o
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
    LDFLAGS =  --no-gc-sections --print-gc-sections -T $(LSCRIPT) -Map=wolfboot.map
    CFLAGS += -fno-stack-protector -fno-PIC -fno-pie -mno-mmx -mno-sse -Os -DDEBUG_UART
    OBJS += hal/x86_fsp_tgl.o
    OBJS += hal/x86_uart.o
    OBJS += src/boot_x86_fsp_payload.o
    OBJS += src/x86/common.o
    OBJS += src/x86/hob.o
    OBJS += src/pci.o
    OBJS += src/x86/ahci.o
    OBJS += src/x86/ata.o
    OBJS += src/x86/gpt.o
    OBJS += src/x86/mptable.o
    UPDATE_OBJS := src/update_disk.o
    ifeq ($(64BIT),1)
      LDFLAGS += -m elf_x86_64 --oformat elf64-x86-64
      CFLAGS += -m64
    else
      CFLAGS += -m32
      LDFLAGS += -m elf_i386 --oformat elf32-i386
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
            -DPLATFORM_X86_64_EFI  -DWOLFBOOT_DUALBOOT
  LDFLAGS = -shared -Bsymbolic -L/usr/lib -T$(GNU_EFI_LSCRIPT)
  LD_START_GROUP = $(GNU_EFI_CRT0)
  LD_END_GROUP = -lgnuefi -lefi
  LD = ld
  UPDATE_OBJS:=src/update_ram.o
endif

ifeq ($(TARGET),sim)
  USE_GCC_HEADLESS=0
  LD = gcc
  UPDATE_OBJS:=src/update_flash.o
  LD_START_GROUP=
  LD_END_GROUP=
  BOOT_IMG=test-app/image.elf
  CFLAGS+=-DARCH_SIM
  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
    CFLAGS+=-DWOLFSSL_SP_DIV_WORD_HALF
  endif
endif

CFLAGS+=-DARCH_FLASH_OFFSET=$(ARCH_FLASH_OFFSET)
BOOT_IMG?=test-app/image.bin

## Update mechanism
ifeq ($(ARCH),AARCH64)
  CFLAGS+=-DMMU -DWOLFBOOT_DUALBOOT
  OBJS+=src/fdt.o
  UPDATE_OBJS:=src/update_ram.o
endif
ifeq ($(DUALBANK_SWAP),1)
  CFLAGS+=-DWOLFBOOT_DUALBOOT
  UPDATE_OBJS:=src/update_flash_hwswap.o
endif

# Set default update object (if not library)
ifneq ($(TARGET),library)
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
ifeq ($(DEBUG),1)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/logging.o
endif

# Debug UART
ifeq ($(DEBUG_UART),1)
  CFLAGS+=-DDEBUG_UART
endif

ifeq ($(NXP_CUSTOM_DCD),1)
  CFLAGS+=-DNXP_CUSTOM_DCD
  OBJS+=$(NXP_CUSTOM_DCD_OBJS)
endif

CFLAGS+=-DWOLFBOOT_ARCH_$(ARCH)
CFLAGS+=-DTARGET_$(TARGET)
