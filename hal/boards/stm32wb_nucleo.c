/* stm32wb_nucleo.c
 *
 * wolfHAL board configuration for the STM32WB55 Nucleo
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <wolfHAL/clock/stm32wb_rcc.h>
#include <wolfHAL/flash/stm32wb_flash.h>
#ifdef DEBUG_UART
#include <wolfHAL/gpio/stm32wb_gpio.h>
#include <wolfHAL/uart/stm32wb_uart.h>
#endif
#include <wolfHAL/platform/st/stm32wb55xx.h>

/* Device instances — defined before configs to break the circular reference */
whal_Flash g_wbFlash;
whal_Clock g_wbClock;

/* PLL configuration */
static whal_Stm32wbRcc_PllClkCfg pllCfg = {
    .clkSrc = WHAL_STM32WB_RCC_PLLCLK_SRC_MSI,
    /* 64 MHz: (4 MHz MSI / 1) * 32 / 2 = 64 MHz */
    .n = 32,
    .m = 0,
    .r = 1,
    .q = 0,
    .p = 0,
};

/* Clock configuration — references flash for latency adjustment */
static whal_Stm32wbRcc_Cfg rccCfg = {
    .flash = &g_wbFlash,
    .flashLatency = WHAL_STM32WB_FLASH_LATENCY_3,
    .sysClkSrc = WHAL_STM32WB_RCC_SYSCLK_SRC_PLL,
    .sysClkCfg = &pllCfg,
};

/* Flash configuration — references clock for peripheral clock gating */
static whal_Stm32wbRcc_Clk flashClk = {WHAL_STM32WB55_FLASH_CLOCK};
static whal_Stm32wbFlash_Cfg flashCfg = {
    .clkCtrl = &g_wbClock,
    .clk = &flashClk,
    .startAddr = 0x08000000,
    .size = 0x100000,
};

/* Minimal clock driver vtable: Enable/Disable are used by the flash, GPIO, and
 * UART drivers for peripheral clock gating. GetRate is only needed by the UART
 * driver for baud rate calculation. */
static const whal_ClockDriver clockDriver = {
    .Enable = whal_Stm32wbRcc_Enable,
    .Disable = whal_Stm32wbRcc_Disable,
#ifdef DEBUG_UART
    .GetRate = whal_Stm32wbRccPll_GetRate,
#endif
};

/* Clock controller: PLL driven from MSI at 64 MHz */
whal_Clock g_wbClock = {
    .regmap = { .base = 0x58000000, .size = 0x400 },
    .driver = &clockDriver,
    .cfg = &rccCfg,
};

/* Flash device */
whal_Flash g_wbFlash = {
    .regmap = { .base = 0x58004000, .size = 0x400 },
    .cfg = &flashCfg,
};

#ifdef DEBUG_UART

/* GPIO pin indices */
enum {
    UART_TX_PIN,
    UART_RX_PIN,
};

/* GPIO — UART1 TX/RX on PB6/PB7 */
static whal_Stm32wbRcc_Clk gpiobClk = {WHAL_STM32WB55_GPIOB_CLOCK};
static whal_Stm32wbGpio_PinCfg gpioPins[] = {
    [UART_TX_PIN] = {
        .port = WHAL_STM32WB_GPIO_PORT_B,
        .pin = 6,
        .mode = WHAL_STM32WB_GPIO_MODE_ALTFN,
        .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
        .speed = WHAL_STM32WB_GPIO_SPEED_FAST,
        .pull = WHAL_STM32WB_GPIO_PULL_UP,
        .altFn = 7,
    },
    [UART_RX_PIN] = {
        .port = WHAL_STM32WB_GPIO_PORT_B,
        .pin = 7,
        .mode = WHAL_STM32WB_GPIO_MODE_ALTFN,
        .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
        .speed = WHAL_STM32WB_GPIO_SPEED_FAST,
        .pull = WHAL_STM32WB_GPIO_PULL_UP,
        .altFn = 7,
    },
};

static whal_Stm32wbGpio_Cfg gpioCfg = {
    .clkCtrl = &g_wbClock,
    .clk = (const void *[1]) { &gpiobClk },
    .clkCount = 1,
    .pinCfg = gpioPins,
    .pinCount = 2,
};

whal_Gpio g_wbGpio = {
    .regmap = { .base = 0x48000000, .size = 0x400 },
    .cfg = &gpioCfg,
};

/* UART1 at 115200 baud */
static whal_Stm32wbRcc_Clk uart1Clk = {WHAL_STM32WB55_UART1_CLOCK};
static whal_Stm32wbUart_Cfg uartCfg = {
    .clkCtrl = &g_wbClock,
    .clk = &uart1Clk,
    .baud = 115200,
};

whal_Uart g_wbUart = {
    .regmap = { .base = 0x40013800, .size = 0x400 },
    .cfg = &uartCfg,
};

#endif /* DEBUG_UART */
