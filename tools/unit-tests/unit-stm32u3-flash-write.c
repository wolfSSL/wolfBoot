/* unit-stm32u3-flash-write.c
 *
 * Regression test for F-12106: hal_flash_write() in hal/stm32u3.c cleared
 * flash errors before programming and waited for completion, but never
 * checked the post-program status register, so programming faults
 * (OPERR/PROGERR/WRPERR/PGAERR/SIZERR/PGSERR) were swallowed and the
 * write reported success. The writer must now return -1 when the status
 * register carries an error bit, and reject addresses not aligned to the
 * 8-byte double-word programming unit.
 *
 * The real functions are extracted by the Makefile; the flash registers
 * are stubbed with plain variables.
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
#include <sys/mman.h>
#include <unistd.h>

#define RAMFUNCTION

/* Stubbed flash registers. */
static volatile uint32_t g_sr;
static volatile uint32_t g_cr;
#define FLASH_NS_SR g_sr
#define FLASH_NS_CR g_cr
#define ISB()
#define FLASH_SR_EOP                (1 << 0)
#define FLASH_SR_OPERR              (1 << 1)
#define FLASH_SR_PROGERR            (1 << 3)
#define FLASH_SR_WRPERR             (1 << 4)
#define FLASH_SR_PGAERR             (1 << 5)
#define FLASH_SR_SIZERR             (1 << 6)
#define FLASH_SR_PGSERR             (1 << 7)
#define FLASH_SR_OPTWERR            (1 << 13)
#define FLASH_SR_BSY                (1 << 16)
#define FLASH_SR_WDW                (1 << 17)
#define FLASH_CR_PG                 (1 << 0)

/* The real functions from hal/stm32u3.c (extracted by the Makefile). */
#include "stm32u3_flash_write_extract.h"

static void setup(void)
{
    g_sr = 0;
    g_cr = 0;
}

static void teardown(void)
{
}

/* The HAL takes a 32-bit address; on a 64-bit host the test buffer must
 * live below 4 GiB so the truncating cast is exact. */
static uint32_t *alloc32(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    ck_assert_ptr_ne(p, MAP_FAILED);
    return (uint32_t *)p;
}

/* Clean status: the write succeeds and the data lands. */
START_TEST(test_write_success){
    uint32_t *dst = alloc32(8);

    memset(dst, 0, 8);
    g_sr = 0;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)dst,
                                     (const uint8_t *)dst, 8), 0);
    munmap(dst, 8);
}
END_TEST

/* A programming fault in the status register must be reported. The stub
 * register keeps the bit set (host has no write-1-to-clear semantics),
 * so the pre-program clear does not hide it. */
START_TEST(test_write_reports_progerr)
{
    uint32_t *dst = alloc32(8);

    memset(dst, 0, 8);
    g_sr = FLASH_SR_PROGERR;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)dst,
                                     (const uint8_t *)dst, 8), -1);
    munmap(dst, 8);
}
END_TEST

/* A write-protection fault must be reported too. */
START_TEST(test_write_reports_wrperr)
{
    uint32_t *dst = alloc32(8);

    memset(dst, 0, 8);
    g_sr = FLASH_SR_WRPERR;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)dst,
                                     (const uint8_t *)dst, 8), -1);
    munmap(dst, 8);
}
END_TEST

/* The programming unit is a double word: a misaligned address is
 * rejected before any flash activity. */
START_TEST(test_write_misaligned_rejected)
{
    uint32_t *dst = alloc32(8);

    memset(dst, 0, 8);

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)(dst + 1),
                                     (const uint8_t *)dst, 8), -1);
    ck_assert_uint_eq(g_cr, 0);
    munmap(dst, 8);
}
END_TEST

Suite *stm32u3_flash_write_suite(void)
{
    Suite *s = suite_create("stm32u3-flash-write");
    TCase *tc = tcase_create("stm32u3-flash-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_success);
    tcase_add_test(tc, test_write_reports_progerr);
    tcase_add_test(tc, test_write_reports_wrperr);
    tcase_add_test(tc, test_write_misaligned_rejected);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = stm32u3_flash_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
