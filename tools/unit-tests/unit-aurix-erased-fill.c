/* unit-aurix-erased-fill.c
 *
 * Tests the PFLASH write, erase and erased check of hal/aurix_tc3xx.c,
 * extracted by the Makefile, against an emulated tc3_flash_* API that tracks
 * programmed pages. Built for both flag polarities.
 *
 * It also covers a regression in programBytesToErasedFlash(), which filled
 * word buffers with the byte value FLASH_BYTE_ERASED. A partial-page write
 * then programmed 0x000000FF words where the default polarity needs
 * 0xFFFFFFFF.
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

#include "wolfboot/wolfboot.h"

/* Target constants normally provided by the TC3xx IAP vendor headers
 * and the board build. */
#define RAMFUNCTION
#define TC3_PFLASH_PAGE_SIZE 0x200
#define TC3_FLASH_ERROR_CMD (-2)
#define TC3_FLASH_NOTBLANK (-5)
#define WOLFBOOT_SECTOR_SIZE 0x1000
#define GET_PAGE_ADDR(addr) \
    ((uintptr_t)(addr) & ~(TC3_PFLASH_PAGE_SIZE - 1))
#define GET_SECTOR_ADDR(addr) \
    ((uintptr_t)(addr) & ~(WOLFBOOT_SECTOR_SIZE - 1))
#define LED_ON(led)
#define LED_OFF(led)
#define HSM_PARK()
#define HSM_RELEASE()

/* Emulated PFlash: two sectors. */
#define FLASH_SIZE (2 * WOLFBOOT_SECTOR_SIZE)
#define FLASH_PAGES (FLASH_SIZE / TC3_PFLASH_PAGE_SIZE)
static uint8_t g_flash[FLASH_SIZE];
static uint8_t g_programmed[FLASH_PAGES];
static int g_blank_check_error;
static int g_programs;
static int g_erases;

static int tc3_flash_Program(uint32_t addr, const uint32_t *buf, uint32_t len)
{
    uint32_t p;

    for (p = addr / TC3_PFLASH_PAGE_SIZE;
         p < (addr + len) / TC3_PFLASH_PAGE_SIZE; p++) {
        ck_assert_msg(!g_programmed[p], "page %u programmed twice", p);
        g_programmed[p] = 1;
    }
    memcpy(g_flash + addr, buf, len);
    g_programs++;
    return 0;
}

static int tc3_flash_Erase(uint32_t addr, uint32_t len)
{
    ck_assert_uint_eq(addr % WOLFBOOT_SECTOR_SIZE, 0);
    ck_assert_uint_eq(len % WOLFBOOT_SECTOR_SIZE, 0);
    memset(g_flash + addr, FLASH_BYTE_ERASED, len);
    memset(g_programmed + addr / TC3_PFLASH_PAGE_SIZE, 0,
        len / TC3_PFLASH_PAGE_SIZE);
    g_erases++;
    return 0;
}

/* Verify Erased reports the programmed state, not the data */
static int tc3_flash_BlankCheck(uint32_t addr, uint32_t len)
{
    uint32_t p;

    if (g_blank_check_error)
        return TC3_FLASH_ERROR_CMD;
    for (p = addr / TC3_PFLASH_PAGE_SIZE;
         p < (addr + len + TC3_PFLASH_PAGE_SIZE - 1) / TC3_PFLASH_PAGE_SIZE;
         p++) {
        if (g_programmed[p])
            return TC3_FLASH_NOTBLANK;
    }
    return 0;
}

void wolfBoot_panic(void)
{
    _exit(120);
}

/* The real functions under test (extracted by the Makefile). */
#include "aurix_erased_extract.h"

static void setup(void)
{
    memset(g_flash, FLASH_BYTE_ERASED, sizeof(g_flash));
    memset(g_programmed, 0, sizeof(g_programmed));
    g_blank_check_error = 0;
    g_programs = 0;
    g_erases = 0;
}

static void teardown(void)
{
}

static void fill(uint8_t *buf, size_t len, uint8_t base)
{
    size_t i;

    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(base + i);
}

/* Program one page with data that reads as erased */
static void program_page(uint32_t page, uint8_t val)
{
    memset(g_flash + page * TC3_PFLASH_PAGE_SIZE, val, TC3_PFLASH_PAGE_SIZE);
    g_programmed[page] = 1;
}

/* A partial-page write must leave the rest of the page holding the
 * erased byte pattern in every byte, not just the low byte of each
 * word. Pre-fix the untouched words were 0x000000FF (default
 * polarity). */
START_TEST(test_partial_page_fill_erased)
{
    uint8_t data[16];
    uint32_t addr = TC3_PFLASH_PAGE_SIZE + 32;
    int i;

    fill(data, sizeof(data), 0x40);
    ck_assert_int_eq(programBytesToErasedFlash(addr, data,
        sizeof(data)), 0);

    for (i = 0; i < 16; i++)
        ck_assert_uint_eq(g_flash[addr + i], data[i]);
    for (i = 0; i < (int)TC3_PFLASH_PAGE_SIZE; i++) {
        if (i >= 32 && i < 48)
            continue;
        ck_assert_uint_eq(g_flash[addr - 32 + i], FLASH_BYTE_ERASED);
    }
    /* The words adjacent to the data must be erased in every byte. */
    ck_assert_uint_eq(g_flash[addr - 1], FLASH_BYTE_ERASED);
    ck_assert_uint_eq(g_flash[addr + 16], FLASH_BYTE_ERASED);
}
END_TEST

/* A full-page write programs exactly the given data. */
START_TEST(test_full_page_write)
{
    uint8_t data[TC3_PFLASH_PAGE_SIZE];
    int i;

    fill(data, sizeof(data), 0x10);
    ck_assert_int_eq(programBytesToErasedFlash(TC3_PFLASH_PAGE_SIZE,
        data, sizeof(data)), 0);
    for (i = 0; i < (int)sizeof(data); i++)
        ck_assert_uint_eq(g_flash[TC3_PFLASH_PAGE_SIZE + i], data[i]);
}
END_TEST

/* A write into blank pages programs them */
START_TEST(test_write_blank_pages)
{
    uint8_t data[2 * TC3_PFLASH_PAGE_SIZE];
    int i;

    fill(data, sizeof(data), 0x21);
    ck_assert_int_eq(hal_flash_write(TC3_PFLASH_PAGE_SIZE, data,
        sizeof(data)), 0);
    for (i = 0; i < (int)sizeof(data); i++)
        ck_assert_uint_eq(g_flash[TC3_PFLASH_PAGE_SIZE + i], data[i]);
    ck_assert(g_programmed[1] && g_programmed[2]);
    ck_assert(!g_programmed[0] && !g_programmed[3]);
}
END_TEST

/* A write that touches a programmed page fails and changes nothing, even if
 * the page data reads as erased */
START_TEST(test_write_programmed_page_fails)
{
    uint8_t data[2 * TC3_PFLASH_PAGE_SIZE];
    uint8_t before[FLASH_SIZE];

    program_page(2, FLASH_BYTE_ERASED);
    memcpy(before, g_flash, sizeof(before));
    fill(data, sizeof(data), 0x33);

    /* page 1 blank, page 2 programmed */
    ck_assert_int_eq(hal_flash_write(TC3_PFLASH_PAGE_SIZE + 8, data,
        sizeof(data) - 8), -1);
    ck_assert_mem_eq(g_flash, before, sizeof(before));
    ck_assert(!g_programmed[1]);
    ck_assert_int_eq(g_programs, 0);
    ck_assert_int_eq(g_erases, 0);
}
END_TEST

/* Only whole sectors erase. Other ranges fail without an erase. */
START_TEST(test_erase_sectors_only)
{
    program_page(0, 0x5A);
    program_page(FLASH_PAGES - 1, 0x5A);

    ck_assert_int_eq(hal_flash_erase(TC3_PFLASH_PAGE_SIZE,
        WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(hal_flash_erase(0, WOLFBOOT_SECTOR_SIZE / 2), -1);
    ck_assert_int_eq(g_erases, 0);
    ck_assert(g_programmed[0]);

    ck_assert_int_eq(hal_flash_erase(0, FLASH_SIZE), 0);
    ck_assert(!g_programmed[0] && !g_programmed[FLASH_PAGES - 1]);
    ck_assert_uint_eq(g_flash[0], FLASH_BYTE_ERASED);
    ck_assert_int_eq(hal_flash_erase(0, 0), 0);
}
END_TEST

/* The erased check follows the blank check, not the data */
START_TEST(test_is_erased_uses_blank_check)
{
    ck_assert_int_eq(hal_flash_is_erased(0, FLASH_SIZE), 1);
    ck_assert_int_eq(ext_flash_is_erased(8, 4), 1);

    program_page(1, FLASH_BYTE_ERASED);
    ck_assert_int_eq(hal_flash_is_erased(TC3_PFLASH_PAGE_SIZE + 4, 4), 0);
    ck_assert_int_eq(ext_flash_is_erased(0, FLASH_SIZE), 0);
    ck_assert_int_eq(hal_flash_is_erased(0, TC3_PFLASH_PAGE_SIZE), 1);

    g_blank_check_error = 1;
    ck_assert_int_eq(hal_flash_is_erased(0, 4), -1);
    ck_assert_int_eq(hal_flash_write(0, (const uint8_t *)"x", 1), -1);
}
END_TEST

Suite *aurix_erased_fill_suite(void)
{
    Suite *s = suite_create("aurix-erased-fill");
    TCase *tc = tcase_create("aurix-erased-fill");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_partial_page_fill_erased);
    tcase_add_test(tc, test_full_page_write);
    tcase_add_test(tc, test_write_blank_pages);
    tcase_add_test(tc, test_write_programmed_page_fails);
    tcase_add_test(tc, test_erase_sectors_only);
    tcase_add_test(tc, test_is_erased_uses_blank_check);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = aurix_erased_fill_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
