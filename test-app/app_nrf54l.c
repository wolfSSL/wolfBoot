/* app_nrf54l.c
 *
 * Test bare-metal application.
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

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "hal/nrf54l.h"
#include "printf.h"

#ifndef TEST_LED_PORT
    #define TEST_LED_PORT 1
#endif
#ifndef TEST_LED_PIN
    #define TEST_LED_PIN 10
#endif

extern void hal_init(void);
extern void wolfBoot_panic(void);

#ifdef RAM_CODE

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

#define WEAKFUNCTION __attribute__((weak))

void WEAKFUNCTION RAMFUNCTION arch_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;

}
#endif

static void led_toggle(void)
{
    uint32_t mask = (1U << TEST_LED_PIN);
    if (GPIO_OUT(TEST_LED_PORT) & mask)
        GPIO_OUTCLR(TEST_LED_PORT) = mask;
    else
        GPIO_OUTSET(TEST_LED_PORT) = mask;
}

void main(void)
{
    uint32_t version = wolfBoot_current_firmware_version();

    hal_init();

    GPIO_PIN_CNF(TEST_LED_PORT, TEST_LED_PIN) =
        (GPIO_CNF_OUT | GPIO_CNF_HIGH_DRIVE_0);
    GPIO_OUTCLR(TEST_LED_PORT) = (1U << TEST_LED_PIN);

    uart_init();
    wolfBoot_printf("Booted firmware version: %d\r\n", version);

    for (;;) {
        led_toggle();
        for (volatile uint32_t n = 0; n < 1000000UL; n++)
            NOP();
    }
}
