/* unit-p1021-read-badblock.c
 *
 * Regression test for F-12064 (and the follow-up bad-block skip
 * review): ext_flash_read() in hal/nxp_p1021.c kept its bad-block
 * page counter for the whole request, so the marker was inspected
 * only on the first two pages read and a bad block later in the
 * request was copied as valid data. When a marker did cause a skip,
 * the logical position was rewound to a block boundary while the
 * output pointer was not, so the read continued past the end of the
 * caller's buffer. A related defect kept the pages of a bad block
 * that were already delivered before the marker was found on its
 * second page; those pages must be discarded as well.
 *
 * The real function is extracted by the Makefile together with the
 * ELBC register macros it uses; the ELBC register access and the
 * flash helpers are mocked on top of a small simulated NAND (three
 * 16 KiB blocks of 512-byte pages, bad-block marker in spare byte 5
 * of the block's first pages).
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

/* ELBC_BASE is built from CCSRBAR in the real file; pin it here. */
#define CCSRBAR 0x0

/* ELBC register macros + NAND command codes from hal/nxp_p1021.c
 * (extracted by the Makefile). */
#include "p1021_read_extract.h"

/* Simulated NAND: three 16 KiB blocks of 512-byte pages. The
 * bad-block marker lives in spare byte 5 of the block's first
 * pages, matching bad_marker = page_size + 5 for small pages. */
#define SIM_PAGES 96
#define SIM_PAGE_SIZE 512
#define SIM_SPARE_SIZE 64
#define SIM_BLOCK_PAGES 32
#define SIM_BLOCK_SIZE (SIM_BLOCK_PAGES * SIM_PAGE_SIZE)
#define SIM_MARKER_SPARE 5

static uint8_t g_nand[SIM_PAGES][SIM_PAGE_SIZE];
static uint8_t g_spare[SIM_PAGES][SIM_SPARE_SIZE];
static int g_cmd_ret;
static int g_cmd_calls;
static int g_last_page;

/* FCM buffer + copy offset, as in the real file. */
static volatile uint8_t g_fcm[SIM_PAGE_SIZE + SIM_SPARE_SIZE];
static volatile uint8_t *flash_buf = g_fcm;
static uint32_t flash_idx;

static void sim_reset(void)
{
    int p, i;

    for (p = 0; p < SIM_PAGES; p++) {
        for (i = 0; i < SIM_PAGE_SIZE; i++)
            g_nand[p][i] = (uint8_t)((p * 7 + i) & 0xFF);
        for (i = 0; i < SIM_SPARE_SIZE; i++)
            g_spare[p][i] = 0xFF;
    }
    g_cmd_ret = 0;
    g_cmd_calls = 0;
    g_last_page = -1;
}

static void sim_mark_bad_block(int block, int marker_pages)
{
    int p;

    for (p = 0; p < marker_pages; p++)
        g_spare[block * SIM_BLOCK_PAGES + p][SIM_MARKER_SPARE] = 0x00;
}

/* Mocks for the ELBC register access helpers (hal/nxp_ppc.h). */
static void set32(volatile unsigned int *addr, unsigned int val)
{
    (void)addr;
    (void)val;
}

static uint32_t get32(volatile unsigned int *addr)
{
    (void)addr;
    return 0; /* MDR status: no error */
}

/* Loads the full page + spare into the FCM buffer (BC = 0) and
 * points the copy at the requested column, as the real helper does. */
static void hal_flash_set_addr(int page, int col)
{
    int i;

    g_last_page = page;
    for (i = 0; i < SIM_PAGE_SIZE; i++)
        flash_buf[i] = g_nand[page][i];
    for (i = 0; i < SIM_SPARE_SIZE; i++)
        flash_buf[SIM_PAGE_SIZE + i] = g_spare[page][i];
    flash_idx = (uint32_t)col;
}

static int hal_flash_command(uint8_t iswrite)
{
    (void)iswrite;

    g_cmd_calls++;
    return g_cmd_ret;
}

static void hal_flash_read_bytes(uint8_t *data, size_t len)
{
    memcpy(data, (const void *)&flash_buf[flash_idx], len);
}

/* The real ext_flash_read() from hal/nxp_p1021.c (extracted). */
#include "p1021_read_fn_extract.h"

/* Two-block read with a bad second block: the bad block's data must
 * not reach the output; the read continues at the third block.
 * Pre-fix the marker counter was never reset, so the bad block was
 * copied as valid data. */
START_TEST (test_bad_block_in_later_block_skipped){
    uint8_t out[2 * SIM_BLOCK_SIZE];
    int ret, p;

    sim_reset();
    sim_mark_bad_block(1, 1);

    ret = ext_flash_read(0, out, 2 * SIM_BLOCK_SIZE);

    ck_assert_int_eq(ret, 2 * SIM_BLOCK_SIZE);
    /* block 0 delivered as-is */
    for (p = 0; p < SIM_BLOCK_PAGES; p++)
        ck_assert_int_eq(memcmp(out + p * SIM_PAGE_SIZE, g_nand[p],
                                SIM_PAGE_SIZE), 0);
    /* block 1 skipped, block 2 takes its place */
    for (p = 0; p < SIM_BLOCK_PAGES; p++)
        ck_assert_int_eq(memcmp(out + (SIM_BLOCK_PAGES + p) *
                                SIM_PAGE_SIZE,
                                g_nand[2 * SIM_BLOCK_PAGES + p],
                                SIM_PAGE_SIZE), 0);
}
END_TEST

/* Bad marker on the second page of the first block: page 0 was
 * already delivered when the skip fires, so it must be discarded
 * along with the rest of the bad block. The output is the first two
 * pages of block 1, nothing is written past the buffer, and the
 * return value is still the full requested length. */
START_TEST(test_bad_marker_second_page_dropped)
{
    uint8_t out[1024 + 64];
    int ret, i;

    sim_reset();
    g_spare[1][SIM_MARKER_SPARE] = 0x00; /* marker on page 1 only */
    for (i = 1024; i < (int)sizeof(out); i++)
        out[i] = 0xEE;

    ret = ext_flash_read(0, out, 1024);

    ck_assert_int_eq(ret, 1024);
    /* block 0 fully skipped: output starts at block 1 page 0 */
    ck_assert_int_eq(memcmp(out, g_nand[SIM_BLOCK_PAGES],
                            SIM_PAGE_SIZE), 0);
    ck_assert_int_eq(memcmp(out + SIM_PAGE_SIZE,
                            g_nand[SIM_BLOCK_PAGES + 1],
                            SIM_PAGE_SIZE), 0);
    for (i = 1024; i < (int)sizeof(out); i++)
        ck_assert_uint_eq(out[i], 0xEE);
}
END_TEST

/* Bad first block, marker on its first page: the classic skip. The
 * whole request is satisfied from the second block. */
START_TEST(test_bad_first_block_page0)
{
    uint8_t out[SIM_BLOCK_SIZE];
    int ret, p;

    sim_reset();
    sim_mark_bad_block(0, 1);

    ret = ext_flash_read(0, out, SIM_BLOCK_SIZE);

    ck_assert_int_eq(ret, SIM_BLOCK_SIZE);
    for (p = 0; p < SIM_BLOCK_PAGES; p++)
        ck_assert_int_eq(memcmp(out + p * SIM_PAGE_SIZE,
                                g_nand[SIM_BLOCK_PAGES + p],
                                SIM_PAGE_SIZE), 0);
}
END_TEST

/* All blocks good: a plain two-block read is byte-exact. */
START_TEST(test_all_good_two_blocks)
{
    uint8_t out[2 * SIM_BLOCK_SIZE];
    int ret, p;

    sim_reset();

    ret = ext_flash_read(0, out, 2 * SIM_BLOCK_SIZE);

    ck_assert_int_eq(ret, 2 * SIM_BLOCK_SIZE);
    for (p = 0; p < 2 * SIM_BLOCK_PAGES; p++)
        ck_assert_int_eq(memcmp(out + p * SIM_PAGE_SIZE, g_nand[p],
                                SIM_PAGE_SIZE), 0);
}
END_TEST

/* Unaligned start mid-page spanning four pages: the column offset
 * and the per-page copy size stay correct across the read. */
START_TEST(test_unaligned_start_across_pages)
{
    uint8_t out[2000];
    int ret, off, p, i;

    sim_reset();

    ret = ext_flash_read(256, out, 2000);

    ck_assert_int_eq(ret, 2000);
    off = 0;
    for (p = 0; off < 2000; p++) {
        int start = (p == 0) ? 256 : 0;
        int take = SIM_PAGE_SIZE - start;

        if (take > 2000 - off)
            take = 2000 - off;
        for (i = 0; i < take; i++)
            ck_assert_uint_eq(out[off + i], g_nand[p][start + i]);
        off += take;
    }
}
END_TEST

Suite *p1021_read_badblock_suite(void)
{
    Suite *s  = suite_create("p1021 read badblock");
    TCase *tc = tcase_create("bad-block");

    tcase_add_test(tc, test_bad_block_in_later_block_skipped);
    tcase_add_test(tc, test_bad_marker_second_page_dropped);
    tcase_add_test(tc, test_bad_first_block_page0);
    tcase_add_test(tc, test_all_good_two_blocks);
    tcase_add_test(tc, test_unaligned_start_across_pages);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = p1021_read_badblock_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
