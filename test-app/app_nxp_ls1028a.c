/* app_nxp_ls1028a.c
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
#include "printf.h"

#ifdef ENABLE_WOLFIP
#include "wolfip_tftp_test.h"
#endif

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#include <wolfssl/wolfcrypt/settings.h>
#endif
#ifdef WOLFCRYPT_TEST
#include <wolfcrypt/test/test.h>
int wolfcrypt_test(void *args);
#endif
#ifdef WOLFCRYPT_BENCHMARK
#include <wolfcrypt/benchmark/benchmark.h>
int benchmark_test(void *args);
#endif

/* UART is up from wolfBoot hal_init; wolfBoot_printf() routes to it (no-op
 * when DEBUG_UART=0). */
__attribute__((section(".boot")))
void main(void)
{
    /* App BSS lives in uninitialized DDR (no crt0 startup), so zero it. */
    extern char _start_bss[], _end_bss[];
    char *p;

    for (p = _start_bss; p < _end_bss; p++)
        *p = 0;

    wolfBoot_printf("Test App\r\n");

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
    wolfCrypt_Init();
#ifdef WOLFCRYPT_TEST
    wolfBoot_printf("\r\nRunning wolfCrypt tests...\r\n");
    wolfcrypt_test(NULL);
    wolfBoot_printf("Tests complete.\r\n");
#endif
#ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("\r\nRunning wolfCrypt benchmarks...\r\n");
    benchmark_test(NULL);
    wolfBoot_printf("Benchmarks complete.\r\n");
#endif
    wolfCrypt_Cleanup();
#endif

#ifdef ENABLE_WOLFIP
    wolfBoot_printf("\r\nStarting wolfIP network test...\r\n");
    if (wolfip_tftp_test_run() == 0)
        wolfBoot_printf("WOLFIP_TEST: PASS\r\n");
    else
        wolfBoot_printf("WOLFIP_TEST: FAIL\r\n");
#endif

    wolfBoot_printf("Test App: idle\r\n");
    while (1)
        ;
}
