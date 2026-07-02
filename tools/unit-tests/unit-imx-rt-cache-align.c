/* unit-imx-rt-cache-align.c
 *
 * Regression test for F-6399: in hal/imx_rt.c, hal_flash_write() and
 * hal_flash_erase() invalidate the data cache over a range computed by
 * rounding "address" down and "len" up to 32-byte cache-line boundaries.
 * The length was rounded up from "len" alone, omitting the down-alignment
 * offset (address - aligned_address). Whenever
 * (address % 32) + (len % 32) > 32, the resulting range's end fell short of
 * the real end of the write/erase (address + len), leaving the last cache
 * line stale after the flash operation.
 *
 * hal_flash_cache_align_range() (hal/imx_rt.h) is the exact routine used by
 * both hal_flash_write() and hal_flash_erase() to compute that range; this
 * test drives it directly and checks that [aligned_address, aligned_address
 * + aligned_len) always fully covers [address, address + len).
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

#include "../../hal/imx_rt.h"

static void check_covers(uint32_t address, uint32_t len)
{
    uint32_t aligned_address, aligned_len;

    hal_flash_cache_align_range(address, len, &aligned_address, &aligned_len);

    /* Both must be 32-byte aligned, as required by DCACHE_InvalidateByRange. */
    ck_assert_uint_eq(aligned_address % 32, 0);
    ck_assert_uint_eq(aligned_len % 32, 0);

    /* The invalidated range must start at or before the write/erase, and end
     * at or after it: [aligned_address, aligned_address + aligned_len)
     * must contain [address, address + len). */
    ck_assert(aligned_address <= address);
    ck_assert(aligned_address + aligned_len >= address + len);
}

/* address % 32 = 4, len % 32 = 29: 4 + 29 = 33 > 32, the case the buggy
 * "len + (32 - (len % 32))" formula under-covered by 8 bytes. */
START_TEST(test_straddles_cache_line)
{
    check_covers(0x60000000u + 4u, 29u);
}
END_TEST

/* Same straddling condition, at the boundary: address % 32 = 31,
 * len % 32 = 1 (31 + 1 = 32, not > 32) must still be covered exactly. */
START_TEST(test_boundary_not_straddling)
{
    check_covers(0x60000000u + 31u, 1u);
}
END_TEST

/* len an exact multiple of 32: must not require a spurious extra line. */
START_TEST(test_len_multiple_of_32)
{
    check_covers(0x60000000u, 64u);
    check_covers(0x60000000u + 32u, 32u);
}
END_TEST

/* Sector-aligned erase (address % 32 == 0): always covered regardless of
 * len % 32. */
START_TEST(test_aligned_address)
{
    check_covers(0x60000000u, 1u);
    check_covers(0x60000000u, 4096u);
    check_covers(0x60000000u, 33u);
}
END_TEST

/* Sweep every (address % 32, len) combination for a representative address
 * base and a range of lengths, to catch any other under-coverage case. */
START_TEST(test_sweep)
{
    uint32_t off;
    uint32_t len;

    for (off = 0; off < 32; off++) {
        for (len = 1; len <= 96; len++) {
            check_covers(0x60000000u + off, len);
        }
    }
}
END_TEST

Suite *imx_rt_cache_align_suite(void)
{
    Suite *s = suite_create("imx-rt-cache-align");
    TCase *tc = tcase_create("imx-rt-cache-align");

    tcase_add_test(tc, test_straddles_cache_line);
    tcase_add_test(tc, test_boundary_not_straddling);
    tcase_add_test(tc, test_len_multiple_of_32);
    tcase_add_test(tc, test_aligned_address);
    tcase_add_test(tc, test_sweep);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = imx_rt_cache_align_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
