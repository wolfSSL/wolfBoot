/* unit-flash-erase-l0.c
 *
 * Unit tests for the page-loop bound in hal_flash_erase() (hal/stm32l0.c).
 * Regression for F-3965: end_address = address + len - 1 (inclusive) combined
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
#include <string.h>

/* hal/stm32l0.c is tightly coupled to STM32L0 hardware registers.
 * Compile only hal_flash_erase() in isolation by defining this guard;
 * all other functions and hardware macros are excluded and replaced below. */
#define WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* RAMFUNCTION must be empty on the host */
#define RAMFUNCTION

/* DMB is a no-op on the host */
#define DMB() /* nothing */

/* Mocked flash control/status registers */
static uint32_t mock_FLASH_PECR;
static uint32_t mock_FLASH_SR;
#define FLASH_PECR mock_FLASH_PECR
#define FLASH_SR   mock_FLASH_SR

/* Mock flash memory: 4 pages of FLASH_PAGE_SIZE (128) bytes each.
 * hal_flash_erase writes 0xFFFFFFFF to the first word of each erased page via:
 *   *(volatile uint32_t *)(p + FLASHMEM_ADDRESS_SPACE) = 0xFFFFFFFF
 * Redirect that write into this buffer by making FLASHMEM_ADDRESS_SPACE the
 * buffer's base address and calling hal_flash_erase with address = 0. */
#define MOCK_FLASH_PAGES 4
#define MOCK_FLASH_SIZE  (MOCK_FLASH_PAGES * 128)
static uint8_t mock_flash_mem[MOCK_FLASH_SIZE];
#define FLASHMEM_ADDRESS_SPACE ((uintptr_t)mock_flash_mem)

#include "../../hal/stm32l0.c"

static void reset_mocks(void)
{
    mock_FLASH_PECR = 0;
    mock_FLASH_SR   = 0;
    memset(mock_flash_mem, 0xAA, sizeof(mock_flash_mem));
}

/* Return 1 if the page at index `page` had its first word written to 0xFFFFFFFF. */
static int page_erased(int page)
{
    uint32_t word;
    memcpy(&word, &mock_flash_mem[page * FLASH_PAGE_SIZE], sizeof(word));
    return word == 0xFFFFFFFFu;
}

/* Erasing exactly one page must erase that page and nothing beyond it. */
START_TEST(test_erase_single_page_aligned)
{
    reset_mocks();
    hal_flash_erase(0, FLASH_PAGE_SIZE);

    ck_assert_int_eq(page_erased(0), 1);
    ck_assert_int_eq(page_erased(1), 0);
}
END_TEST

/* Erasing two full pages must erase both and not touch the third. */
START_TEST(test_erase_two_pages_aligned)
{
    reset_mocks();
    hal_flash_erase(0, 2 * FLASH_PAGE_SIZE);

    ck_assert_int_eq(page_erased(0), 1);
    ck_assert_int_eq(page_erased(1), 1);
    ck_assert_int_eq(page_erased(2), 0);
}
END_TEST

/* Regression for F-3965: len = PAGE_SIZE + 1 spans two pages; both must be
 * erased.  Before the fix, end_address = address + len - 1 landed exactly on
 * the first byte of page 1, so the strict `p < end_address` guard excluded it. */
START_TEST(test_erase_unaligned_len_covers_last_page)
{
    reset_mocks();
    /* len = 129: bytes [0..128], crossing into page 1 */
    hal_flash_erase(0, FLASH_PAGE_SIZE + 1);

    ck_assert_int_eq(page_erased(0), 1);
    ck_assert_int_eq(page_erased(1), 1);
    ck_assert_int_eq(page_erased(2), 0);
}
END_TEST

Suite *flash_erase_l0_suite(void)
{
    Suite *s  = suite_create("flash-erase-l0");
    TCase *tc = tcase_create("flash-erase-l0");

    tcase_add_test(tc, test_erase_single_page_aligned);
    tcase_add_test(tc, test_erase_two_pages_aligned);
    tcase_add_test(tc, test_erase_unaligned_len_covers_last_page);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite   *s  = flash_erase_l0_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
