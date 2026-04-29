/* board.c
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

#include "hal.h"
#include "board.h"
#include <wolfHAL/platform/st/stm32wb55xx.h>

/* Clock */
whal_Clock g_wbClock = {
    .regmap = { .base = 0x58000000, .size = 0x400 },

    .cfg = &(whal_Stm32wbRcc_Cfg) {
        .sysClkSrc = WHAL_STM32WB_RCC_SYSCLK_SRC_PLL,
        .sysClkCfg = &(whal_Stm32wbRcc_PllClkCfg) {
            .clkSrc = WHAL_STM32WB_RCC_PLLCLK_SRC_MSI,
            /* 64 MHz: (4 MHz MSI / 1) * 32 / 2 = 64 MHz */
            .n = 32,
            .m = 0,
            .r = 1,
            .q = 0,
            .p = 0,
        },
    },
};

static const whal_Stm32wbRcc_Clk flashClk = {WHAL_STM32WB55_FLASH_CLOCK};

/* Flash */
whal_Flash g_wbFlash = {
    .regmap = { .base = 0x58004000, .size = 0x400 },

    .cfg = &(whal_Stm32wbFlash_Cfg) {
        .startAddr = 0x08000000,
        .size = 0x100000,
    },
};

#ifdef DEBUG_UART

static const whal_Stm32wbRcc_Clk g_clocks[] = {
    {WHAL_STM32WB55_GPIOB_CLOCK},
    {WHAL_STM32WB55_UART1_CLOCK},
};
#define CLOCK_COUNT (sizeof(g_clocks) / sizeof(g_clocks[0]))

/* GPIO — LED on PB5, UART1 TX/RX on PB6/PB7 */
whal_Gpio g_wbGpio = {
    .regmap = { .base = 0x48000000, .size = 0x400 },

    .cfg = &(whal_Stm32wbGpio_Cfg) {
        .pinCfg = (whal_Stm32wbGpio_PinCfg[]) {
            [BOARD_LED_PIN] = WHAL_STM32WB_GPIO_PIN(
                WHAL_STM32WB_GPIO_PORT_B, 5, WHAL_STM32WB_GPIO_MODE_OUT,
                WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WB_GPIO_SPEED_LOW,
                WHAL_STM32WB_GPIO_PULL_UP, 0),
            [BOARD_UART_TX_PIN] = WHAL_STM32WB_GPIO_PIN(
                WHAL_STM32WB_GPIO_PORT_B, 6, WHAL_STM32WB_GPIO_MODE_ALTFN,
                WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WB_GPIO_SPEED_FAST,
                WHAL_STM32WB_GPIO_PULL_UP, 7),
            [BOARD_UART_RX_PIN] = WHAL_STM32WB_GPIO_PIN(
                WHAL_STM32WB_GPIO_PORT_B, 7, WHAL_STM32WB_GPIO_MODE_ALTFN,
                WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WB_GPIO_SPEED_FAST,
                WHAL_STM32WB_GPIO_PULL_UP, 7),
        },
        .pinCount = BOARD_PIN_COUNT,
    },
};

/* UART1 at 115200 baud */
whal_Uart g_wbUart = {
    .regmap = { .base = 0x40013800, .size = 0x400 },

    .cfg = &(whal_Stm32wbUart_Cfg) {
        .brr = WHAL_STM32WB_UART_BRR(64000000, 115200),
    },
};

#endif /* DEBUG_UART */

void hal_init(void)
{
    /* Enable flash clock and set latency before increasing clock speed */
    whal_Clock_Enable(&g_wbClock, &flashClk);
    whal_Stm32wbFlash_Ext_SetLatency(&g_wbFlash, WHAL_STM32WB_FLASH_LATENCY_3);

    whal_Clock_Init(&g_wbClock);
    whal_Flash_Init(&g_wbFlash);

#ifdef DEBUG_UART
    for (size_t i = 0; i < CLOCK_COUNT; i++) {
        whal_Clock_Enable(&g_wbClock, &g_clocks[i]);
    }

    whal_Gpio_Init(&g_wbGpio);
    whal_Uart_Init(&g_wbUart);
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    whal_Uart_Deinit(&g_wbUart);
    whal_Gpio_Deinit(&g_wbGpio);

    for (size_t i = 0; i < CLOCK_COUNT; i++) {
        whal_Clock_Disable(&g_wbClock, &g_clocks[i]);
    }
#endif

    whal_Flash_Deinit(&g_wbFlash);
    whal_Clock_Deinit(&g_wbClock);

    /* Reduce flash latency then disable flash clock */
    whal_Stm32wbFlash_Ext_SetLatency(&g_wbFlash, WHAL_STM32WB_FLASH_LATENCY_0);
    whal_Clock_Disable(&g_wbClock, &flashClk);
}
