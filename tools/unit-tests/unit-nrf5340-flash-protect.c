/* unit-nrf5340-flash-protect.c
 *
 * Regression test for F-13623: the nRF5340 hal_flash_protect() region math
 * truncated - `n = len / SPU_FLASH_BLOCK_SIZE` rounded the length down, so a
 * len smaller than one 16 KiB SPU block locked nothing, and a len not a whole
 * number of blocks left the tail block writable - while the function returned
 * 0 (success) either way. Every boot path treats a non-negative return as
 * "the region is protected" and proceeds to handoff, so the truncation
 * shipped silently.
 *
 * The real function is extracted by the Makefile and run against a mock SPU
 * region-permission array, so the test can count how many regions actually
 * got PERM_LOCK set.
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
#include <unistd.h>

typedef uintptr_t haladdr_t;

#define RAMFUNCTION
#define TARGET_nrf5340_app
#define FLASH_SIZE (1024UL * 1024UL)
#define SPU_FLASH_BLOCK_SIZE (16 * 1024)

/* Mock the SPU region-permission register bank as an array so the test can
 * inspect which regions got locked. */
#define SPU_NUM_REGIONS 64
static uint32_t g_spu_perm[SPU_NUM_REGIONS];
#define SPU_FLASHREGION_PERM(n) g_spu_perm[(n) & 0x3F]
#define SPU_FLASHREGION_PERM_EXEC    (1 << 0)
#define SPU_FLASHREGION_PERM_READ    (1 << 2)
#define SPU_FLASHREGION_PERM_SECATTR (1 << 4)
#define SPU_FLASHREGION_PERM_LOCK    (1 << 8)

/* The real hal_flash_protect(), extracted from hal/nrf5340.c. */
#include "nrf5340_protect_fn_extract.h"

static void sim_reset(void)
{
    memset(g_spu_perm, 0, sizeof(g_spu_perm));
}

static int locked_count(void)
{
    int i;
    int count = 0;

    for (i = 0; i < SPU_NUM_REGIONS; i++) {
        if (g_spu_perm[i] & SPU_FLASHREGION_PERM_LOCK)
            count++;
    }
    return count;
}

static const uint32_t LOCKED_PERM =
    SPU_FLASHREGION_PERM_EXEC | SPU_FLASHREGION_PERM_READ |
    SPU_FLASHREGION_PERM_SECATTR | SPU_FLASHREGION_PERM_LOCK;

/* A whole number of blocks locks exactly that many regions, each with the
 * full permission set; the region just past the range is untouched. */
START_TEST (test_whole_blocks_lock_exact_regions){
    sim_reset();
    ck_assert_int_eq(hal_flash_protect(0, 4 * SPU_FLASH_BLOCK_SIZE), 0);
    ck_assert_int_eq(locked_count(), 4);
    ck_assert_uint_eq(g_spu_perm[0], LOCKED_PERM);
    ck_assert_uint_eq(g_spu_perm[3], LOCKED_PERM);
    ck_assert_uint_eq(g_spu_perm[4], 0);
}
END_TEST

/* A len smaller than one block must still lock the block containing the
 * range (the old code locked nothing and returned 0). */
START_TEST(test_sub_block_len_locks_containing_block)
{
    sim_reset();
    ck_assert_int_eq(hal_flash_protect(0, 1), 0);
    ck_assert_int_eq(locked_count(), 1);
    ck_assert_uint_eq(g_spu_perm[0], LOCKED_PERM);
}
END_TEST

/* A len not a whole number of blocks must lock the tail block too (the old
 * code truncated and left it writable). 4 blocks + 1 byte -> 5 regions. */
START_TEST(test_partial_tail_block_is_locked)
{
    sim_reset();
    ck_assert_int_eq(hal_flash_protect(0, 4 * SPU_FLASH_BLOCK_SIZE + 1), 0);
    ck_assert_int_eq(locked_count(), 5);
    ck_assert_uint_eq(g_spu_perm[4], LOCKED_PERM);
}
END_TEST

/* An unaligned start: the block containing start is locked whole, so the
 * range [start, start+len) is covered. Protection may widen below start
 * (the partial block is locked whole), which is safe. start mid-block 0,
 * len one block -> blocks 0 and 1. */
START_TEST(test_unaligned_start_covers_range)
{
    sim_reset();
    ck_assert_int_eq(hal_flash_protect(0x2000, 0x4000), 0);
    ck_assert_int_eq(locked_count(), 2);
    ck_assert_uint_eq(g_spu_perm[0], LOCKED_PERM);
    ck_assert_uint_eq(g_spu_perm[1], LOCKED_PERM);
}
END_TEST

/* start past the end of flash is rejected and nothing is locked. */
START_TEST(test_start_past_flash_rejected)
{
    sim_reset();
    ck_assert_int_eq(hal_flash_protect(FLASH_SIZE + 1, 0x1000), -1);
    ck_assert_int_eq(locked_count(), 0);
}
END_TEST

/* A range extending past the end of flash is truncated to the last block,
 * not a crash: start = last block, len = two blocks -> one region. */
START_TEST(test_range_past_flash_truncated)
{
    sim_reset();
    ck_assert_int_eq(hal_flash_protect(FLASH_SIZE - SPU_FLASH_BLOCK_SIZE,
                                       2 * SPU_FLASH_BLOCK_SIZE), 0);
    ck_assert_int_eq(locked_count(), 1);
    ck_assert_uint_eq(g_spu_perm[SPU_NUM_REGIONS - 1], LOCKED_PERM);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("nrf5340-flash-protect");
    TCase *tc = tcase_create("hal_flash_protect");

    tcase_add_test(tc, test_whole_blocks_lock_exact_regions);
    tcase_add_test(tc, test_sub_block_len_locks_containing_block);
    tcase_add_test(tc, test_partial_tail_block_is_locked);
    tcase_add_test(tc, test_unaligned_start_covers_range);
    tcase_add_test(tc, test_start_past_flash_rejected);
    tcase_add_test(tc, test_range_past_flash_truncated);
    suite_add_tcase(s, tc);
    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    Suite *s;
    SRunner *sr;

    (void)argc;
    (void)argv;
    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
