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

#include <stddef.h>
#include "hal.h"
#include "board.h"
#include <wolfHAL/platform/st/stm32wb55xx.h>

/* Clock */
whal_Clock g_wbClock = {
    .regmap = { WHAL_STM32WB55_RCC_REGMAP },
};

/* Flash */
whal_Flash g_wbFlash = {
    .regmap = { WHAL_STM32WB55_FLASH_REGMAP },

    .cfg = &(whal_Stm32wb_Flash_Cfg) {
        .startAddr = 0x08000000,
        .size = 0x100000,
    },
};

#ifdef DEBUG_UART

static const whal_Stm32wb_Rcc_PeriphClk g_periphClks[] = {
    {WHAL_STM32WB55_GPIOB_GATE},
    {WHAL_STM32WB55_UART1_GATE},
};
#define PERIPH_CLK_COUNT (sizeof(g_periphClks) / sizeof(g_periphClks[0]))

/* GPIO — LED on PB5, UART1 TX/RX on PB6/PB7 */
whal_Gpio g_wbGpio = {
    .regmap = { WHAL_STM32WB55_GPIO_REGMAP },

    .cfg = &(whal_Stm32wb_Gpio_Cfg) {
        .pinCfg = (whal_Stm32wb_Gpio_PinCfg[]) {
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
    .regmap = { WHAL_STM32WB55_UART1_REGMAP },

    .cfg = &(whal_Stm32wb_Uart_Cfg) {
        .brr = WHAL_STM32WB_UART_BRR(64000000, 115200),
    },
};

#endif /* DEBUG_UART */

void hal_init(void)
{
    /* Flash latency must be set before SYSCLK rises above ~16 MHz. */
    whal_Stm32wb_Flash_Ext_SetLatency(&g_wbFlash, WHAL_STM32WB_FLASH_LATENCY_3);

    /* MSI 4 MHz -> PLL VCO 128 MHz -> PLLR /2 = 64 MHz -> SYSCLK */
    whal_Stm32wb_Rcc_EnablePll(&g_wbClock, &(whal_Stm32wb_Rcc_PllCfg){
        .clkSrc = WHAL_STM32WB_RCC_PLLCLK_SRC_MSI,
        .n = 32, .m = 0, .r = 1, .q = 0, .p = 0,
    });
    whal_Stm32wb_Rcc_SetSysClock(&g_wbClock, WHAL_STM32WB_RCC_SYSCLK_SRC_PLL);

    whal_Flash_Init(&g_wbFlash);

#ifdef DEBUG_UART
    for (size_t i = 0; i < PERIPH_CLK_COUNT; i++) {
        whal_Stm32wb_Rcc_EnablePeriphClk(&g_wbClock, &g_periphClks[i]);
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

    for (size_t i = PERIPH_CLK_COUNT; i-- > 0; ) {
        whal_Stm32wb_Rcc_DisablePeriphClk(&g_wbClock, &g_periphClks[i]);
    }
#endif

    whal_Flash_Deinit(&g_wbFlash);

    whal_Stm32wb_Rcc_SetSysClock(&g_wbClock, WHAL_STM32WB_RCC_SYSCLK_SRC_MSI);
    whal_Stm32wb_Rcc_DisablePll(&g_wbClock);

    whal_Stm32wb_Flash_Ext_SetLatency(&g_wbFlash, WHAL_STM32WB_FLASH_LATENCY_0);
}
