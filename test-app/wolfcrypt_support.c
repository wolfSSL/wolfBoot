/* wolfcrypt_support.c
 *
 * Support infrastructure for wolfCrypt test and benchmark
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

#include <stdint.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/types.h>

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)

/* ========== TIME FUNCTIONS ========== */

/*
 * Time implementation strategy:
 * 1. If WOLFCRYPT_SECURE_MODE - use secure world API
 * 2. If target has SysTick/timer - use hardware timer
 * 3. Fallback - simple counter (not accurate for benchmarks)
 */

#if defined(WOLFCRYPT_SECURE_MODE)
    /* Use secure mode API for time */
    #include "wolfboot/wc_secure.h"
#elif defined(TARGET_va416x0)
    /* Use Vorago SDK HAL_time_ms (incremented by SysTick_Handler every 1ms) */
    extern volatile uint64_t HAL_time_ms;
#elif defined(TARGET_nxp_t2080) || defined(TARGET_nxp_t1024)
    /* PPC timebase register for accurate timing.
     * Timebase frequency = platform_clock / 16. */
    static uint32_t ppc_tb_hz = 0;
    static unsigned long long ppc_start_ticks = 0;

    static unsigned long long ppc_get_ticks(void)
    {
        unsigned long hi, lo, tmp;
        __asm__ volatile (
            "1: mftbu %0\n"
            "   mftb  %1\n"
            "   mftbu %2\n"
            "   cmpw  %0, %2\n"
            "   bne   1b\n"
            : "=r"(hi), "=r"(lo), "=r"(tmp)
        );
        return ((unsigned long long)hi << 32) | lo;
    }

    static uint32_t ppc_get_timebase_hz(void)
    {
        /* Read Platform PLL ratio from CLOCKING_PLLPGSR register.
         * CCSRBAR=0xFE000000, CLOCKING_BASE=CCSRBAR+0xE1000,
         * PLLPGSR=CLOCKING_BASE+0xC00 */
        volatile uint32_t *pllpgsr =
            (volatile uint32_t *)(0xFE000000UL + 0xE1C00UL);
        uint32_t plat_ratio = ((*pllpgsr) >> 1) & 0x1F;
    #if defined(BOARD_NAII_68PPC2) || defined(TARGET_nxp_t1024)
        uint32_t sys_clk = 100000000; /* 100 MHz */
    #else
        uint32_t sys_clk = 66666667;  /* 66.66 MHz (T2080 RDB) */
    #endif
        return (sys_clk * plat_ratio) / 16;
    }
#else
    /* Simple tick counter fallback */
    static volatile unsigned int tick_counter = 0;
#endif

/* my_time() - Used by wolfCrypt ASN.c for certificate time checking
 * Returns: Current time in seconds since epoch (or counter value)
 */
unsigned long my_time(unsigned long* timer)
{
#if defined(WOLFCRYPT_SECURE_MODE)
    /* Get time from secure world */
    unsigned long t = wolfBoot_nsc_get_time();
    if (timer) *timer = t;
    return t;
#elif defined(TARGET_va416x0)
    unsigned long t = (unsigned long)(HAL_time_ms / 1000);
    if (timer) *timer = t;
    return t;
#elif defined(TARGET_nxp_t2080) || defined(TARGET_nxp_t1024)
    if (ppc_tb_hz == 0)
        ppc_tb_hz = ppc_get_timebase_hz();
    {
        unsigned long t = (unsigned long)(ppc_get_ticks() / ppc_tb_hz);
        if (timer) *timer = t;
        return t;
    }
#else
    /* Simple incrementing counter */
    tick_counter++;
    if (timer) *timer = tick_counter;
    return tick_counter;
#endif
}

#ifdef WOLFCRYPT_BENCHMARK
/* current_time() - Used by wolfCrypt benchmark tool for timing measurements
 * Parameter: reset - if non-zero, reset the timer
 * Returns: Current time in seconds (floating point)
 */
double current_time(int reset)
{
#if defined(WOLFCRYPT_SECURE_MODE)
    return wolfBoot_nsc_current_time(reset);
#elif defined(TARGET_va416x0)
    (void)reset;
    /* Use Vorago SDK SysTick-based millisecond counter */
    return (double)HAL_time_ms / 1000.0;
#elif defined(TARGET_nxp_t2080) || defined(TARGET_nxp_t1024)
    if (ppc_tb_hz == 0)
        ppc_tb_hz = ppc_get_timebase_hz();
    if (reset)
        ppc_start_ticks = ppc_get_ticks();
    return (double)(ppc_get_ticks() - ppc_start_ticks) / (double)ppc_tb_hz;
#else
    /* Simple counter-based timing */
    if (reset)
        tick_counter = 0;
    else
        tick_counter++;
    return (double)tick_counter;
#endif
}
#endif /* WOLFCRYPT_BENCHMARK */

/* ========== RNG SEED FUNCTIONS ========== */

/*
 * RNG seed generation strategy:
 * 1. If WOLFCRYPT_SECURE_MODE - use secure TRNG
 * 2. If target has TRNG - use hardware random
 * 3. Fallback - pseudo-random based on time (NOT cryptographically secure)
 */

/* Simple incrementing RNG for testing (not cryptographically secure).
 * Fixed non-zero seed for deterministic test/benchmark results. */
static uint32_t test_rng_counter = 0x12345678;

/* my_rng_seed_gen() - Generate random seed/data for test/benchmark
 * This is NOT cryptographically secure - only for testing!
 * Returns: 0 on success
 */
int my_rng_seed_gen(unsigned char* output, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        if ((i % 4) == 0) {
            test_rng_counter++;
        }
        output[i] = (unsigned char)(test_rng_counter >> ((i % 4) * 8));
    }
    return 0;
}

#endif /* WOLFCRYPT_TEST || WOLFCRYPT_BENCHMARK */
