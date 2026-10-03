/* unit-stm32h7s-write.c
 *
 * Tests hal_flash_write() from hal/stm32h7s.c against the STM32H7RS
 * 128-bit (16-byte) flash quad-word.
 *
 * The device programs out of a 16-byte write buffer and computes ECC per
 * quad-word, so every program has to cover a whole quad-word: a request
 * that does not is completed by reading the remaining bytes back from
 * flash. Two things must hold for that to be safe, and both are the class
 * of bug the STM32G4/L4/WB/C0 twins were written for (F-11023, F-12062):
 * the aligned fast path must not be entered unless a full quad-word of
 * source data is actually available, or it over-reads the caller's buffer
 * and programs the extra bytes; and a short tail must preserve the flash
 * bytes outside the request rather than padding them.
 *
 * Same harness as those twins: extracted functions, registers on a host
 * file, stale destination flash, canary after the source.
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

/* Host FLASH register file (offsets as in hal/stm32h7s.h). */
static uint32_t g_flash_regs[0x40 / sizeof(uint32_t)];
#define FLASH_BASE_ADDR ((uintptr_t)g_flash_regs)
#define FLASH_CR   (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x10))
#define FLASH_SR   (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x14))
#define FLASH_ISR  (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x1C))
#define FLASH_ICR  (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x20))

#define FLASH_CR_PG         (1 << 1)

#define FLASH_SR_BSY        (1 << 0)
#define FLASH_SR_WBNE       (1 << 1)
#define FLASH_SR_QW         (1 << 2)

#define FLASH_ISR_EOPF      (1 << 16)
#define FLASH_ISR_WRPERRF   (1 << 17)
#define FLASH_ISR_PGSERRF   (1 << 18)
#define FLASH_ISR_STRBERRF  (1 << 19)
#define FLASH_ISR_OBLERRF   (1 << 20)
#define FLASH_ISR_INCERRF   (1 << 21)
#define FLASH_ISR_RDSERRF   (1 << 24)
#define FLASH_ISR_SNECCERRF (1 << 25)
#define FLASH_ISR_DBECCERRF (1 << 26)

#define FLASH_ISR_ERRORS    (FLASH_ISR_WRPERRF | FLASH_ISR_PGSERRF | \
                             FLASH_ISR_STRBERRF | FLASH_ISR_INCERRF | \
                             FLASH_ISR_SNECCERRF | FLASH_ISR_DBECCERRF)

#define STM32H7S_WORD_SIZE  (16)

/* Destination flash: pre-filled with stale data so a short tail that
 * wrongly pads rather than rewriting shows up. hal_flash_write() takes
 * the address as uint32_t, so on a 64-bit host the flash has to live at
 * an address that fits in 32 bits. */
#define FLASH_MEM_SZ   256
#define FLASH_MEM_ADDR 0x10000000UL
static uint8_t *g_flash_mem;

/* Source buffer followed by a canary: a fast path entered without a full
 * quad-word available reads the canary and programs it into flash. */
#define DATA_SZ   64
#define CANARY_SZ 32
static uint8_t g_data[DATA_SZ + CANARY_SZ] __attribute__((aligned(16)));
#define g_canary (g_data + DATA_SZ)

/* The real functions from hal/stm32h7s.c (extracted by the Makefile). */
#include "stm32h7s_write_extract.h"

static void setup(void)
{
    int i;

    memset(g_flash_regs, 0, sizeof(g_flash_regs));
    for (i = 0; i < FLASH_MEM_SZ; i++)
        g_flash_mem[i] = 0x12; /* stale */
    for (i = 0; i < DATA_SZ; i++)
        g_data[i] = (uint8_t)(0x30 + i);
    /* 0x70..0x8F: distinct from the data bytes (0x30..0x6F), the stale
     * flash fill (0x12) and the erased value (0xFF), so a canary hit
     * means source bytes past len were really read. */
    for (i = 0; i < CANARY_SZ; i++)
        g_canary[i] = (uint8_t)(0x70 + i);
}

static void teardown(void)
{
}

static int canary_in_flash(void)
{
    int i;

    for (i = 0; i < CANARY_SZ; i++)
        if (memchr(g_flash_mem, g_canary[i], FLASH_MEM_SZ) != NULL)
            return 1;
    return 0;
}

/* 48 bytes: exactly three quad-words, every one through the fast path. */
START_TEST(test_write_48_full_quadwords)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 48), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 48), 0);
    for (i = 48; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* Exactly one quad-word. The fast path is selected on "len - i >= 16",
 * so this is the boundary case: off by one in that test either skips the
 * fast path entirely or takes it with 15 bytes available. */
START_TEST(test_write_16_exact_quadword)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 16), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 16), 0);
    for (i = 16; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* 60 bytes: three full quad-words then a 12-byte tail. The tail must go
 * through the read-modify-write branch; taking the fast path would read
 * four bytes past len. */
START_TEST(test_write_60_no_overread)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 60), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 60), 0);
    /* bytes 60..63 complete the final quad-word and keep flash content */
    for (i = 60; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* Three bytes: one quad-word is programmed, but only bytes 0..2 take the
 * requested value and 3..15 are rewritten with what flash already held.
 * This is the shape of a partition trailer flag update. */
START_TEST(test_write_3_single_quadword_rmw)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 3), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 3), 0);
    for (i = 3; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* An unaligned destination must not take the fast path even with plenty
 * of source available: the first partial quad-word is read-modified and
 * the bytes below the start address keep their flash content. */
START_TEST(test_write_unaligned_dst)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)(g_flash_mem + 4),
        g_data, 40), 0);

    for (i = 0; i < 4; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(memcmp(g_flash_mem + 4, g_data, 40), 0);
    for (i = 44; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* PG must be set for the duration and cleared on the way out, or a later
 * read of the flash array returns bus errors on real silicon. */
START_TEST(test_pg_cleared_on_exit)
{
    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 32), 0);

    ck_assert_uint_eq(FLASH_CR & FLASH_CR_PG, 0);
}
END_TEST

/* Aligned destination but a misaligned source. The fast path requires both
 * to be quad-word aligned, so this must fall to the read-modify-write
 * branch; taking the fast path would store from a misaligned pointer. */
START_TEST(test_write_aligned_dst_unaligned_src)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data + 1, 32), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data + 1, 32), 0);
    for (i = 32; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A programming error latched in FLASH_ISR must be reported, not swallowed.
 * The device aborts the program itself; returning success would let wolfBoot
 * treat an unwritten image or trailer as written. */
START_TEST(test_write_reports_program_error)
{
    FLASH_ISR = FLASH_ISR_PGSERRF;

    ck_assert_int_lt(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 32), 0);
    /* and PG must still be cleared on the way out */
    ck_assert_uint_eq(FLASH_CR & FLASH_CR_PG, 0);
}
END_TEST

Suite *stm32h7s_write_suite(void)
{
    Suite *s = suite_create("stm32h7s-write");
    TCase *tc = tcase_create("stm32h7s-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_48_full_quadwords);
    tcase_add_test(tc, test_write_16_exact_quadword);
    tcase_add_test(tc, test_write_60_no_overread);
    tcase_add_test(tc, test_write_3_single_quadword_rmw);
    tcase_add_test(tc, test_write_unaligned_dst);
    tcase_add_test(tc, test_pg_cleared_on_exit);
    tcase_add_test(tc, test_write_aligned_dst_unaligned_src);
    tcase_add_test(tc, test_write_reports_program_error);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = stm32h7s_write_suite();
    SRunner *sr = srunner_create(s);

    g_flash_mem = mmap((void *)FLASH_MEM_ADDR, FLASH_MEM_SZ,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1, 0);
    if (g_flash_mem == MAP_FAILED)
        return 99;

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    munmap(g_flash_mem, FLASH_MEM_SZ);

    return fails;
}
