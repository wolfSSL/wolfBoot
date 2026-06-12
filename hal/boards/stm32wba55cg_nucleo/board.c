/* board.c
 *
 * STM32WBA55CG Nucleo board configuration using upstream wolfHAL drivers.
 * Flash/Gpio/Uart singletons are instantiated by the driver .c files from
 * the WHAL_CFG_* initializer macros in board.h; board.c uses the
 * BOARD_*_DEV handles for the rest. RCC is header-inlined and hardcodes
 * its base — no dev pointer needed.
 *
 * Clock target: HSE32 -> PLL1 (M=1, N=25, R=3) -> SYSCLK = 100 MHz, which
 * requires PWR voltage scaling Range 1 and 3 flash wait states (RM0493).
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

/* Flash clock gate — enabled before raising the wait-state count. */
static const whal_Stm32wba_Rcc_PeriphClk g_flashClock = {WHAL_STM32WBA55_FLASH_CLOCK};

#ifdef DEBUG_UART
/* GPIO ports A (LED PA9, USART1 RX PA8) and B (USART1 TX PB12) plus USART1. */
static const whal_Stm32wba_Rcc_PeriphClk g_periphClks[] = {
    {WHAL_STM32WBA55_GPIOA_CLOCK},
    {WHAL_STM32WBA55_GPIOB_CLOCK},
    {WHAL_STM32WBA55_USART1_CLOCK},
};
#define PERIPH_CLK_COUNT (sizeof(g_periphClks) / sizeof(g_periphClks[0]))
#endif

/*
 * Switch PWR voltage scaling to Range 1 (required for >16 MHz operation).
 * After reset the device is in Range 2 (max 16 MHz). Must switch to Range 1
 * before configuring PLL or increasing SYSCLK.
 *
 * PWR base: 0x46020800, PWR_VOSR offset 0x00C
 *   bit 16 VOS: 0=Range2, 1=Range1
 *   bit 15 VOSRDY: read-only, 1 when stable
 */
#define PWR_BASE            0x46020800
#define PWR_VOSR_REG        0x00C
#define PWR_VOSR_VOS_Msk    (1UL << 16)
#define PWR_VOSR_VOSRDY_Msk (1UL << 15)

static void set_vos_range1(void)
{
    whal_Reg_Update(PWR_BASE, PWR_VOSR_REG, PWR_VOSR_VOS_Msk, PWR_VOSR_VOS_Msk);
    while (!(whal_Reg_Read(PWR_BASE, PWR_VOSR_REG) & PWR_VOSR_VOSRDY_Msk))
        ;
}

/* HSE32 -> PLL1 (M=1, N=25, R=3 -> 100 MHz) -> SYSCLK */
static void pll_clock_on(void)
{
    /* Enable PWR clock so the PWR registers are accessible, then move to
     * voltage Range 1 (Range 2 after reset caps SYSCLK at 16 MHz). */
    static const whal_Stm32wba_Rcc_PeriphClk pwrClock = {WHAL_STM32WBA55_PWR_CLOCK};
    whal_Stm32wba_Rcc_EnablePeriphClk(&pwrClock);
    set_vos_range1();

    /* Enable flash clock and set latency before increasing clock speed.
     * 100 MHz @ 3.3V needs 3 wait states (RM0493 Table 69). */
    whal_Stm32wba_Rcc_EnablePeriphClk(&g_flashClock);
    whal_Stm32wba_Flash_Ext_SetLatency(BOARD_FLASH_DEV, 3);

    /* AHB5 max 32 MHz: prescale 100 MHz / 4 = 25 MHz (0b101 = div 4). */
    whal_Stm32wba_Rcc_SetHpre5(5);

    whal_Stm32wba_Rcc_EnableOsc(
        &(whal_Stm32wba_Rcc_OscCfg){WHAL_STM32WBA_RCC_HSE32_CFG});
    whal_Stm32wba_Rcc_EnablePll1(&(whal_Stm32wba_Rcc_Pll1Cfg){
        .clkSrc = WHAL_STM32WBA_RCC_PLL1SRC_HSE32,
        .rge = WHAL_STM32WBA_RCC_PLL1RGE_8_16,
        .m = 1, .n = 25, .r = 3, .q = 0, .p = 0,
    });
    whal_Stm32wba_Rcc_SetSysClock(WHAL_STM32WBA_RCC_SYSCLK_SRC_PLL1);
}

static void pll_clock_off(void)
{
    whal_Stm32wba_Rcc_SetSysClock(WHAL_STM32WBA_RCC_SYSCLK_SRC_HSI16);
    whal_Stm32wba_Rcc_DisablePll1();
    whal_Stm32wba_Rcc_DisableOsc(
        &(whal_Stm32wba_Rcc_OscCfg){WHAL_STM32WBA_RCC_HSE32_CFG});
    whal_Stm32wba_Flash_Ext_SetLatency(BOARD_FLASH_DEV, 1);
}

void hal_init(void)
{
    pll_clock_on();
    whal_Flash_Init(BOARD_FLASH_DEV);

#ifdef DEBUG_UART
    for (size_t i = 0; i < PERIPH_CLK_COUNT; i++) {
        whal_Stm32wba_Rcc_EnablePeriphClk(&g_periphClks[i]);
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
        whal_Stm32wba_Rcc_DisablePeriphClk(&g_periphClks[i]);
    }
#endif

    whal_Flash_Deinit(BOARD_FLASH_DEV);
    pll_clock_off();
}
