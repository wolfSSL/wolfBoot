/* unit-flash-erase-mcxw.c
 *
 * Unit tests for the sector stride in hal_flash_erase() (hal/mcxw.c).
 * Regression for F-7383: the start address was rounded down with the runtime
 * pflash_sector_size (queried from FLASH_GetProperty() in hal_init()) while
 * the loop stepped address/len by the compile-time WOLFBOOT_SECTOR_SIZE.
 * When the two differ, a larger WOLFBOOT_SECTOR_SIZE steps over hardware
 * sectors inside the requested range, leaving them unerased.
 * hal/mcxn.c already uses one consistent sector_size, with a zero guard.
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

#include <check.h>
#include <stdint.h>

/* hal/mcxw.c is tightly coupled to the MCXW FMU registers and to the (not
 * vendored) NXP MCUXpresso SDK headers. Compile only hal_flash_erase() in
 * isolation by defining this guard; everything else is excluded and replaced
 * below. */
#define WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* RAMFUNCTION must be empty on the host */
#define RAMFUNCTION

/* Same value as config/examples/mcxw.config */
#define WOLFBOOT_SECTOR_SIZE 0x2000

/* Record every sector-erase command issued by hal_flash_erase(). */
#define ERASE_LOG_MAX 64
static uint32_t erase_addr[ERASE_LOG_MAX];
static int erase_log_n;

static void erase_flash_sector(uint32_t *dst)
{
    if (erase_log_n < ERASE_LOG_MAX)
        erase_addr[erase_log_n] = (uint32_t)(uintptr_t)dst;
    erase_log_n++;
}

#include "../../hal/mcxw.c"

#define FLASH_BASE 0x00008000UL

static void reset_mocks(uint32_t sector_size)
{
    erase_log_n = 0;
    pflash_sector_size = sector_size;
}

/* Baseline: runtime and compile-time sector size agree (the stock mcxw
 * configuration), so two sectors take exactly two erase commands. */
START_TEST(test_erase_two_sectors_matching_size)
{
    reset_mocks(WOLFBOOT_SECTOR_SIZE);
    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, 2 * WOLFBOOT_SECTOR_SIZE), 0);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(erase_addr[0], FLASH_BASE);
    ck_assert_uint_eq(erase_addr[1], FLASH_BASE + WOLFBOOT_SECTOR_SIZE);
}
END_TEST

/* Regression for F-7383: the part reports 4KB hardware sectors while
 * WOLFBOOT_SECTOR_SIZE is 8KB. Erasing 0x4000 bytes must issue four erase
 * commands, one per hardware sector. Before the fix the loop stepped by
 * WOLFBOOT_SECTOR_SIZE and issued only two, leaving the sectors at
 * FLASH_BASE + 0x1000 and FLASH_BASE + 0x3000 unerased. */
START_TEST(test_erase_runtime_sector_smaller_covers_range)
{
    int i;

    reset_mocks(0x1000);
    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, 0x4000), 0);

    ck_assert_int_eq(erase_log_n, 4);
    for (i = 0; i < 4; i++)
        ck_assert_uint_eq(erase_addr[i], FLASH_BASE + (uint32_t)i * 0x1000U);
}
END_TEST

/* The mirror case: the part reports 16KB sectors. Every erase command must
 * land on a hardware sector boundary; before the fix the 8KB step issued
 * commands in the middle of a sector, and erased the same sector twice. */
START_TEST(test_erase_runtime_sector_larger_stays_aligned)
{
    reset_mocks(0x4000);
    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, 0x8000), 0);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(erase_addr[0], FLASH_BASE);
    ck_assert_uint_eq(erase_addr[1], FLASH_BASE + 0x4000U);
}
END_TEST

/* An unaligned start address is rounded down to the runtime sector boundary,
 * and the stride keeps every following command aligned too. */
START_TEST(test_erase_unaligned_start_rounds_down)
{
    reset_mocks(0x1000);
    ck_assert_int_eq(hal_flash_erase(FLASH_BASE + 0x800, 0x1800), 0);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(erase_addr[0], FLASH_BASE);
    ck_assert_uint_eq(erase_addr[1], FLASH_BASE + 0x1000U);
}
END_TEST

/* FLASH_GetProperty() failing to report a size must not divide by zero:
 * fall back to WOLFBOOT_SECTOR_SIZE, as hal/mcxn.c does. */
START_TEST(test_erase_zero_runtime_sector_falls_back)
{
    reset_mocks(0);
    ck_assert_int_eq(hal_flash_erase(FLASH_BASE + 0x10, WOLFBOOT_SECTOR_SIZE),
            0);

    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(erase_addr[0], FLASH_BASE);
}
END_TEST

Suite *flash_erase_suite(void)
{
    Suite *s = suite_create("flash-erase-mcxw");
    TCase *tc = tcase_create("flash-erase-mcxw");

    tcase_add_test(tc, test_erase_two_sectors_matching_size);
    tcase_add_test(tc, test_erase_runtime_sector_smaller_covers_range);
    tcase_add_test(tc, test_erase_runtime_sector_larger_stays_aligned);
    tcase_add_test(tc, test_erase_unaligned_start_rounds_down);
    tcase_add_test(tc, test_erase_zero_runtime_sector_falls_back);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = flash_erase_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
