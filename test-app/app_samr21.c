/* samr21.c
 *
 * Test bare-metal application for SAM D21 / SAM R21
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

/* SAM D21 PORT. One 0x80-byte register group per port, so group B is
 * PORT_BASE + 0x80. The SAM E51 layout is the same at a different base. */
#define PORT_BASE       (0x41004400)
#define PORT_GROUP(g)   (PORT_BASE + ((g) * 0x80))
#define PORT_DIRSET(g)  *((volatile uint32_t *)(PORT_GROUP(g) + 0x08))
#define PORT_OUTCLR(g)  *((volatile uint32_t *)(PORT_GROUP(g) + 0x14))
#define PORT_OUTSET(g)  *((volatile uint32_t *)(PORT_GROUP(g) + 0x18))
#define PORT_OUTTGL(g)  *((volatile uint32_t *)(PORT_GROUP(g) + 0x1C))

/* LED0 on the SAM D21 Xplained Pro is PB30, driven low to light. Override
 * for a board that wires its LED elsewhere. */
#ifndef LED_GROUP
#define LED_GROUP 1
#endif
#ifndef LED_PIN
#define LED_PIN 30
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 1
#endif

#define LED_MASK (1U << LED_PIN)

static void led_on(void)
{
#if LED_ACTIVE_LOW
    PORT_OUTCLR(LED_GROUP) = LED_MASK;
#else
    PORT_OUTSET(LED_GROUP) = LED_MASK;
#endif
}

static void delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 1000000; i++)
        ;
}

void main(void)
{
    uint32_t version = wolfBoot_current_firmware_version();

    PORT_DIRSET(LED_GROUP) = LED_MASK;
    led_on();

    /* A solid LED is the factory image; a blinking one is an update that
     * has booted. Confirming it here is what stops the next boot from
     * rolling the update back. */
    if (version > 1) {
        wolfBoot_success();
        while (1) {
            delay();
            PORT_OUTTGL(LED_GROUP) = LED_MASK;
        }
    }

    while (1)
        __asm__ volatile ("WFI");
}
