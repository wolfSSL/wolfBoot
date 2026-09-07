/* unit-stm32c0-write.c
 *
 * Regression test: hal_flash_write() in hal/stm32c0.c took the
 * double-word fast path on aligned requests and added 0x08000000 to
 * the destination, but the HAL contract is absolute addresses (the
 * erase path and every NVM caller pass 0x08000000-based addresses),
 * so fast-path writes targeted an address space past the flash while
 * the read-modify-write path targeted the right location. The RMW
 * path also located its unit from the request base (address & ~7)
 * indexed by i/4, which lands in the wrong 8-byte unit once the
 * request starts inside one.
 *
 * The fix programs through the absolute address: the fast path
 * copies the two words at (data + i) to the 8-byte unit at
 * (address + i), and the RMW path re-computes the unit from
 * (address + i) and rewrites it whole.
 *
 * The real functions are extracted by the Makefile and run with the
 * FLASH registers on a host register file, the destination flash
 * pre-filled with stale data, a second mapping standing in for the
 * wrong address space (flash base + 0x08000000) that the pre-fix
 * fast path writes into, and a canary after the source buffer.
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

/* Host stand-in for the ARM placement attribute. */
#define RAMFUNCTION

/* Host FLASH register file. */
static uint32_t g_flash_regs[0x40 / sizeof(uint32_t)];
#define FLASH_BASE ((uintptr_t)g_flash_regs)
#define FLASH_SR   (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_CR   (*(volatile uint32_t *)(FLASH_BASE + 0x14))
#define FLASH_SR_EOP      (1 << 0)
#define FLASH_SR_PROGERR  (1 << 3)
#define FLASH_SR_WRPERR   (1 << 4)
#define FLASH_SR_PGAERR   (1 << 5)
#define FLASH_SR_SIZERR   (1 << 6)
#define FLASH_SR_BSY1     (1 << 16)
#define FLASH_CR_PG       (1 << 0)

/* Flash base as defined in hal/stm32c0.c: the pre-fix fast path added
 * it to an already absolute address, so the extracted code only
 * compiles while that constant is visible here. */
#define FLASHMEM_ADDRESS_SPACE (0x08000000)

/* Destination flash: pre-filled with stale data (rewrite scenario).
 * hal_flash_write() takes the address as uint32_t (32-bit MCU), so
 * on the 64-bit host the flash must live at an address that fits in
 * 32 bits: map it at a fixed low location. */
#define FLASH_MEM_SZ 256
#define FLASH_MEM_ADDR 0x10000000UL
static uint8_t *g_flash_mem;

/* The address space the pre-fix fast path wrote into: flash base +
 * 0x08000000. Mapped so a pre-fix run fails the checks below instead
 * of faulting; it must stay untouched by a correct write. */
#define POISON_MEM_SZ 256
#define POISON_MEM_ADDR (FLASH_MEM_ADDR + 0x08000000UL)
static uint8_t *g_poison;

/* Source buffer, page aligned so the pointer is 8-aligned (the fast
 * path checks the data alignment); a canary follows the data: a
 * pre-fix short write reads the canary and lands it in the
 * destination flash. The canary range avoids the data bytes
 * (0x30..0x6F), the stale flash fill (0x12), the poison fill (0x5A)
 * and the erased value (0xFF). */
#define DATA_MAP_SZ 128
#define DATA_SZ 64
#define CANARY_SZ 32
static uint8_t *g_data_map;
static uint8_t *g_data;
#define g_canary (g_data + DATA_SZ)

/* The real functions from hal/stm32c0.c (extracted by the Makefile). */
#include "stm32c0_write_extract.h"

static void setup(void)
{
    int i;

    memset(g_flash_regs, 0, sizeof(g_flash_regs));
    for (i = 0; i < FLASH_MEM_SZ; i++)
        g_flash_mem[i] = 0x12; /* stale */
    for (i = 0; i < POISON_MEM_SZ; i++)
        g_poison[i] = 0x5A;
    g_data = g_data_map;
    for (i = 0; i < DATA_SZ; i++)
        g_data[i] = (uint8_t)(0x30 + i);
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

/* The wrong address space must never receive a byte. */
static int poison_untouched(void)
{
    int i;

    for (i = 0; i < POISON_MEM_SZ; i++)
        if (g_poison[i] != 0x5A)
            return 0;
    return 1;
}

/* An 8-aligned write of 64 bytes: all fast path. Pre-fix, every
 * double word lands in the wrong address space and the flash keeps
 * its stale content. */
START_TEST(test_write_64_aligned){
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 64), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 64), 0);
    for (i = 64; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(poison_untouched(), 1);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* An 8-aligned write of 60 bytes: fast path for the first 56 bytes,
 * read-modify-write for the 4-byte tail. */
START_TEST(test_write_60_tail)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)g_flash_mem,
        g_data, 60), 0);

    ck_assert_int_eq(memcmp(g_flash_mem, g_data, 60), 0);
    for (i = 60; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(poison_untouched(), 1);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A write starting 4 bytes into an 8-byte unit: the first unit is
 * partially programmed, then the request runs through the rest of
 * the flash. The bytes before the request keep their stale value and
 * nothing lands in the wrong address space. */
START_TEST(test_write_24_unaligned4)
{
    int i;

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)(g_flash_mem + 4),
        g_data, 24), 0);

    for (i = 0; i < 4; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(memcmp(g_flash_mem + 4, g_data, 24), 0);
    for (i = 28; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(poison_untouched(), 1);
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
    ck_assert_int_eq(poison_untouched(), 1);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

/* A write starting 5 bytes into an 8-byte unit with a source 5 bytes
 * into its own unit: the fast path fires mid-request with an index
 * that is not a multiple of 4, so the two copied words must be taken
 * from (data + i) and stored at the 8-byte unit at (address + i). */
START_TEST(test_write_24_unaligned5)
{
    int i;

    g_data = g_data_map + 5;
    for (i = 0; i < DATA_SZ; i++)
        g_data[i] = (uint8_t)(0x30 + i);
    for (i = 0; i < CANARY_SZ; i++)
        g_canary[i] = (uint8_t)(0x70 + i);

    ck_assert_int_eq(hal_flash_write((uint32_t)(uintptr_t)(g_flash_mem + 5),
        g_data, 24), 0);

    for (i = 0; i < 5; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(memcmp(g_flash_mem + 5, g_data, 24), 0);
    for (i = 29; i < FLASH_MEM_SZ; i++)
        ck_assert_uint_eq(g_flash_mem[i], 0x12);
    ck_assert_int_eq(poison_untouched(), 1);
    ck_assert_int_eq(canary_in_flash(), 0);
}
END_TEST

Suite *stm32c0_write_suite(void)
{
    Suite *s = suite_create("stm32c0-write");
    TCase *tc = tcase_create("stm32c0-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_64_aligned);
    tcase_add_test(tc, test_write_60_tail);
    tcase_add_test(tc, test_write_24_unaligned4);
    tcase_add_test(tc, test_write_3_unaligned4);
    tcase_add_test(tc, test_write_24_unaligned5);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = stm32c0_write_suite();
    SRunner *sr = srunner_create(s);

    g_flash_mem = mmap((void *)FLASH_MEM_ADDR, FLASH_MEM_SZ,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
        MAP_FIXED,
        -1, 0);
    if (g_flash_mem == MAP_FAILED)
        return 99;
    g_poison = mmap((void *)POISON_MEM_ADDR, POISON_MEM_SZ,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
        MAP_FIXED,
        -1, 0);
    if (g_poison == MAP_FAILED)
        return 99;
    g_data_map = mmap(NULL, DATA_MAP_SZ,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0);
    if (g_data_map == MAP_FAILED)
        return 99;

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
