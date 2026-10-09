/* unit-nrf54l-flash.c
 *
 * Unit tests for the nRF54L RRAM flash driver, clock and benchmark timer,
 * and the nRF54L15/nRF54LM20 variant map in hal/nrf54l.h.
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

#define TARGET_nrf54l
#define __WOLFBOOT
#define BOOT_BENCHMARK
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
#define WOLFCRYPT_SECURE_MODE

#include <check.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "../../hal/nrf54l.h"

static uint32_t mock_rramc[0x600 / 4];
static uint32_t mock_icache[0x500 / 4];
static uint32_t mock_timer[0x600 / 4];
static uint32_t mock_cracen[0x500 / 4];
static uint32_t mock_cracencore[0x1100 / 4];
static uint32_t mock_rng_control;
static int mock_rng_control_writes;
static uint32_t mock_rng_state;
static int mock_rng_error_until_reset;
static int mock_rng_empty_until_reset;
static uint32_t mock_rng_fifolevel;
static uint32_t mock_rng_fifo_word;
static uint32_t mock_pll_freq;
static int mock_pll_busy_reads;
static int mock_icache_busy_reads;
static uint32_t mock_commit_pending;
static int mock_commits;
static int mock_wen_at_commit;

static volatile uint32_t *mock_commit_reg(void)
{
    return &mock_commit_pending;
}

/* The write buffer drains only after COMMITWRITEBUF */
static uint32_t mock_bufstatus(void)
{
    if (mock_commit_pending != 0U) {
        mock_commits++;
        mock_wen_at_commit = (int)(mock_rramc[0x500 / 4] & RRAMC_CONFIG_WEN_Msk);
        mock_commit_pending = 0U;
        return RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk;
    }
    return 0U;
}

/* CURRENTFREQ follows FREQ only after a few reads, like the PLL switch */
static uint32_t mock_pll_currentfreq(void)
{
    if (mock_pll_busy_reads > 0) {
        mock_pll_busy_reads--;
        return 3U;
    }
    return mock_pll_freq;
}

static uint32_t mock_icache_status(void)
{
    if (mock_icache_busy_reads > 0) {
        mock_icache_busy_reads--;
        return ICACHE_STATUS_READY_Busy;
    }
    return 0U;
}

static volatile uint32_t *mock_rng_control_reg(void)
{
    mock_rng_control_writes++;
    return &mock_rng_control;
}

/* The ERROR state clears once the block is soft-reset the given number of times */
static uint32_t mock_rng_status(void)
{
    uint32_t state = mock_rng_state;

    if (mock_rng_error_until_reset > mock_rng_control_writes / 2)
        state = CRACENCORE_RNG_STATUS_STATE_ERROR;
    return state << CRACENCORE_RNG_STATUS_STATE_Pos;
}

static uint32_t mock_rng_level(void)
{
    if ((mock_rng_status() >> CRACENCORE_RNG_STATUS_STATE_Pos) ==
        CRACENCORE_RNG_STATUS_STATE_ERROR)
        return 0U;
    if (mock_rng_empty_until_reset > mock_rng_control_writes / 2)
        return 0U;
    return mock_rng_fifolevel;
}

static uint32_t mock_rng_fifo(void)
{
    mock_rng_fifo_word += 0x04040404U;
    return mock_rng_fifo_word;
}

#undef RRAMC_BASE
#define RRAMC_BASE ((uintptr_t)mock_rramc)
#undef RRAMC_TASKS_COMMITWRITEBUF
#define RRAMC_TASKS_COMMITWRITEBUF (*mock_commit_reg())
#undef RRAMC_BUFSTATUS_WRITEBUFEMPTY
#define RRAMC_BUFSTATUS_WRITEBUFEMPTY (mock_bufstatus())
#undef ICACHE_BASE
#define ICACHE_BASE ((uintptr_t)mock_icache)
#undef ICACHE_STATUS
#define ICACHE_STATUS (mock_icache_status())
#undef TIMER20_BASE
#define TIMER20_BASE ((uintptr_t)mock_timer)
#undef CRACEN_BASE
#define CRACEN_BASE ((uintptr_t)mock_cracen)
#undef CRACENCORE_BASE
#define CRACENCORE_BASE ((uintptr_t)mock_cracencore)
#undef CRACENCORE_RNG_CONTROL
#define CRACENCORE_RNG_CONTROL (*mock_rng_control_reg())
#undef CRACENCORE_RNG_STATUS
#define CRACENCORE_RNG_STATUS (mock_rng_status())
#undef CRACENCORE_RNG_FIFOLEVEL
#define CRACENCORE_RNG_FIFOLEVEL (mock_rng_level())
#undef CRACENCORE_RNG_FIFO
#define CRACENCORE_RNG_FIFO (mock_rng_fifo())
#undef OSCILLATORS_PLL_FREQ
#define OSCILLATORS_PLL_FREQ mock_pll_freq
#undef OSCILLATORS_PLL_CURRENTFREQ
#define OSCILLATORS_PLL_CURRENTFREQ (mock_pll_currentfreq())

#include "../../hal/nrf54l.c"

#ifdef MAP_32BIT
#define MOCK_FLASH_HINT   NULL
#define MOCK_FLASH_FLAGS  (MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT)
#else
#define MOCK_FLASH_HINT   ((void *)0x10000000UL)
#define MOCK_FLASH_FLAGS  (MAP_PRIVATE | MAP_ANONYMOUS)
#endif
#define MOCK_FLASH_SIZE   0x4000

static uint8_t *flash;

static uint32_t flash_addr(uint32_t off)
{
    return (uint32_t)(uintptr_t)(flash + off);
}

static void setup(void)
{
    if (flash == NULL) {
        flash = mmap(MOCK_FLASH_HINT, MOCK_FLASH_SIZE, PROT_READ | PROT_WRITE,
                MOCK_FLASH_FLAGS, -1, 0);
        ck_assert_ptr_ne(flash, MAP_FAILED);
        ck_assert_uint_lt((uintptr_t)flash + MOCK_FLASH_SIZE, 0xFFFFFFFFUL);
    }
    memset(flash, 0xAA, MOCK_FLASH_SIZE);
    memset(mock_rramc, 0, sizeof(mock_rramc));
    memset(mock_icache, 0, sizeof(mock_icache));
    memset(mock_timer, 0, sizeof(mock_timer));
    mock_rramc[0x400 / 4] = RRAMC_READY_READY_Msk;
    mock_rramc[0x404 / 4] = RRAMC_READYNEXT_READYNEXT_Msk;
    mock_commit_pending = 0U;
    mock_commits = 0;
    mock_wen_at_commit = 0;
    mock_pll_busy_reads = 0;
    mock_icache_busy_reads = 0;
    memset(mock_cracen, 0, sizeof(mock_cracen));
    memset(mock_cracencore, 0xAA, sizeof(mock_cracencore));
    mock_rng_control = 0U;
    mock_rng_control_writes = 0;
    mock_rng_state = 2U; /* IDLERON */
    mock_rng_error_until_reset = 0;
    mock_rng_empty_until_reset = 0;
    mock_rng_fifolevel = 16U;
    mock_rng_fifo_word = 0x00010203U;
}

static void assert_committed_and_locked(void)
{
    ck_assert_int_eq(mock_commits, 1);
    ck_assert_int_eq(mock_wen_at_commit, 1);
    ck_assert_uint_eq(mock_rramc[0x500 / 4] & RRAMC_CONFIG_WEN_Msk, 0U);
    ck_assert_uint_eq(mock_icache[0x008 / 4],
        ICACHE_TASKS_INVALIDATECACHE_Trigger);
}

START_TEST(test_write_unaligned_keeps_neighbours)
{
    uint8_t src[8] = { 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
    int i;

    ck_assert_int_eq(hal_flash_write(flash_addr(0x103), src + 1, 6), 0);
    for (i = 0; i < 6; i++)
        ck_assert_uint_eq(flash[0x103 + i], src[1 + i]);
    ck_assert_uint_eq(flash[0x102], 0xAA);
    ck_assert_uint_eq(flash[0x109], 0xAA);
    assert_committed_and_locked();
}
END_TEST

START_TEST(test_write_aligned_words)
{
    uint32_t src[16];
    int i;

    for (i = 0; i < 16; i++)
        src[i] = 0x01020304U * (uint32_t)(i + 1);
    ck_assert_int_eq(hal_flash_write(flash_addr(0x1000), (uint8_t *)src,
        sizeof(src)), 0);
    ck_assert_mem_eq(flash + 0x1000, src, sizeof(src));
    ck_assert_uint_eq(flash[0x0FFF], 0xAA);
    ck_assert_uint_eq(flash[0x1000 + sizeof(src)], 0xAA);
    assert_committed_and_locked();
}
END_TEST

START_TEST(test_erase_sector)
{
    int i;

    mock_icache_busy_reads = 2;
    ck_assert_int_eq(hal_flash_erase(flash_addr(0x1000), 0x1000), 0);
    ck_assert_int_eq(mock_icache_busy_reads, 0);
    for (i = 0x1000; i < 0x2000; i++)
        ck_assert_uint_eq(flash[i], 0xFF);
    ck_assert_uint_eq(flash[0x0FFF], 0xAA);
    ck_assert_uint_eq(flash[0x2000], 0xAA);
    assert_committed_and_locked();
}
END_TEST

START_TEST(test_erase_partial_chunk)
{
    int i;

    ck_assert_int_eq(hal_flash_erase(flash_addr(0x3002), 101), 0);
    for (i = 0x3002; i < 0x3002 + 101; i++)
        ck_assert_uint_eq(flash[i], 0xFF);
    ck_assert_uint_eq(flash[0x3001], 0xAA);
    ck_assert_uint_eq(flash[0x3002 + 101], 0xAA);
    assert_committed_and_locked();
}
END_TEST

START_TEST(test_cpu_clock_128mhz)
{
    mock_pll_freq = 3U;
    mock_pll_busy_reads = 3;
    cpu_clock_init();
    ck_assert_uint_eq(mock_pll_freq, OSCILLATORS_PLL_FREQ_FREQ_CK128M);
    ck_assert_int_eq(mock_pll_busy_reads, 0);
}
END_TEST

START_TEST(test_icache_enabled)
{
    mock_icache_busy_reads = 3;
    icache_init();
    ck_assert_int_eq(mock_icache_busy_reads, 0);
    ck_assert_uint_eq(mock_icache[0x404 / 4], ICACHE_ENABLE_ENABLE_Enabled);
    ck_assert_uint_eq(mock_icache[0x008 / 4],
        ICACHE_TASKS_INVALIDATECACHE_Trigger);
}
END_TEST

START_TEST(test_timer_1mhz_and_wrap)
{
    uint64_t t0, t1;

    bench_timer_init();
    ck_assert_uint_eq(mock_timer[0x510 / 4], 4U); /* 16 MHz / 2^4 */
    ck_assert_uint_eq(mock_timer[0x508 / 4], TIMER_BITMODE_BITMODE_32Bit);
    ck_assert_uint_eq(mock_timer[0x000 / 4], TIMER_TASK_Trigger);

    mock_timer[0x540 / 4] = 0xFFFFFFF0U;
    t0 = hal_get_timer_us();
    mock_timer[0x540 / 4] = 0x10U;
    t1 = hal_get_timer_us();
    ck_assert_uint_eq(mock_timer[0x040 / 4], TIMER_TASK_Trigger);
    ck_assert_uint_eq(t1 - t0, 0x20U);
}
END_TEST

START_TEST(test_variant_map)
{
#ifdef NRF54LM20
    ck_assert_uint_eq(FLASH_SIZE, 0x1FD000U);
    ck_assert_uint_eq(RRAMC_BASE_DEFAULT, 0x5004E000U);
    ck_assert_uint_eq(SPIM00_BASE_DEFAULT, 0x5004D000U);
    ck_assert_uint_eq(CRACEN_BASE_DEFAULT, 0x50059000U);
    ck_assert_uint_eq(CRACENCORE_BASE_DEFAULT, 0x50010000U);
    ck_assert_uint_eq(TAMPC_BASE, 0x500EF000U);
    ck_assert_int_eq(PIN_TX_MONITOR, 16);
    ck_assert_int_eq(PIN_RX_MONITOR, 17);
    ck_assert_int_eq(PIN_TX_DOWNLOAD, 6);
    ck_assert_int_eq(PIN_RX_DOWNLOAD, 7);
    ck_assert_uint_eq(hal_gpio_port_base(3), 0x500D8600U);
#else
    ck_assert_uint_eq(FLASH_SIZE, 0x17D000U);
    ck_assert_uint_eq(RRAMC_BASE_DEFAULT, 0x5004B000U);
    ck_assert_uint_eq(SPIM00_BASE_DEFAULT, 0x5004A000U);
    ck_assert_uint_eq(CRACEN_BASE_DEFAULT, 0x50048000U);
    ck_assert_uint_eq(CRACENCORE_BASE_DEFAULT, 0x51800000U);
    ck_assert_uint_eq(TAMPC_BASE, 0x500DC000U);
    ck_assert_int_eq(PIN_TX_MONITOR, 4);
    ck_assert_int_eq(PIN_RX_MONITOR, 5);
    ck_assert_int_eq(PIN_TX_DOWNLOAD, 0);
    ck_assert_int_eq(PIN_RX_DOWNLOAD, 1);
    ck_assert_uint_eq(hal_gpio_port_base(3), GPIO_P0_S_BASE);
#endif
    ck_assert_uint_eq(UARTE20_S_BASE, 0x500C6000U);
    ck_assert_uint_eq(UARTE30_S_BASE, 0x50104000U);
    ck_assert_int_eq(PORT_MONITOR, 1);
}
END_TEST

START_TEST(test_trng_init_registers)
{
    hal_trng_init();
    ck_assert_uint_eq(mock_cracen[0x400 / 4] & CRACEN_ENABLE_RNG_Msk,
        CRACEN_ENABLE_RNG_Msk);
    ck_assert_uint_eq(mock_rng_control, CRACENCORE_RNG_CONTROL_ENABLE_Msk |
        (4U << CRACENCORE_RNG_CONTROL_NB128BITBLOCKS_Pos));
    ck_assert_uint_eq(mock_cracencore[0x1034 / 4], 512U);
#ifdef NRF54LM20
    /* SAMPLINGPERIOD keeps its reset value; 0x1040 does not exist */
    ck_assert_uint_eq(mock_cracencore[0x1044 / 4], 0xFFFU);
    ck_assert_uint_eq(mock_cracencore[0x1040 / 4], 0xAAAAAAAAU);
#else
    ck_assert_uint_eq(mock_cracencore[0x1044 / 4], 0U);
    ck_assert_uint_eq(mock_cracencore[0x1040 / 4], 0U);
#endif
    ck_assert_int_eq(trng_ready, 1);
}
END_TEST

START_TEST(test_trng_entropy_reads_fifo)
{
    uint8_t out[7];

    hal_trng_init();
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), 0);
    ck_assert_uint_eq(out[0], 0x07);
    ck_assert_uint_eq(out[3], 0x04);
    ck_assert_uint_eq(out[4], 0x0B);
    ck_assert_uint_eq(out[6], 0x09);
}
END_TEST

START_TEST(test_trng_error_fails_without_hang)
{
    uint8_t out[8];

    mock_rng_error_until_reset = 100;
    hal_trng_init();
    ck_assert_int_eq(trng_ready, 0);
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), -1);
}
END_TEST

START_TEST(test_trng_error_recovers_after_reset)
{
    uint8_t out[8];

    hal_trng_init();
    mock_rng_error_until_reset = 2;
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), 0);
    ck_assert_int_eq(mock_rng_control_writes, 4);
    ck_assert_int_eq(trng_ready, 1);
}
END_TEST

START_TEST(test_trng_fifo_starved_fails)
{
    uint8_t out[4];

    hal_trng_init();
    mock_rng_empty_until_reset = 100;
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), -1);
    ck_assert_int_eq(mock_rng_control_writes, 4);
    ck_assert_int_eq(trng_ready, 0);
}
END_TEST

START_TEST(test_trng_fifo_recovers_after_timeout)
{
    uint8_t out[4];

    hal_trng_init();
    mock_rng_empty_until_reset = 2;
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), 0);
    ck_assert_int_eq(mock_rng_control_writes, 4);
}
END_TEST

START_TEST(test_prepare_boot_flushes_cache_and_stops_timer)
{
    hal_prepare_boot();
    ck_assert_uint_eq(mock_icache[0x008 / 4],
        ICACHE_TASKS_INVALIDATECACHE_Trigger);
    ck_assert_uint_eq(mock_timer[0x004 / 4], TIMER_TASK_Trigger);
}
END_TEST

START_TEST(test_trng_startup_timeout)
{
    uint8_t out[4];

    mock_rng_state = 1U; /* stuck in STARTUP */
    hal_trng_init();
    ck_assert_int_eq(trng_ready, 0);
    ck_assert_int_eq(hal_trng_get_entropy(out, sizeof(out)), -1);
}
END_TEST

static Suite *nrf54l_suite(void)
{
    Suite *s = suite_create("nrf54l-flash");
    TCase *tc = tcase_create("nrf54l-flash");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_write_unaligned_keeps_neighbours);
    tcase_add_test(tc, test_write_aligned_words);
    tcase_add_test(tc, test_erase_sector);
    tcase_add_test(tc, test_erase_partial_chunk);
    tcase_add_test(tc, test_cpu_clock_128mhz);
    tcase_add_test(tc, test_icache_enabled);
    tcase_add_test(tc, test_timer_1mhz_and_wrap);
    tcase_add_test(tc, test_variant_map);
    tcase_add_test(tc, test_trng_init_registers);
    tcase_add_test(tc, test_trng_entropy_reads_fifo);
    tcase_add_test(tc, test_trng_error_fails_without_hang);
    tcase_add_test(tc, test_trng_error_recovers_after_reset);
    tcase_add_test(tc, test_trng_startup_timeout);
    tcase_add_test(tc, test_trng_fifo_starved_fails);
    tcase_add_test(tc, test_trng_fifo_recovers_after_timeout);
    tcase_add_test(tc, test_prepare_boot_flushes_cache_and_stops_timer);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = nrf54l_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
