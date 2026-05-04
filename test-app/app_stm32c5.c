/* app_stm32c5.c
 *
 * Test bare-metal application for the STM32C5 (NUCLEO-C5A3ZG).
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
#include "hal/stm32c5.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int len);

static void uart_print(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

/* NUCLEO-C5A3ZG: user LED LD2 (green) on PG1. */
#define LED_USR_PIN     (1)

static void led_init(void)
{
    uint32_t reg;
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOGEN;
    reg = RCC_AHB2ENR;
    (void)reg;

    reg = GPIOG_MODER & ~(0x03u << (LED_USR_PIN * 2));
    GPIOG_MODER = reg | (0x01u << (LED_USR_PIN * 2));
    GPIOG_PUPDR &= ~(0x03u << (LED_USR_PIN * 2));
}

static void busy_delay(uint32_t count)
{
    volatile uint32_t i;
    for (i = 0; i < count; i++)
        __asm__ volatile ("nop");
}

/* NUCLEO-C5A3ZG LD2 is active LOW: PG1 sourced through R18 to 3V3,
 * cathode to PG1.  Drive PG1 low to light the LED.
 */
static void led_on(void)
{
    GPIOG_BSRR = (1u << (LED_USR_PIN + 16));
}

static void led_off(void)
{
    GPIOG_BSRR = (1u << LED_USR_PIN);
}

static void system_reset(void)
{
    /* Preserve PRIGROUP[10:8]; everything else in AIRCR is either
     * write-only key, write-1-clear, or RES0.  DSB before/after to
     * complete pending memory ops and ensure the reset write retires.
     */
    uint32_t prigroup = AIRCR & 0x0700U;
    DSB();
    AIRCR = AIRCR_VKEY | prigroup | AIRCR_SYSRESETREQ;
    DSB();
    while (1)
        ;
}

void main(void)
{
    uint32_t version;
    uint32_t v;
    uint32_t on_ticks, off_ticks;
    /* uint32_t max is 4294967295 (10 digits) + NUL. */
    char num[12];
    char tmp[11];
    int t;
    int n;
    int blinks;

    hal_init();
    led_init();
    led_off();

    uart_init();
    uart_print("TEST APP\r\n");

    version = wolfBoot_current_firmware_version();

    v = version;
    n = 0;
    if (v == 0) {
        num[n++] = '0';
    }
    else {
        t = 0;
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            num[n++] = tmp[--t];
    }
    uart_write("App version: ", sizeof("App version: ") - 1);
    uart_write(num, n);
    uart_write("\r\n", 2);

    /* v1: blink slowly 5 times, then trigger an update.  Once the
     * update flag is set, wolfBoot will perform the swap on the next
     * reset.
     * v2+: confirm success (sticks in bank2) and blink fast forever.
     */
    if (version >= 2) {
        wolfBoot_success();
        uart_print("update OK -- success confirmed\r\n");
        on_ticks  = 200000;
        off_ticks = 200000;
        while (1) {
            led_on();
            busy_delay(on_ticks);
            led_off();
            busy_delay(off_ticks);
        }
    }

    on_ticks  = 600000;
    off_ticks = 600000;
    for (blinks = 0; blinks < 5; blinks++) {
        led_on();
        busy_delay(on_ticks);
        led_off();
        busy_delay(off_ticks);
    }

    uart_print("triggering update -> reset\r\n");
    wolfBoot_update_trigger();
    system_reset();
}
