/* unit-rp2350-flash-write.c
 *
 * Regression test for F-12061: hal_flash_write() in hal/rp2350.c
 * chunked the write by WOLFBOOT_SECTOR_SIZE (8 KiB) and passed the
 * resulting sizes straight to pico-sdk flash_range_program(), which
 * requires a 256-byte page aligned address and a page multiple
 * length (invalid_params_if on both, no partial-page support). The
 * partition-state path without NVM_FLASH_WRITEONCE violates the
 * contract on every state transition: trailer_write() issues a
 * 1-byte write and partition_magic_write() a 4-byte write.
 *
 * The real function is extracted by the Makefile and run against a
 * mock flash_range_program() that enforces the ROM contract (any
 * unaligned or non page-multiple call is recorded as a violation)
 * and programs with flash AND semantics. The flash image is mmap'd
 * at XIP_BASE so the read-modify-write page reads hit real memory,
 * exactly as the XIP mapping would on hardware.
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

/* Host stand-in for the ARM build attribute. */
#define RAMFUNCTION

/* RP2350 XIP flash base (pico-sdk hardware/regs/addressmap.h). */
#define XIP_BASE 0x10000000UL
#define FLASH_PAGE_SIZE 256
/* Sector size from config/examples/rp2350.config. */
#define WOLFBOOT_SECTOR_SIZE 0x2000

#define FLASH_MEM_SZ (4 * FLASH_PAGE_SIZE)
static uint8_t *g_flash; /* mmap'd at XIP_BASE */

/* "SRAM" source buffer: bit 29 set, so the HAL takes the in-RAM
 * direct-program branch, the same branch a stack/.bss pointer takes
 * on the real target (SRAM at 0x20000000). */
#define SRAM_ADDR 0x21000000UL
#define SRAM_SZ 1024
static uint8_t *g_sram;

/* Contract violations recorded by the mock ROM call. */
static int g_violations;

void flash_range_program(uint32_t flash_offs, const uint8_t *data,
                         size_t count)
{
    size_t i;

    if ((flash_offs & (FLASH_PAGE_SIZE - 1)) ||
        (count & (FLASH_PAGE_SIZE - 1)) ||
        (flash_offs + count > FLASH_MEM_SZ)) {
        g_violations++;
        return;
    }
    /* Flash can only clear bits: programmed = old & new. */
    for (i = 0; i < count; i++)
        g_flash[flash_offs + i] &= data[i];
}

/* The real hal_flash_write() from hal/rp2350.c (extracted). */
#include "rp2350_flash_write_extract.h"

static void setup(void)
{
    memset(g_flash, 0xFF, FLASH_MEM_SZ); /* erased */
    memset(g_sram, 0x5A, SRAM_SZ);
    g_violations = 0;
}

static void teardown(void)
{
}

/* The exact partition-state write: trailer_write() without
* NVM_FLASH_WRITEONCE compiles to hal_flash_write(addr, &val, 1).
* Pre-fix the 1-byte length is passed to flash_range_program() and
* violates the ROM contract; post-fix the page is RMW'd. */
START_TEST(test_one_byte_trailer_write){
    uint8_t val = 0x7E;
    int i;

    /* Seed the page with an existing pattern; only the write offset
     * is erased, so the programmed byte must land as requested while
     * every other byte of the page is preserved. */
    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        g_flash[FLASH_PAGE_SIZE + i] = (i == 100) ? 0xFF :
                                       (uint8_t)(0xA0 ^ (i & 0x0F));
    g_sram[0] = val;

    ck_assert_int_eq(hal_flash_write((uint32_t)(XIP_BASE + FLASH_PAGE_SIZE +
                                                100), g_sram, 1), 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_uint_eq(g_flash[FLASH_PAGE_SIZE + 100], val);
    for (i = 0; i < FLASH_PAGE_SIZE; i++) {
        if (i != 100)
            ck_assert_uint_eq(g_flash[FLASH_PAGE_SIZE + i],
                              (uint8_t)(0xA0 ^ (i & 0x0F)));
    }
}
END_TEST

/* partition_magic_write() compiles to a 4-byte write of the magic
 * trailer. Same contract violation pre-fix. */
START_TEST(test_four_byte_magic_write)
{
    uint32_t magic = 0x600DF00D;
    uint8_t *m = (uint8_t *)&magic;
    int i;

    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        g_flash[i] = (uint8_t)(0x0F ^ (i & 0x70));
    /* The magic region is erased, as on a fresh partition tail. */
    g_flash[200] = g_flash[201] = g_flash[202] = g_flash[203] = 0xFF;
    memcpy(g_sram, m, 4);

    ck_assert_int_eq(hal_flash_write((uint32_t)(XIP_BASE + 200), g_sram, 4),
                     0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_uint_eq(*(uint32_t *)(g_flash + 200), magic);
    for (i = 0; i < FLASH_PAGE_SIZE; i++) {
        if (i >= 200 && i <= 203)
            continue;
        ck_assert_uint_eq(g_flash[i], (uint8_t)(0x0F ^ (i & 0x70)));
    }
}
END_TEST

/* An unaligned write spanning three pages: pre-fix the whole 600
 * bytes go to flash_range_program() in one non-page-multiple call;
 * post-fix each page is RMW'd and the bytes outside the request are
 * preserved. */
START_TEST(test_unaligned_multi_page_write)
{
    int i;
    int ret;

    for (i = 0; i < SRAM_SZ; i++)
        g_sram[i] = (uint8_t)(0x30 + (i & 0x0F));

    ret = hal_flash_write((uint32_t)(XIP_BASE + 100), g_sram, 600);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_mem_eq(g_flash + 100, g_sram, 600);
    for (i = 0; i < 100; i++)
        ck_assert_uint_eq(g_flash[i], 0xFF);
    for (i = 700; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash[i], 0xFF);
}
END_TEST

/* Page aligned start and page multiple length: the fast path. Data
 * in RAM is programmed directly, unchanged by the fix. */
START_TEST(test_aligned_page_multiple_write_sram)
{
    int i;

    for (i = 0; i < 512; i++)
        g_sram[i] = (uint8_t)(0xC0 ^ (i & 0x1F));

    ck_assert_int_eq(hal_flash_write((uint32_t)XIP_BASE, g_sram, 512), 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_mem_eq(g_flash, g_sram, 512);
    for (i = 512; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash[i], 0xFF);
}
END_TEST

/* Fast path with the source in XIP flash: the data must be staged
 * to RAM before programming (XIP is disabled while a page is
 * programmed). */
START_TEST(test_aligned_page_multiple_write_xip)
{
    int i;

    for (i = 0; i < 512; i++)
        g_flash[2 * FLASH_PAGE_SIZE + i] = (uint8_t)(0x80 ^ (i & 0x3F));

    ck_assert_int_eq(hal_flash_write((uint32_t)XIP_BASE,
                                     (uint8_t *)(XIP_BASE +
                                                 2 * FLASH_PAGE_SIZE),
                                     512), 0);
    ck_assert_int_eq(g_violations, 0);
    for (i = 0; i < 512; i++)
        ck_assert_uint_eq(g_flash[i], (uint8_t)(0x80 ^ (i & 0x3F)));
}
END_TEST

/* Aligned start, non page-multiple length, source in XIP: pre-fix
 * the 300-byte tail is passed to flash_range_program() as-is;
 * post-fix the last partial page is RMW'd. Source (pages 0-1) and
 * destination (pages 2-3) do not overlap. */
START_TEST(test_xip_partial_write)
{
    int i;

    for (i = 0; i < 300; i++)
        g_flash[i] = (uint8_t)(0x40 + (i & 0x07));

    ck_assert_int_eq(hal_flash_write((uint32_t)(XIP_BASE + 2 * FLASH_PAGE_SIZE),
                                     (uint8_t *)XIP_BASE,
                                     300), 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_mem_eq(g_flash + 2 * FLASH_PAGE_SIZE, g_flash, 300);
    /* Source region untouched. */
    for (i = 0; i < 300; i++)
        ck_assert_uint_eq(g_flash[i], (uint8_t)(0x40 + (i & 0x07)));
    for (i = 300; i < 2 * FLASH_PAGE_SIZE; i++)
        ck_assert_uint_eq(g_flash[i], 0xFF);
    for (i = 2 * FLASH_PAGE_SIZE + 300; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash[i], 0xFF);
}
END_TEST

Suite *rp2350_flash_write_suite(void)
{
    Suite *s  = suite_create("rp2350 flash write");
    TCase *tc = tcase_create("page-contract");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_one_byte_trailer_write);
    tcase_add_test(tc, test_four_byte_magic_write);
    tcase_add_test(tc, test_unaligned_multi_page_write);
    tcase_add_test(tc, test_aligned_page_multiple_write_sram);
    tcase_add_test(tc, test_aligned_page_multiple_write_xip);
    tcase_add_test(tc, test_xip_partial_write);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = rp2350_flash_write_suite();
    SRunner *sr = srunner_create(s);

    g_flash = mmap((void *)XIP_BASE, FLASH_MEM_SZ,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (g_flash == MAP_FAILED)
        return 99;
    g_sram = mmap((void *)SRAM_ADDR, SRAM_SZ,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (g_sram == MAP_FAILED)
        return 99;

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    munmap(g_flash, FLASH_MEM_SZ);
    munmap(g_sram, SRAM_SZ);

    return fails;
}
