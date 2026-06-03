ARCH_FLASH_OFFSET=0x08000000
LSCRIPT_IN=hal/stm32wb.ld

CFLAGS+=-DWHAL_CFG_NO_TIMEOUT

# Upstream wolfHAL drivers from lib/wolfHAL/src/. wolfBoot's hal_flash_*
# contract is satisfied by hal/wolfhal.c (added automatically because
# WOLFHAL=1) calling whal_Flash_*.

CFLAGS+=-DWHAL_CFG_STM32WB_FLASH_DIRECT_API_MAPPING
CFLAGS+=-DWHAL_CFG_STM32WB_GPIO_DIRECT_API_MAPPING
CFLAGS+=-DWHAL_CFG_STM32WB_UART_DIRECT_API_MAPPING
# UART driver is single-instance — reads whal_Stm32wb_Uart_Dev from board.h
# instead of the passed dev pointer.
CFLAGS+=-DWHAL_CFG_STM32WB_UART_SINGLE_INSTANCE

WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/reg.o
WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/flash/stm32wb_flash.o
ifeq ($(DEBUG_UART),1)
  WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/gpio/stm32wb_gpio.o
  WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/uart/stm32wb_uart.o
endif

OBJS+=$(WOLFHAL_OBJS)
APP_OBJS+=$(WOLFHAL_OBJS)
