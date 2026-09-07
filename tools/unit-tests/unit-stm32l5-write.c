/* unit-stm32l5-write.c
 *
 * Regression test: hal_flash_write() in hal/stm32l5.c read both words
 * of the 8-byte program unit regardless of the remaining length, so a
 * write not a multiple of 8 read up to 4 bytes past the caller's
 * buffer and programmed them. It also programmed through the caller's
 * address without aligning it down to the 8-byte program unit, so a
 * write starting inside a unit issued its two word stores in two
 * different units: the flash has no 32-bit program mode, so nothing
 * is programmed and the second store faults on alignment.
 *
 * The fix read-modify-writes the whole unit: the destination is
 * aligned down to the unit, the bytes outside the requested span come
 * from flash and go back unchanged, and both words are stored through
 * the aligned pointer. That is asserted here for unaligned starts.
 * The program-window invariant is enforced by the mock
 * hal_flash_wait_complete below: the real one only spins on
 * FLASH_SR_BSY (never set on the host register file), so the mock
 * diffs the flash against the previous window and asserts that the
 * changed bytes fit in one aligned 8-byte unit. A pre-fix HAL split
 * the two word stores across two units for an unaligned start, and
 * the 20-byte unaligned test goes red on it.
 *
 * Same harness as the STM32U5 twin: extracted functions, registers on
 * a host file, stale destination flash, canary after the source.
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

/* Host stand-ins for the ARM primitives and the TZ build selection. */
#define RAMFUNCTION
#define ISB() do { } while (0)
#define TZ_SECURE() (0)

/* Host FLASH register file (offsets as in hal/stm32l5.h, non-secure
 * variant). */
static uint32_t g_flash_regs[0x40 / sizeof(uint32_t)];
#define FLASH_BASE ((uintptr_t)g_flash_regs)
#define FLASH_SR   (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_CR   (*(volatile uint32_t *)(FLASH_BASE + 0x28))
#define FLASH_SR_EOP     (1 << 0)
#define FLASH_SR_OPERR   (1 << 1)
#define FLASH_SR_PROGERR (1 << 3)
#define FLASH_SR_WRPERR  (1 << 4)
#define FLASH_SR_PGAERR  (1 << 5)
#define FLASH_SR_SIZERR  (1 << 6)
#define FLASH_SR_PGSERR  (1 << 7)
#define FLASH_SR_OPTWERR (1 << 13)
#define FLASH_SR_BSY     (1 << 16)
#define FLASH_CR_PG      (1 << 0)

/* Destination flash: pre-filled with stale data (rewrite scenario).
 * hal_flash_write() takes the address as uint32_t (32-bit MCU), so on
 * the 64-bit host the flash must live at an address that fits in 32
 * bits: map it at a fixed low location. */
#define FLASH_MEM_SZ 256
#define FLASH_MEM_ADDR 0x10000000UL
static uint8_t *g_flash_mem;

/* Snapshot of the flash at the last program-window boundary; the
 * mock hal_flash_wait_complete() diffs against it. */
static uint8_t g_flash_prev[FLASH_MEM_SZ];

/* Source buffer followed by a canary: a pre-fix short write reads the
 * canary and lands it in the destination flash. */
#define DATA_SZ 64
#define CANARY_SZ 32
static uint8_t g_data[DATA_SZ + CANARY_SZ];
#define g_canary (g_data + DATA_SZ)

/* Mock hal_flash_wait_complete(): the real one (hal/stm32l5.c) only
 * spins on FLASH_SR_BSY, which the host register file never sets. This
 * one adds the program-window check the host model cannot see any
 * other way: the bytes changed since the previous window must fit
 * within one aligned 8-byte program unit. The pre-fix HAL issued its
 * two word stores relative to the caller address, so an unaligned
 * start split them across two units and this assertion goes red. */
static void hal_flash_wait_complete(uint8_t bank)
{
    int i;
    int first = -1;
    int last = -1;

    for (i = 0; i < FLASH_MEM_SZ; i++) {
        if (g_flash_mem[i] != g_flash_prev[i]) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first >= 0)
        ck_assert_int_le(last, (first & ~0x07) + 7);
    memcpy(g_flash_prev, g_flash_mem, FLASH_MEM_SZ);
}

/* The real functions from hal/stm32l5.c (extracted by the Makefile). */
#include "stm32l5_write_extract.h"

static void setup(void)
{
    int i;

    memset(g_flash_regs, 0, sizeof(g_flash_regs));
    for (i = 0; i < FLASH_MEM_SZ; i++)
        g_flash_mem[i] = 0x12; /* stale */
    memcpy(g_flash_prev, g_flash_mem, FLASH_MEM_SZ);
    for (i = 0; i < DATA_SZ; i++)
        g_data[i] = (uint8_t)(0x30 + i);
    /* 0x70..0x8F: distinct from the data bytes (0x30..0x6F), the stale
     * flash fill (0x12) and the erased-value padding (0xFF), so a
     * canary hit means source bytes past len were really read. */
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

/* A write of 60 bytes (not a multiple of 8): the last complete word
 * lands, the bytes past len keep their stale value, and no canary
 * byte is read or written. */
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

/* A write of 3 bytes: the whole 8-byte unit is programmed, but only
 * bytes 0..2 take the requested value; the rest is rewritten with
 * what flash already held. */
START_TEST(test_write_3_single_word_padded)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 3), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 3), 0);
    /* byte 3 and the whole second word are rewritten unchanged */
    ck_assert_uint_eq(g_flash_mem[3], 0x12);
    for (i = 4; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A write that is a multiple of 8 behaves exactly as before. */
START_TEST(test_write_64_full_units)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 64), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 64), 0);
    for (i = 64; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
}
END_TEST

/* A write of 20 bytes starting 4 bytes into an 8-byte unit: the
 * first unit is only half requested, the rest of it keeps its stale
 * value, and the request runs on through the following units. */
START_TEST(test_write_20_unaligned4)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)(g_flash_mem + 4),
        g_data, 20), 0);

    for (i = 0; i < 4; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(memcmp(g_flash_mem + 4, g_data, 20), 0);
    for (i = 24; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A 3-byte write starting 4 bytes into an 8-byte unit: one partial
 * unit, the rest of it rewritten unchanged. */
START_TEST(test_write_3_unaligned4)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)(g_flash_mem + 4),
        g_data, 3), 0);

    for (i = 0; i < 4; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(memcmp(g_flash_mem + 4, g_data, 3), 0);
    for (i = 7; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

Suite *stm32l5_write_suite(void)
{
    Suite *s = suite_create("stm32l5-write");
    TCase *tc = tcase_create("stm32l5-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_60_no_overread);
    tcase_add_test(tc, test_write_58_partial_word_padded);
    tcase_add_test(tc, test_write_3_single_word_padded);
    tcase_add_test(tc, test_write_64_full_units);
    tcase_add_test(tc, test_write_20_unaligned4);
    tcase_add_test(tc, test_write_3_unaligned4);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = stm32l5_write_suite();
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
