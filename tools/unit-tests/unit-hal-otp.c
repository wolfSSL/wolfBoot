/* unit-hal-otp.c
 *
 * Unit tests for OTP block rounding helpers.
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

#include "../../include/hal_otp.h"

#define FLASH_OTP_BLOCK_SIZE 64U
#define OTP_BLOCKS 32U

START_TEST(test_rounds_partial_block_before_bounds_check)
{
    uint32_t start_block = OTP_BLOCKS - 1U;
    uint32_t count = hal_otp_blocks_for_length(1U, FLASH_OTP_BLOCK_SIZE);

    ck_assert_uint_eq(count, 1U);
    ck_assert_uint_gt(start_block + count, OTP_BLOCKS - 1U);
}
END_TEST

START_TEST(test_exact_multiple_keeps_exact_block_count)
{
    ck_assert_uint_eq(
        hal_otp_blocks_for_length(FLASH_OTP_BLOCK_SIZE * 2U, FLASH_OTP_BLOCK_SIZE),
        2U);
}
END_TEST

static Suite *hal_otp_suite(void)
{
    Suite *s = suite_create("hal-otp");
    TCase *tc = tcase_create("rounding");

    tcase_add_test(tc, test_rounds_partial_block_before_bounds_check);
    tcase_add_test(tc, test_exact_multiple_keeps_exact_block_count);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = hal_otp_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
