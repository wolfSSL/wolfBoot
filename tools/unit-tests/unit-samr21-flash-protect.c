/* unit-samr21-flash-protect.c
 *
 * Covers the lock-region arithmetic in hal_flash_protect() (hal/samr21.c).
 * NVMCTRL grants write protection in whole lock regions, one sixteenth of the
 * array each, so the region count has to be rounded up: a range ending inside
 * a region leaves that region writable if the count is truncated, and the
 * call still reports success. The region size comes from NVMCTRL.PARAM rather
 * than a compile-time constant, because a 128KB part has 8KB regions where a
 * 256KB part has 16KB, and sizing a 128KB part as if it were 256KB locks half
 * the range asked for.
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

/* hal.h picks this by pointer width; the target is 32-bit. */
typedef uint32_t haladdr_t;

/* Host mock of the NVMCTRL register window. The extracted macros index it
 * through NVMCTRL_BASE: command @ 0x0, PARAM @ 0x8, INTFLAG @ 0x14,
 * STATUS @ 0x18, ADDR @ 0x1c. */
static uint8_t g_nvm[0x20];
#define NVMCTRL_BASE ((uintptr_t)g_nvm)

/* Host mock of the PAC1 write-protect registers: WPCLR @ 0x0, WPSET @ 0x4.
 * The hardware treats these as write-one-to-clear and write-one-to-set, so a
 * plain memory mock only records that the bit was written, which is all the
 * assertions below need. */
static uint8_t g_pac[0x8];
#define PAC1_BASE ((uintptr_t)g_pac)

/* NVMCTRL register macros, command codes and the PARAM/region constants
 * from hal/samr21.c (extracted by the Makefile). */
#include "samr21_protect_extract.h"

/* Every SAMD21 writes flash a 64-byte page at a time, so PSZ is 3
 * (8 << 3 == 64) and only the page count tells the parts apart. */
#define PSZ_64 (3U << NVMPARAM_PSZ_SHIFT)
#define PARAM_256K ((4096U) | PSZ_64)   /* J18A/G18A: 256KB, 16KB regions */
#define PARAM_128K ((2048U) | PSZ_64)   /* J17A/G17A: 128KB,  8KB regions */

/* ADDR holds a 16-bit (half-word) address, so a byte address is halved. */
#define ADDR_OF(byte_addr) ((uint32_t)(byte_addr) >> 1)

/* An address no region start can equal, so a test can tell "ADDR was never
 * written" from "region 0 was locked". */
#define ADDR_UNTOUCHED (0xA5A5A5A5)

static void mock_reset(uint32_t param)
{
    memset(g_nvm, 0, sizeof(g_nvm));
    memset(g_pac, 0, sizeof(g_pac));
    NVMCTRL_PARAM = param;
    NVMCTRL_ADDR = ADDR_UNTOUCHED;
    /* Idle peripheral, no latched error: the waits fall straight through. */
    NVMCTRL_INTFLAG = NVMCTRL_INTFLAG_NVMREADY;
    NVMCTRL_STATUS = 0;
}

/* The real nvm_wait(), hal_flash_unlock(), hal_flash_lock() and
 * hal_flash_protect() from hal/samr21.c. */
#include "samr21_protect_fn_extract.h"

/* The caller runs this after its last write, by which point hal_flash_lock()
 * has handed NVMCTRL back to the PAC, so the command register will not take a
 * write until the PAC is cleared again. Without the unlock the lock commands
 * are silently refused and the bootloader boots unprotected, which no
 * assertion on ADDR can see: the mock has no PAC enforcement. */
START_TEST (test_protect_clears_pac_around_commands)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0, 0x8000);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_ne(PAC1_WPCLR & PAC_WP_NVMCTL, 0);
    /* And handed back afterwards, so the application does not inherit an
     * unguarded flash controller. */
    ck_assert_uint_ne(PAC1_WPSET & PAC_WP_NVMCTL, 0);
}
END_TEST

/* The same on the error path: a failed lock must not leave the PAC open. */
START_TEST (test_protect_restores_pac_on_error)
{
    int ret;

    mock_reset(PARAM_256K);
    NVMCTRL_STATUS = NVMSTATUS_ERR;

    ret = hal_flash_protect(0, 0x8000);

    ck_assert_int_eq(ret, -1);
    ck_assert_uint_ne(PAC1_WPSET & PAC_WP_NVMCTL, 0);
}
END_TEST

/* The case the port actually relies on: a 32KB bootloader region on a 256KB
 * part is regions 0 and 1 of 16KB, so the last region locked starts at
 * 0x4000. */
START_TEST (test_protect_bootloader_256k)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0, 0x8000);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0x4000));
}
END_TEST

/* The same 32KB request on a 128KB part spans four 8KB regions, so the last
 * one starts at 0x6000. Sizing the regions from a hardcoded 256KB would stop
 * after 0x4000 and leave the top half of the bootloader writable. */
START_TEST (test_protect_bootloader_128k)
{
    int ret;

    mock_reset(PARAM_128K);

    ret = hal_flash_protect(0, 0x8000);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0x6000));
}
END_TEST

/* A range ending inside a region must lock that region whole. 0x3000..0x5000
 * on 16KB regions covers regions 0 and 1, so the last is 0x4000. Truncating
 * the count (len / region_size == 0 here) would lock region 0 alone, or
 * nothing at all, and still return success. */
START_TEST (test_protect_partial_region_rounds_up)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0x3000, 0x2000);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0x4000));
}
END_TEST

/* A single byte still locks the whole region containing it. */
START_TEST (test_protect_one_byte_locks_region)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0, 1);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0));
}
END_TEST

/* The start region is the one containing the address, not always region 0:
 * one region's worth at the second region boundary locks only that region. */
START_TEST (test_protect_start_region_offset)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0x4000, 0x4000);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0x4000));
}
END_TEST

/* The region the loop STARTS at, which the assertions above cannot see: they
 * read ADDR after the call, so only the last region written survives. A
 * latched error makes nvm_wait() fail on the first command and return
 * straight out, leaving ADDR holding the first region instead of the last.
 * Without this, starting the loop at region 0 regardless of the address
 * passes every other test here, and over-locks everything below the range. */
START_TEST (test_protect_first_region_is_start)
{
    int ret;

    mock_reset(PARAM_256K);
    NVMCTRL_STATUS = NVMSTATUS_ERR;

    ret = hal_flash_protect(0x8000, 0x4000);

    ck_assert_int_eq(ret, -1);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0x8000));
}
END_TEST

/* An empty range locks nothing and succeeds. The address here is deliberately
 * not region-aligned: the round-up carries the start offset, so without an
 * early return a zero length widens to the one region containing the address.
 * A region-aligned address would pass either way and prove nothing. */
START_TEST (test_protect_zero_len_locks_nothing)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0x3000, 0);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_UNTOUCHED);
}
END_TEST

/* A negative length is a caller bug, not an empty range: reject it rather
 * than sign-extending it into a span of the whole array. */
START_TEST (test_protect_negative_len_rejected)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0, -1);

    ck_assert_int_eq(ret, -1);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_UNTOUCHED);
}
END_TEST

/* A start past the end of the array has no region to lock. */
START_TEST (test_protect_start_past_end_rejected)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0x40000, 0x100);

    ck_assert_int_eq(ret, -1);
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_UNTOUCHED);
}
END_TEST

/* A range running off the end is clamped to the array rather than rejected,
 * and must stop at the last region instead of walking past it. */
START_TEST (test_protect_overlong_len_clamped)
{
    int ret;

    mock_reset(PARAM_256K);

    ret = hal_flash_protect(0x3C000, 0x10000);

    ck_assert_int_eq(ret, 0);
    /* Last of the sixteen 16KB regions. */
    ck_assert_uint_eq(NVMCTRL_ADDR, ADDR_OF(0x3C000));
}
END_TEST

/* A latched controller error has to surface: hal_flash_protect() failing is
 * what makes the caller panic instead of booting unprotected. */
START_TEST (test_protect_reports_nvm_error)
{
    int ret;

    mock_reset(PARAM_256K);
    NVMCTRL_STATUS = NVMSTATUS_ERR;

    ret = hal_flash_protect(0, 0x8000);

    ck_assert_int_eq(ret, -1);
}
END_TEST

/* A PARAM of zero (no page count) would divide by zero working out the
 * region size. Report it rather than fault. */
START_TEST (test_protect_zero_param_rejected)
{
    int ret;

    mock_reset(0);

    ret = hal_flash_protect(0, 0x8000);

    ck_assert_int_eq(ret, -1);
}
END_TEST

Suite *samr21_protect_suite(void)
{
    Suite *s  = suite_create("samr21 flash protect");
    TCase *tc = tcase_create("lock-regions");

    tcase_add_test(tc, test_protect_bootloader_256k);
    tcase_add_test(tc, test_protect_bootloader_128k);
    tcase_add_test(tc, test_protect_partial_region_rounds_up);
    tcase_add_test(tc, test_protect_one_byte_locks_region);
    tcase_add_test(tc, test_protect_start_region_offset);
    tcase_add_test(tc, test_protect_first_region_is_start);
    tcase_add_test(tc, test_protect_zero_len_locks_nothing);
    tcase_add_test(tc, test_protect_negative_len_rejected);
    tcase_add_test(tc, test_protect_start_past_end_rejected);
    tcase_add_test(tc, test_protect_overlong_len_clamped);
    tcase_add_test(tc, test_protect_reports_nvm_error);
    tcase_add_test(tc, test_protect_zero_param_rejected);
    tcase_add_test(tc, test_protect_clears_pac_around_commands);
    tcase_add_test(tc, test_protect_restores_pac_on_error);
    /* An unterminated region loop hangs rather than failing; the timeout is
     * what turns that into a reported failure. */
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = samr21_protect_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
