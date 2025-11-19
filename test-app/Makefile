-include ../.config
-include ../tools/config.mk
-include ../options.mk
-include ./wcs/pkcs11.mk

WOLFBOOT_LIB_WOLFSSL?=../lib/wolfssl
WOLFSSL_LOCAL_OBJDIR?=wolfssl_obj
vpath %.c $(WOLFBOOT_LIB_WOLFSSL)
vpath %.S $(WOLFBOOT_LIB_WOLFSSL)

TARGET?=none
ARCH?=ARM
MCUXPRESSO_CMSIS?=$(MCUXPRESSO)/CMSIS
ifneq (,$(filter $(TARGET),mcxa mcxw mcxn))
  TEST_APP_NO_RNG?=1
endif
ifeq ($(TZEN),1)
  # wcs directory contains a user_settings.h, which will conflict with
  # the one in the include directory if the test app needs it (e.g. wolfHSM)
  CFLAGS:=-I./wcs $(CFLAGS)
endif
CFLAGS+=-I. -I..
DEBUG?=1
DELTA_DATA_SIZE?=2000
USE_GCC?=1
USE_GCC_HEADLESS?=1
BOOTLOADER_PARTITION_SIZE?=$$(( $(WOLFBOOT_PARTITION_BOOT_ADDRESS) - $(ARCH_FLASH_OFFSET)))

# For test-applications we should only use user_settings.h
CFLAGS+=-DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS

ifeq ($(WOLFBOOT_ATTESTATION_TEST),1)
  CFLAGS+=-DWOLFBOOT_ATTESTATION_TEST
endif

ifeq ($(HASH),SHA256)
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA256"
endif

ifeq ($(NVM_FLASH_WRITEONCE),1)
  CFLAGS+=-D"NVM_FLASH_WRITEONCE"
endif


ifeq ($(HASH),SHA3_384)
  WOLFCRYPT_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA3_384"
endif



ifeq ($(TARGET),ti_hercules)
  APP_OBJS:=app_$(TARGET).o ../test-app/libwolfboot.o
  CFLAGS+=-I"../include"
else
  CFLAGS+=-Wall -ffreestanding -Wno-unused
  # Stack usage computation not supported on TriCore
  ifneq ($(ARCH),AURIX_TC3)
    CFLAGS+=-Wstack-usage=1024 -nostartfiles
  endif
  CFLAGS+=-DTARGET_$(TARGET) -I../include
  CFLAGS+=-g
  ifeq ($(USE_GCC),1)
    CFLAGS+=-ggdb3
  else ifneq ($(ARCH),AURIX_TC3)
    CFLAGS+=-gstabs
  endif

  ifeq ($(ARCH),RENESAS_RX)
    APP_OBJS:=app_renesas_rx.o ../test-app/libwolfboot.o ../src/boot_renesas.o ../src/boot_renesas_start.o ../hal/renesas-rx.o
    LDFLAGS+=-ffreestanding -nostartfiles
    CFLAGS+=-DWOLFBOOT_RENESAS_APP
  else
    APP_OBJS:=app_$(TARGET).o led.o system.o timer.o ../test-app/libwolfboot.o
  endif
endif

include ../arch.mk

# Optional alias for clearer TZ PSA selection in app builds.
ifeq ($(WOLFCRYPT_TZ_PSA),1)
  WOLFCRYPT_TZ=1
  WOLFCRYPT_TZ_PSA=1
endif

# Setup default linker flags
LDFLAGS+=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=image.map

# Setup default objcopy flags
OBJCOPY_FLAGS+=--gap-fill $(FILL_BYTE)

ifeq ($(DEBUG_UART),1)
    CFLAGS+=-DDEBUG_UART
    APP_OBJS+=../src/string.o
endif

ifeq ($(TZEN),1)
    CFLAGS+=-DNONSECURE_APP
    CFLAGS+=-I./
    APP_OBJS+=../hal/$(TARGET)_ns.o
    ifeq ($(WOLFCRYPT_TZ),1)
        CFLAGS+=-I"$(WOLFBOOT_LIB_WOLFSSL)"
        APP_OBJS+=../src/wc_secure_calls.o
        WOLFCRYPT_APP_OBJS+=\
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/memory.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hash.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha256.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha512.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sha3.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/hmac.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/pwdbased.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/pkcs12.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/coding.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/asn.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/ecc.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/rsa.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/aes.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wolfmath.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_int.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_cortexm.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/sp_c32.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_port.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wolfentropy.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/dh.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/wc_encrypt.o
        ifeq ($(TEST_APP_NO_RNG),1)
            CFLAGS+=-DWC_NO_RNG
        else
            WOLFCRYPT_APP_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/random.o
        endif
        ifeq ($(WOLFCRYPT_TZ_PKCS11),1)
            CFLAGS+=-DWOLFSSL_USER_SETTINGS -DWOLFTPM_USER_SETTINGS
            CFLAGS+=-DWOLFBOOT_PKCS11_APP -DSECURE_PKCS11 -DWOLFBOOT_TZ_PKCS11
            CFLAGS+=-I"$(WOLFBOOT_LIB_WOLFPKCS11)"
            APP_OBJS+=./wcs/pkcs11_test_ecc.o
            APP_OBJS+=./wcs/pkcs11_stub.o
            APP_OBJS+=./wcs/ecc.o
            APP_OBJS+=./wcs/rsa.o
            APP_OBJS+=./wcs/asn.o
            APP_OBJS+=./wcs/aes.o
            APP_OBJS+=./wcs/hmac.o
            APP_OBJS+=./wcs/pwdbased.o
            APP_OBJS+=./wcs/hash.o
            APP_OBJS+=./wcs/sha256.o
            APP_OBJS+=./wcs/sha512.o
            APP_OBJS+=./wcs/sha3.o
            APP_OBJS+=./wcs/integer.o
            APP_OBJS+=./wcs/tfm.o
            APP_OBJS+=./wcs/sp_c32.o
            APP_OBJS+=./wcs/sp_int.o
            APP_OBJS+=./wcs/cryptocb.o
            APP_OBJS+=./wcs/wc_pkcs11.o
            APP_OBJS+=./wcs/memory.o
            APP_OBJS+=./wcs/wolfmath.o
            APP_OBJS+=./wcs/dh.o
            APP_OBJS+=./wcs/random.o
            APP_OBJS+=./wcs/coding.o
            APP_OBJS+=./wcs/wc_encrypt.o
            APP_OBJS+=./wcs/wc_port.o

            ifeq ($(SPMATH),1)
              CFLAGS+=-DWOLFSSL_HAVE_SP_RSA -DWOLFSSL_HAVE_SP_ECC
              ifneq ($(NO_ASM),1)
                CFLAGS+=-fomit-frame-pointer # required with debug builds only
                CFLAGS+=-DWOLFSSL_SP_ARM_CORTEX_M_ASM
                APP_OBJS+=./wcs/sp_cortexm.o
              endif
            endif
        endif
        CFLAGS+=-DWOLFBOOT_SECURE_CALLS -Wstack-usage=19184
        LDFLAGS+=--specs=nosys.specs -u _printf_float
    endif
    ifeq ($(WOLFCRYPT_TZ_PSA),1)
        CFLAGS+=-DWOLFCRYPT_TZ_PSA -DWOLFSSL_HAVE_PSA
        CFLAGS+=-I../zephyr/include -I../lib/wolfPSA/wolfpsa
        CFLAGS+=-I"$(WOLFBOOT_LIB_WOLFSSL)" -I"$(WOLFBOOT_LIB_WOLFSSL)/wolfssl"
        APP_OBJS+=hal_trng_psa.o
        APP_OBJS+=../zephyr/src/wolfboot_psa_ns_api.o
        APP_OBJS+=arm_tee_ns_interface_stub.o
        APP_OBJS+=../zephyr/src/arm_tee_attest_api.o
        APP_OBJS+=../zephyr/src/arm_tee_crypto_api.o
        WOLFCRYPT_APP_OBJS+=\
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/psa/psa.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/psa/psa_aes.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/psa/psa_hash.o \
            $(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/port/psa/psa_pkcbs.o
    endif
    WOLFCRYPT_APP_OBJS := $(patsubst $(WOLFBOOT_LIB_WOLFSSL)/%, \
        $(WOLFSSL_LOCAL_OBJDIR)/%, $(WOLFCRYPT_APP_OBJS))
    ifneq ($(WOLFCRYPT_TZ_PKCS11),1)
        APP_OBJS+=$(sort $(WOLFCRYPT_APP_OBJS))
    endif
else
    APP_OBJS+=../hal/$(TARGET).o
endif

ifeq ($(ARCH),RISCV)
  APP_OBJS+=startup_riscv.o vector_riscv.o
endif

ifeq ($(ARCH),AARCH64)
  APP_OBJS:=boot_arm64_start.o $(APP_OBJS)
  # Prevent inclusion of standard C runtime startup files that conflict with boot_arm64_start.S
  # Use -Wl, prefix to pass flags directly to linker when GCC is used as linker driver
  LDFLAGS+=-nostartfiles -nostdlib -nodefaultlibs -Wl,--entry=_start
endif

ifeq ($(ARCH),RISCV64)
  APP_OBJS+=startup_riscv.o vector_riscv.o
endif

ifeq ($(ARCH),ARM)
  APP_OBJS+=startup_arm.o
endif
ifeq ($(ARCH),ARM_BE)
  APP_OBJS+=startup_arm.o
  CFLAGS+=-DSTACK_PAINTING=0
endif

ifeq ($(ENCRYPT),1)
    CFLAGS+=-D"EXT_ENCRYPTED=1"
endif

ENTRY_POINT=`cat .entry-point-address`
LSCRIPT:=../config/target-app.ld
LSCRIPT_TEMPLATE:=$(ARCH).ld

ifeq ($(TARGET),stm32f7)
  LSCRIPT_TEMPLATE=ARM-stm32f7.ld
  CFLAGS+=-DDUALBANK_SWAP
endif

ifeq ($(TARGET),stm32h7)
  LSCRIPT_TEMPLATE=ARM-stm32h7.ld
endif

ifeq ($(TARGET),stm32c0)
  LSCRIPT_TEMPLATE=ARM-stm32c0.ld
endif

ifeq ($(TARGET),sama5d3)
  APP_OBJS+=./boot_arm32_start.o
  LSCRIPT_TEMPLATE:=$(ARCH)-$(TARGET).ld
endif

ifeq ($(TARGET),stm32l4)
  APP_OBJS+=$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.o
  APP_OBJS+=$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.o
  CFLAGS+=-DSTM32L4A6xx -DUSE_HAL_DRIVER -I../src -I../hal \
    -I$(STM32CUBE)/Drivers/STM32L4xx_HAL_Driver/Inc/ \
    -I$(STM32CUBE)/Drivers/BSP/STM32L4xx_Nucleo_144/ \
    -I$(STM32CUBE)/Drivers/CMSIS/Device/ST/STM32L4xx/Include/ \
    -I$(STM32CUBE)/Drivers/CMSIS/Include/
endif

ifeq ($(TARGET),stm32l5)
  ifeq ($(TZEN),1)
    LSCRIPT_TEMPLATE=ARM-stm32l5-ns.ld
    APP_OBJS+=wcs/wolfcrypt_secure.o
  else
    LSCRIPT_TEMPLATE=ARM-stm32l5.ld
  endif
  CFLAGS+=-mcpu=cortex-m33 -ffunction-sections -fdata-sections -fno-common
  LDFLAGS+=-mcpu=cortex-m33
  LDFLAGS+=-Wl,-gc-sections -Wl,-Map=image.map
  CFLAGS+=-I..
  APP_OBJS+=../hal/uart/uart_drv_$(UART_TARGET).o
endif

ifeq ($(TARGET),stm32h5)
  ifeq ($(TZEN),1)
    LSCRIPT_TEMPLATE=ARM-stm32h5-ns.ld
    APP_OBJS+=wcs/wolfcrypt_secure.o
    ifeq ($(WOLFCRYPT_TZ),1)
      APP_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/src/logging.o
      APP_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/test/test.o
      APP_OBJS+=$(WOLFBOOT_LIB_WOLFSSL)/wolfcrypt/benchmark/benchmark.o
    endif
  else
    LSCRIPT_TEMPLATE=ARM-stm32h5.ld
  endif
  CFLAGS+=-DAPP_HAS_SYSTICK
  CFLAGS+=-DRAMFUNCTION='__attribute__((used,section(".ramcode"),long_call))'
  CFLAGS+=-mcpu=cortex-m33 -ffunction-sections -fdata-sections -fno-common
  CFLAGS+=-mlong-calls
  LDFLAGS+=-mcpu=cortex-m33
  LDFLAGS+=-Wl,-gc-sections -Wl,-Map=image.map
  CFLAGS+=-I..
  APP_OBJS+=../hal/uart/uart_drv_$(UART_TARGET).o
  ifneq ($(SIGN),NONE)
    ifeq ($(FLASH_OTP_KEYSTORE),1)
      APP_OBJS+=../src/flash_otp_keystore.o
    else
      APP_OBJS+=../src/keystore.o
    endif
  endif
endif

ifeq ($(TARGET),stm32u5)
  ifeq ($(TZEN),1)
    LSCRIPT_TEMPLATE=ARM-stm32u5-ns.ld
  else
    LSCRIPT_TEMPLATE=ARM-stm32u5.ld
  endif
  CFLAGS+=-mcpu=cortex-m33
  LDFLAGS+=-mcpu=cortex-m33
endif

ifeq ($(TARGET),nrf5340)
  ifeq ($(TZEN),1)
	LSCRIPT_TEMPLATE=ARM-nrf5340-ns.ld
  else
	LSCRIPT_TEMPLATE=ARM.ld
  endif
endif

ifeq ($(TARGET),nrf5340_net)
  CFLAGS+=-mcpu=cortex-m33+nodsp+nofp
  LDFLAGS+=-mcpu=cortex-m33+nodsp+nofp
  LSCRIPT_TEMPLATE=ARM-nrf5340_net.ld
endif

ifeq ($(TARGET),ti_hercules)
  LSCRIPT_TEMPLATE=ARM-r5be.ld
  # Override linker flags
  LDFLAGS=--run_linker $(LSCRIPT) --map_file=image.map
  OBJCOPY_FLAGS=
endif

ifeq ($(TARGET),va416x0)
  APP_OBJS+=$(SDK_OBJS)
  LSCRIPT_TEMPLATE=ARM-va416x0.ld
  ifneq ($(SIGN),NONE)
    APP_OBJS+=../src/keystore.o
  endif
endif

ifeq ($(TARGET),sim)
  # LD on MacOS does not support "-Map="
  LDMAPSUPPORTED=$(shell $(CC) -Wl,-Map=image.map 2>&1 | grep 'unknown option')
  LDFLAGS=
  ifeq ($(FORCE_32BIT),1)
    LDFLAGS+=-m32
    CFLAGS+=-m32
  endif
  ifeq ($(LDMAPSUPPORTED),)
    # Override linker flags
    LDFLAGS+=-Wl,-Map=image.map
  endif
  ifeq ($(ELF_FLASH_SCATTER),1)
    LSCRIPT_TEMPLATE=sim_scattered.ld
    APP_OBJS=app_sim_scattered.o ../src/string.o
    CFLAGS+=-D"WOLFBOOT_ELF_FLASH_SCATTER=1" -nostartfiles -ffreestanding -static -nostdlib
    LDFLAGS+=-ffreestanding -nostartfiles -static -T$(LSCRIPT) -nostdlib
  else
    APP_OBJS=app_sim.o
    APP_OBJS+=../test-app/libwolfboot.o ../hal/$(TARGET).o
  endif
endif

# Self-header verification requires image.c (for verify_integrity/verify_authenticity)
# and keystore (for public key lookup), plus wolfcrypt for crypto operations
ifeq ($(WOLFBOOT_SELF_HEADER),1)
  CFLAGS+=-D"WOLFBOOT_SELF_HEADER"
  CFLAGS+=-I"$(WOLFBOOT_LIB_WOLFSSL)"
  # Test app needs addtional image, keystore, and crypto objects to do verification
  APP_OBJS+=../src/image.o ../src/keystore.o
  # Only add wolfcrypt objs if WOLFHSM blocks won't provide them
  ifeq ($(WOLFHSM_CLIENT)$(WOLFHSM_SERVER),)
    # remove duplicates for certain object files
    APP_OBJS+=$(sort $(WOLFCRYPT_OBJS))
  endif
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+=-D"EXT_FLASH=1" -D"PART_UPDATE_EXT=1"
  ifeq ($(NO_XIP),1)
    CFLAGS+=-D"PART_BOOT_EXT=1"
  endif
endif

ifeq ($(SPI_FLASH),1)
  CFLAGS+=-D"SPI_FLASH"
  APP_OBJS+=../src/spi_flash.o
  ifeq ($(ARCH),RENESAS_RX)
    APP_OBJS+=../hal/spi/spi_drv_renesas_rx.o
  else
    APP_OBJS+=../hal/spi/spi_drv_$(SPI_TARGET).o
  endif
endif

ifeq ($(OCTOSPI_FLASH),1)
  QSPI_FLASH = 1
  CFLAGS+=-D"OCTOSPI_FLASH"
endif

ifeq ($(QSPI_FLASH),1)
  CFLAGS+=-D"QSPI_FLASH"
  APP_OBJS+=../src/qspi_flash.o
  ifeq ($(ARCH),RENESAS_RX)
    APP_OBJS+=../hal/spi/spi_drv_renesas_rx.o
  else
    ifeq ($(TZEN),1)
      APP_OBJS+=../hal/spi/spi_drv_$(SPI_TARGET)_ns.o
    else
      APP_OBJS+=../hal/spi/spi_drv_$(SPI_TARGET).o
    endif
  endif
endif

ifeq ($(UART_FLASH),1)
  CFLAGS+=-D"UART_FLASH=1"
  APP_OBJS+= ../src/uart_flash.o ../hal/uart/uart_drv_$(UART_TARGET).o
else
  ifeq ($(TARGET),stm32wb)
    APP_OBJS+=../hal/uart/uart_drv_$(UART_TARGET).o
  endif
endif

ifeq ($(TARGET),kinetis)
  APP_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o
  ifeq ($(MCUXSDK),1)
    APP_OBJS+=\
      $(MCUXPRESSO)/drivers/flash/fsl_ftfx_flash.o \
      $(MCUXPRESSO)/drivers/flash/fsl_ftfx_cache.o \
      $(MCUXPRESSO)/drivers/flash/fsl_ftfx_controller.o
  else
    APP_OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_flash.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_cache.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_controller.o
  endif
endif

ifeq ($(TARGET),mcxa)
    LSCRIPT_TEMPLATE=ARM-mcxa.ld
    APP_OBJS+=$(MCUXPRESSO_PROJECT_TEMPLATE)/clock_config.o
    APP_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o
    APP_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_reset.o
    APP_OBJS+=$(MCUXPRESSO)/drivers/gpio/fsl_gpio.o
    APP_OBJS+=$(MCUXPRESSO)/drivers/mcx_spc/fsl_spc.o
    LDFLAGS+=--specs=nosys.specs
endif

ifeq ($(TARGET),s32k1xx)
    LSCRIPT_TEMPLATE=ARM-s32k1xx.ld
    ifneq ($(SIGN),NONE)
        APP_OBJS+=../src/keystore.o
    endif
    CFLAGS+=-DAPP_HAS_SYSTICK
    CFLAGS+=-DRAM_CODE -DDEBUG_UART
endif

ifeq ($(TARGET),mcxw)
    ifeq ($(TZEN),1)
      LSCRIPT_TEMPLATE=ARM-mcxw-ns.ld
      CFLAGS:=$(filter-out -mcmse, $(CFLAGS))
    else
      LSCRIPT_TEMPLATE=ARM-mcxw.ld
    endif
    APP_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o
    APP_OBJS+=$(MCUXPRESSO)/drivers/gpio/fsl_gpio.o
    APP_OBJS+=$(MCUXPRESSO)/drivers/spc/fsl_spc.o
    APP_OBJS+=$(MCUXPRESSO_PROJECT_TEMPLATE)/clock_config.o
    APP_OBJS+=$(MCUXPRESSO)/drivers/ccm32k/fsl_ccm32k.o
    APP_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/romapi/fsl_romapi.o
    LDFLAGS+=--specs=nosys.specs
endif

ifeq ($(TARGET),mcxn)
    ifeq ($(TZEN),1)
      LSCRIPT_TEMPLATE=ARM-mcxn-ns.ld
      CFLAGS:=$(filter-out -mcmse, $(CFLAGS))
    else
      LSCRIPT_TEMPLATE=ARM-mcxn.ld
    endif
    APP_OBJS+=\
      $(MCUXPRESSO_PROJECT_TEMPLATE)/clock_config.o \
      $(MCUXPRESSO)/drivers/gpio/fsl_gpio.o \
      $(MCUXPRESSO)/drivers/lpflexcomm/fsl_lpflexcomm.o \
      $(MCUXPRESSO)/drivers/lpuart/fsl_lpuart.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_reset.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
      $(MCUXPRESSO_DRIVERS)/drivers/romapi/flash/src/fsl_flash.o
    ifeq (,$(findstring nosys.specs,$(LDFLAGS)))
      LDFLAGS+=--specs=nosys.specs
    endif
endif

ifeq ($(TARGET),imx_rt)
  LDFLAGS+=\
    -mcpu=cortex-m7 -Wall --specs=nosys.specs -fno-common -ffunction-sections -fdata-sections \
    -ffreestanding -fno-builtin -mthumb -mapcs -Xlinker --gc-sections -Xlinker -static -Xlinker -z \
    -Xlinker muldefs -Xlinker -Map=output.map -static -lm -lc -lnosys
  LSCRIPT_TEMPLATE=imx_rt.ld
  APP_OBJS+=\
    imx_rt_clock_config.o \
    $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
    $(MCUXPRESSO)/components/uart/fsl_adapter_lpuart.o \
    $(MCUXPRESSO)/components/serial_manager/fsl_component_serial_manager.o \
    $(MCUXPRESSO)/components/lists/fsl_component_generic_list.o \
    $(MCUXPRESSO)/components/serial_manager/fsl_component_serial_port_uart.o \
    $(MCUXPRESSO)/components/flash/nor/flexspi/fsl_flexspi_nor_flash.o
  ifeq ($(MCUXSDK),1)
    APP_OBJS+=\
      $(MCUXPRESSO)/drivers/igpio/fsl_gpio.o \
      $(MCUXPRESSO)/drivers/cache/armv7-m7/fsl_cache.o \
      $(MCUXPRESSO)/drivers/common/fsl_common.o \
      $(MCUXPRESSO)/drivers/common/fsl_common_arm.o \
      $(MCUXPRESSO)/drivers/flexspi/fsl_flexspi.o \
      $(MCUXPRESSO)/drivers/lpuart/fsl_lpuart.o \
      $(MCUXPRESSO)/components/str/fsl_str.o \
      $(MCUXPRESSO)/components/debug_console/fsl_debug_console.o
  else
    APP_OBJS+=\
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_gpio.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_cache.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_common.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_common_arm.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexspi.o \
      $(MCUXPRESSO_DRIVERS)/utilities/str/fsl_str.o \
      $(MCUXPRESSO_DRIVERS)/drivers/fsl_lpuart.o \
      $(MCUXPRESSO_DRIVERS)/utilities/debug_console/fsl_debug_console.o
  endif

  ifeq ($(MCUXPRESSO_CPU),MIMXRT1042XJM5B)
    ifeq ($(MCUXSDK),1)
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1040/project_template \
              -I$(MCUXPRESSO)/examples/_boards/evkmimxrt1040/xip/
    else
      CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/project_template/ \
              -I$(MCUXPRESSO)/boards/evkmimxrt1040/xip/
    endif
    APP_OBJS+=$(MCUXPRESSO_DRIVERS)/system_MIMXRT1042.o
  else ifeq ($(MCUXPRESSO_CPU),MIMXRT1052DVJ6B)
    ifeq ($(MCUXSDK),1)
      CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbimxrt1050/project_template \
              -I$(MCUXPRESSO)/examples/_boards/evkbimxrt1050/xip/
    else
      CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/project_template/ \
              -I$(MCUXPRESSO)/boards/evkmimxrt1050/xip/
    endif
    APP_OBJS+=$(MCUXPRESSO_DRIVERS)/system_MIMXRT1052.o
  else
    ifeq ($(MCUXPRESSO_CPU),MIMXRT1062DVL6A)
      ifeq ($(MCUXSDK),1)
        CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1060/project_template \
                -I$(MCUXPRESSO)/examples/_boards/evkmimxrt1060/xip/
      else
        CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/project_template/ \
                -I$(MCUXPRESSO)/boards/evkmimxrt1060/xip/
      endif
      APP_OBJS+=$(MCUXPRESSO_DRIVERS)/system_MIMXRT1062.o
    endif
    ifeq ($(MCUXPRESSO_CPU),MIMXRT1062DVL6B)
      ifeq ($(MCUXSDK),1)
        CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/project_template \
                -I$(MCUXPRESSO)/examples/_boards/evkbmimxrt1060/xip/
      else
        CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/project_template/ \
                -I$(MCUXPRESSO)/boards/evkbmimxrt1060/xip/
      endif
      APP_OBJS+=$(MCUXPRESSO_DRIVERS)/system_MIMXRT1062.o
    endif
    ifeq ($(MCUXPRESSO_CPU),MIMXRT1064DVL6A)
      ifeq ($(MCUXSDK),1)
        CFLAGS+=-I$(MCUXPRESSO)/examples/_boards/evkmimxrt1064/project_template \
                -I$(MCUXPRESSO)/examples/_boards/evkmimxrt1064/xip/
      else
        CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/project_template/ \
                -I$(MCUXPRESSO)/boards/evkmimxrt1064/xip/
      endif
      APP_OBJS+=$(MCUXPRESSO_DRIVERS)/system_MIMXRT1064.o
    endif
  endif
endif

ifeq ($(TARGET),stm32g0)
  CFLAGS+=-DNVM_FLASH_WRITEONCE=1
endif
ifeq ($(TARGET),stm32c0)
  CFLAGS+=-DNVM_FLASH_WRITEONCE=1
endif

ifeq ($(TARGET),hifive1.freedom)
  CFLAGS+=-I$(FREEDOM_E_SDK)/freedom-metal/ -D__METAL_MACHINE_HEADER=\"$(FREEDOM_E_SDK)/bsp/sifive-hifive1/metal.h\"
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/clock.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/led.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/cache.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/cpu.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/gpio.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/interrupt.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/uart.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/tty.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/spi.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/shutdown.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/timer.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,hfrosc.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,hfxosc.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,pll.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,prci.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,spi0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,uart0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,global-external-interrupts0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,local-external-interrupts0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,gpio0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,gpio-leds.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,clint0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,plic0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,cpu.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/fixed-clock.o
endif

ifeq ($(TARGET),hifive1)
  CFLAGS+=-DRAMFUNCTION='__attribute__((used,section(".ramcode")))'
  APP_OBJS+=hifive1_write_page.o
endif

ifeq ($(TARGET),mpfs250)
  LSCRIPT_TEMPLATE:=RISCV64-mpfs250.ld
  CFLAGS+=-DRAMFUNCTION='__attribute__((used,section(".ramcode")))'
endif

# $(CYPRESS_PDL)/devices/templates/COMPONENT_MTB/COMPONENT_CM0P/system_psoc6_cm0plus.o

ifeq ($(TARGET),psoc6)
  LSCRIPT_TEMPLATE:=ARM-psoc6.ld
  APP_OBJS+= $(CYPRESS_PDL)/drivers/source/cy_gpio.o
  APP_OBJS+= $(CYPRESS_PDL)/drivers/source/cy_device.o
  CFLAGS+=-I$(CYPRESS_PDL)/drivers/include/
  CFLAGS+=-I$(CYPRESS_PDL)/devices/include
  CFLAGS+=-I$(CYPRESS_PDL)/cmsis/include
  CFLAGS+=-I$(CYPRESS_TARGET_LIB)
  CFLAGS+=-I$(CYPRESS_CORE_LIB)/include
  CFLAGS+=-I$(CYPRESS_PDL)/devices/include/ip
  CFLAGS+=-I$(CYPRESS_PDL)/devices/templates/COMPONENT_MTB
  CFLAGS+=-DCY8C624ABZI_D44
endif

ifeq ($(TARGET),x86_fsp_qemu)
  APP_OBJS:=app_$(TARGET).o ../hal/x86_uart.o ../src/x86/common.o ../src/string.o
  LSCRIPT_TEMPLATE:=x86_fsp.ld
  LDFLAGS=
endif

ifeq ($(TARGET),versal)
  LSCRIPT_TEMPLATE:=AARCH64.ld
  LDFLAGS+=-nostdlib
  # Enable DEBUG_UART for test-app to use wolfBoot_printf and hal functions
  DEBUG_UART:=1
  CFLAGS+=-DDEBUG_UART
endif

ifeq ($(TARGET),zynq)
  LSCRIPT_TEMPLATE:=AARCH64.ld
endif

ifeq ($(TARGET),pic32ck)
  APP_OBJS+=../hal/pic32c.o
endif

ifeq ($(TARGET), pic32cz)
  APP_OBJS+=../hal/pic32c.o
endif

CFLAGS+=-I"$(WOLFBOOT_LIB_WOLFSSL)"

ifeq ($(TARGET),aurix_tc3xx)
  # Add BSP objects (same as in arch.mk for wolfBoot)
  APP_OBJS += $(TC3_DIR)/src/tc3_clock.o \
              $(TC3_DIR)/src/tc3_flash.o \
              $(TC3_DIR)/src/tc3_gpio.o \
              $(TC3_DIR)/src/tc3_uart.o \
              $(TC3_DIR)/src/tc3.o

  ifeq ($(AURIX_TC3_HSM),1)
    APP_OBJS += $(TC3_DIR)/src/tc3arm_crt.o \
                $(TC3_DIR)/src/tc3arm.o
	# Use TC3 Arm app linker script
    LSCRIPT_TEMPLATE = tc3arm_app.ld
  else
    APP_OBJS += $(TC3_DIR)/src/tc3tc_isr.o \
                $(TC3_DIR)/src/tc3tc_traps.o \
                $(TC3_DIR)/src/tc3tc.o \
                $(TC3_DIR)/src/tc3tc_crt.o
	# Use TC3 app linker script
    LSCRIPT_TEMPLATE = tc3tc_app.ld
  endif


  ifeq ($(WOLFHSM_SERVER),1)
    APP_OBJS += $(WOLFBOOT_LIB_WOLFHSM)/src/wh_transport_mem.o
    APP_OBJS += $(WOLFHSM_INFINEON_TC3XX)/port/server/port_halflash_df1.o
  endif
endif

# Capture final flags for locally built wolfSSL objects.
WOLFSSL_CFLAGS:=$(CFLAGS)

ifeq ($(WOLFHSM_CLIENT),1)
  CFLAGS += -DSTRING_USER -I"$(WOLFBOOT_LIB_WOLFSSL)"
  APP_OBJS += $(WOLFHSM_OBJS)
  APP_OBJS += $(sort $(patsubst $(WOLFBOOT_LIB_WOLFSSL)/%, $(WOLFBOOT_LIB_WOLFSSL)/%, $(WOLFCRYPT_OBJS)))
endif

ifeq ($(WOLFHSM_SERVER),1)
  CFLAGS += -DSTRING_USER -I"$(WOLFBOOT_LIB_WOLFSSL)"
  APP_OBJS += $(WOLFHSM_OBJS)
  APP_OBJS += $(sort $(patsubst $(WOLFBOOT_LIB_WOLFSSL)/%, $(WOLFBOOT_LIB_WOLFSSL)/%, $(WOLFCRYPT_OBJS)))
endif


standalone:CFLAGS+=-D"TEST_APP_STANDALONE"
standalone:LDFLAGS:=-T standalone.ld -Wl,-gc-sections -Wl,-Map=image.map

delta-extra-data:CFLAGS+=-D"TEST_DELTA_DATA=$(DELTA_DATA_SIZE)" -ffunction-sections -fdata-sections
delta-extra-data:LDFLAGS=-Wl,-Map=image.map

image.bin: image.elf
	@echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O binary $^ $@

image.hex: image.elf
	@echo "\t[HEX] $@"
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O ihex $^ $@

image.srec: image.elf
	@echo "\t[SREC] $@"
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) -O srec $^ $@

APP_OBJS := $(patsubst $(WOLFBOOT_LIB_WOLFSSL)/%, \
    $(WOLFSSL_LOCAL_OBJDIR)/%, $(APP_OBJS))

ifeq ($(ELF_FLASH_SCATTER),1)
# When ELF_FLASH_SCATTER=1, preprocess the ELF file with the squashelf tool
SQUASHELF_TOOL = ../tools/squashelf/squashelf
image-orig.elf: $(APP_OBJS) $(LSCRIPT)
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) $(APP_OBJS) $(OUTPUT_FLAG) $@

image.elf: image-orig.elf
	@echo "\t[SQUASHELF] $@"
	$(Q)$(SQUASHELF_TOOL) -v --nosht $< $@
else
# Default behavior when ELF_FLASH_SCATTER is not set
image.elf: $(APP_OBJS) $(LSCRIPT)
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) $(APP_OBJS) $(OUTPUT_FLAG) $@
endif

standalone: image.bin

delta-extra-data: image.bin

../test-app/libwolfboot.o: ../src/libwolfboot.c FORCE
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ ../src/libwolfboot.c

../hal/$(TARGET).o: ../hal/$(TARGET).c FORCE
	$(Q)$(CC) $(CFLAGS) -c -o $(@) ../hal/$(TARGET).c

../hal/$(TARGET)_ns.o: ../hal/$(TARGET).c FORCE
	$(Q)$(CC) $(CFLAGS) -c -o $(@) ../hal/$(TARGET).c -DNONSECURE_APP

../hal/spi/spi_drv_$(SPI_TARGET)_ns.o: ../hal/spi/spi_drv_$(SPI_TARGET).c FORCE
	$(Q)$(CC) $(CFLAGS) -c -o $(@) ../hal/spi/spi_drv_$(SPI_TARGET).c -DNONSECURE_APP

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

$(WOLFSSL_LOCAL_OBJDIR)/%.o: %.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(WOLFSSL_CFLAGS) -c $(OUTPUT_FLAG) $@ $<

clean:
	$(Q)rm -f *.bin *.elf tags *.o $(LSCRIPT) $(APP_OBJS) wcs/*.o
	$(Q)rm -rf $(WOLFSSL_LOCAL_OBJDIR)

$(LSCRIPT): $(LSCRIPT_TEMPLATE) FORCE
	$(Q)printf "%d" $(WOLFBOOT_PARTITION_BOOT_ADDRESS) > .wolfboot-offset
	$(Q)printf "%d" $(WOLFBOOT_PARTITION_SIZE) > .partition-size
	$(Q)printf "%d" $(IMAGE_HEADER_SIZE) > .header-size
ifeq ($(ELF_FLASH_SCATTER),1)
# Optionally set the base address for the test-app linker MEMORY{} region if the
# HAL allows for it to be specified in the config via WOLFBOOT_ELF_EXEC_ADDRESS
ifdef WOLFBOOT_ELF_EXEC_ADDRESS
	# For ELF scatter mode, use execution space address directly (with no header offset)
	$(Q)printf "%d" $(WOLFBOOT_ELF_EXEC_ADDRESS) > .entry-point
	$(Q)printf "0x%X" `cat .entry-point` > .entry-point
	$(Q)printf "%d" $(WOLFBOOT_ELF_EXEC_SIZE) > .app-size
	$(Q)printf "0x%X" `cat .app-size` > .app-size
endif
else
	# Standard mode: boot partition address + header offset
	$(Q)expr `cat .wolfboot-offset` + `cat .header-size` > .entry-point
	$(Q)printf "0x%X" `cat .entry-point` > .entry-point
	$(Q)expr `cat .partition-size` - `cat .header-size` > .app-size
	$(Q)printf "0x%X" `cat .app-size` > .app-size
endif
	$(Q)cat $(LSCRIPT_TEMPLATE) | \
		sed -e "s/@WOLFBOOT_TEST_APP_SIZE@/`cat .app-size`/g" | \
		sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/`cat .entry-point`/g" | \
		sed -e "s/@WOLFBOOT_LOAD_ADDRESS@/$(WOLFBOOT_LOAD_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_BOOT_ADDRESS@/$(WOLFBOOT_PARTITION_BOOT_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_SIZE@/$(WOLFBOOT_PARTITION_SIZE)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_UPDATE_ADDRESS@/$(WOLFBOOT_PARTITION_UPDATE_ADDRESS)/g" | \
		sed -e "s/@WOLFBOOT_PARTITION_SWAP_ADDRESS@/$(WOLFBOOT_PARTITION_SWAP_ADDRESS)/g" \
		> $(@)
	$(Q)rm -f .app-size .entry-point .wolfboot-offset .partition-size .header-size




FORCE:

.PHONY: FORCE clean
