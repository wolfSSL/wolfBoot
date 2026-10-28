/* unit-mpfs-wdt.c
 *
 * Unit tests for the MSS watchdog refresh gate in hal/mpfs250.c.
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
#include <sys/mman.h>

/* The extracted function initializes a static const array from the five
 * MSS_WDT_*_BASE macros, so the register file has to live at those addresses
 * rather than in a host array: the test maps the MSS watchdog span instead of
 * rebasing the macros, which keeps the real base addresses under test. */
#define MPFS_WDT_SPAN_BASE  0x20000000UL
#define MPFS_WDT_SPAN_SIZE  0x00108000UL

#define MSS_WDT_E51_BASE    0x20001000UL
#define MSS_WDT_U54_1_BASE  0x20101000UL
#define MSS_WDT_U54_2_BASE  0x20103000UL
#define MSS_WDT_U54_3_BASE  0x20105000UL
#define MSS_WDT_U54_4_BASE  0x20107000UL

#include "mpfs250_wdt_extract.h"

/* A refresh is one write of this value to offset 0x00. */
#define WDT_REFRESH_MAGIC   0xDEADC0DEU
/* Pre-seeded into the refresh word, so "untouched" is distinguishable. */
#define WDT_NO_REFRESH      0x00000000U

/* Sentinels for the registers a refresh must never touch: the first write to
 * any of offsets 0x00-0x0C locks TRIGGER, MVRP and TIME until the next system
 * reset, so a stray write here is unrecoverable on hardware. */
#define CONTROL_SENTINEL    0x11111111U
#define TIME_SENTINEL       0x22222222U
#define MVRP_SENTINEL       0x33333333U
#define TRIGGER_SENTINEL    0x44444444U

static void wdt_mock_reset(void)
{
    unsigned int i;

    memset((void *)(uintptr_t)MPFS_WDT_SPAN_BASE, 0, MPFS_WDT_SPAN_SIZE);
    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        MSS_WDT_REFRESH(mpfs_wdt_bases[i]) = WDT_NO_REFRESH;
        MSS_WDT_CONTROL(mpfs_wdt_bases[i]) = CONTROL_SENTINEL;
        MSS_WDT_STATUS(mpfs_wdt_bases[i])  = 0U;
        MSS_WDT_TIME(mpfs_wdt_bases[i])    = TIME_SENTINEL;
        MSS_WDT_MVRP(mpfs_wdt_bases[i])    = MVRP_SENTINEL;
        MSS_WDT_TRIGGER(mpfs_wdt_bases[i]) = TRIGGER_SENTINEL;
    }
}

static void wdt_mock_setup(void)
{
    void *p = mmap((void *)(uintptr_t)MPFS_WDT_SPAN_BASE, MPFS_WDT_SPAN_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    ck_assert_ptr_eq(p, (void *)(uintptr_t)MPFS_WDT_SPAN_BASE);
    wdt_mock_reset();
}

static void wdt_mock_teardown(void)
{
    munmap((void *)(uintptr_t)MPFS_WDT_SPAN_BASE, MPFS_WDT_SPAN_SIZE);
}

static int refreshed(unsigned int hart)
{
    return MSS_WDT_REFRESH(mpfs_wdt_bases[hart]) == WDT_REFRESH_MAGIC;
}

static void assert_windows_untouched(void)
{
    unsigned int i;

    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        ck_assert_uint_eq(MSS_WDT_CONTROL(mpfs_wdt_bases[i]), CONTROL_SENTINEL);
        ck_assert_uint_eq(MSS_WDT_TIME(mpfs_wdt_bases[i]), TIME_SENTINEL);
        ck_assert_uint_eq(MSS_WDT_MVRP(mpfs_wdt_bases[i]), MVRP_SENTINEL);
        ck_assert_uint_eq(MSS_WDT_TRIGGER(mpfs_wdt_bases[i]), TRIGGER_SENTINEL);
    }
}

/* Counter below MVRP on every hart: all five take the refresh. */
START_TEST(test_refresh_when_permitted)
{
    unsigned int i;

    mpfs_wdt_pet_all();
    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        ck_assert_int_eq(refreshed(i), 1);
    }
    assert_windows_untouched();
}
END_TEST

/* The forbidden window: a refresh here is answered with a chip reset, so the
 * gate must leave every one of them alone. */
START_TEST(test_no_refresh_when_forbidden)
{
    unsigned int i;

    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        MSS_WDT_STATUS(mpfs_wdt_bases[i]) = MSS_WDT_STAT_FORBIDDEN;
    }
    mpfs_wdt_pet_all();
    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        ck_assert_int_eq(refreshed(i), 0);
    }
    assert_windows_untouched();
}
END_TEST

/* The window is per-hart, so the decision has to be too. */
START_TEST(test_refresh_is_decided_per_hart)
{
    unsigned int i;

    for (i = 0; i < MPFS_NUM_HARTS; i += 2) {
        MSS_WDT_STATUS(mpfs_wdt_bases[i]) = MSS_WDT_STAT_FORBIDDEN;
    }
    mpfs_wdt_pet_all();
    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        ck_assert_int_eq(refreshed(i), (i % 2) ? 1 : 0);
    }
    assert_windows_untouched();
}
END_TEST

/* Only bit 2 gates the refresh: LOCKED and DEVRST say nothing about the
 * window, and treating them as a block would starve the watchdogs. */
START_TEST(test_other_status_bits_do_not_block)
{
    unsigned int i;

    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        MSS_WDT_STATUS(mpfs_wdt_bases[i]) =
            MSS_WDT_STAT_LOCKED | MSS_WDT_STAT_DEVRST | 0xFFFFFFF8U;
    }
    mpfs_wdt_pet_all();
    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        ck_assert_int_eq(refreshed(i), 1);
    }
}
END_TEST

/* The status read must come from the same hart as the refresh write: a
 * single forbidden hart must not suppress the other four. */
START_TEST(test_one_forbidden_hart_does_not_suppress_the_rest)
{
    unsigned int h, i;

    for (h = 0; h < MPFS_NUM_HARTS; h++) {
        wdt_mock_reset();
        MSS_WDT_STATUS(mpfs_wdt_bases[h]) = MSS_WDT_STAT_FORBIDDEN;
        mpfs_wdt_pet_all();
        for (i = 0; i < MPFS_NUM_HARTS; i++) {
            ck_assert_int_eq(refreshed(i), (i == h) ? 0 : 1);
        }
    }
}
END_TEST

Suite *mpfs_wdt_suite(void)
{
    Suite *s = suite_create("mpfs-wdt");
    TCase *tc = tcase_create("pet");

    tcase_add_checked_fixture(tc, wdt_mock_setup, wdt_mock_teardown);
    tcase_add_test(tc, test_refresh_when_permitted);
    tcase_add_test(tc, test_no_refresh_when_forbidden);
    tcase_add_test(tc, test_refresh_is_decided_per_hart);
    tcase_add_test(tc, test_other_status_bits_do_not_block);
    tcase_add_test(tc, test_one_forbidden_hart_does_not_suppress_the_rest);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = mpfs_wdt_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
