/* system.c
 *
 * Test bare-metal blinking led application
 *
 * Copyright (C) 2020 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#if defined(PLATFORM_stm32f4) || defined(PLATFORM_stm32f7)
#include <stdint.h>
#include "system.h"

/*** FLASH ***/
#define FLASH_BASE (0x40023C00)
#define FLASH_ACR  (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_ENABLE_DATA_CACHE (1 << 10)
#define FLASH_ACR_ENABLE_INST_CACHE (1 << 9)

/*** RCC ***/

#define RCC_BASE (0x40023800)
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))

#define RCC_CR_PLLRDY               (1 << 25)
#define RCC_CR_PLLON                (1 << 24)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 1)
#define RCC_CR_HSION                (1 << 0)

#define RCC_CFGR_SW_HSI             0x0
#define RCC_CFGR_SW_HSE             0x1
#define RCC_CFGR_SW_PLL             0x2


#define RCC_PLLCFGR_PLLSRC          (1 << 22)

#define RCC_PRESCALER_DIV_NONE 0
#define RCC_PRESCALER_DIV_2    8
#define RCC_PRESCALER_DIV_4    9


/* STM32F4-Discovery, 168 MHz */
#ifdef PLATFORM_stm32f4
#  define PLLM 8
#  define PLLN 336
#  define PLLP 2 
#  define PLLQ 7
#  define PLLR 0
#  define TARGET_FLASH_WAITSTATES 5
#endif

/* STM32F7-Discovery, 216 MHz */
#ifdef PLATFORM_stm32f7
#  define PLLM 25
#  define PLLN 432
#  define PLLP 2
#  define PLLQ 9
#  define PLLR 0
#  define TARGET_FLASH_WAITSTATES 7
#endif

void flash_set_waitstates(void)
{
    FLASH_ACR |= TARGET_FLASH_WAITSTATES | FLASH_ACR_ENABLE_DATA_CACHE | FLASH_ACR_ENABLE_INST_CACHE;
}


void clock_config(void)
{
    uint32_t reg32;
    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */

    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Enable external high-speed oscillator 8MHz. */
    RCC_CR |= RCC_CR_HSEON;
    DMB();
    while ((RCC_CR & RCC_CR_HSERDY) == 0) {};

    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     */
    reg32 = RCC_CFGR;
    reg32 &= ~(0xF0);
    RCC_CFGR = (reg32 | (RCC_PRESCALER_DIV_NONE << 4));
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x1C00);
    RCC_CFGR = (reg32 | (RCC_PRESCALER_DIV_2 << 10));
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x07 << 13);
    RCC_CFGR = (reg32 | (RCC_PRESCALER_DIV_4 << 13));
    DMB();

    /* Set PLL config */
    reg32 = RCC_PLLCFGR;
    reg32 &= ~(PLL_FULL_MASK);
    RCC_PLLCFGR = reg32 | RCC_PLLCFGR_PLLSRC | PLLM |
        (PLLN << 6) | (((PLLP >> 1) - 1) << 16) |
        (PLLQ << 24);
    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /* Disable internal high-speed oscillator. */
    RCC_CR &= ~RCC_CR_HSION;
}

#endif /* PLATFORM_stm32f4 */
