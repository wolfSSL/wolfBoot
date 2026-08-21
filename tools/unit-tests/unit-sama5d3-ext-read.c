/* unit-sama5d3-ext-read.c
 *
 * Regression test: ext_flash_read() in hal/sama5d3.c never applied the
 * intra-page offset of the start address (a partial read returned bytes
 * from the head of the page instead of the requested column), copied
 * sub-page reads in 32-bit words (dropping a sub-word tail), and wrote
 * a full NAND page into the caller's buffer for a multi-page read
 * ending mid-page (overrunning the buffer).
 *
 * The HAL cannot be built on the host, so the Makefile extracts
 * ext_flash_read() verbatim along with the nand_flash geometry struct;
 * the test provides host div_u()/mod() (the HAL's software-division
 * wrappers exist only because the Cortex-A5 has no divider) and
 * emulated nand_read_page()/nand_check_bad_block() backed by a
 * deterministic byte array.
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
#include <stdio.h>
#include <string.h>

/* sama5d3.h constants the extracted function needs. */
#define NAND_FLASH_PAGE_SIZE 0x800 /* 2KB */
#define NAND_FLASH_OOB_SIZE  0x40  /* 64B */
#define MAX_ECC_BYTES        8
#define wolfBoot_printf(...) do {} while (0)

/* Emulated NAND: 8 blocks x 16 pages x 2KB = 256KB. */
#define EMU_BLOCKS        8
#define EMU_PAGES_PER_BLK 16
#define EMU_PAGE_SIZE     NAND_FLASH_PAGE_SIZE
#define EMU_BLOCK_SIZE    (EMU_PAGES_PER_BLK * EMU_PAGE_SIZE)
#define EMU_TOTAL         (EMU_BLOCKS * EMU_BLOCK_SIZE)
static uint8_t emu_nand[EMU_TOTAL];

/* Host equivalents of the HAL's software-division wrappers. */
static uint32_t div_u(uint32_t dividend, uint32_t divisor)
{
    return dividend / divisor;
}

static uint32_t mod(uint32_t dividend, uint32_t divisor)
{
    return dividend % divisor;
}

/* Emulated NAND primitives: a full page read from column 0, like the
 * hardware path; every block is good. */
static int nand_check_bad_block(uint32_t block)
{
    (void)block;
    return 0;
}

static int nand_read_page(uint32_t block, uint32_t page, uint8_t *data)
{
    uint32_t row = block * EMU_PAGES_PER_BLK + page;

    memcpy(data, emu_nand + row * EMU_PAGE_SIZE, EMU_PAGE_SIZE);
    return 0;
}

/* The real ext_flash_read() and nand_flash struct from hal/sama5d3.c
 * (extracted by the Makefile). */
#include "sama5d3_read_extract.h"

/* Read buffer plus an adjacent canary region: a multi-page read ending
 * mid-page used to write a full page past the requested length. */
#define SCRATCH_LEN (EMU_PAGE_SIZE * 4)
static uint8_t scratch[SCRATCH_LEN + 64];
#define CANARY (scratch + SCRATCH_LEN)
#define CANARY_LEN 64

static void setup(void)
{
    uint32_t i;

    /* Deterministic pattern: every byte depends on row and column. */
    for (i = 0; i < EMU_TOTAL; i++)
        emu_nand[i] = (uint8_t)((i / EMU_PAGE_SIZE) * 7 + i);

    nand_flash.page_size     = EMU_PAGE_SIZE;
    nand_flash.block_size    = EMU_BLOCK_SIZE;
    nand_flash.block_count   = EMU_BLOCKS;
    nand_flash.pages_per_block = EMU_PAGES_PER_BLK;
    nand_flash.pages_per_device = EMU_BLOCKS * EMU_PAGES_PER_BLK;
    nand_flash.total_size    = EMU_TOTAL;
}

static void teardown(void)
{
}

static void fill_expected(uint8_t *dst, uint32_t address, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        dst[i] = emu_nand[address + i];
}

/* Run one read and compare against the emulated device byte for byte. */
static void read_case(uint32_t address, int len)
{
    uint8_t expected[SCRATCH_LEN];
    int i;
    int ret;

    ck_assert_int_lt(len, SCRATCH_LEN);
    memset(scratch, 0xEE, sizeof(scratch));
    fill_expected(expected, address, (uint32_t)len);

    ret = ext_flash_read(address, scratch, len);
    ck_assert_int_eq(ret, len);
    if (len > 0)
        ck_assert_mem_eq(scratch, expected, (size_t)len);

    /* The canary must be untouched: nothing may be written past len. */
    for (i = 0; i < CANARY_LEN; i++)
        ck_assert_uint_eq(CANARY[i], 0xEE);
}

START_TEST(test_read_zero_length)
{
    memset(scratch, 0xEE, sizeof(scratch));
    ck_assert_int_eq(ext_flash_read(0x4000, scratch, 0), 0);
    ck_assert_uint_eq(scratch[0], 0xEE);
}
END_TEST

START_TEST(test_read_aligned_full_page)
{
    read_case(0, EMU_PAGE_SIZE);
    read_case(0x10000, EMU_PAGE_SIZE);
}
END_TEST

START_TEST(test_read_unaligned_small)
{
    /* Mid-page starts: the head of the page must not be returned. */
    read_case(1, 4);
    read_case(0x7FC, 8);
    read_case(0x400, 64);
}
END_TEST

START_TEST(test_read_subword_lengths)
{
    /* 1-3 byte reads copy nothing in a word-count loop. */
    read_case(0x800, 1);
    read_case(0x801, 2);
    read_case(0x1800, 3);
}
END_TEST

START_TEST(test_read_sha_block_pattern)
{
    /* The integrity check hashes the image in 64-byte blocks from
     * fw_base + offset: every block after the first in each page is
     * an unaligned small read. */
    uint32_t offset;

    for (offset = 0; offset < EMU_PAGE_SIZE; offset += 0x40)
        read_case(0x800 + offset, 64);
}
END_TEST

START_TEST(test_read_page_boundaries)
{
    read_case(0, EMU_PAGE_SIZE - 1);
    read_case(0, EMU_PAGE_SIZE + 1);
    /* Start near the end of a page and cross into the next. */
    read_case(0x7F0, 0x20);
    read_case(0x7FF, 0x101);
}
END_TEST

START_TEST(test_read_multipage_partial_tail)
{
    /* Multi-page reads whose tail is shorter than a page (and not a
     * multiple of 4): the tail page must not be written in full. */
    read_case(0x100, 0x903);
    read_case(0, 0x1805);
    read_case(0x40, 0x1F01);
}
END_TEST

START_TEST(test_read_cross_block)
{
    read_case(0x7F00, 0x120);
    read_case(0x7000, 0x200);
    read_case(0x7001, 0x1000);
}
END_TEST

Suite *sama5d3_ext_read_suite(void)
{
    Suite *s = suite_create("sama5d3-ext-read");
    TCase *tc = tcase_create("sama5d3-ext-read");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_read_zero_length);
    tcase_add_test(tc, test_read_aligned_full_page);
    tcase_add_test(tc, test_read_unaligned_small);
    tcase_add_test(tc, test_read_subword_lengths);
    tcase_add_test(tc, test_read_sha_block_pattern);
    tcase_add_test(tc, test_read_page_boundaries);
    tcase_add_test(tc, test_read_multipage_partial_tail);
    tcase_add_test(tc, test_read_cross_block);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = sama5d3_ext_read_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
