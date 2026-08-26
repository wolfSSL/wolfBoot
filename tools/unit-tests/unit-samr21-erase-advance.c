/* unit-samr21-erase-advance.c
 *
 * Regression test for F-11036: hal_flash_erase() in hal/samr21.c
 * decremented the remaining length as the body of the NVMREADY wait
 * loop instead of once per completed erase, and never advanced the
 * address. With the peripheral idle (NVMREADY set) the wait body never
 * ran, the length never shrank, and the loop re-erased the same page
 * forever; the rest of the requested range was never reached.
 *
 * hal/samr21.c needs the SAMR21 SDK headers and cannot be built on the
 * host, so the Makefile extracts the register macros and the function
 * verbatim. The NVMCTRL register window is a host array with NVMREADY
 * preset (an idle peripheral whose erases complete immediately), and
 * the test checks the page left programmed by the loop.
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

/* NVMCTRL register macros + command codes + page size from
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

/* A 64-byte page is one erase; a multi-page range must end with the
 * last page of the range programmed, not the first. */
START_TEST (test_erase_multi_page_advances)
{
    int ret;

    mock_reset();

    ret = hal_flash_erase(0x1000, 128);

    ck_assert_int_eq(ret, 0);
    /* Two 64-byte pages: 0x1000 then 0x1040. The programmed row
     * register holds (byte address) >> 1; the final one must be the
     * last page of the range. Pre-fix the loop re-erased page 0x1000
     * forever (this case times out). */
    ck_assert_uint_eq(NVMCTRL_ADDR, (0x1040 >> 1));
}
END_TEST

/* Four pages from the start of the flash must advance to the last one. */
START_TEST (test_erase_four_pages_advances)
{
    int ret;

    mock_reset();

    ret = hal_flash_erase(0, 256);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, (192u >> 1));
}
END_TEST

/* A single-page erase completes and leaves its page programmed. */
START_TEST (test_erase_single_page)
{
    int ret;

    mock_reset();

    ret = hal_flash_erase(0x2000, 64);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, (0x2000 >> 1));
}
END_TEST

Suite *samr21_erase_suite(void)
{
    Suite *s  = suite_create("samr21 erase advance");
    TCase *tc = tcase_create("erase-address");

    tcase_add_test(tc, test_erase_multi_page_advances);
    tcase_add_test(tc, test_erase_four_pages_advances);
    tcase_add_test(tc, test_erase_single_page);
    /* Pre-fix the idle-peripheral model hangs in the re-erase loop;
     * the tcase timeout is what turns that hang into a failure. */
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
