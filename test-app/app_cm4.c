/* app_cm4.c
 *
 * Test application for Raspberry Pi CM4 (BCM2711). Prints a banner over the
 * PL011 UART and, when built against wolfCrypt FIPS (HAVE_FIPS), runs the
 * power-on self-tests and registers a FIPS callback that reports the runtime
 * in-core integrity hash for the verifyCore[] bootstrap.
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

#ifdef HAVE_FIPS
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/fips_test.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#endif

#ifdef TARGET_cm4

static void uart_init(void)
{
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
    *UART0_DR = (unsigned int)c;
}

static void uart_puts(const char* s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_putdec(int v)
{
    char buf[12];
    unsigned int u;
    int i = 0;

    if (v < 0) {
        uart_putc('-');
        u = (unsigned int)(-v);
    }
    else {
        u = (unsigned int)v;
    }
    do {
        buf[i++] = (char)('0' + (u % 10));
        u /= 10;
    } while (u != 0);
    while (i > 0)
        uart_putc(buf[--i]);
}

#ifdef HAVE_FIPS
/* wolfCrypt FIPS callback. On an in-core integrity mismatch (IN_CORE_FIPS_E)
 * the module reports the runtime hash here; copy it into verifyCore[] in
 * wolfcrypt/src/fips_test.c and rebuild to seal the module boundary. */
static void cm4_fipsCb(int ok, int err, const char* hash)
{
    uart_puts("FIPS callback: ok=");
    uart_putdec(ok);
    uart_puts(" err=");
    uart_putdec(err);
    uart_puts("\nhash = ");
    uart_puts(hash != NULL ? hash : "(null)");
    uart_puts("\n");
    if (err == IN_CORE_FIPS_E) {
        uart_puts("In-core integrity mismatch: copy the hash above into\n");
        uart_puts("verifyCore[] in wolfcrypt/src/fips_test.c and rebuild.\n");
    }
}
#endif /* HAVE_FIPS */

void main(void)
{
    uart_init();
    uart_puts("\n=== wolfBoot CM4 test-app ===\n");

#ifdef HAVE_FIPS
    uart_puts("wolfCrypt FIPS 140-3 power-on self-test\n");
    wolfCrypt_SetCb_fips(cm4_fipsCb);
    if (wc_RunAllCast_fips() == 0)
        uart_puts("FIPS CASTs: PASS\n");
    else
        uart_puts("FIPS CASTs: FAIL (see callback output above)\n");
    uart_puts("FIPS status: ");
    uart_putdec(wolfCrypt_GetStatus_fips());
    uart_puts("\n");
#endif /* HAVE_FIPS */

    uart_puts("test-app done; halting.\n");
    while (1)
        ;
}
#endif /* TARGET_cm4 */
