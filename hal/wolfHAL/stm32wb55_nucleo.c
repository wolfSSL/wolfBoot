#include <wolfHAL/platform/st/stm32wb55xx.h>

/*
 * STM32WB55 Nucleo board configuration for wolfHAL.
 *
 * This file wires up the board-level clock, flash, GPIO, and optional UART
 * instances used by wolfBoot. Most values are board defaults; override with
 * build-time defines (e.g., DEBUG_UART/UART_FLASH) as needed.
 */

whal_Clock wbClockController;
whal_Flash wbFlash;

/* Core clock controller (MSI -> PLL -> SYSCLK at 64 MHz). */
whal_Clock wbClockController = {
    WHAL_STM32WB55_RCC_PLL_DEVICE,

    .cfg = &(whal_StRcc_Cfg) {
        .flash = &wbFlash,
        .flashLatency = WHAL_ST_FLASH_LATENCY_3,

        .sysClkSrc = WHAL_ST_RCC_SYSCLK_SRC_PLL,
        .sysClkCfg = &(whal_StRcc_PllClkCfg)
        {
            .clkSrc = WHAL_ST_RCC_PLLCLK_SRC_MSI,
            /* 64 MHz */
            .n = 32,
            .m = 0,
            .r = 1,
            .q = 0,
            .p = 0,
        },
    },
};

/* Internal flash mapping used by wolfBoot. */
whal_Flash wbFlash = {
    WHAL_STM32WB55_FLASH_DEVICE,

    .cfg = &(whal_StFlash_Cfg) {
        .clkCtrl = &wbClockController,
        .clk = &(whal_StRcc_Clk){WHAL_STM32WB55_FLASH_CLOCK},

        .startAddr = 0x08000000,
        .size = 0x100000,
    },
};

/* GPIO pin configuration: LED on PB5 and optional UART1 pins. */
whal_StGpio_PinCfg pinCfg[] = {
        { /* LED */
            .port = WHAL_STGPIO_PORT_B,
            .pin = 5,
            .mode = WHAL_STGPIO_MODE_OUT,
            .outType = WHAL_STGPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STGPIO_SPEED_LOW,
            .pull = WHAL_STGPIO_PULL_UP,
            .altFn = 0,
        },
#if defined(DEBUG_UART) || defined(UART_FLASH)
        { /* UART1 TX */
            .port = WHAL_STGPIO_PORT_B,
            .pin = 6,
            .mode = WHAL_STGPIO_MODE_ALTFN,
            .outType = WHAL_STGPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STGPIO_SPEED_FAST,
            .pull = WHAL_STGPIO_PULL_UP,
            .altFn = 7,
        },
        { /* UART1 RX */
            .port = WHAL_STGPIO_PORT_B,
            .pin = 7,
            .mode = WHAL_STGPIO_MODE_ALTFN,
            .outType = WHAL_STGPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STGPIO_SPEED_FAST,
            .pull = WHAL_STGPIO_PULL_UP,
            .altFn = 7,
        },
#endif /* DEBUG_UART || UART_FLASH */
};

/* GPIO controller for configured pins. */
whal_Gpio wbGpio = {
    WHAL_STM32WB55_GPIO_DEVICE,

    .cfg = &(whal_StGpio_Cfg) {
        .clkCtrl = &wbClockController,
        .clk = &(whal_StRcc_Clk) {WHAL_STM32WB55_GPIOB_CLOCK},

        .pinCfg = pinCfg,
        .pinCount = sizeof(pinCfg) / sizeof(whal_StGpio_PinCfg),
    },
};

#if defined(DEBUG_UART) || defined(UART_FLASH)
/* UART1 configuration for debug/flash operations. */
whal_Uart wbUart = {
    WHAL_STM32WB55_UART1_DEVICE,

    .cfg = &(whal_StUart_Cfg){
        .clkCtrl = &wbClockController,
        .clk = &(whal_StRcc_Clk) {WHAL_STM32WB55_UART1_CLOCK},

        .baud = 115200,
    },
};
#endif /* DEBUG_UART || UART_FLASH */
