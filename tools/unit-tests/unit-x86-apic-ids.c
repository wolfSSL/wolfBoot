/* unit-x86-apic-ids.c
 *
 * Unit tests for the CPU list the x86 FSP MADT and MP table are built from
 * (x86_apic_ids_from_topology() in src/x86/common.c, extracted by the
 * Makefile): enabled cores and threads from MSR_CORE_THREAD_COUNT, APIC ID
 * layout from the SMT shift, primary threads first.
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

#include "x86_apic_ids_extract.h"

#define COUNTS(cores, threads) (((uint32_t)(cores) << 16) | (threads))

START_TEST(test_ht_off_four_cores)
{
    const uint8_t want[] = { 0, 2, 4, 6 };
    uint8_t ids[8];

    memset(ids, 0xee, sizeof(ids));
    ck_assert_int_eq(x86_apic_ids_from_topology(COUNTS(4, 4), 1, ids, 8), 4);
    ck_assert_int_eq(memcmp(ids, want, sizeof(want)), 0);
}
END_TEST

START_TEST(test_ht_on_primaries_first)
{
    const uint8_t want[] = { 0, 2, 4, 6, 1, 3, 5, 7 };
    uint8_t ids[8];

    ck_assert_int_eq(x86_apic_ids_from_topology(COUNTS(4, 8), 1, ids, 8), 8);
    ck_assert_int_eq(memcmp(ids, want, sizeof(want)), 0);
}
END_TEST

START_TEST(test_fewer_cores)
{
    const uint8_t want[] = { 0, 2, 1, 3 };
    uint8_t ids[8];

    ck_assert_int_eq(x86_apic_ids_from_topology(COUNTS(2, 4), 1, ids, 8), 4);
    ck_assert_int_eq(memcmp(ids, want, sizeof(want)), 0);
}
END_TEST

START_TEST(test_capacity_is_respected)
{
    const uint8_t want[] = { 0, 2, 4 };
    uint8_t ids[8];

    memset(ids, 0xee, sizeof(ids));
    ck_assert_int_eq(x86_apic_ids_from_topology(COUNTS(4, 8), 1, ids, 3), 3);
    ck_assert_int_eq(memcmp(ids, want, sizeof(want)), 0);
    ck_assert_uint_eq(ids[3], 0xee);
}
END_TEST

START_TEST(test_unusable_counts)
{
    uint8_t ids[8];

    ck_assert_int_eq(x86_apic_ids_from_topology(COUNTS(0, 4), 1, ids, 8), 0);
    ck_assert_int_eq(x86_apic_ids_from_topology(COUNTS(4, 2), 1, ids, 8), 0);
    ck_assert_int_eq(x86_apic_ids_from_topology(0, 1, ids, 8), 0);
}
END_TEST

static Suite *apic_ids_suite(void)
{
    Suite *s = suite_create("x86-apic-ids");
    TCase *tc = tcase_create("x86-apic-ids");

    tcase_add_test(tc, test_ht_off_four_cores);
    tcase_add_test(tc, test_ht_on_primaries_first);
    tcase_add_test(tc, test_fewer_cores);
    tcase_add_test(tc, test_capacity_is_respected);
    tcase_add_test(tc, test_unusable_counts);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = apic_ids_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
