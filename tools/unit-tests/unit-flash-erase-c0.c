/* unit-flash-erase-c0.c
 *
 * Unit tests for the page-loop bound in hal_flash_erase() (hal/stm32c0.c).
 * Regression for F-3963: end_address = address + len - 1 (inclusive) combined
 * with p < end_address (strict) skips the final page when len % PAGE_SIZE != 0.
 *
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
#include <stdio.h>

/* hal/stm32c0.c is tightly coupled to STM32C0 hardware registers.
 * Compile only hal_flash_erase() in isolation by defining this guard;
 * all other functions and hardware macros are excluded and replaced below. */
#define WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* RAMFUNCTION must be empty on the host */
#define RAMFUNCTION

/* DMB is a no-op on the host (stm32c0 DMB fires before STRT is written, so
 * we capture from flash_wait_complete instead). */
#define DMB() /* nothing */

/* Mocked flash control/status registers */
static uint32_t mock_FLASH_CR;
static uint32_t mock_FLASH_SR;
#define FLASH_CR mock_FLASH_CR
#define FLASH_SR mock_FLASH_SR

/* Constants from hal/stm32c0.c (must mirror exactly). */
#define FLASH_CR_STRT           (1 << 16)
#define FLASH_CR_PER            (1 << 1)
#define FLASH_CR_PNB_SHIFT      3
#define FLASH_CR_PNB_MASK       0x7f

/* Record the FLASH_CR value on each page-erase command (captured at the
 * start of flash_wait_complete, when STRT has just been written). */
#define ERASE_LOG_MAX 64
static uint32_t erase_cr[ERASE_LOG_MAX];
static int erase_log_n;

/* Stubs for functions called by hal_flash_erase().
 * flash_wait_complete is called immediately after FLASH_CR |= FLASH_CR_STRT,
 * so it is the right place to capture the current register state.
 * It clears STRT to mirror what real hardware does automatically. */
static void flash_wait_complete(void)
{
    if ((mock_FLASH_CR & FLASH_CR_STRT) && erase_log_n < ERASE_LOG_MAX) {
        erase_cr[erase_log_n] = mock_FLASH_CR;
        erase_log_n++;
    }
    mock_FLASH_CR &= ~FLASH_CR_STRT;
}
static void flash_clear_errors(void) {}

#include "../../hal/stm32c0.c"

/* Decode the page number field from a captured FLASH_CR value. */
static uint32_t page_of(uint32_t cr)
{
    return (cr >> FLASH_CR_PNB_SHIFT) & FLASH_CR_PNB_MASK;
}

static void reset_mocks(void)
{
    mock_FLASH_CR = 0;
    mock_FLASH_SR = 0;
    erase_log_n   = 0;
}

/* Erasing exactly one page must issue exactly one erase command. */
START_TEST(test_erase_single_page_aligned)
{
    reset_mocks();
    hal_flash_erase(0x08000000UL, FLASH_PAGE_SIZE);

    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(page_of(erase_cr[0]), 0);
}
END_TEST

/* Erasing two full pages must issue exactly two erase commands. */
START_TEST(test_erase_two_pages_aligned)
{
    reset_mocks();
    hal_flash_erase(0x08000000UL, 2 * FLASH_PAGE_SIZE);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(page_of(erase_cr[0]), 0);
    ck_assert_uint_eq(page_of(erase_cr[1]), 1);
}
END_TEST

/* Regression for F-3963: len = PAGE_SIZE + 1 spans two pages and both must be
 * erased.  Before the fix, end_address = address + len - 1 landed exactly on
 * the start of page 1, so the strict `p < end_address` guard excluded it. */
START_TEST(test_erase_unaligned_len_covers_last_page)
{
    reset_mocks();
    /* len = 0x801: bytes [0..0x800], crossing into page 1 */
    hal_flash_erase(0x08000000UL, FLASH_PAGE_SIZE + 1);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(page_of(erase_cr[0]), 0);
    ck_assert_uint_eq(page_of(erase_cr[1]), 1);
}
END_TEST

Suite *flash_erase_c0_suite(void)
{
    Suite *s  = suite_create("flash-erase-c0");
    TCase *tc = tcase_create("flash-erase-c0");

    tcase_add_test(tc, test_erase_single_page_aligned);
    tcase_add_test(tc, test_erase_two_pages_aligned);
    tcase_add_test(tc, test_erase_unaligned_len_covers_last_page);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite   *s  = flash_erase_c0_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
