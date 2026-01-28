OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/reg.o

OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/clock.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/uart/uart.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/gpio/gpio.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/flash/flash.o


OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/st_rcc.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/uart/st_uart.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/gpio/st_gpio.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/flash/st_flash.o

APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/reg.o

APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/clock.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/uart/uart.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/gpio/gpio.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/flash/flash.o


APP_OBJS += $(WOLFBOOT_LIB_WOLFHAL)/src/clock/st_rcc.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/uart/st_uart.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/gpio/st_gpio.o 	\
		$(WOLFBOOT_LIB_WOLFHAL)/src/flash/st_flash.o
