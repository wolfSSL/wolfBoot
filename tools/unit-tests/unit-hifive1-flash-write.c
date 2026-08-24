/* unit-hifive1-flash-write.c
 *
 * Regression test for F-11035: hal_flash_write() in hal/hifive1.c
 * selected the page path and sized the partial-page copy from the
 * original total `len` instead of the bytes still remaining. A
 * multi-page write that ends in a partial page therefore took the
 * full-page branch on the last iteration, read past the end of the
 * caller's buffer and programmed a whole page past the requested range.
 *
 * The real function is extracted by the Makefile and run against a
 * mock fespi model: FLASH_BASE points at a flash image buffer,
 * fespi_write_address()/fespi_sw_tx() program into it, and the
 * read-modify-write path reads the current image back through
 * FLASH_BASE, exactly as the hardware would.
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

#define RAMFUNCTION
#define FLASH_PAGE_SIZE 256
#define FESPI_PAGE_PROGRAM 0x02

#define NUM_PAGES 4
static uint8_t g_flash[NUM_PAGES * FLASH_PAGE_SIZE];
#define FLASH_BASE ((uintptr_t)g_flash)

static uint32_t g_txmark;
#define FESPI_REG_TXMARK (g_txmark)

/* ---- mock fespi model ------------------------------------------------ */

static uint32_t g_cur; /* byte offset in g_flash the next sw_tx lands at */

static void fespi_write_address(uint32_t address)
{
    g_cur = address;
}

static void fespi_sw_tx(uint8_t b)
{
    if (g_cur < sizeof(g_flash)) {
        g_flash[g_cur++] = b;
    }
}

static void fespi_hwmode(void)     { }
static void fespi_swmode(void)     { }
static void fespi_csmode_hold(void){ }
static void fespi_csmode_auto(void){ }
static void fespi_wait_txwm(void)  { }
static void fespi_wait_flash_busy(void) { }
static void fespi_write_enable(void)    { }

/* The real hal_flash_write() from hal/hifive1.c (extracted). */
#include "hifive1_flash_write_extract.h"

#define ERASED 0xEE
#define DATA_B 0xA1
#define CANARY 0x5A
#define REQ_LEN 356
#define CANARY_LEN 160

static void model_reset(void)
{
    memset(g_flash, ERASED, sizeof(g_flash));
    g_txmark = 0;
    g_cur = 0;
}

/* The bug: a page-aligned multi-page write whose final page is partial.
 * Pre-fix the last iteration took the full-page branch, read
 * data[len..len+155] (past the buffer) and programmed 256 bytes where
 * only 100 were requested. The tail of the last page must keep the
 * erased pattern, and nothing past the requested bytes may change. */
START_TEST (test_final_partial_page_not_overread)
{
    /* One contiguous buffer so the canary is guaranteed to sit right
     * after the requested data: an out-of-range read lands on known
     * bytes instead of unspecified stack layout. */
    uint8_t buf[REQ_LEN + CANARY_LEN];
    uint8_t *data = buf;
    uint8_t *canary = buf + REQ_LEN;
    int i;
    int ret;

    for (i = 0; i < REQ_LEN; i++)
        data[i] = DATA_B;
    for (i = 0; i < CANARY_LEN; i++)
        canary[i] = CANARY;

    /* Relative offsets are accepted as-is (address < FLASH_BASE), which
     * keeps the 32-bit address parameter away from 64-bit host pointers. */
    model_reset();
    ret = hal_flash_write(0, data, REQ_LEN);
    ck_assert_int_eq(ret, 0);

    /* Page 0: full page of data. */
    ck_assert_mem_eq(g_flash, data, FLASH_PAGE_SIZE);

    /* Page 1: first 100 bytes are data, the remaining 156 must stay
     * erased (a full-page branch would have written data/canary here). */
    ck_assert_mem_eq(g_flash + FLASH_PAGE_SIZE, data + FLASH_PAGE_SIZE,
                     100);
    for (i = 100; i < FLASH_PAGE_SIZE; i++) {
        ck_assert_uint_eq(g_flash[FLASH_PAGE_SIZE + i], ERASED);
    }

    /* Nothing past the request was written in the buffer. */
    for (i = 0; i < CANARY_LEN; i++) {
        ck_assert_uint_eq(canary[i], CANARY);
    }
}
END_TEST

/* A single unaligned write that stays within one page (the RMW path
 * that already worked): leading and trailing bytes of the page are
 * preserved. */
START_TEST (test_unaligned_single_page_rmw)
{
    uint8_t data[64];
    int i;
    int ret;

    for (i = 0; i < (int)sizeof(data); i++)
        data[i] = (uint8_t)(i + 1);

    model_reset();
    ret = hal_flash_write(100, data, sizeof(data));
    ck_assert_int_eq(ret, 0);

    /* Offsets before and after the write keep the erased pattern. */
    for (i = 0; i < 100; i++)
        ck_assert_uint_eq(g_flash[i], ERASED);
    ck_assert_mem_eq(g_flash + 100, data, sizeof(data));
    for (i = 100 + (int)sizeof(data); i < FLASH_PAGE_SIZE; i++)
        ck_assert_uint_eq(g_flash[i], ERASED);
}
END_TEST

/* A write of exactly one full page is unchanged by the fix. */
START_TEST (test_exact_full_page)
{
    uint8_t data[FLASH_PAGE_SIZE];
    int i;
    int ret;

    for (i = 0; i < (int)sizeof(data); i++)
        data[i] = (uint8_t)(0xC0 ^ (i & 0x1F));

    model_reset();
    ret = hal_flash_write(0, data, sizeof(data));
    ck_assert_int_eq(ret, 0);
    ck_assert_mem_eq(g_flash, data, FLASH_PAGE_SIZE);
    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        ck_assert_uint_eq(g_flash[FLASH_PAGE_SIZE + i], ERASED);
}
END_TEST

Suite *hifive1_flash_write_suite(void)
{
    Suite *s  = suite_create("hifive1 flash write");
    TCase *tc = tcase_create("final-partial-page");

    tcase_add_test(tc, test_final_partial_page_not_overread);
    tcase_add_test(tc, test_unaligned_single_page_rmw);
    tcase_add_test(tc, test_exact_full_page);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = hifive1_flash_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
