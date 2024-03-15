/* app_stm32h5.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include "wolfboot/wolfboot.h"

#define LED_BOOT_PIN (4) /* PG4 - Nucleo - Red Led */
#define LED_USR_PIN  (0) /* PB0 - Nucleo - Green Led */
#define LED_USR2_PIN  (4) /* PF4 - Nucleo - Orange Led */

/*Non-Secure */
#define RCC_BASE            (0x44020C00)   /* RM0481 - Table 3 */
#define GPIOG_BASE 0x42021800
#define GPIOB_BASE 0x42020400
#define GPIOF_BASE 0x42021400


#define GPIOG_MODER (*(volatile uint32_t *)(GPIOG_BASE + 0x00)) 
#define GPIOG_PUPDR (*(volatile uint32_t *)(GPIOG_BASE + 0x0C)) 
#define GPIOG_BSRR  (*(volatile uint32_t *)(GPIOG_BASE + 0x18)) 

#define GPIOB_MODER (*(volatile uint32_t *)(GPIOB_BASE + 0x00)) 
#define GPIOB_PUPDR (*(volatile uint32_t *)(GPIOB_BASE + 0x0C)) 
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18)) 

#define GPIOF_MODER (*(volatile uint32_t *)(GPIOF_BASE + 0x00)) 
#define GPIOF_PUPDR (*(volatile uint32_t *)(GPIOF_BASE + 0x0C)) 
#define GPIOF_BSRR  (*(volatile uint32_t *)(GPIOF_BASE + 0x18)) 

#define RCC_AHB2ENR1_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C ))
#define GPIOG_AHB2ENR1_CLOCK_ER (1 << 6)
#define GPIOF_AHB2ENR1_CLOCK_ER (1 << 5)
#define GPIOB_AHB2ENR1_CLOCK_ER (1 << 1)

static void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;

    RCC_AHB2ENR1_CLOCK_ER|= GPIOG_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR1_CLOCK_ER;

    reg = GPIOG_MODER & ~(0x03 << (pin * 2));
    GPIOG_MODER = reg | (1 << (pin * 2));
    GPIOG_PUPDR &= ~(0x03 << (pin * 2));
    GPIOG_BSRR |= (1 << (pin));
}

static void boot_led_off(void)
{
    GPIOG_BSRR |= (1 << (LED_BOOT_PIN + 16));
}

void usr_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR_PIN;

    RCC_AHB2ENR1_CLOCK_ER|= GPIOB_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR1_CLOCK_ER;

    reg = GPIOB_MODER & ~(0x03 << (pin * 2));
    GPIOB_MODER = reg | (1 << (pin * 2));
    GPIOB_PUPDR &= ~(0x03 << (pin * 2));
    GPIOB_BSRR |= (1 << (pin));
}

void usr_led_off(void)
{
    GPIOB_BSRR |= (1 << (LED_USR_PIN + 16));
}

void usr2_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_USR2_PIN;

    RCC_AHB2ENR1_CLOCK_ER|= GPIOF_AHB2ENR1_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2ENR1_CLOCK_ER;

    reg = GPIOF_MODER & ~(0x03 << (pin * 2));
    GPIOF_MODER = reg | (1 << (pin * 2));
    GPIOF_PUPDR &= ~(0x03 << (pin * 2));
    GPIOF_BSRR |= (1 << (pin));
}

void usr2_led_off(void)
{
    GPIOF_BSRR |= (1 << (LED_USR2_PIN + 16));
}

void main(void)
{
    hal_init();
    boot_led_on();
    usr_led_on();
    boot_led_off();
    if (wolfBoot_current_firmware_version() > 1)
        usr2_led_on();
    while(1)
        ;
}
