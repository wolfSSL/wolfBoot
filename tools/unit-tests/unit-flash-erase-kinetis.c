/* unit-flash-erase-kinetis.c
 *
 * Regression test: hal/kinetis.c hal_flash_erase() was a do/while loop,
 * so one full WOLFBOOT_SECTOR_SIZE FLASH_Erase() was issued before `len`
 * was ever tested; a zero (or negative) length request destroyed one
 * sector at `address`. The loop is now pre-tested with a `len <= 0`
 * guard matching the other in-tree HALs (stm32c0, stm32wb, mcxw, ...).
 *
 * The erase body is extracted verbatim from hal/kinetis.c; the NXP
 * FTFx SDK calls are mocked to record every erase issued.
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

/* NXP FTFx stand-ins */
typedef int status_t;
#define kStatus_FTFx_Success 0
#define kFTFx_ApiEraseKey 0x6b65796b

typedef struct ftf_flash_config {
    int dummy;
} ftf_flash_config_t;

static ftf_flash_config_t pflash;
static ftf_flash_config_t pcache;

static int erase_calls;
static uint32_t last_erase_addr;
static uint32_t last_erase_len;

static void do_flash_init(void)
{
}

static status_t FLASH_Erase(ftf_flash_config_t *config, uint32_t start,
                            uint32_t len, uint32_t key)
{
    (void)config;
    (void)key;
    erase_calls++;
    last_erase_addr = start;
    last_erase_len = len;
    return kStatus_FTFx_Success;
}

static void FTFx_CACHE_ClearCachePrefetchSpeculation(ftf_flash_config_t *config,
                                                     uint32_t enable)
{
    (void)config;
    (void)enable;
}

/* Extracted verbatim from hal/kinetis.c */
#define RAMFUNCTION
#define WOLFBOOT_SECTOR_SIZE 4096U
#include "kinetis_erase_extract.h"

static void setup(void)
{
    erase_calls = 0;
    last_erase_addr = 0;
    last_erase_len = 0;
}

static void teardown(void)
{
}

START_TEST(test_zero_len_erases_nothing){
    ck_assert_int_eq(hal_flash_erase(0x08000000U, 0), -1);
    ck_assert_int_eq(erase_calls, 0);
}
END_TEST

START_TEST(test_negative_len_erases_nothing)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000U, -1), -1);
    ck_assert_int_eq(erase_calls, 0);
}
END_TEST

START_TEST(test_sub_sector_len_erases_one_sector)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000U, 1), 0);
    ck_assert_int_eq(erase_calls, 1);
    ck_assert_uint_eq(last_erase_addr, 0x08000000U);
    ck_assert_uint_eq(last_erase_len, WOLFBOOT_SECTOR_SIZE);
}
END_TEST

START_TEST(test_multi_sector_len_erases_each_sector)
{
    ck_assert_int_eq(hal_flash_erase(0x08000000U,
                                     2 * (int)WOLFBOOT_SECTOR_SIZE + 7), 0);
    ck_assert_int_eq(erase_calls, 3);
    ck_assert_uint_eq(last_erase_addr, 0x08000000U + 2 * WOLFBOOT_SECTOR_SIZE);
}
END_TEST

static Suite *kinetis_erase_suite(void)
{
    Suite *s = suite_create("kinetis_erase");
    TCase *tc = tcase_create("hal_flash_erase");

    suite_add_tcase(s, tc);
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_zero_len_erases_nothing);
    tcase_add_test(tc, test_negative_len_erases_nothing);
    tcase_add_test(tc, test_sub_sector_len_erases_one_sector);
    tcase_add_test(tc, test_multi_sector_len_erases_each_sector);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s = kinetis_erase_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? 0 : 1;
}
