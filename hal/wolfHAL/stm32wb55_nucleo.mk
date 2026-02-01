CFLAGS += -DWHAL_CFG_NO_CALLBACKS=1

OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/reg.o

OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/st_rcc.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/gpio/st_gpio.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/flash/st_flash.o

ifeq ($(DEBUG_UART),1)
	OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/uart/st_uart.o
endif

APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/reg.o

APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/st_rcc.o 	\
			$(WOLFBOOT_LIB_WOLFHAL)/src/gpio/st_gpio.o 	\
			$(WOLFBOOT_LIB_WOLFHAL)/src/flash/st_flash.o

ifeq ($(DEBUG_UART),1)
	APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/uart/st_uart.o
endif
