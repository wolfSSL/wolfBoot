/* unit-flash-erase-h7s.c
 *
 * Tests hal_flash_erase() from hal/stm32h7s.c.
 *
 * STM32H7RS internal flash is a single bank of eight 8 KB sectors. An
 * erase is armed by putting the sector number in CR.SSN and setting SER
 * and START in the same register write, so the sector arithmetic is the
 * whole of the logic and an off-by-one erases the wrong sector. The
 * STM32H7 twin of this test exists because that family's bank-relative
 * arithmetic underflowed (F-5129); here the equivalent hazard is an
 * address below the flash base, or a length that runs past the last
 * sector, both of which must be rejected rather than wrapping.
 *
 * hal/stm32h7s.c is coupled to the device registers and to a partition
 * layout from target.h, so only hal_flash_erase() is compiled, via
 * WOLFBOOT_UNIT_TEST_FLASH_ERASE, with host-side mocks for the few
 * registers and helpers it touches. The constants mirror hal/stm32h7s.h.
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

#define WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* Constants (mirror hal/stm32h7s.h) */
#define FLASHMEM_ADDRESS_SPACE  (0x08000000UL)
#define STM32H7S_SECTOR_SIZE    (0x2000)
#define STM32H7S_SECTOR_COUNT   (8)
#define FLASH_CR_SER            (1 << 2)
#define FLASH_CR_BER            (1 << 3)
#define FLASH_CR_START          (1 << 5)
#define FLASH_CR_SSN_SHIFT      6
#define FLASH_CR_SSN_MASK       0x7
#define FLASH_CR_ALL_BANKS      (1 << 24)

#define STM32H7S_FLASH_SIZE     (STM32H7S_SECTOR_SIZE * STM32H7S_SECTOR_COUNT)

#define FLASH_ISR_WRPERRF       (1 << 17)
#define FLASH_ISR_PGSERRF       (1 << 18)
#define FLASH_ISR_STRBERRF      (1 << 19)
#define FLASH_ISR_INCERRF       (1 << 21)
#define FLASH_ISR_SNECCERRF     (1 << 25)
#define FLASH_ISR_DBECCERRF     (1 << 26)
#define FLASH_ISR_ERRORS        (FLASH_ISR_WRPERRF | FLASH_ISR_PGSERRF | \
                                 FLASH_ISR_STRBERRF | FLASH_ISR_INCERRF | \
                                 FLASH_ISR_SNECCERRF | FLASH_ISR_DBECCERRF)

#define RAMFUNCTION

/* Mocked flash registers */
static uint32_t mock_FLASH_CR;
static uint32_t mock_FLASH_ISR;
#define FLASH_CR mock_FLASH_CR
#define FLASH_ISR mock_FLASH_ISR

/* Capture every programmed CR value. The driver issues a DMB() straight
 * after writing the sector number plus SER and START, and before the
 * busy wait, so recording on DMB() snapshots each erase command exactly
 * once per loop iteration. */
#define ERASE_LOG_MAX 32
static uint32_t erase_cr[ERASE_LOG_MAX];
static int erase_log_n;

#define DMB() do { \
    if (erase_log_n < ERASE_LOG_MAX) { \
        erase_cr[erase_log_n] = mock_FLASH_CR; \
        erase_log_n++; \
    } \
} while (0)

/* hal_flash_erase() polls the device and clears error flags; no-ops are
 * enough on the host. */
static void flash_wait_complete(void) { }
static void flash_clear_errors(void) { }

#include "../../hal/stm32h7s.c"

static void setup(void)
{
    mock_FLASH_CR = 0;
    mock_FLASH_ISR = 0;
    erase_log_n = 0;
}

static void teardown(void)
{
}

/* Decode the SSN sector field programmed into a captured CR value. */
static uint32_t ssn_of(uint32_t cr)
{
    return (cr >> FLASH_CR_SSN_SHIFT) & FLASH_CR_SSN_MASK;
}

START_TEST(test_erase_first_sector)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000, STM32H7S_SECTOR_SIZE), 0);

    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(ssn_of(erase_cr[0]), 0);
    ck_assert_uint_ne(erase_cr[0] & FLASH_CR_SER, 0);
    ck_assert_uint_ne(erase_cr[0] & FLASH_CR_START, 0);
    /* Bank erase must never be armed by a sector erase */
    ck_assert_uint_eq(erase_cr[0] & FLASH_CR_BER, 0);
    ck_assert_uint_eq(erase_cr[0] & FLASH_CR_ALL_BANKS, 0);
}
END_TEST

START_TEST(test_erase_middle_sector)
{
    /* Sector 3 starts at 0x08000000 + 3 * 0x2000 */
    ck_assert_int_eq(hal_flash_erase(0x08006000, STM32H7S_SECTOR_SIZE), 0);

    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(ssn_of(erase_cr[0]), 3);
}
END_TEST

START_TEST(test_erase_last_sector)
{
    /* Sector 7 is the last one; 0x08000000 + 7 * 0x2000 */
    ck_assert_int_eq(hal_flash_erase(0x0800E000, STM32H7S_SECTOR_SIZE), 0);

    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(ssn_of(erase_cr[0]), 7);
}
END_TEST

START_TEST(test_erase_whole_flash)
{
    int i;

    ck_assert_int_eq(hal_flash_erase(0x08000000,
        STM32H7S_SECTOR_SIZE * STM32H7S_SECTOR_COUNT), 0);

    ck_assert_int_eq(erase_log_n, STM32H7S_SECTOR_COUNT);
    for (i = 0; i < STM32H7S_SECTOR_COUNT; i++)
        ck_assert_uint_eq(ssn_of(erase_cr[i]), (uint32_t)i);
}
END_TEST

START_TEST(test_erase_spanning_two_sectors)
{
    ck_assert_int_eq(hal_flash_erase(0x08002000,
        STM32H7S_SECTOR_SIZE * 2), 0);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(ssn_of(erase_cr[0]), 1);
    ck_assert_uint_eq(ssn_of(erase_cr[1]), 2);
}
END_TEST

START_TEST(test_erase_zero_length_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000, 0), -1);
    ck_assert_int_eq(erase_log_n, 0);
}
END_TEST

START_TEST(test_erase_negative_length_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000, -1), -1);
    ck_assert_int_eq(erase_log_n, 0);
}
END_TEST

/* Past the last sector: the sector index would be 8, which does not fit
 * the three-bit SSN field and would alias onto sector 0. */
START_TEST(test_erase_past_end_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x08010000, STM32H7S_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_log_n, 0);
}
END_TEST

/* A length that starts in range but runs off the end must stop rather
 * than wrapping onto low sectors, and must still disarm the control
 * register on the way out: this path returns after at least one sector
 * has already set SER, so an early return that skips the cleanup leaves
 * sector-erase mode armed. */
START_TEST(test_erase_overrun_stops)
{
    ck_assert_int_eq(hal_flash_erase(0x0800E000,
        STM32H7S_SECTOR_SIZE * 4), -1);

    /* Sector 7 is erased, then the loop rejects sector 8 */
    ck_assert_int_eq(erase_log_n, 1);
    ck_assert_uint_eq(ssn_of(erase_cr[0]), 7);
    ck_assert_uint_eq(mock_FLASH_CR & FLASH_CR_SER, 0);
}
END_TEST

/* An address below the flash base underflows the relative offset. It
 * must be rejected, not wrapped into a valid-looking sector. */
START_TEST(test_erase_below_base_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x07FFE000, STM32H7S_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_log_n, 0);
}
END_TEST

START_TEST(test_ser_cleared_on_exit)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000, STM32H7S_SECTOR_SIZE), 0);
    ck_assert_uint_eq(mock_FLASH_CR & FLASH_CR_SER, 0);
}
END_TEST

/* A range that starts below the flash base wraps the relative offset, and
 * the wrapped end can compare lower than the start. Before the range was
 * validated up front that skipped the loop entirely and returned success
 * having erased nothing, which is worse than erasing the wrong sector. */
START_TEST(test_erase_crossing_base_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x07FFF000, STM32H7S_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_log_n, 0);
}
END_TEST

/* WOLFBOOT_SECTOR_SIZE on this target is the NOR subsector size, which is
 * smaller than an internal sector, so a caller-aligned address can sit
 * inside a sector. Both sectors the range touches must be erased. */
START_TEST(test_erase_unaligned_start_covers_both_sectors)
{
    ck_assert_int_eq(hal_flash_erase(0x08001000, STM32H7S_SECTOR_SIZE), 0);

    ck_assert_int_eq(erase_log_n, 2);
    ck_assert_uint_eq(ssn_of(erase_cr[0]), 0);
    ck_assert_uint_eq(ssn_of(erase_cr[1]), 1);
}
END_TEST

/* A length wider than the whole device would overflow the end offset. */
START_TEST(test_erase_oversized_length_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000,
        STM32H7S_FLASH_SIZE + STM32H7S_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_log_n, 0);
}
END_TEST

/* BSY/QW clearing says the operation finished, not that it worked. A
 * write-protected sector latches WRPERRF and stays unerased, so a zero
 * return there would let wolfBoot write into un-erased flash. */
START_TEST(test_erase_reports_error)
{
    mock_FLASH_ISR = FLASH_ISR_WRPERRF;

    ck_assert_int_eq(hal_flash_erase(0x08000000, STM32H7S_SECTOR_SIZE), -1);
    ck_assert_uint_eq(mock_FLASH_CR & FLASH_CR_SER, 0);
}
END_TEST

Suite *flash_erase_h7s_suite(void)
{
    Suite *s = suite_create("flash-erase-h7s");
    TCase *tc = tcase_create("flash-erase-h7s");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_erase_first_sector);
    tcase_add_test(tc, test_erase_middle_sector);
    tcase_add_test(tc, test_erase_last_sector);
    tcase_add_test(tc, test_erase_whole_flash);
    tcase_add_test(tc, test_erase_spanning_two_sectors);
    tcase_add_test(tc, test_erase_zero_length_rejected);
    tcase_add_test(tc, test_erase_negative_length_rejected);
    tcase_add_test(tc, test_erase_past_end_rejected);
    tcase_add_test(tc, test_erase_overrun_stops);
    tcase_add_test(tc, test_erase_below_base_rejected);
    tcase_add_test(tc, test_ser_cleared_on_exit);
    tcase_add_test(tc, test_erase_crossing_base_rejected);
    tcase_add_test(tc, test_erase_unaligned_start_covers_both_sectors);
    tcase_add_test(tc, test_erase_oversized_length_rejected);
    tcase_add_test(tc, test_erase_reports_error);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = flash_erase_h7s_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
