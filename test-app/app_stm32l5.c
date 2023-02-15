/* stm32l5.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
#include "wolfboot/wc_secure.h"

#define LED_BOOT_PIN (12)  //PG12 - Discovery - Green Led
#define LED_USR_PIN (3) //PD3  - Discovery  - Red Led

/*Non-Secure */
#define RCC_BASE            (0x40021000)   //RM0438 - Table 4
#define PWR_BASE            (0x40007000)   //RM0438 - Table 4
#define GPIOD_BASE 0x42020C00
#define GPIOG_BASE 0x42021800


#define GPIOG_MODER  (*(volatile uint32_t *)(GPIOG_BASE + 0x00))
#define GPIOG_PUPDR  (*(volatile uint32_t *)(GPIOG_BASE + 0x0C))
#define GPIOG_BSRR  (*(volatile uint32_t *)(GPIOG_BASE + 0x18))

#define GPIOD_MODER  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_PUPDR  (*(volatile uint32_t *)(GPIOD_BASE + 0x0C))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))

#define RCC_AHB2_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x4C ))
#define GPIOG_AHB2_CLOCK_ER (1 << 6)
#define GPIOD_AHB2_CLOCK_ER (1 << 3)

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

    RCC_AHB2_CLOCK_ER|= GPIOD_AHB2_CLOCK_ER;
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_AHB2_CLOCK_ER;

    reg = GPIOD_MODER & ~(0x03 << (pin * 2));
    GPIOD_MODER = reg | (1 << (pin * 2));
    GPIOD_PUPDR &= ~(0x03 << (pin * 2));
    GPIOD_BSRR |= (1 << (pin + 16));
}

void usr_led_off(void)
{
    GPIOD_BSRR |= (1 << (LED_USR_PIN));
}

static char CaBuf[2048];
static uint8_t my_pubkey[200];

void main(void)
{
#ifdef WOLFBOOT_SECURE_CALLS
    uint32_t rand;
    uint32_t i;
    uint32_t klen = 200;
    wcs_get_random((void*)&rand, 4);
    for (i = 0; i < (rand / 100000000); i++)
        ;

    wcs_slot_read(0, CaBuf, 2048);
    wcs_ecc_getpublic(1, my_pubkey, &klen);
    wcs_ecc_import_public(4, my_pubkey, klen, 7);

#endif
    hal_init();
    boot_led_on();
    usr_led_on();
    boot_led_off();
    if (wolfBoot_current_firmware_version() > 1)
        boot_led_on();

    while(1)
        ;
}
