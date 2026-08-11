/* unit-flash-write-nrf52.c
 *
 * Regression test for F-6757: in the byte-wise (partial word) path of
 * hal_flash_write() (hal/nrf52.c, hal/nrf5340.c, hal/stm32l0.c) the base of
 * the containing word was derived from the original (call-time) "address"
 * instead of the current position "address + i":
 *     int off = (address + i) - (((address + i) >> 2) << 2);
 *     dst = (uint32_t *)(address - off);
 *     val = dst[i >> 2];
 * "dst[i >> 2]" then addresses physical byte "address - off + (i & ~3)"
 * rather than the intended word containing "address + i". For every
 * iteration with "i" not a multiple of 4 the wrong destination byte is
 * modified (and, when off != 0, through a misaligned 32-bit flash access,
 * which faults outright on the Cortex-M0+ of stm32l0). This is the same
 * defect already fixed for hal/samr21.c and hal/same51.c under F-5964.
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

#include "image.h"

#include "../../hal/nrf52.c"

/* hal_flash_write() polls NVMC_READY and pokes NVMC_CONFIG directly (both
 * within one page of the fixed NVMC_BASE); map that page and leave READY
 * asserted so the flash state machine appears idle. */
static void map_nvmc(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#else
    flags |= MAP_FIXED;
#endif
    void *p = mmap((void *)(uintptr_t)NVMC_BASE, 4096,
            PROT_READ | PROT_WRITE, flags, -1, 0);
    ck_assert_ptr_eq(p, (void *)(uintptr_t)NVMC_BASE);
    NVMC_READY = 1;
}

static void unmap_nvmc(void)
{
    munmap((void *)(uintptr_t)NVMC_BASE, 4096);
}

/* "address" is treated as a real pointer into memory-mapped flash. Keep the
 * mock flash buffer inside the 32-bit range, matching how "address" (a
 * uint32_t) is used by the real target. The buffer sits in the middle of a
 * larger mapping so that the out-of-word accesses made by the buggy code
 * report as byte mismatches instead of killing the test with SIGSEGV. */
#define MOCK_FLASH_SIZE 64
#define MOCK_MAP_SIZE   4096
#define MOCK_MAP_OFFSET 128
static uint8_t *mock_map;
static uint8_t *mock_flash;

static void setup(void)
{
    map_nvmc();
    mock_map = mmap(NULL, MOCK_MAP_SIZE, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ck_assert_ptr_ne(mock_map, MAP_FAILED);
    memset(mock_map, 0xFF, MOCK_MAP_SIZE);
    mock_flash = mock_map + MOCK_MAP_OFFSET;
}

static void teardown(void)
{
    munmap(mock_map, MOCK_MAP_SIZE);
    unmap_nvmc();
}

/* Word-aligned destination and source, length not a multiple of 4: the fast
 * 32-bit path copies the first word, then the 2-byte tail goes through the
 * byte-wise path. Before the fix the second tail byte (i = 5) lands on
 * physical byte "address + 4", overwriting the first tail byte and leaving
 * "address + 5" erased. */
START_TEST(test_aligned_write_unaligned_tail)
{
    uint8_t data[6] __attribute__((aligned(4)));
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;
    int i;

    for (i = 0; i < 6; i++)
        data[i] = (uint8_t)(0xA0 + i);

    ck_assert_int_eq(hal_flash_write(base, data, 6), 0);

    for (i = 0; i < 6; i++)
        ck_assert_uint_eq(mock_flash[i], data[i]);
    for (i = 6; i < MOCK_FLASH_SIZE; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
}
END_TEST

/* Destination misaligned by 1 (mod 4), source buffer misaligned by 2 (mod
 * 4): the two never share the same alignment, so the fast 32-bit path is
 * never taken and the whole transfer runs through the byte-wise path. */
START_TEST(test_unaligned_write_mismatched_alignment)
{
    uint8_t rawbuf[64];
    uint8_t *data = rawbuf;
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;
    int i;

    while (((uintptr_t)data % 4) != 2)
        data++;
    for (i = 0; i < 8; i++)
        data[i] = (uint8_t)(0xC0 + i);

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
 * buggy and fixed forms agree here (i is always 0 in the byte-wise path),
 * guarding against a fix that breaks the common case. */
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
    Suite *s = suite_create("flash-write-nrf52");
    TCase *tc = tcase_create("flash-write-nrf52");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_aligned_write_unaligned_tail);
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
