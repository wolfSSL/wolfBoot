/* app_mpfs250.c
 *
 * Test bare-metal application for PolarFire SoC MPFS250.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"
#include "printf.h"

#include "../hal/mpfs250.h"

/* wolfCrypt test/benchmark support */
#ifdef WOLFCRYPT_TEST
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/test/test.h>
int wolfcrypt_test(void *args);
#endif

#ifdef WOLFCRYPT_BENCHMARK
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/benchmark/benchmark.h>
int benchmark_test(void *args);
#endif

#ifdef WOLFCRYPT_BENCHMARK
/* Buffer sizes the whole benchmark is run at, in turn: the Athena F5200 does
 * one engine transfer per call, so a sector-sized call and a bulk one amortise
 * the setup very differently, and the software build runs the same set as the
 * baseline. Each needs twice the size from the static pool
 * (WOLFSSL_STATIC_MEMORY_TEST_SZ). */
#ifndef MPFS_BENCH_SIZES
#define MPFS_BENCH_SIZES { 1024, 512, 4096 }
#endif
#endif /* WOLFCRYPT_BENCHMARK */

#ifdef MPFS_ATHENA
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/port/microchip/mpfs_athena.h>

static void mpfs_athena_app_init(void)
{
    int ret;

    /* The engine is brought up here, not by the bootloader: a wolfBoot built
     * without MPFS_ATHENA never touched it, and the port's own init handles
     * one that did. wolfcrypt_test and benchmark_test register the device
     * again, which the port accepts. */
    ret = wolfCrypt_Init();
    if (ret != 0) {
        wolfBoot_printf("Athena: wolfCrypt_Init failed (%d)\r\n", ret);
        return;
    }
    ret = wc_MpfsAthena_RegisterDevice(WC_MPFS_ATHENA_DEVID);
    if (ret != 0) {
        wolfBoot_printf("Athena: RegisterDevice failed (%d)\r\n", ret);
        return;
    }
    ret = wc_MpfsAthena_SelfTest();
    if (ret != 0) {
        wolfBoot_printf("Athena: self-test failed: %s (rc %d)\r\n",
            wc_MpfsAthena_SelfTestDesc(), ret);
        return;
    }
    wolfBoot_printf("Athena: self-test passed, engine served %u call(s)\r\n",
        (unsigned int)wc_MpfsAthena_GetCbCount());
}

#endif /* MPFS_ATHENA */


#ifdef WOLFBOOT_RISCV_MMODE
#include "../hal/riscv.h"
/* MTIME is not running in an M-mode boot: start it, then let the machine
 * timer interrupt refresh the watchdogs every 5 s for as long as the app
 * runs. */
#define MPFS_WDT_PET_TICKS (5ULL * RTC_CLOCK_FREQ)

/* The vector saved the caller's registers, so plain C: refresh the five MSS
 * watchdogs, which the bitstream arms and which reset the chip well inside a
 * test run, then rearm. */
void isr_mtimer(void)
{
    mpfs_wdt_pet_all();
    CLINT_MTIMECMP_REG(0) = CLINT_MTIME_REG + MPFS_WDT_PET_TICKS;
}

static void mpfs_wdt_timer_start(void)
{
    mpfs_enable_mtime();
    CLINT_MTIMECMP_REG(0) = CLINT_MTIME_REG + MPFS_WDT_PET_TICKS;
    csr_set(mie, MIE_MTIE);
    csr_set(mstatus, MSTATUS_MIE);
#ifdef MPFS_DDR_INIT
    /* The bootloader raised the E51 to the Libero PLL rate; this copy of the
     * HAL's clock variable, which times the benchmark, still holds the reset
     * rate. Without DDR init the clock is never raised and that reset rate is
     * the right one. */
    mpfs_cpu_freq_mhz =
        (uint32_t)(LIBERO_SETTING_MSS_COREPLEX_CPU_CLK / 1000000UL);
#endif
}
#endif

void main(void)
{
    /* wolfBoot fully configured UART0 before jumping here.
     * Calling uart_init() again clears the TX FIFO (FCR write) while wolfBoot's
     * last output may still be draining, which can leave THRE stuck at 0.
     * Calling hal_init() writes to _main_hart_hls=0 (NULL ptr crash).
     * So use wolfBoot_printf directly — UART0 is already ready. */

    wolfBoot_printf("========================\r\n");
    wolfBoot_printf("PolarFire SoC MPFS250 wolfBoot demo Application\r\n");
    wolfBoot_printf("Copyright 2025 wolfSSL Inc\r\n");
    wolfBoot_printf("GPL v3\r\n");
    wolfBoot_printf("========================\r\n");

#ifdef WOLFBOOT_RISCV_MMODE
    mpfs_wdt_timer_start();
#endif

#ifdef MPFS_ATHENA
    mpfs_athena_app_init();
#endif

#ifdef WOLFCRYPT_TEST
    wolfBoot_printf("\r\nRunning wolfCrypt tests...\r\n");
    wolfCrypt_Init();
    wolfcrypt_test(NULL);
    wolfCrypt_Cleanup();
    wolfBoot_printf("Tests complete.\r\n\r\n");
#endif

#ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("Running wolfCrypt benchmarks...\r\n");
    wolfCrypt_Init();
    {
        static const unsigned int sizes[] = MPFS_BENCH_SIZES;
        unsigned int i;
        /* benchmark_test() puts the block size back to its default on its
         * first run, so take that run's setup first and configure after. */
        benchmark_init();
        benchmark_free();
        for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            wolfBoot_printf("\r\n--- %u-byte calls ---\r\n", sizes[i]);
            benchmark_configure(sizes[i]);
            benchmark_test(NULL);
        }
    }
    wolfCrypt_Cleanup();
    wolfBoot_printf("Benchmarks complete.\r\n\r\n");
#endif

#ifdef MPFS_ATHENA
    /* A passing test or a plausible figure proves nothing about offload on
     * its own: every case the port declines runs in software. */
    wolfBoot_printf("Athena: engine served %u call(s) in total\r\n",
        (unsigned int)wc_MpfsAthena_GetCbCount());
#endif

    while(1) {
        /* Main application loop */
        /* TODO: Add watchdog feed if needed */
    }
}

