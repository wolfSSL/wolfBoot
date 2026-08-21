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
    env_ltoa(a, v, sizeof(v));
    uboot_env_set(env, ENVLEN, "BOOT_A_LEFT", v);
    env_ltoa(b, v, sizeof(v));
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

/* M4: a full-size env with a bad (non-redundant) CRC is reinitialized to
 * defaults IN MEMORY and flagged reinitialized, so the caller skips writeback. */
START_TEST(test_select_badcrc_sets_reinitialized)
{
    struct uboot_slot s;
    memset(env, 0x5A, ENVLEN); /* garbage: invalid simple AND redundant CRC */
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), 0);
    ck_assert_int_eq(s.reinitialized, 1);
    ck_assert_int_eq(s.selected, 1);
    ck_assert_str_eq(s.name, "A");
}
END_TEST

/* M4: a valid REDUNDANT-layout env ([crc32][flags][data]) is refused (return
 * -1) rather than wiped - overwriting it would destroy live RAUC state. */
START_TEST(test_select_redundant_env_refused)
{
    struct uboot_slot s;
    uint8_t before[ENVLEN];
    uint32_t crc;
    memset(env, 0, ENVLEN);
    env[4] = 0x01; /* redundant flags byte */
    memcpy(env + 5, "BOOT_ORDER=A B\0", 15);
    crc = ubootenv_crc32(env + 5, ENVLEN - 5);
    wr_le32(env, crc);
    memcpy(before, env, ENVLEN);
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), -1);
    ck_assert_int_eq(s.selected, 0);
    ck_assert_mem_eq(env, before, ENVLEN); /* not wiped */
}
END_TEST

/* M3: valid CRC, BOOT_ORDER present but BOOT_A_LEFT absent and no room to append
 * the decremented counter -> the inner set fails and select must NOT report a
 * slot (else the hung slot would retry forever). */
START_TEST(test_select_innerset_failure_no_select)
{
    uint8_t small[28];
    struct uboot_slot s;
    memset(small, 0, sizeof(small));
    ck_assert_int_eq(uboot_env_set(small, sizeof(small), "BOOT_ORDER", "A B"), 0);
    uboot_env_reseal(small, sizeof(small));
    ck_assert_int_eq(uboot_env_select_slot(small, sizeof(small), &s), -1);
    ck_assert_int_eq(s.selected, 0);
}
END_TEST

/* L11: valid CRC but no BOOT_ORDER key -> defaults to "A B" and selects A. */
START_TEST(test_select_valid_crc_missing_bootorder)
{
    struct uboot_slot s;
    memset(env, 0, ENVLEN);
    uboot_env_set(env, ENVLEN, "SOMETHING", "x");
    uboot_env_reseal(env, ENVLEN);
    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &s), 0);
    ck_assert_int_eq(s.selected, 1);
    ck_assert_str_eq(s.name, "A");
    ck_assert_int_eq(s.reinitialized, 0);
}
END_TEST

/* L11: NULL / too-short buffers are rejected, not dereferenced. */
START_TEST(test_select_null_and_short)
{
    struct uboot_slot s;
    char v[8];
    ck_assert_int_eq(uboot_env_select_slot(NULL, ENVLEN, &s), -1);
    ck_assert_int_eq(uboot_env_select_slot(env, 4, &s), -1);
    ck_assert_int_eq(uboot_env_verify(NULL, ENVLEN), 0);
    ck_assert_int_eq(uboot_env_get(NULL, ENVLEN, "K", v, sizeof(v)), -1);
}
END_TEST

/* L2/L11: env_leftkey bounds the name - a name too long to form
 * "BOOT_<name>_LEFT" is refused (returns -1), never truncated into an alias. */
START_TEST(test_leftkey_long_name_bounds)
{
    char key[UBOOT_ENV_VAL_MAX];
    char longname[UBOOT_ENV_VAL_MAX];
    ck_assert_int_eq(env_leftkey("A", key, sizeof(key)), 0);
    ck_assert_str_eq(key, "BOOT_A_LEFT");
    memset(longname, 'X', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    ck_assert_int_eq(env_leftkey(longname, key, sizeof(key)), -1);
}
END_TEST

/* env_atol() promises -1 on a non-numeric value; make sure that is what it
 * does. "1x" used to parse as 1 (only the first char was checked), which then
 * selected the slot and rewrote the counter, laundering a malformed value into
 * a valid one. */
START_TEST(test_atol_rejects_trailing_garbage)
{
    ck_assert_int_eq((int)env_atol("1x"), -1);
    ck_assert_int_eq((int)env_atol("3 junk"), -1);
    ck_assert_int_eq((int)env_atol("12-"), -1);
    ck_assert_int_eq((int)env_atol(""), -1);
    ck_assert_int_eq((int)env_atol("x"), -1);
    ck_assert_int_eq((int)env_atol(NULL), -1);
    /* Still accepts well-formed values, with surrounding spaces. */
    ck_assert_int_eq((int)env_atol("0"), 0);
    ck_assert_int_eq((int)env_atol("3"), 3);
    ck_assert_int_eq((int)env_atol("  7  "), 7);
}
END_TEST

/* Over-long counters are refused instead of wrapping or being truncated into a
 * different number on writeback. */
START_TEST(test_atol_rejects_overlong)
{
    ck_assert_int_eq((int)env_atol("999999999"), 999999999); /* 9 digits: ok */
    ck_assert_int_eq((int)env_atol("1000000000"), -1);       /* 10 digits */
    ck_assert_int_eq((int)env_atol("9999999999999999"), -1); /* 16 digits */
}
END_TEST

/* env_ltoa() must honour the caller's buffer size. The old version wrote
 * tmp[16] plus a terminator into a 16-byte buffer for a 16-digit value. */
START_TEST(test_ltoa_respects_bound)
{
    char buf[8];
    char guard[16];

    memset(guard, 0xAA, sizeof(guard));
    env_ltoa(0, guard, sizeof(guard));
    ck_assert_str_eq(guard, "0");

    env_ltoa(12345, buf, sizeof(buf));
    ck_assert_str_eq(buf, "12345");

    /* Tight buffer: must still terminate inside it, never past it. */
    memset(buf, 0xAA, sizeof(buf));
    env_ltoa(123456789, buf, 4);
    ck_assert_uint_lt(strlen(buf), 4);
    ck_assert_uint_eq((unsigned char)buf[7], 0xAA); /* tail untouched */

    /* Zero-length buffer must not be written at all. */
    memset(buf, 0xAA, sizeof(buf));
    env_ltoa(5, buf, 0);
    ck_assert_uint_eq((unsigned char)buf[0], 0xAA);
}
END_TEST

/* A malformed counter degrades to "no tries left" for that slot, so selection
 * falls through to the next one rather than trusting the garbage. */
START_TEST(test_select_malformed_counter_skips_slot)
{
    struct uboot_slot slot;

    memset(env, 0, ENVLEN);
    uboot_env_set(env, ENVLEN, "BOOT_ORDER", "A B");
    uboot_env_set(env, ENVLEN, "BOOT_A_LEFT", "2x");
    uboot_env_set(env, ENVLEN, "BOOT_B_LEFT", "3");
    uboot_env_reseal(env, ENVLEN);

    ck_assert_int_eq(uboot_env_select_slot(env, ENVLEN, &slot), 0);
    ck_assert_int_eq(slot.selected, 1);
    ck_assert_str_eq(slot.name, "B");
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
    tcase_add_test(tc, test_select_badcrc_sets_reinitialized);
    tcase_add_test(tc, test_select_redundant_env_refused);
    tcase_add_test(tc, test_select_innerset_failure_no_select);
    tcase_add_test(tc, test_select_valid_crc_missing_bootorder);
    tcase_add_test(tc, test_select_null_and_short);
    tcase_add_test(tc, test_leftkey_long_name_bounds);
    tcase_add_test(tc, test_atol_rejects_trailing_garbage);
    tcase_add_test(tc, test_atol_rejects_overlong);
    tcase_add_test(tc, test_ltoa_respects_bound);
    tcase_add_test(tc, test_select_malformed_counter_skips_slot);
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
