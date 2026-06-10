/* unit-flash-erase-h7.c
 *
 * Unit tests for the dual-bank sector arithmetic in hal_flash_erase()
 * (hal/stm32h7.c).
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

/* hal/stm32h7.c is tightly coupled to STM32H7 hardware registers and to a
 * partition layout from target.h (its header even #errors on small partition
 * sizes). Compile only hal_flash_erase() in isolation and provide host-side
 * mocks for the few register accesses and constants it needs. The constant
 * values mirror hal/stm32h7.h. */
#define WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* Constants (mirror hal/stm32h7.h) */
#define FLASHMEM_ADDRESS_SPACE  (0x08000000UL)
#define FLASH_PAGE_SIZE         (0x20000)            /* 128KB */
#define FLASH_BANK2_BASE        (0x08100000UL)
#define FLASH_BANK2_BASE_REL    (FLASH_BANK2_BASE - FLASHMEM_ADDRESS_SPACE)
#define FLASH_TOP               (0x081FFFFFUL)
#define FLASH_CR_SNB_SHIFT      8
#define FLASH_CR_SNB_MASK       0x7
#define FLASH_CR_STRT           (1 << 7)
#define FLASH_CR_PSIZE          (1 << 4)
#define FLASH_CR_SER            (1 << 2)
#define FLASH_BANK_1            0
#define FLASH_BANK_2            1

#define RAMFUNCTION

/* Mocked flash control registers */
static uint32_t mock_FLASH_CR1;
static uint32_t mock_FLASH_CR2;
#define FLASH_CR1 mock_FLASH_CR1
#define FLASH_CR2 mock_FLASH_CR2

/* Capture every programmed (CR1, CR2) pair. The driver issues a DMB() right
 * after writing the sector number into FLASH_CRx and before setting STRT, so
 * recording on DMB() snapshots each erase command exactly once per loop
 * iteration. */
#define ERASE_LOG_MAX 64
static uint32_t erase_cr1[ERASE_LOG_MAX];
static uint32_t erase_cr2[ERASE_LOG_MAX];
static int erase_log_n;

#define DMB() do { \
    if (erase_log_n < ERASE_LOG_MAX) { \
        erase_cr1[erase_log_n] = mock_FLASH_CR1; \
        erase_cr2[erase_log_n] = mock_FLASH_CR2; \
        erase_log_n++; \
    } \
} while (0)

/* hal_flash_erase() polls hardware via flash_wait_complete(); a no-op suffices
 * on the host. */
static void flash_wait_complete(uint8_t bank) { (void)bank; }

static void reset_mocks(void)
{
    mock_FLASH_CR1 = 0;
    mock_FLASH_CR2 = 0;
    erase_log_n = 0;
}

#include "../../hal/stm32h7.c"

/* Decode the SNB sector field programmed into a captured CRx value. */
static uint32_t snb_of(uint32_t cr)
{
    return (cr >> FLASH_CR_SNB_SHIFT) & FLASH_CR_SNB_MASK;
}

/* Regression for F-5129: the Bank 2 branch subtracted the absolute
 * FLASH_BANK2_BASE (0x08100000) from the relative offset p instead of
 * FLASH_BANK2_BASE_REL (0x00100000). That underflowed uint32_t, so the wrong
 * sector index was programmed into FLASH_CR2 and the loop variable was clobbered
 * to ~0xF8000000, terminating any multi-sector Bank 2 erase after a single
 * iteration. The canonical stm32h7 SWAP lives at 0x081C0000 (Bank 2 sector 6). */
START_TEST(test_erase_bank2_two_sectors)
{
    reset_mocks();
    /* Erase Bank 2 sectors 6 and 7 (0x081C0000 .. 0x081FFFFF). */
    hal_flash_erase(0x081C0000UL, 2 * FLASH_PAGE_SIZE);

    /* Both sectors must be erased: two FLASH_CR2 commands, not one. */
    ck_assert_int_eq(erase_log_n, 2);
    /* Each command must target the correct Bank 2 sector. */
    ck_assert_uint_eq(snb_of(erase_cr2[0]), 6);
    ck_assert_uint_eq(snb_of(erase_cr2[1]), 7);
}
END_TEST

/* A single-sector Bank 2 erase must program the actually requested sector, not
 * sector 0 (the underflow always produced sector 0). */
START_TEST(test_erase_bank2_single_sector)
{
    reset_mocks();
    hal_flash_erase(0x081C0000UL, FLASH_PAGE_SIZE);

    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(snb_of(erase_cr2[0]), 6);
}
END_TEST

/* Bank 1 path is unaffected and must still walk every requested sector. */
START_TEST(test_erase_bank1_two_sectors)
{
    reset_mocks();
    /* Bank 1 sectors 0 and 1 (0x08000000 .. 0x0803FFFF). */
    hal_flash_erase(0x08000000UL, 2 * FLASH_PAGE_SIZE);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(snb_of(erase_cr1[0]), 0);
    ck_assert_uint_eq(snb_of(erase_cr1[1]), 1);
}
END_TEST

/* Regression for F-5745: when len % FLASH_PAGE_SIZE != 0 the final page was
 * skipped because end_address = base + len - 1 lands exactly on the start of
 * that last page and the strict `p < end_address` guard excludes it. */
START_TEST(test_erase_unaligned_len_covers_last_page)
{
    reset_mocks();
    /* len = PAGE_SIZE + 1: two pages must be erased (sectors 0 and 1). */
    hal_flash_erase(0x08000000UL, FLASH_PAGE_SIZE + 1);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(snb_of(erase_cr1[0]), 0);
    ck_assert_uint_eq(snb_of(erase_cr1[1]), 1);
}
END_TEST

Suite *flash_erase_suite(void)
{
    Suite *s = suite_create("flash-erase-h7");
    TCase *tc = tcase_create("flash-erase-h7");

    tcase_add_test(tc, test_erase_bank2_two_sectors);
    tcase_add_test(tc, test_erase_bank2_single_sector);
    tcase_add_test(tc, test_erase_bank1_two_sectors);
    tcase_add_test(tc, test_erase_unaligned_len_covers_last_page);

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
