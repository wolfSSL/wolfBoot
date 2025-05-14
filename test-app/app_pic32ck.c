/* app_pic32ck.c
 *
 * Test bare-metal boot-led-on application
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

#include "hal.h"
#include "target.h"
#include "wolfboot/wolfboot.h"
#include <stdint.h>

#if defined(TARGET_pic32ck)

#define PORT_BASE (0x44801000U)

#define PORTB_BASE (PORT_BASE + 0x80 * 1)
#define PORTB_DIRSET (*(volatile uint32_t *)(PORTB_BASE + 0x08))
#define PORTB_DIRSET_OUT(X) (1 << (X))
#define PORTB_OUTSET (*(volatile uint32_t *)(PORTB_BASE + 0x18))
#define PORTB_OUTSET_OUT(X) (1 << (X))
#define PORTB_OUTCLR (*(volatile uint32_t *)(PORTB_BASE + 0x14))
#define PORTB_OUTCLR_OUT(X) (1 << (X))

#define PORTD_BASE (PORT_BASE + 0x80 * 3)
#define PORTD_DIRSET (*(volatile uint32_t *)(PORTD_BASE + 0x08))
#define PORTD_DIRSET_OUT(X) (1 << (X))
#define PORTD_OUTSET (*(volatile uint32_t *)(PORTD_BASE + 0x18))
#define PORTD_OUTSET_OUT(X) (1 << (X))
#define PORTD_OUTCLR (*(volatile uint32_t *)(PORTD_BASE + 0x14))
#define PORTD_OUTCLR_OUT(X) (1 << (X))

static void led0_on(void)
{
    PORTD_DIRSET = PORTD_DIRSET_OUT(20);
    PORTD_OUTCLR = PORTD_OUTCLR_OUT(20);
}

static void led1_on(void)
{
    PORTB_DIRSET = PORTB_DIRSET_OUT(25);
    PORTB_OUTCLR = PORTB_OUTCLR_OUT(25);
}

void main(void)
{
    uint32_t boot_version;
    hal_init();
    boot_version = wolfBoot_current_firmware_version();
    if (boot_version == 1) {
        wolfBoot_update_trigger();
        led0_on();
    } else if (boot_version >= 2) {
        wolfBoot_success();
        led1_on();
    }

    /* Wait for reboot */
    while (1) {}
}
#endif /* TARGET_pic32ck */
