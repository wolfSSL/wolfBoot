/* app_cm5.c
 *
 * Test application for Raspberry Pi CM5 (BCM2712). Prints a banner over the
 * debug PL011 that wolfBoot left running and halts.
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
#include "wolfboot/wolfboot.h"
#include "hal/cm5.h"

#ifdef TARGET_cm5

static void uart_putc(char c)
{
    while (*UART10_FR & PL011_FR_TXFF)
        ;
    *UART10_DR = (unsigned int)(unsigned char)c;
}

static void uart_puts(const char* s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void main(void)
{
    uart_puts("\n=== wolfBoot CM5 test-app ===\n");
    uart_puts("test-app done; halting.\n");
    while (1)
        ;
}
#endif /* TARGET_cm5 */
