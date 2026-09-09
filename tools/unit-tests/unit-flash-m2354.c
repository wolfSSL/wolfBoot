/* unit-flash-m2354.c
 *
 * Unit tests for the FMC ISP flash driver in hal/m2354.c: the read-modify-
 * write merge and multi-word fast path in hal_flash_write(), the page stride
 * and blank-page skip in hal_flash_erase(), the APROM bounds and wrap guards,
 * and masking of the non-secure alias before an address reaches the engine.
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
#include <stdlib.h>

/* Compile only the flash logic out of hal/m2354.c. */
#define WOLFBOOT_UNIT_TEST_FLASH

/* RAMFUNCTION must be empty on the host */
#define RAMFUNCTION

/* Model the whole 1 MB APROM so the tests use real device addresses. */
#define MOCK_APROM_SIZE (0x100000UL)
static uint8_t mock_aprom[MOCK_APROM_SIZE];

/* Flash at or above this offset is attributed non-secure. Tests that do not
 * exercise TrustZone leave it at the top of APROM. */
static uint32_t mock_nscba;

/* Record of every ISP command hal_flash_* issued */
#define ISP_LOG_MAX 2048
static uint32_t isp_cmd_log[ISP_LOG_MAX];
static uint32_t isp_addr_log[ISP_LOG_MAX];
static int isp_log_n;
static int isp_fail_at; /* -1 = never */

static uint32_t mock_read32(uint32_t addr);
#define FLASH_READ32(a) mock_read32((uint32_t)(a))

/* Applies flash semantics: a program only clears bits, an erase sets 0xFF. */
static int fmc_isp_run(uint32_t cmd, uint32_t addr, uint32_t data);

/* Multi-word program: one command per aligned 16-byte block. */
#define MULTI_LOG_MAX 512
static uint32_t multi_addr_log[MULTI_LOG_MAX];
static int multi_log_n;
static int fmc_isp_program_multi(uint32_t addr, const uint32_t *w);

#include "../../hal/m2354.c"

static uint32_t mock_read32(uint32_t addr)
{
    uint32_t v;
    uint32_t phys = addr & ~NS_OFFSET;

    /* Bound the whole access, not just its start. */
    ck_assert_uint_le((uint64_t)phys + sizeof(v), MOCK_APROM_SIZE);

    /* A secure-alias read of flash the hardware has attributed non-secure
     * reads as zero (M2354 TRM Rev 1.01, 6.4.4.2), so the driver must keep
     * the caller's alias on every mapped read. */
    if ((addr & NS_OFFSET) == 0 && phys >= mock_nscba)
        return 0x00000000UL;

    memcpy(&v, &mock_aprom[phys], sizeof(v));
    return v;
}

static int fmc_isp_run(uint32_t cmd, uint32_t addr, uint32_t data)
{
    uint32_t cur;

    if (isp_log_n < ISP_LOG_MAX) {
        isp_cmd_log[isp_log_n] = cmd;
        isp_addr_log[isp_log_n] = addr;
    }
    isp_log_n++;

    if (isp_fail_at >= 0 && isp_log_n > isp_fail_at)
        return -1;

    /* The driver must have masked the alias bit off already. */
    if (cmd == FMC_ISPCMD_PROGRAM) {
        ck_assert_uint_eq(addr & 0x3, 0);
        ck_assert_uint_le((uint64_t)addr + sizeof(cur), MOCK_APROM_SIZE);
        memcpy(&cur, &mock_aprom[addr], sizeof(cur));
        cur &= data; /* programming only clears bits */
        memcpy(&mock_aprom[addr], &cur, sizeof(cur));
    }
    else if (cmd == FMC_ISPCMD_PAGE_ERASE) {
        ck_assert_uint_eq(addr & (FLASH_PAGE_SIZE - 1), 0);
        ck_assert_uint_le((uint64_t)addr + FLASH_PAGE_SIZE, MOCK_APROM_SIZE);
        memset(&mock_aprom[addr], 0xFF, FLASH_PAGE_SIZE);
    }
    return 0;
}

static int fmc_isp_program_multi(uint32_t addr, const uint32_t *w)
{
    uint32_t cur[4];
    int k;

    if (multi_log_n < MULTI_LOG_MAX)
        multi_addr_log[multi_log_n] = addr;
    multi_log_n++;

    if (isp_fail_at >= 0 && (isp_log_n + multi_log_n) > isp_fail_at)
        return -1;

    /* 16-byte aligned, alias bit already masked off. */
    ck_assert_uint_eq(addr & 0xF, 0);
    ck_assert_uint_le((uint64_t)addr + sizeof(cur), MOCK_APROM_SIZE);

    memcpy(cur, &mock_aprom[addr], sizeof(cur));
    for (k = 0; k < 4; k++)
        cur[k] &= w[k]; /* programming only clears bits */
    memcpy(&mock_aprom[addr], cur, sizeof(cur));
    return 0;
}

static void reset_mocks(void)
{
    mock_nscba = MOCK_APROM_SIZE;
    multi_log_n = 0;
    memset(mock_aprom, 0xFF, sizeof(mock_aprom));
    isp_log_n = 0;
    isp_fail_at = -1;
}

/* --- hal_flash_write ---------------------------------------------------- */

START_TEST(test_write_word_aligned)
{
    const uint8_t d[4] = { 0x11, 0x22, 0x33, 0x44 };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_int_eq(memcmp(&mock_aprom[0x10000], d, 4), 0);
}
END_TEST

/* A write starting mid-word must preserve the bytes below it. */
START_TEST(test_write_unaligned_head_preserves_leading_bytes)
{
    const uint8_t d[2] = { 0xAA, 0xBB };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10001, d, sizeof(d)), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);       /* containing word */
    ck_assert_uint_eq(mock_aprom[0x10000], 0xFF);      /* untouched */
    ck_assert_uint_eq(mock_aprom[0x10001], 0xAA);
    ck_assert_uint_eq(mock_aprom[0x10002], 0xBB);
    ck_assert_uint_eq(mock_aprom[0x10003], 0xFF);      /* untouched */
}
END_TEST

/* A length that is not a whole number of words must not clobber the tail. */
START_TEST(test_write_unaligned_tail_preserves_trailing_bytes)
{
    const uint8_t d[5] = { 1, 2, 3, 4, 5 };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), 0);
    ck_assert_int_eq(isp_log_n, 2);                    /* two words touched */
    ck_assert_int_eq(memcmp(&mock_aprom[0x10000], d, 5), 0);
    ck_assert_uint_eq(mock_aprom[0x10005], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x10007], 0xFF);
}
END_TEST

/* A single byte in the middle of a word: the hardest case for the merge. */
START_TEST(test_write_single_byte_mid_word)
{
    const uint8_t d = 0x5A;
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10002, &d, 1), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(mock_aprom[0x10000], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x10001], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x10002], 0x5A);
    ck_assert_uint_eq(mock_aprom[0x10003], 0xFF);
}
END_TEST

/* Straddling three words, starting and ending unaligned. */
START_TEST(test_write_spans_words_unaligned_both_ends)
{
    uint8_t d[9];
    int i;
    for (i = 0; i < 9; i++)
        d[i] = (uint8_t)(0xA0 + i);
    reset_mocks();
    /* Three words: 0x10000 contributes its top byte, then two whole ones.
     * The end is exclusive, so 0x1000C is untouched. */
    ck_assert_int_eq(hal_flash_write(0x10003, d, sizeof(d)), 0);
    ck_assert_int_eq(isp_log_n, 3);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(isp_addr_log[1], 0x10004);
    ck_assert_uint_eq(isp_addr_log[2], 0x10008);
    ck_assert_int_eq(memcmp(&mock_aprom[0x10003], d, 9), 0);
    ck_assert_uint_eq(mock_aprom[0x10002], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x1000C], 0xFF);
}
END_TEST

/* The non-secure alias must be masked off before the ISP engine sees it. */
START_TEST(test_write_masks_nonsecure_alias)
{
    const uint8_t d[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10080000, d, sizeof(d)), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x80000); /* physical, not aliased */
    ck_assert_int_eq(memcmp(&mock_aprom[0x80000], d, 4), 0);
}
END_TEST

START_TEST(test_write_zero_length_is_a_noop)
{
    const uint8_t d = 0;
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, &d, 0), 0);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* A negative length is a caller bug, not a no-op: it must not report success. */
START_TEST(test_write_rejects_negative_length)
{
    const uint8_t d[4] = { 1, 2, 3, 4 };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, d, -1), -1);
    ck_assert_int_eq(isp_log_n, 0);
    ck_assert_int_eq(multi_log_n, 0);
}
END_TEST

START_TEST(test_erase_rejects_negative_length)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10000, -1), -1);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

START_TEST(test_write_rejects_null_data)
{
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, NULL, 4), -1);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* Fail-secure: nothing may be programmed past the end of APROM. */
START_TEST(test_write_rejects_past_aprom_end)
{
    const uint8_t d[8] = { 0 };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(FLASH_APROM_END - 4, d, 8), -1);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* Fail-secure: a length that wraps the address space must be rejected. */
START_TEST(test_write_rejects_length_overflow)
{
    const uint8_t d[4] = { 0 };
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0xFFF00000, d, (int)0x7FFFFFFF), -1);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* An ISP failure part way through must propagate, not be swallowed. */
START_TEST(test_write_propagates_isp_failure)
{
    const uint8_t d[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    reset_mocks();
    isp_fail_at = 1; /* first command succeeds, second fails */
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), -1);
    ck_assert_int_eq(isp_log_n, 2);
}
END_TEST

/* One multi-word command, not four single-word ones. */
/* The multi-word path moves the bulk of an update, so its error return has
 * to propagate, not be swallowed. */
START_TEST(test_write_propagates_multi_word_failure)
{
    uint8_t d[32];
    int i;
    for (i = 0; i < 32; i++)
        d[i] = (uint8_t)i;
    reset_mocks();
    isp_fail_at = 1; /* first block programs, second fails */
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), -1);
    ck_assert_int_eq(multi_log_n, 2);
}
END_TEST

/* A failure on the very first aligned block must also propagate. */
START_TEST(test_write_propagates_multi_word_failure_first_block)
{
    uint8_t d[16];
    memset(d, 0x5A, sizeof(d));
    reset_mocks();
    isp_fail_at = 0;
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), -1);
    ck_assert_int_eq(multi_log_n, 1);
}
END_TEST

START_TEST(test_write_aligned_block_uses_one_multi_command)
{
    uint8_t d[16];
    int i;
    for (i = 0; i < 16; i++)
        d[i] = (uint8_t)(0x10 + i);
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), 0);
    ck_assert_int_eq(multi_log_n, 1);
    ck_assert_int_eq(isp_log_n, 0);
    ck_assert_uint_eq(multi_addr_log[0], 0x10000);
    ck_assert_int_eq(memcmp(&mock_aprom[0x10000], d, 16), 0);
}
END_TEST

/* A full 2 KB page: 128 multi-word commands rather than 512 single-word ones. */
START_TEST(test_write_full_page_uses_multi_throughout)
{
    static uint8_t d[2048];
    int i;
    for (i = 0; i < 2048; i++)
        d[i] = (uint8_t)(i & 0xFF);
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), 0);
    ck_assert_int_eq(multi_log_n, 2048 / 16);
    ck_assert_int_eq(isp_log_n, 0);
    ck_assert_int_eq(memcmp(&mock_aprom[0x10000], d, 2048), 0);
}
END_TEST

/* Crawl to the block boundary, then switch to multi-word. */
START_TEST(test_write_unaligned_head_then_multi)
{
    static uint8_t d[45];
    int i;
    for (i = 0; i < 45; i++)
        d[i] = (uint8_t)(0xC0 + i);
    reset_mocks();
    /* 0x10004..0x10030: 12 bytes to reach 0x10010, then 32 aligned, then 1 */
    ck_assert_int_eq(hal_flash_write(0x10004, d, sizeof(d)), 0);
    ck_assert_int_eq(multi_log_n, 2);
    ck_assert_uint_eq(multi_addr_log[0], 0x10010);
    ck_assert_uint_eq(multi_addr_log[1], 0x10020);
    ck_assert_int_eq(memcmp(&mock_aprom[0x10004], d, 45), 0);
    ck_assert_uint_eq(mock_aprom[0x10003], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x10031], 0xFF);
}
END_TEST

/* A run one byte short of a block must stay on the merging path. */
START_TEST(test_write_fifteen_bytes_stays_single_word)
{
    uint8_t d[15];
    int i;
    for (i = 0; i < 15; i++)
        d[i] = (uint8_t)(0x40 + i);
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10000, d, sizeof(d)), 0);
    ck_assert_int_eq(multi_log_n, 0);
    ck_assert_int_eq(memcmp(&mock_aprom[0x10000], d, 15), 0);
    ck_assert_uint_eq(mock_aprom[0x1000F], 0xFF);
}
END_TEST

START_TEST(test_write_multi_masks_nonsecure_alias)
{
    uint8_t d[16];
    memset(d, 0x5A, sizeof(d));
    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x10080000, d, sizeof(d)), 0);
    ck_assert_int_eq(multi_log_n, 1);
    ck_assert_uint_eq(multi_addr_log[0], 0x80000);
    ck_assert_int_eq(memcmp(&mock_aprom[0x80000], d, 16), 0);
}
END_TEST

/* --- hal_flash_erase ---------------------------------------------------- */

/* A blank page must not be erased: the skip dominates update time. */
START_TEST(test_erase_skips_already_blank_pages)
{
    reset_mocks();  /* mock_aprom is all 0xFF */
    ck_assert_int_eq(hal_flash_erase(0x10000, 8 * FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* A single dirty byte anywhere in the page must still force the erase. */
START_TEST(test_erase_one_dirty_byte_still_erases)
{
    reset_mocks();
    mock_aprom[0x10000 + FLASH_PAGE_SIZE - 1] = 0xFE;
    ck_assert_int_eq(hal_flash_erase(0x10000, FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(mock_aprom[0x10000 + FLASH_PAGE_SIZE - 1], 0xFF);
}
END_TEST

/* Mixed run: only the dirty pages cost an erase. */
START_TEST(test_erase_mixed_blank_and_dirty)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, FLASH_PAGE_SIZE);          /* dirty */
    memset(&mock_aprom[0x10000 + 2 * FLASH_PAGE_SIZE], 0x00, FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10000, 4 * FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 2);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(isp_addr_log[1], 0x10000 + 2 * FLASH_PAGE_SIZE);
}
END_TEST

START_TEST(test_erase_single_page)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10000, FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(mock_aprom[0x10000], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x10000 + FLASH_PAGE_SIZE - 1], 0xFF);
}
END_TEST

/* The range is end-exclusive: exactly two pages take exactly two commands. */
START_TEST(test_erase_two_pages_exact)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, 2 * FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10000, 2 * FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 2);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(isp_addr_log[1], 0x10000 + FLASH_PAGE_SIZE);
}
END_TEST

/* One byte past a page boundary must pull in the next page, not stop short. */
START_TEST(test_erase_one_byte_into_second_page)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, 2 * FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10000, FLASH_PAGE_SIZE + 1), 0);
    ck_assert_int_eq(isp_log_n, 2);
    ck_assert_uint_eq(isp_addr_log[1], 0x10000 + FLASH_PAGE_SIZE);
}
END_TEST

/* A start inside a page must round down to the containing page. */
START_TEST(test_erase_unaligned_start_rounds_down)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10001, 4), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
}
END_TEST

/* Rounding down must not silently drop the last page of the range. */
START_TEST(test_erase_unaligned_start_spanning_boundary)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, 2 * FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x10800 - 1, 2), 0);
    ck_assert_int_eq(isp_log_n, 2);
    ck_assert_uint_eq(isp_addr_log[0], 0x10000);
    ck_assert_uint_eq(isp_addr_log[1], 0x10800);
}
END_TEST

START_TEST(test_erase_masks_nonsecure_alias)
{
    reset_mocks();
    memset(&mock_aprom[0xFE000], 0x00, FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(0x100FE000, FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0xFE000);
}
END_TEST

START_TEST(test_erase_zero_length_is_a_noop)
{
    reset_mocks();
    ck_assert_int_eq(hal_flash_erase(0x10000, 0), 0);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

START_TEST(test_erase_rejects_past_aprom_end)
{
    reset_mocks();
    ck_assert_int_eq(hal_flash_erase(FLASH_APROM_END - FLASH_PAGE_SIZE,
                                     2 * FLASH_PAGE_SIZE), -1);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

START_TEST(test_erase_rejects_length_overflow)
{
    reset_mocks();
    ck_assert_int_eq(hal_flash_erase(0xFFF00000, (int)0x7FFFFFFF), -1);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* The last page of APROM must be erasable: an off-by-one in the bound would
 * reject it and make the top of flash unusable. */
START_TEST(test_erase_last_page_is_allowed)
{
    reset_mocks();
    memset(&mock_aprom[FLASH_APROM_END - FLASH_PAGE_SIZE], 0x00, FLASH_PAGE_SIZE);
    ck_assert_int_eq(hal_flash_erase(FLASH_APROM_END - FLASH_PAGE_SIZE,
                                     FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], FLASH_APROM_END - FLASH_PAGE_SIZE);
}
END_TEST

START_TEST(test_erase_propagates_isp_failure)
{
    reset_mocks();
    memset(&mock_aprom[0x10000], 0x00, 3 * FLASH_PAGE_SIZE);
    isp_fail_at = 1;
    ck_assert_int_eq(hal_flash_erase(0x10000, 3 * FLASH_PAGE_SIZE), -1);
    ck_assert_int_eq(isp_log_n, 2);
}
END_TEST

/* --- TrustZone alias ----------------------------------------------------- */

/* Under m2354-tz.config both partitions live at the +0x10000000 alias and the
 * flash holding them is attributed non-secure. The read-modify-write merge
 * must read through the caller's alias: through the secure one the word reads
 * as zero and the bytes outside the requested range get programmed to 0x00,
 * which no later write can undo. */
START_TEST(test_write_partial_word_reads_through_nonsecure_alias)
{
    uint8_t d = 0x5A;

    reset_mocks();
    mock_nscba = 0x80000;
    ck_assert_int_eq(hal_flash_write(0x10080001, &d, 1), 0);
    ck_assert_uint_eq(mock_aprom[0x80000], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x80001], 0x5A);
    ck_assert_uint_eq(mock_aprom[0x80002], 0xFF);
    ck_assert_uint_eq(mock_aprom[0x80003], 0xFF);
}
END_TEST

/* Same merge, spanning three words in the update partition. */
START_TEST(test_write_spans_words_in_nonsecure_flash)
{
    const uint8_t d[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    reset_mocks();
    mock_nscba = 0x80000;
    ck_assert_int_eq(hal_flash_write(0x100BF003, d, sizeof(d)), 0);
    ck_assert_int_eq(memcmp(&mock_aprom[0xBF003], d, sizeof(d)), 0);
    ck_assert_uint_eq(mock_aprom[0xBF002], 0xFF);
    ck_assert_uint_eq(mock_aprom[0xBF00C], 0xFF);
}
END_TEST

/* The blank-page skip has to survive the alias too, or every page of a
 * non-secure partition is erased on every update. */
START_TEST(test_erase_blank_nonsecure_page_is_skipped)
{
    reset_mocks();
    mock_nscba = 0x80000;
    ck_assert_int_eq(hal_flash_erase(0x100BF000, FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 0);
}
END_TEST

/* ... while the engine still gets the physical address. */
START_TEST(test_erase_dirty_nonsecure_page_uses_physical_address)
{
    reset_mocks();
    mock_nscba = 0x80000;
    mock_aprom[0xBF000 + FLASH_PAGE_SIZE - 1] = 0x00;
    ck_assert_int_eq(hal_flash_erase(0x100BF000, FLASH_PAGE_SIZE), 0);
    ck_assert_int_eq(isp_log_n, 1);
    ck_assert_uint_eq(isp_addr_log[0], 0xBF000);
}
END_TEST

static Suite *m2354_flash_suite(void)
{
    Suite *s = suite_create("m2354 flash");
    TCase *tw = tcase_create("hal_flash_write");
    TCase *te = tcase_create("hal_flash_erase");
    TCase *tz = tcase_create("trustzone alias");

    tcase_add_test(tw, test_write_word_aligned);
    tcase_add_test(tw, test_write_unaligned_head_preserves_leading_bytes);
    tcase_add_test(tw, test_write_unaligned_tail_preserves_trailing_bytes);
    tcase_add_test(tw, test_write_single_byte_mid_word);
    tcase_add_test(tw, test_write_spans_words_unaligned_both_ends);
    tcase_add_test(tw, test_write_masks_nonsecure_alias);
    tcase_add_test(tw, test_write_zero_length_is_a_noop);
    tcase_add_test(tw, test_write_rejects_null_data);
    tcase_add_test(tw, test_write_rejects_negative_length);
    tcase_add_test(tw, test_write_rejects_past_aprom_end);
    tcase_add_test(tw, test_write_rejects_length_overflow);
    tcase_add_test(tw, test_write_propagates_isp_failure);
    tcase_add_test(tw, test_write_propagates_multi_word_failure);
    tcase_add_test(tw, test_write_propagates_multi_word_failure_first_block);
    tcase_add_test(tw, test_write_aligned_block_uses_one_multi_command);
    tcase_add_test(tw, test_write_full_page_uses_multi_throughout);
    tcase_add_test(tw, test_write_unaligned_head_then_multi);
    tcase_add_test(tw, test_write_fifteen_bytes_stays_single_word);
    tcase_add_test(tw, test_write_multi_masks_nonsecure_alias);
    suite_add_tcase(s, tw);

    tcase_add_test(te, test_erase_single_page);
    tcase_add_test(te, test_erase_two_pages_exact);
    tcase_add_test(te, test_erase_one_byte_into_second_page);
    tcase_add_test(te, test_erase_unaligned_start_rounds_down);
    tcase_add_test(te, test_erase_unaligned_start_spanning_boundary);
    tcase_add_test(te, test_erase_masks_nonsecure_alias);
    tcase_add_test(te, test_erase_zero_length_is_a_noop);
    tcase_add_test(te, test_erase_rejects_past_aprom_end);
    tcase_add_test(te, test_erase_rejects_length_overflow);
    tcase_add_test(te, test_erase_rejects_negative_length);
    tcase_add_test(te, test_erase_last_page_is_allowed);
    tcase_add_test(te, test_erase_propagates_isp_failure);
    tcase_add_test(te, test_erase_skips_already_blank_pages);
    tcase_add_test(te, test_erase_one_dirty_byte_still_erases);
    tcase_add_test(te, test_erase_mixed_blank_and_dirty);
    suite_add_tcase(s, te);

    tcase_add_test(tz, test_write_partial_word_reads_through_nonsecure_alias);
    tcase_add_test(tz, test_write_spans_words_in_nonsecure_flash);
    tcase_add_test(tz, test_erase_blank_nonsecure_page_is_skipped);
    tcase_add_test(tz, test_erase_dirty_nonsecure_page_uses_physical_address);
    suite_add_tcase(s, tz);
    return s;
}

int main(void)
{
    int failed;
    SRunner *sr = srunner_create(m2354_flash_suite());
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
