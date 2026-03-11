/* app_stm32n6.c
 *
 * Test bare-metal application for NUCLEO-N657X0-Q.
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
#include "system.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

#define RCC_BASE        (0x56028000UL)
#define RCC_AHB4ENR     (*(volatile uint32_t *)(RCC_BASE + 0x25C))
#define RCC_AHB4ENR_GPIOGEN  (1 << 6)
#define RCC_AHB4ENR_PWREN    (1 << 18)

/* PWR I/O supply valid bits (required for Port G output) */
#define PWR_BASE        (0x56024800UL)
#define PWR_SVMCR3      (*(volatile uint32_t *)(PWR_BASE + 0x3C))
#define PWR_SVMCR3_VDDIO2SV  (1 << 8)
#define PWR_SVMCR3_VDDIO3SV  (1 << 9)

#define GPIO_MODER(base)    (*(volatile uint32_t *)((base) + 0x00))
#define GPIO_OSPEEDR(base)  (*(volatile uint32_t *)((base) + 0x08))
#define GPIO_PUPDR(base)    (*(volatile uint32_t *)((base) + 0x0C))
#define GPIO_BSRR(base)     (*(volatile uint32_t *)((base) + 0x18))

#define GPIOG_BASE      (0x56021800UL)

/* User LEDs: active LOW on Port G (LD6=PG0 green, LD7=PG8 blue, LD5=PG10 red) */
#define LED_GREEN_PIN   0
#define LED_BLUE_PIN    8
#define LED_RED_PIN     10

static void led_init(void)
{
    uint32_t reg;

    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOGEN | RCC_AHB4ENR_PWREN;
    DMB();

    /* Mark I/O supply valid for Port G */
    PWR_SVMCR3 |= PWR_SVMCR3_VDDIO2SV | PWR_SVMCR3_VDDIO3SV;
    DMB();

    /* Set PG0, PG8, PG10 to output mode */
    reg = GPIO_MODER(GPIOG_BASE);
    reg &= ~(0x3 << (LED_GREEN_PIN * 2));
    reg |= (0x1 << (LED_GREEN_PIN * 2));
    reg &= ~(0x3 << (LED_BLUE_PIN * 2));
    reg |= (0x1 << (LED_BLUE_PIN * 2));
    reg &= ~(0x3 << (LED_RED_PIN * 2));
    reg |= (0x1 << (LED_RED_PIN * 2));
    GPIO_MODER(GPIOG_BASE) = reg;
}

/* Active LOW: BSRR reset = ON, BSRR set = OFF */
static void led_on(uint32_t gpio_base, int pin)
{
    GPIO_BSRR(gpio_base) = (1 << (pin + 16));
}

static void led_off(uint32_t gpio_base, int pin)
{
    GPIO_BSRR(gpio_base) = (1 << pin);
}

static void delay(volatile uint32_t count)
{
    while (count--)
        ;
}

volatile uint32_t app_running __attribute__((section(".data"))) = 0;

void main(void)
{
    /* hal_init() not called — XSPI2 already configured by wolfBoot for XIP */
    led_init();

    app_running = 0xCAFEBEEF;
    (void)wolfBoot_current_firmware_version();

    led_on(GPIOG_BASE, LED_GREEN_PIN);

    /* Test flash erase from XIP (RAMFUNCTION verification) */
    hal_flash_unlock();
    hal_flash_erase(0x70010000, 0x1000);
    hal_flash_lock();

    app_running = 0xF1A5F1A5;

    /* Mark firmware stable (flash ops are RAMFUNCTION, safe from XIP) */
    wolfBoot_success();

    while (1) {
        led_on(GPIOG_BASE, LED_BLUE_PIN);
        delay(2000000);
        led_off(GPIOG_BASE, LED_BLUE_PIN);
        delay(2000000);
    }
}
