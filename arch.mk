## CPU Architecture selection via $ARCH
UPDATE_OBJS:=./src/update_flash.o


# check for FASTMATH or SP_MATH
ifeq ($(SPMATH),1)
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/sp_int.o
else
  CFLAGS+=-DUSE_FAST_MATH
  MATH_OBJS:=./lib/wolfssl/wolfcrypt/src/integer.o ./lib/wolfssl/wolfcrypt/src/tfm.o
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
  OBJS+=src/boot_x86_64.o
  ifeq ($(DEBUG),1)
    CFLAGS+=-DWOLFBOOT_DEBUG_EFI=1
  endif
endif

## ARM
ifeq ($(ARCH),AARCH64)
  CROSS_COMPILE:=aarch64-none-elf-
  CFLAGS+=-DARCH_AARCH64 -march=armv8-a
  OBJS+=src/boot_aarch64.o src/boot_aarch64_start.o
  CFLAGS+=-DNO_QNX
  ifeq ($(SPMATH),1)
    MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
  endif
endif

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
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32g0)
    CORTEX_M0=1
    ARCH_FLASH_OFFSET=0x08000000
    WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)

    # Enable this feature for secure memory support
    # Makes the flash sectors for the bootloader unacessible from the application
    # Requires using the STM32CubeProgrammer to set FLASH_SECR -> SEC_SIZE pages
    CFLAGS+=-DFLASH_SECURABLE_MEMORY_SUPPORT
  endif

  ifeq ($(TARGET),stm32f4)
    ARCH_FLASH_OFFSET=0x08000000
    WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32l4)
    SPI_TARGET=stm32
    ARCH_FLASH_OFFSET=0x08000000
    WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)
    OBJS+=$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.o
    OBJS+=$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.o
    CFLAGS+=-DSTM32L4A6xx -DUSE_HAL_DRIVER -Isrc -Ihal \
      -I$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Inc/ \
      -I$(STM32CUBE)/Drivers/BSP/STM32L4xx_Nucleo_144/ \
      -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32L4xx/Include/ \
      -I$(STM32CUBE)/Drivers/CMSIS/Include/
  endif

  ifeq ($(TARGET),stm32f7)
    ARCH_FLASH_OFFSET=0x08000000
    WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32h7)
    ARCH_FLASH_OFFSET=0x08000000
    WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)
    SPI_TARGET=stm32
  endif

  ifeq ($(TARGET),stm32wb)
    ARCH_FLASH_OFFSET=0x08000000
    WOLFBOOT_ORIGIN=$(ARCH_FLASH_OFFSET)
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
    CFLAGS+=-Ihal -DCORTEX_M33
    ARCH_FLASH_OFFSET=0x08000000
    ifeq ($(TZEN),1)
      WOLFBOOT_ORIGIN=0x0C000000
    else
      WOLFBOOT_ORIGIN=0x08000000
    endif
  endif

  ifeq ($(TARGET),stm32u5)
    CORTEX_M33=1
    CFLAGS+=-Ihal -DCORTEX_M33
    ARCH_FLASH_OFFSET=0x08000000
    ifeq ($(TZEN),1)
      WOLFBOOT_ORIGIN=0x0C000000
    else
      WOLFBOOT_ORIGIN=0x08000000
    endif
  endif

  ## Cortex-M CPU
  ifeq ($(CORTEX_M33),1)
    CFLAGS+=-mcpu=cortex-m33
    LDFLAGS+=-mcpu=cortex-m33
    ifeq ($(TZEN),1)
      CFLAGS += -mcmse
    endif
    ifeq ($(SPMATH),1)
      MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o
    endif
  else
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

# powerpc
ifeq ($(ARCH),PPC)
  CROSS_COMPILE:=powerpc-linux-gnu-
  CFLAGS+=-fno-builtin-printf -DUSE_M_TIME -g -nostartfiles
  LDFLAGS+=-Wl,--build-id=none
  #MATH_OBJS += ./lib/wolfssl/wolfcrypt/src/sp_c32.o

  # Prune unused functions and data
  CFLAGS +=-ffunction-sections -fdata-sections
  LDFLAGS+=-Wl,--gc-sections

  OBJS+=src/boot_ppc_start.o src/boot_ppc.o
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

ifeq ($(TARGET),imx_rt)
  ARCH_FLASH_OFFSET=0x60000000
  CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -I$(MCUXPRESSO)/middleware/mflash/mimxrt1062 \
      -I$(MCUXPRESSO_DRIVERS)/utilities/debug_console/ \
      -I$(MCUXPRESSO_DRIVERS)/utilities/str/ \
      -I$(MCUXPRESSO)/components/uart/ \
      -I$(MCUXPRESSO)/components/flash/nor \
      -I$(MCUXPRESSO)/components/flash/nor/flexspi \
      -I$(MCUXPRESSO)/components/serial_manager/ \
      -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -I$(MCUXPRESSO_DRIVERS)/project_template/ \
      -I$(MCUXPRESSO)/boards/evkmimxrt1060/xip/ -DXIP_EXTERNAL_FLASH=1 -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -DPRINTF_ADVANCED_ENABLE=1 \
      -DSCANF_ADVANCED_ENABLE=1 -DSERIAL_PORT_TYPE_UART=1 -DNDEBUG=1
  OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexspi.o
  ifeq ($(PKA),1)
     PKA_EXTRA_OBJS+= \
         $(MCUXPRESSO)/devices/MIMXRT1062/drivers/fsl_dcp.o \
         ./lib/wolfssl/wolfcrypt/src/port/nxp/dcp_port.o
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

ifeq ($(TARGET),ti_hercules)
  # HALCoGen Source and Include?
  CORTEX_R5=1
  CFLAGS+=-D"CORTEX_R5" -D"BIG_ENDIAN_ORDER" -D"NVM_FLASH_WRITEONCE" -D"FLASHBUFFER_SIZE=32"
  STACK_USAGE=0
  USE_GCC=0

  ifeq ($(CCS_ROOT),)
    $(error "CCS_ROOT must be defined to root of tools")
  endif
  CROSS_COMPILE=$(CCS_ROOT)/bin/

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
  CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1
  OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_flashiap.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_power.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_reset.o
  OBJS+=$(MCUXPRESSO_DRIVERS)/mcuxpresso/libpower_softabi.a $(MCUXPRESSO_DRIVERS)/drivers/fsl_common.o
  OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_usart.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexcomm.o
endif

ifeq ($(TARGET),psoc6)
    CORTEX_M0=1
    OBJS+= $(CYPRESS_PDL)/drivers/source/cy_flash.o \
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
    PSOC6_CRYPTO_OBJS=./lib/wolfssl/wolfcrypt/src/port/cypress/psoc6_crypto.o \
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

    CFLAGS+=-I$(CYPRESS_PDL)/drivers/include/ \
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


CFLAGS+=-DARCH_FLASH_OFFSET=$(ARCH_FLASH_OFFSET)


USE_GCC?=1
ifeq ($(USE_GCC),1)
  ## Toolchain setup
  CC=$(CROSS_COMPILE)gcc
  LD=$(CROSS_COMPILE)gcc
  AS=$(CROSS_COMPILE)gcc
  OBJCOPY=$(CROSS_COMPILE)objcopy
  SIZE=$(CROSS_COMPILE)size
  OUTPUT_FLAG=-o
endif


ifeq ($(TARGET),x86_64_efi)
  GNU_EFI_LIB_PATH?=/usr/lib
  GNU_EFI_CRT0=$(GNU_EFI_LIB_PATH)/crt0-efi-x86_64.o
  GNU_EFI_LSCRIPT=$(GNU_EFI_LIB_PATH)/elf_x86_64_efi.lds
  CFLAGS += -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
            -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
  CFLAGS += -I/usr/include/efi -I/usr/include/efi/x86_64 -DPLATFORM_X86_64_EFI
  LDFLAGS = -shared -Bsymbolic -L/usr/lib -T$(GNU_EFI_LSCRIPT)
  LD_START_GROUP = $(GNU_EFI_CRT0)
  LD_END_GROUP = -lgnuefi -lefi
  LD = ld
  UPDATE_OBJS:=src/update_ram.o
endif

BOOT_IMG?=test-app/image.bin

## Update mechanism
ifeq ($(ARCH),AARCH64)
  CFLAGS+=-DMMU
  UPDATE_OBJS:=src/update_ram.o
endif
ifeq ($(DUALBANK_SWAP),1)
  UPDATE_OBJS:=src/update_flash_hwswap.o
endif

## Debug
ifeq ($(DEBUG),1)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/logging.o
endif

