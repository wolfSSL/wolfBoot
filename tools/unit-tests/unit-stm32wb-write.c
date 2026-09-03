/* unit-stm32wb-write.c
 *
 * Regression test for F-12063: the double-word fast path of
 * hal_flash_write() in hal/stm32wb.c was selected on "len - i > 3"
 * but always reads and programs two 32-bit words (eight bytes), so
 * an aligned 4-7 byte tail read up to four bytes past the caller's
 * buffer and programmed them into flash. The fix requires at least
 * eight remaining bytes before taking the fast path; shorter tails
 * fall to the RMW branch, which rewrites the unit with the
 * out-of-range bytes read back from flash.
 *
 * Same harness as the STM32G4 (F-11023) and STM32L4 (F-12062)
 * twins: extracted functions, registers on a host file, stale
 * destination flash, canary after the source. The source buffer is
 * 8-byte aligned so the fast-path alignment test on the data
 * pointer can pass. The WB HAL uses bare FLASH_SR/FLASH_CR macros
 * like the G4; the error clear is a write-1-to-clear store that
 * leaves no error visible on the host, modeling a program that
 * raises no errors.
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

/* Host FLASH register file (offsets as in the WB CMSIS header). The
 * WB flash controller has the same status/control layout as the G4
 * and L4. */
static uint32_t g_flash_regs[0x20 / sizeof(uint32_t)];
#define FLASH_BASE ((uintptr_t)g_flash_regs)
#define FLASH_SR   (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_CR   (*(volatile uint32_t *)(FLASH_BASE + 0x14))

#define FLASH_SR_EOP     (1u << 0)
#define FLASH_SR_PROGERR (1u << 3)
#define FLASH_SR_WRPERR  (1u << 4)
#define FLASH_SR_PGAERR  (1u << 5)
#define FLASH_SR_SIZERR  (1u << 6)
#define FLASH_SR_BSY     (1u << 16)
#define FLASH_SR_CFGBSY  (1u << 18)
#define FLASH_CR_PG      (1u << 0)
#define FLASH_CR_FSTPG   (1u << 4)

/* Destination flash: pre-filled with stale data (rewrite scenario).
 * hal_flash_write() takes the address as uint32_t (32-bit MCU), so
 * on the 64-bit host the flash must live at an address that fits in
 * 32 bits: map it at a fixed low location. */
#define FLASH_MEM_SZ 256
#define FLASH_MEM_ADDR 0x10000000UL
static uint8_t *g_flash_mem;

/* Source buffer followed by a canary: a pre-fix short write reads
 * the canary and lands it in the destination flash. */
#define DATA_SZ 64
#define CANARY_SZ 32
static uint8_t g_data[DATA_SZ + CANARY_SZ] __attribute__((aligned(8)));
#define g_canary (g_data + DATA_SZ)

/* The real functions from hal/stm32wb.c (extracted by the Makefile). */
#include "stm32wb_write_extract.h"

static void setup(void)
{
    int i;

    memset(g_flash_regs, 0, sizeof(g_flash_regs));
    for (i = 0; i < FLASH_MEM_SZ; i++)
        g_flash_mem[i] = 0x12; /* stale */
    for (i = 0; i < DATA_SZ; i++)
        g_data[i] = (uint8_t)(0x30 + i);
    /* 0x70..0x8F: distinct from the data bytes (0x30..0x6F), the
     * stale flash fill (0x12) and the erased-value padding (0xFF),
     * so a canary hit means source bytes past len were really read. */
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

/* A write of 60 bytes: seven full double words, then a 4-byte tail.
 * Pre-fix the tail took the fast path and programmed bytes 60..63
 * from source bytes past len. Post-fix the tail is RMW'd and nothing
 * past len is read or written. */
START_TEST(test_write_60_no_overread){
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
                                     g_data, 60), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 60), 0);
    for (i = 60; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A write of 63 bytes: the longest 4-7 byte tail (7). Pre-fix the
 * fast path over-reads data[60..63] and programs byte 63, which the
 * request does not cover. */
START_TEST(test_write_63_max_tail)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
                                     g_data, 63), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 63), 0);
    /* byte 63 keeps its flash content */
    ck_assert_uint_eq(g_flash_mem[63], 0x12);
    for (i = 64; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A 4-byte aligned write: the partition magic write shape. Pre-fix
 * the whole request took the fast path and programmed four canary
 * bytes after the magic. */
START_TEST(test_write_4_magic)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
                                     g_data, 4), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 4), 0);
    /* bytes 4..7 keep their flash content */
    for (i = 4; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A write of 56 bytes, a multiple of 8: the fast path is taken for
 * every unit and behaves exactly as before the fix. */
START_TEST(test_write_56_full_units)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
                                     g_data, 56), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 56), 0);
    for (i = 56; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
}
END_TEST

/* A write of 58 bytes: the final unit is partial (bytes 58,59 are
 * outside the request); they are read back from flash and rewritten
 * unchanged, and nothing past len is read. */
START_TEST(test_write_58_partial_word_padded)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
                                     g_data, 58), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 58), 0);
    /* word 14 (bytes 56..59): 58,59 keep their flash content */
    ck_assert_uint_eq(g_flash_mem[58], 0x12);
    ck_assert_uint_eq(g_flash_mem[59], 0x12);
    for (i = 60; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

Suite *stm32wb_write_suite(void)
{
    Suite *s = suite_create("stm32wb-write");
    TCase *tc = tcase_create("stm32wb-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_60_no_overread);
    tcase_add_test(tc, test_write_63_max_tail);
    tcase_add_test(tc, test_write_4_magic);
    tcase_add_test(tc, test_write_56_full_units);
    tcase_add_test(tc, test_write_58_partial_word_padded);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = stm32wb_write_suite();
    SRunner *sr = srunner_create(s);

    g_flash_mem = mmap((void *)FLASH_MEM_ADDR, FLASH_MEM_SZ,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
                       MAP_FIXED,
                       -1, 0);
    if (g_flash_mem == MAP_FAILED)
        return 99;

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    munmap(g_flash_mem, FLASH_MEM_SZ);

    return fails;
}
