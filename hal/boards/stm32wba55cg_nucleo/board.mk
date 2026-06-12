ARCH_FLASH_OFFSET=0x08000000
LSCRIPT_IN=hal/stm32wba.ld

CFLAGS+=-DWHAL_CFG_NO_TIMEOUT

# Upstream wolfHAL drivers from lib/wolfHAL/src/. wolfBoot's hal_flash_*
# contract is satisfied by hal/wolfhal.c (added automatically because
# WOLFHAL=1) calling whal_Flash_*.
#
# Direct API mapping binds each driver's functions straight to the
# top-level whal_* symbols (no vtable dispatch source compiled).

# Flash is a native STM32WBA driver — it checks the WBA-prefixed flag.
CFLAGS+=-DWHAL_CFG_STM32WBA_FLASH_DIRECT_API_MAPPING

# GPIO/UART reuse the STM32WB implementation (the stm32wba_*.c TUs include
# stm32wb_*.c). The UART wrapper forwards its WBA flags to the WB names, so
# it takes the WBA-prefixed flags. The GPIO wrapper does NOT forward, so the
# underlying stm32wb_gpio.c must be given the WB-prefixed flag directly.
CFLAGS+=-DWHAL_CFG_STM32WB_GPIO_DIRECT_API_MAPPING
CFLAGS+=-DWHAL_CFG_STM32WBA_UART_DIRECT_API_MAPPING
# UART is single-instance — reads its singleton from board.h instead of the
# passed dev pointer.
CFLAGS+=-DWHAL_CFG_STM32WBA_UART_SINGLE_INSTANCE

WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/reg.o
WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/flash/stm32wba_flash.o
ifeq ($(DEBUG_UART),1)
  WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/gpio/stm32wba_gpio.o
  WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/uart/stm32wba_uart.o
endif

OBJS+=$(WOLFHAL_OBJS)
APP_OBJS+=$(WOLFHAL_OBJS)

# With RAM_CODE=1, place the flash driver in RAM so erase/program runs while
# the same flash bank is being modified.
ifeq ($(RAM_CODE),1)
  WOLFHAL_FLASH_EXCLUDE_TEXT=*(EXCLUDE_FILE(*stm32wba_flash.o) .text*)
  WOLFHAL_FLASH_EXCLUDE_RODATA=*(EXCLUDE_FILE(*stm32wba_flash.o) .rodata*)
  WOLFHAL_FLASH_RAM_SECTIONS=*stm32wba_flash.o(.text* .rodata*)
endif
