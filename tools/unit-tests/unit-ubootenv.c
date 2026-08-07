/* unit-ubootenv.c
 *
 * Unit tests for the U-Boot environment A/B slot selection (src/ubootenv.c):
 * the RAUC-compatible BOOT_ORDER / BOOT_<name>_LEFT state machine wolfBoot uses
 * to replace a U-Boot boot script.
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

#include "../../src/ubootenv.c"

#define ENVLEN 512
static uint8_t env[ENVLEN];

static void init_env(const char *order, int a, int b)
{
    char v[16];
    memset(env, 0, ENVLEN);
    uboot_env_set(env, ENVLEN, "BOOT_ORDER", order);
    env_ltoa(a, v);
    uboot_env_set(env, ENVLEN, "BOOT_A_LEFT", v);
    env_ltoa(b, v);
    uboot_env_set(env, ENVLEN, "BOOT_B_LEFT", v);
    uboot_env_reseal(env, ENVLEN);
}

/* Count how many entries start with "<key>=" in the data region. */
static int count_key(const char *key)
{
    const char *p = (const char *)(env + 4);
    const char *end = (const char *)(env + ENVLEN);
    size_t klen = strlen(key);
    int c = 0;
    while (p < end && *p != '\0') {
        const char *entry = p;
        size_t elen;
        while (p < end && *p != '\0')
            p++;
        elen = (size_t)(p - entry);
        if (p < end)
            p++;
        if (elen > klen && entry[klen] == '=' && memcmp(entry, key, klen) == 0)
            c++;
    }
    return c;
}

START_TEST(test_crc_roundtrip)
{
    init_env("A B", 3, 3);
    ck_assert_int_eq(uboot_env_verify(env, ENVLEN), 1);
    env[10] ^= 0xFF; /* corrupt a data byte */
    ck_assert_int_eq(uboot_env_verify(env, ENVLEN), 0);
}
END_TEST

START_TEST(test_get_set_roundtrip)
{
    char v[32];
    init_env("A B", 3, 3);
    ck_assert_int_ge(uboot_env_get(env, ENVLEN, "BOOT_ORDER", v, sizeof(v)), 0);
    ck_assert_str_eq(v, "A B");
    ck_assert_int_eq(uboot_env_get(env, ENVLEN, "NOPE", v, sizeof(v)), -1);
}
END_TEST

START_TEST(test_set_overwrite_no_dup)
{
    char v[16];
    init_env("A B", 3, 3);
    uboot_env_set(env, ENVLEN, "BOOT_A_LEFT", "2");
    ck_assert_int_eq(count_key("BOOT_A_LEFT"), 1);
    uboot_env_get(env, ENVLEN, "BOOT_A_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "2");
    /* the neighbouring key must survive the overwrite */
    uboot_env_get(env, ENVLEN, "BOOT_B_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "3");
}
END_TEST

START_TEST(test_select_blank_defaults)
{
    struct uboot_slot s;
    char v[16];
    memset(env, 0xFF, ENVLEN); /* invalid CRC -> defaults */
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), 0);
    ck_assert_int_eq(s.selected, 1);
    ck_assert_str_eq(s.name, "A");
    uboot_env_get(env, ENVLEN, "BOOT_A_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "2"); /* default 3, minus this boot */
    ck_assert_int_eq(uboot_env_verify(env, ENVLEN), 1); /* resealed */
}
END_TEST

START_TEST(test_select_decrement_and_failover)
{
    struct uboot_slot s;
    char v[16];
    init_env("A B", 3, 3);
    uboot_env_select_slot(env, ENVLEN, &s);
    ck_assert_str_eq(s.name, "A");
    uboot_env_select_slot(env, ENVLEN, &s);
    ck_assert_str_eq(s.name, "A");
    uboot_env_select_slot(env, ENVLEN, &s);
    ck_assert_str_eq(s.name, "A");
    uboot_env_get(env, ENVLEN, "BOOT_A_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "0");
    /* A exhausted -> fail over to B, decrement B */
    uboot_env_select_slot(env, ENVLEN, &s);
    ck_assert_int_eq(s.selected, 1);
    ck_assert_str_eq(s.name, "B");
    uboot_env_get(env, ENVLEN, "BOOT_B_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "2");
}
END_TEST

START_TEST(test_select_rearm_when_exhausted)
{
    struct uboot_slot s;
    char v[16];
    init_env("A B", 0, 0);
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), 0);
    ck_assert_int_eq(s.selected, 0);
    ck_assert_int_eq(s.rearmed, 1);
    uboot_env_get(env, ENVLEN, "BOOT_A_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "3");
    uboot_env_get(env, ENVLEN, "BOOT_B_LEFT", v, sizeof(v));
    ck_assert_str_eq(v, "3");
}
END_TEST

START_TEST(test_select_order_b_first)
{
    struct uboot_slot s;
    init_env("B A", 3, 3);
    uboot_env_select_slot(env, ENVLEN, &s);
    ck_assert_str_eq(s.name, "B");
}
END_TEST

/* A replacement that does not fit must fail WITHOUT destroying the old value. */
START_TEST(test_set_nofit_nondestructive)
{
    uint8_t small[40];
    char v[16];
    memset(small, 0, sizeof(small));
    ck_assert_int_eq(uboot_env_set(small, sizeof(small), "K", "9"), 0);
    ck_assert_int_eq(uboot_env_set(small, sizeof(small), "K",
        "12345678901234567890123456789012345678901234567890"), -1);
    ck_assert_int_eq(uboot_env_get(small, sizeof(small), "K", v, sizeof(v)), 1);
    ck_assert_str_eq(v, "9"); /* old value survives the failed set */
}
END_TEST

/* uboot_env_get truncates a value longer than val_max-1 and NUL-terminates. */
START_TEST(test_get_truncation)
{
    char v[4];
    init_env("A B", 3, 3);
    uboot_env_set(env, ENVLEN, "LONG", "abcdefgh");
    uboot_env_reseal(env, ENVLEN);
    ck_assert_int_eq(uboot_env_get(env, ENVLEN, "LONG", v, sizeof(v)), 3);
    ck_assert_str_eq(v, "abc");
}
END_TEST

/* An empty or whitespace-only BOOT_ORDER is malformed -> select returns -1. */
START_TEST(test_select_empty_order)
{
    struct uboot_slot s;
    memset(env, 0, ENVLEN);
    uboot_env_set(env, ENVLEN, "BOOT_ORDER", "");
    uboot_env_reseal(env, ENVLEN);
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), -1);
    memset(env, 0, ENVLEN);
    uboot_env_set(env, ENVLEN, "BOOT_ORDER", "   ");
    uboot_env_reseal(env, ENVLEN);
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), -1);
}
END_TEST

/* A non-numeric BOOT_<name>_LEFT counts as no tries -> that slot is skipped. */
START_TEST(test_select_nonnumeric_left)
{
    struct uboot_slot s;
    init_env("A B", 3, 3);
    uboot_env_set(env, ENVLEN, "BOOT_A_LEFT", "junk");
    uboot_env_reseal(env, ENVLEN);
    uboot_env_select_slot(env, ENVLEN, &s);
    ck_assert_int_eq(s.selected, 1);
    ck_assert_str_eq(s.name, "B");
}
END_TEST

Suite *ubootenv_suite(void)
{
    Suite *s = suite_create("ubootenv");
    TCase *tc = tcase_create("ab_state_machine");

    tcase_add_test(tc, test_crc_roundtrip);
    tcase_add_test(tc, test_get_set_roundtrip);
    tcase_add_test(tc, test_set_overwrite_no_dup);
    tcase_add_test(tc, test_select_blank_defaults);
    tcase_add_test(tc, test_select_decrement_and_failover);
    tcase_add_test(tc, test_select_rearm_when_exhausted);
    tcase_add_test(tc, test_select_order_b_first);
    tcase_add_test(tc, test_set_nofit_nondestructive);
    tcase_add_test(tc, test_get_truncation);
    tcase_add_test(tc, test_select_empty_order);
    tcase_add_test(tc, test_select_nonnumeric_left);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = ubootenv_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
