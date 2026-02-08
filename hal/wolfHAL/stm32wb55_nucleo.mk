CFLAGS += -DWHAL_CFG_NO_CALLBACKS=1

OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/reg.o

OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/stm32wb_rcc.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/flash/stm32wb_flash.o

APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/reg.o

APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/stm32wb_rcc.o 	\
			$(WOLFBOOT_LIB_WOLFHAL)/src/flash/stm32wb_flash.o

ifeq ($(WOLFHAL_NO_GPIO),)
	OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/gpio/stm32wb_gpio.o
	APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/gpio/stm32wb_gpio.o
endif

ifeq ($(DEBUG_UART),1)
	OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/uart/stm32wb_uart.o
	APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/uart/stm32wb_uart.o
endif
