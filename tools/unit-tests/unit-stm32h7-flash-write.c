/* unit-stm32h7-flash-write.c
 *
 * Regression test for F-12871: hal_flash_write() in hal/stm32h7.c picked
 * the bank from the start address and programmed the whole request on
 * that bank, so a request crossing the bank boundary wrote its tail to
 * the wrong bank. The exported writer must now split such requests at
 * FLASH_BANK2_BASE_REL.
 *
 * The dispatcher is extracted by the Makefile; hal_flash_write_part()
 * is a recording mock.
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

#define RAMFUNCTION
#define FLASH_BANK_1 0
#define FLASH_BANK_2 1
#define FLASH_BANK2_BASE_REL 0x100000u

/* The real dispatcher from hal/stm32h7.c (extracted by the Makefile);
 * it calls hal_flash_write_part(), mocked below (prototype first). */
static int hal_flash_write_part(uint32_t address, const uint8_t *data, int len);

#include "stm32h7_flash_write_extract.h"

/* Recording mock for the per-bank programming pass. */
#define MAX_CALLS 4
static int g_calls;
static uint32_t g_addr[MAX_CALLS];
static int g_len[MAX_CALLS];
static int g_first_ret;

static int hal_flash_write_part(uint32_t address, const uint8_t *data, int len)
{
    int ret;

    (void)data;
    ck_assert_int_le(g_calls, MAX_CALLS - 1);
    g_addr[g_calls] = address;
    g_len[g_calls] = len;
    if (g_calls == 0)
        ret = g_first_ret;
    else
        ret = 0;
    g_calls++;
    return ret;
}

static void setup(void)
{
    g_calls = 0;
    g_first_ret = 0;
    memset(g_addr, 0, sizeof(g_addr));
    memset(g_len, 0, sizeof(g_len));
}

static void teardown(void)
{
}

/* A request crossing the boundary must be split: first pass up to the
 * boundary on bank 1, remainder starting at the boundary on bank 2. */
START_TEST(test_write_crosses_boundary){
    uint8_t data[64];

    memset(data, 0xAA, sizeof(data));

    ck_assert_int_eq(hal_flash_write(FLASH_BANK2_BASE_REL - 16, data, 32), 0);
    ck_assert_int_eq(g_calls, 2);
    ck_assert_uint_eq(g_addr[0], FLASH_BANK2_BASE_REL - 16);
    ck_assert_int_eq(g_len[0], 16);
    ck_assert_uint_eq(g_addr[1], FLASH_BANK2_BASE_REL);
    ck_assert_int_eq(g_len[1], 16);
}
END_TEST

/* A request fully inside bank 1 stays a single pass. */
START_TEST(test_write_bank1_only)
{
    uint8_t data[16];

    memset(data, 0xAA, sizeof(data));

    ck_assert_int_eq(hal_flash_write(0x400, data, 16), 0);
    ck_assert_int_eq(g_calls, 1);
    ck_assert_uint_eq(g_addr[0], 0x400);
    ck_assert_int_eq(g_len[0], 16);
}
END_TEST

/* A request fully inside bank 2 stays a single pass. */
START_TEST(test_write_bank2_only)
{
    uint8_t data[16];

    memset(data, 0xAA, sizeof(data));

    ck_assert_int_eq(hal_flash_write(FLASH_BANK2_BASE_REL + 0x400, data, 16),
                     0);
    ck_assert_int_eq(g_calls, 1);
    ck_assert_uint_eq(g_addr[0], FLASH_BANK2_BASE_REL + 0x400);
    ck_assert_int_eq(g_len[0], 16);
}
END_TEST

/* A failure in the first pass must stop the split and propagate. */
START_TEST(test_write_first_part_fails)
{
    uint8_t data[64];

    memset(data, 0xAA, sizeof(data));
    g_first_ret = -1;

    ck_assert_int_eq(hal_flash_write(FLASH_BANK2_BASE_REL - 16, data, 32),
                     -1);
    ck_assert_int_eq(g_calls, 1);
}
END_TEST

Suite *stm32h7_flash_write_suite(void)
{
    Suite *s = suite_create("stm32h7-flash-write");
    TCase *tc = tcase_create("stm32h7-flash-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_crosses_boundary);
    tcase_add_test(tc, test_write_bank1_only);
    tcase_add_test(tc, test_write_bank2_only);
    tcase_add_test(tc, test_write_first_part_fails);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = stm32h7_flash_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
