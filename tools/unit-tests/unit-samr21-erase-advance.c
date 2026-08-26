/* unit-samr21-erase-advance.c
 *
 * Regression test for the hal_flash_erase() loop in hal/samr21.c:
 * the remaining length was decremented as the body of the NVMREADY
 * wait loop instead of once per completed erase, and the address was
 * never advanced, so the loop re-erased the first row forever and the
 * rest of the requested range was never reached. The erase command
 * (NVMCMD_ERASE, 0x02) is the SAM D/R NVMCTRL row erase: one command
 * erases a 256-byte row (4 x FLASH_PAGESIZE), so the loop must
 * advance by the row size.  A page-size stride issues four row-erase
 * commands against the same row (the later ones with a
 * non-row-aligned address), quadrupling erase time and wear.
 *
 * hal/samr21.c needs the SAMR21 SDK headers and cannot be built on the
 * host, so the Makefile extracts the register macros and the function
 * verbatim. The NVMCTRL register window is a host array with NVMREADY
 * preset (an idle peripheral whose erases complete immediately), and
 * the test checks the row left programmed by the loop.
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

/* The extracted function is a RAMFUNCTION; on the host that is nothing. */
#define RAMFUNCTION

/* Host mock of the NVMCTRL register window. The extracted macros index
 * it through NVMCTRL_BASE: command @ 0x0, INTFLAG @ 0x14, ADDR @ 0x1c. */
static uint8_t g_nvm[0x20];
#define NVMCTRL_BASE ((uintptr_t)g_nvm)

/* NVMCTRL register macros + command codes + erase granularity from
 * hal/samr21.c (extracted by the Makefile). */
#include "samr21_erase_extract.h"

static void mock_reset(void)
{
    memset(g_nvm, 0, sizeof(g_nvm));
    /* Idle peripheral: the last erase completed, NVMREADY is set. */
    g_nvm[0x14] = NVMCTRL_INTFLAG_NVMREADY;
}

/* The real hal_flash_erase() from hal/samr21.c (extracted). */
#include "samr21_erase_fn_extract.h"

/* A sub-row request issues a single row erase: the row containing the
 * range is erased once and the row register holds its start.  A
 * page-size stride would program the next page (0x1040) instead. */
START_TEST (test_erase_sub_row_single_erase)
{
    int ret;

    mock_reset();

    ret = hal_flash_erase(0x1000, 128);

    ck_assert_int_eq(ret, 0);
    /* The programmed row register holds (byte address) >> 1; the row
     * erased must be the one containing the requested range. */
    ck_assert_uint_eq(NVMCTRL_ADDR, (0x1000 >> 1));
}
END_TEST

/* Two rows from a row-aligned start must advance to the second row. */
START_TEST (test_erase_two_rows_advances)
{
    int ret;

    mock_reset();

    ret = hal_flash_erase(0, 512);

    ck_assert_int_eq(ret, 0);
    /* Rows 0x0 and 0x100: the final programmed row must be 0x100.
     * A page-size stride ends at 0x2C0; pre-advance it re-erased row
     * 0 forever (this case times out). */
    ck_assert_uint_eq(NVMCTRL_ADDR, (0x100 >> 1));
}
END_TEST

/* Exactly one full row erases once and stops, no extra row. */
START_TEST (test_erase_single_row)
{
    int ret;

    mock_reset();

    ret = hal_flash_erase(0x2000, 256);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, (0x2000 >> 1));
}
END_TEST

Suite *samr21_erase_suite(void)
{
    Suite *s  = suite_create("samr21 erase advance");
    TCase *tc = tcase_create("erase-address");

    tcase_add_test(tc, test_erase_sub_row_single_erase);
    tcase_add_test(tc, test_erase_two_rows_advances);
    tcase_add_test(tc, test_erase_single_row);
    /* Pre-advance the idle-peripheral model hangs in the re-erase
     * loop; the tcase timeout is what turns that hang into a failure. */
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = samr21_erase_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
