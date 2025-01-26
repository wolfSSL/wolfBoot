/* app_stm32f1.c
 *
 * Test bare-metal application for Blue Pill (STM32F103) board.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#include <stdint.h>

#include "hal.h"
#include "system.h"
#include "target.h"

#include "wolfboot/wolfboot.h"

/*** LED ***/
#define RCC_BASE    (0x40021000U)
#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB2ENR_IOPCEN  (1 << 4)

#define GPIOC_BASE          (0x40011000)
#define GPIOC_CRL           (*(volatile uint32_t *)(GPIOC_BASE + 0x00U))
#define GPIOC_CRH           (*(volatile uint32_t *)(GPIOC_BASE + 0x04U))
#define GPIOC_IDR           (*(volatile uint32_t *)(GPIOC_BASE + 0x08U))
#define GPIOC_ODR           (*(volatile uint32_t *)(GPIOC_BASE + 0x0CU))
#define GPIOC_BSRR          (*(volatile uint32_t *)(GPIOC_BASE + 0x10U))
#define GPIOC_BRR           (*(volatile uint32_t *)(GPIOC_BASE + 0x14U))

/* Register values */
#define GPIOx_CRL_MASK(pin)         (0xF << ((pin) * 4))
#define GPIOx_CRH_MASK(pin)         (0xF << (((pin) - 8) * 4))

#define GPIOx_CRL_CNF(pin, cnf)     ((cnf) << (2 + ((pin) * 4)))
#define GPIOx_CRH_CNF(pin, cnf)     ((cnf) << (2 + (((pin) - 8) * 4)))
#define GPIOx_CRy_CNF_ANALOG        (0)
#define GPIOx_CRy_CNF_FLOATING      (1)
#define GPIOx_CRy_CNF_PULL          (2)
#define GPIOx_CRy_CNF_OUTPUT_PP     (0)
#define GPIOx_CRy_CNF_OUTPUT_OD     (1)
#define GPIOx_CRy_CNF_AF_PP         (2)
#define GPIOx_CRy_CNF_AF_OD         (3)

#define GPIOx_CRL_MODE(pin, mode) ((mode) << ((pin) * 4))
#define GPIOx_CRH_MODE(pin, mode) ((mode) << (((pin) - 8) * 4))
#define GPIOx_CRy_MODE_INPUT        (0)
#define GPIOx_CRy_MODE_OUT_10_MHZ   (1)
#define GPIOx_CRy_MODE_OUT_2_MHZ    (2)
#define GPIOx_CRy_MODE_OUT_50_MHZ   (3)

static void led_init(void)
{
    uint32_t reg32;

    /* Use PC13 as led output as it's connected to the onboard LED of the
     * Blue Pill board. */

    /* Enable GPIOC clock */
    RCC_APB2ENR |= RCC_APB2ENR_IOPCEN;

    /* Configure pin PC13 as slow (2 MHz) output */
    reg32 = GPIOC_CRH;
    reg32 &= ~(GPIOx_CRH_MASK(13));
    reg32 |= GPIOx_CRH_CNF(13, GPIOx_CRy_CNF_OUTPUT_PP);
    reg32 |= GPIOx_CRH_MODE(13, GPIOx_CRy_MODE_OUT_2_MHZ);
    GPIOC_CRH = reg32;

    /* Set PC13 high to turn led OFF */
    GPIOC_BSRR |= 1 << 13;
}

void led_toggle(void)
{
    if (GPIOC_IDR & (1 << 13))
        GPIOC_BRR |= 1 << 13;
    else
        GPIOC_BSRR |= 1 << 13;
}

void main(void) {
    hal_init();
    led_init();

    switch (wolfBoot_current_firmware_version()) {
    case 1:
        wolfBoot_success();
        wolfBoot_update_trigger();
        led_toggle();
        break;
    case 2:
        wolfBoot_success();
        led_toggle();
        break;
    default:
        break;
    }

    while(1)
        WFI();
}
