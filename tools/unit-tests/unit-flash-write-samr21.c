/* unit-flash-write-samr21.c
 *
 * Regression test for F-5964: in the byte-wise (unaligned) path of
 * hal_flash_write() (hal/samr21.c, hal/same51.c), the in-word offset was
 * computed every loop iteration as
 *     uint32_t off = (address % 4);
 * using the original (call-time) "address", never the current position
 * "address + i". The word index "dst_idx = (i + off) >> 2" does advance
 * with "i", but the byte-fill loop
 *     while (off < 4) { ... vbytes[off++] = data[i++]; ... }
 * starts filling at the stale "off" instead of "(address + i) % 4". Once
 * the first partial word has been written, if the destination address and
 * the source buffer have different alignment mod 4 (so the fast 32-bit
 * path never re-syncs), every subsequent word is filled starting at the
 * wrong byte position: some destination bytes are left unwritten and
 * others get the wrong source byte.
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

/* hal/samr21.c's hal_init() contains an ARMv6-M-only "cpsid i" instruction
 * that the host assembler rejects; this test never calls hal_init(), so
 * exclude just that instruction (not otherwise used by hal_flash_write()). */
#define WOLFBOOT_UNIT_TEST_FLASH_WRITE

#include "image.h"

#include "../../hal/samr21.c"

/* hal_flash_write() pokes the NVMCTRL command register (NVMCTRLA_REG, fixed
 * at NVMCTRL_BASE == 0x41004000) directly at entry/exit. Map that page so
 * those writes don't fault; its contents are otherwise irrelevant here. */
static void map_nvmctrl(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#else
    flags |= MAP_FIXED;
#endif
    void *p = mmap((void *)(uintptr_t)NVMCTRL_BASE, 4096,
            PROT_READ | PROT_WRITE, flags, -1, 0);
    ck_assert_ptr_eq(p, (void *)(uintptr_t)NVMCTRL_BASE);
}

static void unmap_nvmctrl(void)
{
    munmap((void *)(uintptr_t)NVMCTRL_BASE, 4096);
}

/* "address" is treated as a real pointer into memory-mapped flash. Keep the
 * mock flash buffer inside the 32-bit range, matching how "address" (a
 * uint32_t) is used by the real target. */
#define MOCK_FLASH_SIZE 64
static uint8_t *mock_flash;

static void setup(void)
{
    map_nvmctrl();
    mock_flash = mmap(NULL, MOCK_FLASH_SIZE, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ck_assert_ptr_ne(mock_flash, MAP_FAILED);
    memset(mock_flash, 0xFF, MOCK_FLASH_SIZE);
}

static void teardown(void)
{
    munmap(mock_flash, MOCK_FLASH_SIZE);
    unmap_nvmctrl();
}

/* Destination misaligned by 1 (mod 4), source buffer misaligned by 2 (mod
 * 4): the two never share the same alignment, so after the first partial
 * word the fast 32-bit path can never re-sync and every remaining word
 * goes through the buggy byte-wise path. Before the fix, this drops and
 * misplaces bytes past the first word. */
START_TEST(test_unaligned_write_mismatched_alignment)
{
    uint8_t rawbuf[64];
    uint8_t *data = rawbuf;
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;
    int i;

    while (((uintptr_t)data % 4) != 2)
        data++;
    for (i = 0; i < 12; i++)
        data[i] = (uint8_t)(0xA0 + i);

    ck_assert_int_eq(hal_flash_write(base + 5, data, 8), 0);

    for (i = 0; i < 5; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
    for (i = 0; i < 8; i++)
        ck_assert_uint_eq(mock_flash[5 + i], data[i]);
    for (i = 13; i < MOCK_FLASH_SIZE; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
}
END_TEST

/* A write that fits entirely inside a single flash word must still work:
 * buggy and fixed forms agree here (the fill loop runs to completion on
 * the first iteration), guarding against a fix that breaks the common
 * case. */
START_TEST(test_unaligned_write_single_word)
{
    uint8_t data[3];
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;
    int i;

    for (i = 0; i < 3; i++)
        data[i] = (uint8_t)(0xB0 + i);

    ck_assert_int_eq(hal_flash_write(base + 1, data, 3), 0);

    ck_assert_uint_eq(mock_flash[0], 0xFF);
    for (i = 0; i < 3; i++)
        ck_assert_uint_eq(mock_flash[1 + i], data[i]);
    for (i = 4; i < MOCK_FLASH_SIZE; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
}
END_TEST

Suite *flash_write_suite(void)
{
    Suite *s = suite_create("flash-write-samr21");
    TCase *tc = tcase_create("flash-write-samr21");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_unaligned_write_mismatched_alignment);
    tcase_add_test(tc, test_unaligned_write_single_word);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = flash_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
