/* app_psoc_c3.c
 *
 * Test bare-metal application for Infineon PSOC Control C3.
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
#include "hal/psoc_c3.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

/* Application Interrupt and Reset Control. Several test applications carry
 * their own copy of these; they are duplicated rather than shared because the
 * apps that already define them do not all include a common header. */
#define AIRCR             (*(volatile uint32_t *)(0xE000ED0C))
#define AIRCR_VKEY        (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int len);

static void uart_print(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

static void print_dec(uint32_t v)
{
    char tmp[12];
    char num[12];
    int t = 0;
    int n = 0;

    if (v == 0) {
        num[n++] = '0';
    }
    else {
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            num[n++] = tmp[--t];
    }
    uart_write(num, (unsigned int)n);
}

/* Request a system reset, preserving the configured priority grouping. */
static void system_reset(void)
{
    uint32_t prigroup = AIRCR & 0x0700U;

    AIRCR = AIRCR_VKEY | prigroup | AIRCR_SYSRESETREQ;
    while (1)
        ;
}

static void busy_delay(volatile uint32_t ticks)
{
    while (ticks > 0)
        ticks--;
}

void main(void)
{
    uint32_t version;
    int i;

    uart_init();
    uart_print("TEST APP\r\n");

    version = wolfBoot_current_firmware_version();
    uart_print("App version: ");
    print_dec(version);
    uart_print("\r\n");

    /* v1 asks for an update and resets so wolfBoot performs the swap.
     * v2 and later confirm the update so it is not rolled back. */
    if (version >= 2) {
        wolfBoot_success();
        uart_print("update OK -- success confirmed\r\n");
        while (1)
            busy_delay(1000000);
    }

    /* Pause before asking for the update. Without a window here the reset
     * that follows comes round fast enough to interrupt a debugger trying to
     * program the update partition. */
    for (i = 0; i < 5; i++) {
        busy_delay(4000000);
        uart_print(".");
    }
    uart_print("\r\ntriggering update -> reset\r\n");
    wolfBoot_update_trigger();
    system_reset();
}
