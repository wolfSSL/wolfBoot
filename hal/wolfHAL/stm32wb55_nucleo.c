#include <wolfHAL/platform/st/stm32wb55xx.h>

/*
 * STM32WB55 Nucleo board configuration for wolfHAL.
 *
 * This file wires up the board-level clock, flash, GPIO, and optional UART
 * instances used by wolfBoot. Most values are board defaults; override with
 * build-time defines (e.g., DEBUG_UART/UART_FLASH) as needed.
 */

whal_Clock g_whalClock;
whal_Flash g_whalFlash;

/* Core clock controller (MSI -> PLL -> SYSCLK at 64 MHz). */
whal_Clock g_whalClock = {
    WHAL_STM32WB55_RCC_PLL_DEVICE,

    .cfg = &(whal_Stm32wbRcc_Cfg) {
        .flash = &g_whalFlash,
        .flashLatency = WHAL_STM32WB_FLASH_LATENCY_3,

        .sysClkSrc = WHAL_STM32WB_RCC_SYSCLK_SRC_PLL,
        .sysClkCfg = &(whal_Stm32wbRcc_PllClkCfg)
        {
            .clkSrc = WHAL_STM32WB_RCC_PLLCLK_SRC_MSI,
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
whal_Flash g_whalFlash = {
    WHAL_STM32WB55_FLASH_DEVICE,

    .cfg = &(whal_Stm32wbFlash_Cfg) {
        .clkCtrl = &g_whalClock,
        .clk = &(whal_Stm32wbRcc_Clk){WHAL_STM32WB55_FLASH_CLOCK},

        .startAddr = 0x08000000,
        .size = 0x100000,
    },
};

#ifndef WOLFHAL_NO_GPIO
/* GPIO pin configuration: LED on PB5 and optional UART1 pins. */
whal_Stm32wbGpio_PinCfg g_whalPinCfg[] = {
        { /* LED */
            .port = WHAL_STM32WB_GPIO_PORT_B,
            .pin = 5,
            .mode = WHAL_STM32WB_GPIO_MODE_OUT,
            .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STM32WB_GPIO_SPEED_LOW,
            .pull = WHAL_STM32WB_GPIO_PULL_UP,
            .altFn = 0,
        },
#if defined(DEBUG_UART) || defined(UART_FLASH)
        { /* UART1 TX */
            .port = WHAL_STM32WB_GPIO_PORT_B,
            .pin = 6,
            .mode = WHAL_STM32WB_GPIO_MODE_ALTFN,
            .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STM32WB_GPIO_SPEED_FAST,
            .pull = WHAL_STM32WB_GPIO_PULL_UP,
            .altFn = 7,
        },
        { /* UART1 RX */
            .port = WHAL_STM32WB_GPIO_PORT_B,
            .pin = 7,
            .mode = WHAL_STM32WB_GPIO_MODE_ALTFN,
            .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STM32WB_GPIO_SPEED_FAST,
            .pull = WHAL_STM32WB_GPIO_PULL_UP,
            .altFn = 7,
        },
#endif /* DEBUG_UART || UART_FLASH */
};

/* GPIO controller for configured pins. */
whal_Gpio g_whalGpio = {
    WHAL_STM32WB55_GPIO_DEVICE,

    .cfg = &(whal_Stm32wbGpio_Cfg) {
        .clkCtrl = &g_whalClock,
        .clk = &(whal_Stm32wbRcc_Clk) {WHAL_STM32WB55_GPIOB_CLOCK},

        .pinCfg = g_whalPinCfg,
        .pinCount = sizeof(g_whalPinCfg) / sizeof(whal_Stm32wbGpio_PinCfg),
    },
};
#endif

#if defined(DEBUG_UART) || defined(UART_FLASH)
/* UART1 configuration for debug/flash operations. */
whal_Uart g_whalUart = {
    WHAL_STM32WB55_UART1_DEVICE,

    .cfg = &(whal_Stm32wbUart_Cfg){
        .clkCtrl = &g_whalClock,
        .clk = &(whal_Stm32wbRcc_Clk) {WHAL_STM32WB55_UART1_CLOCK},

        .baud = 115200,
    },
};
#endif /* DEBUG_UART || UART_FLASH */
