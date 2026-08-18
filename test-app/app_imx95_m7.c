/* app_imx95_m7.c
 *
 * Test application for the Cortex-M7 on the NXP i.MX95.
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

/* Output goes to the shared-memory console in hal/uart/uart_drv_imx95_m7.c,
 * because this part has no M7 UART routed to a host-accessible channel on the
 * Toradex SMARC carrier. wolfCrypt's test and benchmark call printf(), which
 * test-app/syscalls.c routes to uart_write(), so their output lands there too.
 *
 * Liveness markers stay in a shared status block so the demo's right pane can
 * poll them cheaply without parsing console text:
 *
 *   devmem 0x80F10010 -> 0x41505031 ("APP1") once the app is running
 *   devmem 0x80F10014 -> heartbeat, increments continuously
 *
 * wolfBoot writes its own progress codes at 0x80F10000, so a reader can tell
 * "bootloader ran" from "payload ran" with no peripheral wired up. The block
 * sits just above the console ring in the M7's DDR window, clear of the
 * RPMsg reservations at 0x88000000.
 */

#include <stdint.h>
#include "wolfboot/wolfboot.h"

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#endif
#ifdef WOLFCRYPT_TEST
int wolfcrypt_test(void *args);
#endif
#ifdef WOLFCRYPT_BENCHMARK
int benchmark_test(void *args);
#endif

#define APP_STATUS_ADDR  0x80F10010UL
#define APP_STATUS_MAGIC 0x41505031UL /* "APP1" */

extern int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop);
extern void uart_write(const char *buf, unsigned int len);

/* Local, so the banner works in builds that do not link ../src/string.o. */
static void say(const char *s)
{
    unsigned int n = 0;

    while (s[n] != '\0')
        n++;
    uart_write(s, n);
}

void main(void)
{
    volatile uint32_t *status = (volatile uint32_t *)APP_STATUS_ADDR;
    uint32_t heartbeat = 0;
    /* remoteproc loads only PT_LOAD segments, and .bss is NOBITS - nothing
     * zeroes it before entry, so do it here before any static is read. */
    extern char _start_bss[], _end_bss[];
    char *p;

    for (p = _start_bss; p < _end_bss; p++)
        *p = 0;

    (void)uart_init(115200, 8, 'N', 1);

    status[0] = APP_STATUS_MAGIC;
    status[1] = heartbeat;

    say("\r\nwolfBoot test app: i.MX95 Cortex-M7\r\n");

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
    wolfCrypt_Init();
#ifdef WOLFCRYPT_TEST
    say("\r\nRunning wolfCrypt tests...\r\n");
    wolfcrypt_test(NULL);
    say("Tests complete.\r\n");
#endif
#ifdef WOLFCRYPT_BENCHMARK
    say("\r\nRunning wolfCrypt benchmarks...\r\n");
    benchmark_test(NULL);
    say("Benchmarks complete.\r\n");
#endif
    wolfCrypt_Cleanup();
#endif

    say("Idle.\r\n");

    /* Confirm this image booted so wolfBoot does not roll it back. */
    wolfBoot_success();

    while (1) {
        status[1] = ++heartbeat;
    }
}
