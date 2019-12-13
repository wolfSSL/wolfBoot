/* stm32h7.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2019 wolfSSL Inc.
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


#define AHB4_CLOCK_ER (*(volatile uint32_t *)(0x580244E0))
#define GPIOB_AHB4_CLOCK_ER (1 << 1)
#define GPIOE_AHB4_CLOCK_ER (1 << 4)
#define GPIOB_BASE 0x58020400
#define GPIOE_BASE 0x58021000
#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x0c))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))
#define GPIOE_MODE  (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_PUPD  (*(volatile uint32_t *)(GPIOE_BASE + 0x0c))
#define GPIOE_BSRR  (*(volatile uint32_t *)(GPIOE_BASE + 0x18))
#define GPIOE_AFL   (*(volatile uint32_t *)(GPIOE_BASE + 0x20))
#define GPIOE_AFH   (*(volatile uint32_t *)(GPIOE_BASE + 0x24))


#define LED_BOOT_PIN (0) //PB0 - Nucleo LD1 - Green Led
#define LED_USR_PIN (1) //PE1  - Nucleo LD2 - Yellow Led
static void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;
    AHB4_CLOCK_ER |= GPIOB_AHB4_CLOCK_ER;
    reg = GPIOB_MODE & ~(0x03 << (pin * 2));
    GPIOB_MODE = reg | (1 << (pin * 2));
    reg = GPIOB_PUPD & ~(0x03 << (pin * 2));
    GPIOB_PUPD = reg | (1 << (pin * 2));
    GPIOB_BSRR |= (1 << pin);
}

static void boot_led_off(void)
{
    GPIOB_BSRR |= (1 << (LED_BOOT_PIN + 16));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;
    AHB4_CLOCK_ER |= GPIOE_AHB4_CLOCK_ER;
    reg = GPIOE_MODE & ~(0x03 << (pin * 2));
    GPIOE_MODE = reg | (1 << (pin * 2));
    reg = GPIOE_PUPD & ~(0x03 << (pin * 2));
    GPIOE_PUPD = reg | (1 << (pin * 2));
    GPIOE_BSRR |= (1 << pin);
}

void usr_led_off(void)
{
    GPIOE_BSRR |= (1 << (LED_USR_PIN + 16));
}

void main(void)
{
    hal_init();

    boot_led_on();
    usr_led_on();
    boot_led_off();
    while(1)
        ;
}
