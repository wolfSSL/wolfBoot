/* unit-pic32-pfswap.c
 *
 * Unit tests for the PIC32 FCW dual-bank PFSWAP read-modify-write.
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

#define FCW_SWAP_PFSWAP (1u << 8)
#define FCW_UNLOCK_SWAPKEY 0x91C32C02u
#define FCW_OTHER_BITS 0x000000A5u

static uint32_t g_fcw_swap;
static uint32_t g_fcw_key;

static int pfswap_get(void)
{
    return !!(g_fcw_swap & FCW_SWAP_PFSWAP);
}

static void pfswap_set_bytemask(int sw)
{
    uint32_t reg;

    reg = g_fcw_swap;
    reg &= FCW_SWAP_PFSWAP;
    if (sw)
        reg |= FCW_SWAP_PFSWAP;
    g_fcw_key = FCW_UNLOCK_SWAPKEY;
    g_fcw_swap = reg;
}

static void pfswap_set_clearbit(int sw)
{
    uint32_t reg;

    reg = g_fcw_swap;
    reg &= ~FCW_SWAP_PFSWAP;
    if (sw)
        reg |= FCW_SWAP_PFSWAP;
    g_fcw_key = FCW_UNLOCK_SWAPKEY;
    g_fcw_swap = reg;
}

START_TEST(test_masked_set_cannot_clear_bit)
{
    g_fcw_swap = FCW_OTHER_BITS;

    pfswap_set_bytemask(!pfswap_get());
    ck_assert_int_eq(pfswap_get(), 1);

    pfswap_set_bytemask(!pfswap_get());
    ck_assert_int_eq(pfswap_get(), 1);
    ck_assert_uint_eq(g_fcw_swap & FCW_OTHER_BITS, 0u);
}
END_TEST

START_TEST(test_cleared_set_toggles_and_preserves)
{
    g_fcw_swap = FCW_OTHER_BITS;

    pfswap_set_clearbit(!pfswap_get());
    ck_assert_int_eq(pfswap_get(), 1);
    ck_assert_uint_eq(g_fcw_swap & FCW_OTHER_BITS, FCW_OTHER_BITS);

    pfswap_set_clearbit(!pfswap_get());
    ck_assert_int_eq(pfswap_get(), 0);
    ck_assert_uint_eq(g_fcw_swap & FCW_OTHER_BITS, FCW_OTHER_BITS);
}
END_TEST

Suite *pic32_pfswap_suite(void)
{
    Suite *s = suite_create("pic32-pfswap");
    TCase *tc = tcase_create("dualbank-swap");

    tcase_add_test(tc, test_masked_set_cannot_clear_bit);
    tcase_add_test(tc, test_cleared_set_toggles_and_preserves);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = pic32_pfswap_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
