/* app_cm4.c
 *
 * Test application for Raspberry Pi CM4 (BCM2711). Prints a banner over the
 * mini-UART (the console wired to GPIO14/15; build with CM4_UART_PL011 to use
 * the PL011 instead) and halts. wolfBoot's FIPS power-on self-test and in-core
 * integrity check run in the bootloader (src/loader.c), not in the app.
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
#include "hal/cm4.h"     /* BCM2711 UART register map */

#ifdef TARGET_cm4

#if defined(CM4_UART_PL011)
static void uart_init(void)
{
    /* Route GPIO14 (TXD0) and GPIO15 (RXD0) to the PL011 via ALT0, so the app's
     * console reaches the 40-pin debug header (matching hal/cm4.c uart_init()).
     * Without this the PL011 stays wired to the Bluetooth pins and the app is
     * silent. GPFSEL1 (GPIO_BASE+0x04) holds FSEL10-19: ALT0 = 0b100. */
    volatile unsigned int *gpfsel1 =
        (volatile unsigned int *)(BCM2711_GPIO_BASE + 0x04);
    unsigned int fsel;

    fsel = *gpfsel1;
    fsel &= ~((7u << 12) | (7u << 15));
    fsel |=  ((4u << 12) | (4u << 15));
    *gpfsel1 = fsel;

    /* PL011 for 115200 8N1 from the 48 MHz UART clock (see hal/cm4.c) */
    *UART0_CR = 0;
    *UART0_ICR = 0x7FF;
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    *UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6);
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

static void uart_putc(char c)
{
    while (*UART0_FR & 0x20) /* wait while TX FIFO full */
        ;
    *UART0_DR = (unsigned int)(unsigned char)c;
}
#else /* mini-UART (default) - inherit the firmware's enabled console */
static void uart_init(void)
{
    /* The firmware leaves the mini-UART enabled at a stable baud; wolfBoot ran
     * on it too. Nothing to program - just write AUX_MU_IO. */
}

static void uart_putc(char c)
{
    while ((*MU_LSR & MU_LSR_TXFF_EMPTY) == 0) /* wait until TX can accept */
        ;
    *MU_IO = (unsigned int)(unsigned char)c;
}
#endif /* CM4_UART_PL011 */

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
    uart_init();
    uart_puts("\n=== wolfBoot CM4 test-app ===\n");
    uart_puts("test-app done; halting.\n");
    while (1)
        ;
}
#endif /* TARGET_cm4 */
