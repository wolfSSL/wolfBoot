/* board.c
 *
 * STM32WB55 Nucleo board configuration using upstream wolfHAL drivers.
 * Gpio/Flash/Uart singletons are instantiated by the driver .c files
 * from WHAL_CFG_STM32WB_*_DEV initializer macros in board.h; board.c
 * uses the BOARD_*_DEV handles for the rest. RCC is header-inlined and
 * hardcodes WHAL_STM32WB_RCC_BASE — no dev pointer needed.
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

#ifdef DEBUG_UART
static const whal_Stm32wb_Rcc_PeriphClk g_periphClks[] = {
    {WHAL_STM32WB55_GPIOB_GATE},
    {WHAL_STM32WB55_UART1_GATE},
};
#define PERIPH_CLK_COUNT (sizeof(g_periphClks) / sizeof(g_periphClks[0]))
#endif

/* MSI 4 MHz -> PLL VCO 128 MHz -> PLLR /2 = 64 MHz -> SYSCLK */
static void pll_clock_on(void)
{
    whal_Stm32wb_Flash_Ext_SetLatency(BOARD_FLASH_DEV, WHAL_STM32WB_FLASH_LATENCY_3);
    whal_Stm32wb_Rcc_EnablePll(&(whal_Stm32wb_Rcc_PllCfg){
        .clkSrc = WHAL_STM32WB_RCC_PLLCLK_SRC_MSI,
        .n = 32, .m = 0, .r = 1, .q = 0, .p = 0,
    });
    whal_Stm32wb_Rcc_SetSysClock(WHAL_STM32WB_RCC_SYSCLK_SRC_PLL);
}

static void pll_clock_off(void)
{
    whal_Stm32wb_Rcc_SetSysClock(WHAL_STM32WB_RCC_SYSCLK_SRC_MSI);
    whal_Stm32wb_Rcc_DisablePll();
    whal_Stm32wb_Flash_Ext_SetLatency(BOARD_FLASH_DEV, WHAL_STM32WB_FLASH_LATENCY_0);
}

void hal_init(void)
{
    pll_clock_on();
    whal_Flash_Init(BOARD_FLASH_DEV);

#ifdef DEBUG_UART
    for (size_t i = 0; i < PERIPH_CLK_COUNT; i++) {
        whal_Stm32wb_Rcc_EnablePeriphClk(&g_periphClks[i]);
    }

    whal_Gpio_Init(BOARD_GPIO_DEV);
    whal_Uart_Init(BOARD_UART_DEV);
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    whal_Uart_Deinit(BOARD_UART_DEV);
    whal_Gpio_Deinit(BOARD_GPIO_DEV);

    for (size_t i = PERIPH_CLK_COUNT; i-- > 0; ) {
        whal_Stm32wb_Rcc_DisablePeriphClk(&g_periphClks[i]);
    }
#endif

    whal_Flash_Deinit(BOARD_FLASH_DEV);
    pll_clock_off();
}
