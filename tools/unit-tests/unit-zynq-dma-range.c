/* unit-zynq-dma-range.c
 *
 * 2MB block-index arithmetic behind hal_dma_set_noncached() on ZynqMP. These
 * indices decide which physical blocks get re-attributed, so an off-by-one
 * would silently re-map the wrong memory. The dc/tlbi around them cannot run
 * on the host; this arithmetic can.
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

#include "../../hal/zynq.h"

#define BLK (1ULL << 21)

START_TEST(test_block_range_single_block)
{
    uint64_t first = 99, last = 99;

    /* The wolfBoot default DMA window: one 2MB block at 0x8200000. */
    ck_assert_int_eq(zynqmp_l2_block_range(0x8200000, 0x8400000, &first, &last),
        0);
    ck_assert_uint_eq(first, 65);
    ck_assert_uint_eq(last, 65);

    /* A sub-block range still names that one block, so a caller must own
     * the whole thing. */
    ck_assert_int_eq(zynqmp_l2_block_range(0x8200000, 0x8200040, &first, &last),
        0);
    ck_assert_uint_eq(first, 65);
    ck_assert_uint_eq(last, 65);
}
END_TEST

START_TEST(test_block_range_boundaries)
{
    uint64_t first = 0, last = 0;

    /* end is exclusive: one block, not two. */
    ck_assert_int_eq(zynqmp_l2_block_range(0, BLK, &first, &last), 0);
    ck_assert_uint_eq(first, 0);
    ck_assert_uint_eq(last, 0);

    /* one byte past the boundary pulls in the next block */
    ck_assert_int_eq(zynqmp_l2_block_range(0, BLK + 1, &first, &last), 0);
    ck_assert_uint_eq(last, 1);

    /* an unaligned start rounds down to its containing block */
    ck_assert_int_eq(zynqmp_l2_block_range(BLK + 0x40, BLK + 0x80, &first,
        &last), 0);
    ck_assert_uint_eq(first, 1);
    ck_assert_uint_eq(last, 1);

    /* the last block the table describes */
    ck_assert_int_eq(zynqmp_l2_block_range(0xFFE00000, 0x100000000ULL, &first,
        &last), 0);
    ck_assert_uint_eq(first, ZYNQMP_L2_ENTRIES - 1);
    ck_assert_uint_eq(last, ZYNQMP_L2_ENTRIES - 1);
}
END_TEST

START_TEST(test_block_range_rejects_bad_input)
{
    uint64_t first = 0, last = 0;

    /* empty and inverted ranges */
    ck_assert_int_eq(zynqmp_l2_block_range(BLK, BLK, &first, &last), -1);
    ck_assert_int_eq(zynqmp_l2_block_range(0x8400000, 0x8200000, &first, &last),
        -1);

    /* one byte past the 4GB the table covers */
    ck_assert_int_eq(zynqmp_l2_block_range(0xFFE00000, 0x100000001ULL, &first,
        &last), -1);
    /* and far past it */
    ck_assert_int_eq(zynqmp_l2_block_range(0x100000000ULL, 0x100200000ULL,
        &first, &last), -1);

    ck_assert_int_eq(zynqmp_l2_block_range(0, BLK, NULL, &last), -1);
    ck_assert_int_eq(zynqmp_l2_block_range(0, BLK, &first, NULL), -1);
}
END_TEST

START_TEST(test_block_range_leaves_outputs_alone_on_error)
{
    uint64_t first = 0xAAAA, last = 0x5555;

    /* An out of range end is rejected only after the indices are computed,
     * so check the caller's variables are still untouched. */
    ck_assert_int_eq(zynqmp_l2_block_range(0x100000000ULL, 0x100200000ULL,
        &first, &last), -1);
    ck_assert_uint_eq(first, 0xAAAA);
    ck_assert_uint_eq(last, 0x5555);

    ck_assert_int_eq(zynqmp_l2_block_range(BLK, BLK, &first, &last), -1);
    ck_assert_uint_eq(first, 0xAAAA);
    ck_assert_uint_eq(last, 0x5555);
}
END_TEST

static Suite *dma_range_suite(void)
{
    Suite *s = suite_create("zynq-dma-range");
    TCase *tc = tcase_create("zynq-dma-range");

    tcase_add_test(tc, test_block_range_single_block);
    tcase_add_test(tc, test_block_range_boundaries);
    tcase_add_test(tc, test_block_range_rejects_bad_input);
    tcase_add_test(tc, test_block_range_leaves_outputs_alone_on_error);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = dma_range_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
