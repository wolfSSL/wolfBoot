/* app_stm32u3.c
 *
 * Test bare-metal application for the STM32U3 (NUCLEO-U385RG-Q).
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

#include <stdint.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

/* UART always available in test-app (uart_init/uart_write live in hal/stm32u3.c) */
extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int len);

static void uart_print(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

/* NUCLEO-U385RG-Q: user LED LD2 on PA5 (NUCLEO-64 convention). */
#define LED_USR_PIN     (5)

#define RCC_BASE        (0x40030C00)
#define GPIOA_BASE      (0x42020000)

#define RCC_AHB2ENR1    (*(volatile uint32_t *)(RCC_BASE + 0x8C))
#define GPIOA_EN        (1 << 0)

#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR     (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR      (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

static void led_init(void)
{
    uint32_t reg;
    RCC_AHB2ENR1 |= GPIOA_EN;
    reg = RCC_AHB2ENR1;
    (void)reg;

    reg = GPIOA_MODER & ~(0x03u << (LED_USR_PIN * 2));
    GPIOA_MODER = reg | (0x01u << (LED_USR_PIN * 2));
    GPIOA_PUPDR &= ~(0x03u << (LED_USR_PIN * 2));
}

static void led_on(void)
{
    GPIOA_BSRR = (1u << LED_USR_PIN);
}

static void led_off(void)
{
    GPIOA_BSRR = (1u << (LED_USR_PIN + 16));
}

static void busy_delay(uint32_t count)
{
    volatile uint32_t i;
    for (i = 0; i < count; i++)
        __asm__ volatile ("nop");
}

void main(void)
{
    uint32_t version;
    uint32_t v;
    uint32_t on_ticks, off_ticks;
    char num[4];
    int idx = 0;

    hal_init();
    led_init();

    uart_init();
    uart_print("TEST APP\r\n");

    version = wolfBoot_current_firmware_version();

    v = version;
    if (v >= 100) { num[idx++] = '0' + (v / 100); v %= 100; }
    if (v >= 10 || idx > 0) { num[idx++] = '0' + (v / 10); v %= 10; }
    num[idx++] = '0' + v;
    num[idx] = '\0';
    uart_write("App version: ", sizeof("App version: ") - 1);
    uart_write(num, idx);
    uart_write("\r\n", 2);

    /* v1: slow blink. v2+: fast blink. */
    if (version >= 2) {
        wolfBoot_success();
        on_ticks  = 200000;
        off_ticks = 200000;
    } else {
        on_ticks  = 600000;
        off_ticks = 600000;
    }

    while (1) {
        led_on();
        busy_delay(on_ticks);
        led_off();
        busy_delay(off_ticks);
    }
}
