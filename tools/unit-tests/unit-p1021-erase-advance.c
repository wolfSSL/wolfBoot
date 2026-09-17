/* unit-p1021-erase-advance.c
 *
 * Regression test for F-11034: ext_flash_erase() in hal/nxp_p1021.c
 * decremented the remaining length but never advanced `address`, so
 * every loop iteration derived the same page and re-erased the first
 * block while the rest of the requested range stayed intact.
 *
 * The real function is extracted by the Makefile together with the
 * ELBC register macros it uses; the ELBC register access (set32/get32)
 * and the flash helpers are mocked, and the test records the page
 * address programmed for each erase command.
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

/* ELBC_BASE is built from CCSRBAR in the real file; pin it here. */
#define CCSRBAR 0x0

/* ELBC register macros + NAND command codes from hal/nxp_p1021.c
 * (extracted by the Makefile). */
#include "p1021_erase_extract.h"

#define MAX_TRACKED_PAGES 32
static int g_pages[MAX_TRACKED_PAGES];
static int g_page_calls;
static int g_cmd_calls;
static int g_cmd_ret;
static uint32_t g_status;

/* ONFI status byte: DQ0 set = program/erase fail, DQ7 clear = protected.
 * 0x80 is a clean success (no fail, not protected); 0x81 is an
 * erase/program failure (DQ0 set); 0x00 is write-protected (DQ7 clear). */
#define STATUS_OK      0x80
#define STATUS_ERASE_FAIL 0x81
#define STATUS_WP_PROTECTED 0x00

static void mock_reset(int cmd_ret, uint32_t status)
{
    g_page_calls = 0;
    g_cmd_calls = 0;
    g_cmd_ret = cmd_ret;
    g_status = status;
}

static void hal_flash_set_addr(int page, int col)
{
    (void)col;

    if (g_page_calls < MAX_TRACKED_PAGES) {
        g_pages[g_page_calls] = page;
    }
    g_page_calls++;
}

static int hal_flash_command(uint8_t iswrite)
{
    (void)iswrite;

    g_cmd_calls++;
    return g_cmd_ret;
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
    return g_status; /* MDR: the NAND status byte */
}

/* The real ext_flash_erase() from hal/nxp_p1021.c (extracted). */
#include "p1021_erase_fn_extract.h"

/* Small-page geometry from the extracted FLASH_PAGE_SIZE (512):
 * a 16 KiB block spans 32 pages. */
#define TEST_PAGE_SIZE 512u
#define TEST_BLOCK_SIZE (16u * 1024u)

START_TEST (test_erase_advances_through_blocks)
{
    int ret;

    mock_reset(0, STATUS_OK);

    ret = ext_flash_erase(0, 2 * (int)TEST_BLOCK_SIZE);

    ck_assert_int_eq(ret, 0);
    /* Two blocks erased: page 0, then page 32 (one block later). */
    ck_assert_int_eq(g_page_calls, 2);
    ck_assert_int_eq(g_pages[0], 0);
    ck_assert_int_eq(g_pages[1], (int)(TEST_BLOCK_SIZE / TEST_PAGE_SIZE));
}
END_TEST

START_TEST (test_erase_stops_on_command_error)
{
    int ret;

    mock_reset(-1, STATUS_OK);

    ret = ext_flash_erase(0, 2 * (int)TEST_BLOCK_SIZE);

    ck_assert_int_eq(ret, -1);
    ck_assert_int_eq(g_page_calls, 1);
    ck_assert_int_eq(g_pages[0], 0);
}
END_TEST

START_TEST (test_erase_stops_on_status_fail)
{
    int ret;

    /* The command sequence completes (cmd_ret 0) but the NAND reports an
     * erase failure in the status byte (DQ0 set). */
    mock_reset(0, STATUS_ERASE_FAIL);

    ret = ext_flash_erase(0, 2 * (int)TEST_BLOCK_SIZE);

    ck_assert_int_eq(ret, -1);
    ck_assert_int_eq(g_page_calls, 1);
    ck_assert_int_eq(g_pages[0], 0);
}
END_TEST

START_TEST (test_erase_stops_on_write_protected)
{
    int ret;

    /* The command sequence completes (cmd_ret 0) but the NAND reports the
     * block as write-protected in the status byte (DQ7 clear). The erase
     * must fail and not advance to the next block. */
    mock_reset(0, STATUS_WP_PROTECTED);

    ret = ext_flash_erase(0, 2 * (int)TEST_BLOCK_SIZE);

    ck_assert_int_eq(ret, -1);
    ck_assert_int_eq(g_page_calls, 1);
    ck_assert_int_eq(g_pages[0], 0);
}
END_TEST

Suite *p1021_erase_suite(void)
{
    Suite *s  = suite_create("p1021 erase advance");
    TCase *tc = tcase_create("erase-address");

    tcase_add_test(tc, test_erase_advances_through_blocks);
    tcase_add_test(tc, test_erase_stops_on_command_error);
    tcase_add_test(tc, test_erase_stops_on_status_fail);
    tcase_add_test(tc, test_erase_stops_on_write_protected);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = p1021_erase_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
