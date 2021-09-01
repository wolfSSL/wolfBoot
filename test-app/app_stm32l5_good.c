/* stm32l5.c
 *
 * Test bare-metal application.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "system.h"
#include "hal.h"
#include <wolfboot/wolfboot.h>

#define LED_BOOT_PIN (12)  //PG12 - Discovery - Green Led
#define LED_USR_PIN (14) //PD3  - Discovery  - Red Led


/*Non-Secure */
#define RCC_BASE            (0x40021000)   //RM0438 - Table 4
#define PWR_BASE            (0x40007000)   //RM0438 - Table 4
#define GPIOD_BASE 0x42020C00
#define GPIOG_BASE 0x42021800

#define GPIOC_BASE 0x42020800
#define GPIOB_BASE 0x42020400
#define GPIOA_BASE 0x42020000

#define GPIOG_MODER  (*(volatile uint32_t *)(GPIOG_BASE + 0x00))
#define GPIOG_PUPDR  (*(volatile uint32_t *)(GPIOG_BASE + 0x0C))
#define GPIOG_BSRR  (*(volatile uint32_t *)(GPIOG_BASE + 0x18))

#define GPIOD_MODER  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_PUPDR  (*(volatile uint32_t *)(GPIOD_BASE + 0x0C))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))

#define GPIOC_MODER  (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_PUPDR  (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_BSRR  (*(volatile uint32_t *)(GPIOC_BASE + 0x18))

#define GPIOB_MODER  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPDR  (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_IDR  (*(volatile uint32_t *)(GPIOB_BASE + 0x10))


#define GPIOA_MODER  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR  (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

#define RCC_AHB2_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x4C ))
#define GPIOG_AHB2_CLOCK_ER (1 << 6)
#define GPIOD_AHB2_CLOCK_ER (1 << 3)
#define GPIOC_AHB2_CLOCK_ER (1 << 2)
#define GPIOB_AHB2_CLOCK_ER (1 << 1)

#define PWR_CR2              (*(volatile uint32_t *)(PWR_BASE + 0x04))
#define PWR_CR2_IOSV         (1 << 9)

static void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;

    RCC_AHB2_CLOCK_ER|= GPIOG_AHB2_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2_CLOCK_ER;
    PWR_CR2 |= PWR_CR2_IOSV;

    reg = GPIOG_MODER & ~(0x03 << (pin * 2));
    GPIOG_MODER = reg | (1 << (pin * 2));
    GPIOG_PUPDR &= ~(0x03 << (pin * 2));
    GPIOG_BSRR |= (1 << (pin + 16));
}

static void boot_led_off(void)
{
    GPIOG_BSRR |= (1 << (LED_BOOT_PIN));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;

    RCC_AHB2_CLOCK_ER|= GPIOC_AHB2_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2_CLOCK_ER;

    reg = GPIOC_MODER & ~(0x03 << (pin * 2));
    GPIOC_MODER = reg | (1 << (pin * 2));
    GPIOC_PUPDR &= ~(0x03 << (pin * 2));
    GPIOC_BSRR |= (1 << (pin));
}


void usr_led_off(void);
void check_for_boot(void)
{
    while (!(GPIOB_IDR & (1 << 12)));
    wolfBoot_update_trigger();
    usr_led_off();
}

void usr_led_off(void)
{
    GPIOC_BSRR |= (1 << (LED_USR_PIN + 16));
}

void main(void)
{
    uint32_t reg;
    wolfBoot_success();

    RCC_AHB2_CLOCK_ER|= GPIOB_AHB2_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2_CLOCK_ER;

    GPIOB_MODER &= ~(0b11 << 24);
    reg = GPIOB_PUPDR & ~(0b11 << 24);
    GPIOB_PUPDR = reg | (0b10 << 24);

    hal_init();
    usr_led_on();
    check_for_boot();


    while(1)
        ;
}
