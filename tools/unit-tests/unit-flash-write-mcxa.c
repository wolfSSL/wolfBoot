/* unit-flash-write-mcxa.c
 *
 * Regression test for F-5963: in the unaligned/partial-word path of
 * hal_flash_write() (hal/mcxa.c), the post-program bookkeeping did
 *     address += i;
 *     len -= i;
 * where "i" is the flash-word-relative loop index (it starts at start_off,
 * the byte offset of "address" within its 16-byte flash word), not the
 * number of data bytes actually consumed. That is (i - start_off), which is
 * how far "w" (the data source index) really advanced. Whenever an unaligned
 * write spans more than one flash word, both address and len end up
 * over-advanced by start_off: the next word is targeted start_off bytes too
 * high, and len is start_off too small, so start_off bytes of the input are
 * silently dropped instead of being written.
 *
 * hal/kinetis_kl26.c already has the correct form:
 *     address = address_align + i;
 *     len -= (int)(i - start_off);
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

/* The NXP SDK ROM API calls through a fixed ROM address; point it at a mock
 * tree whose flash driver simulates programming as a memcpy onto the buffer
 * that "address" points into. */
extern struct _bootloader_tree mock_rom_api;
#define FSL_FEATURE_ROMAPI_BASE ((uintptr_t)&mock_rom_api)
#include "fsl_common.h"
#include "fsl_romapi.h"
#include "image.h"

/* Records of the (mock) ROM erase calls issued by hal_flash_erase(). */
static int erase_calls;
static uint32_t last_erase_start;
static uint32_t last_erase_len;

static status_t mock_program_phrase(flash_config_t *config, uint32_t start,
        uint8_t *src, uint32_t len)
{
    (void)config;
    memcpy((void*)(uintptr_t)start, src, len);
    return kStatus_Success;
}

static status_t mock_erase_sector(flash_config_t *config, uint32_t start,
        uint32_t length_in_bytes, uint32_t key)
{
    (void)config; (void)key;
    erase_calls++;
    last_erase_start = start;
    last_erase_len = length_in_bytes;
    return kStatus_Success;
}

static flash_driver_interface_t mock_flash_driver;
bootloader_tree_t mock_rom_api;

#include "../../hal/mcxa.c"

/* hal_flash_write() treats "address" as a real pointer into memory-mapped
 * flash (it memcpy()s from it directly). struct fields carrying such
 * addresses are uint32_t, matching the real 32-bit target, so the mock flash
 * buffer is mapped below 4GB (MAP_32BIT on x86-64, a low hint elsewhere). */
#ifdef MAP_32BIT
#define MOCK_FLASH_HINT  NULL
#define MOCK_FLASH_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT)
#else
#define MOCK_FLASH_HINT  ((void *)0x20000000UL)
#define MOCK_FLASH_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)
#endif
#define MOCK_FLASH_SIZE 64
static uint8_t *mock_flash;

static void setup(void)
{
    mock_flash = mmap(MOCK_FLASH_HINT, MOCK_FLASH_SIZE, PROT_READ | PROT_WRITE,
            MOCK_FLASH_FLAGS, -1, 0);
    ck_assert_ptr_ne(mock_flash, MAP_FAILED);
    ck_assert_uint_lt((uintptr_t)mock_flash, 0xFFFFFFFFUL - MOCK_FLASH_SIZE);
    memset(mock_flash, 0xFF, MOCK_FLASH_SIZE);
    mock_flash_driver.flash_program_phrase = mock_program_phrase;
    mock_flash_driver.flash_erase_sector = mock_erase_sector;
    mock_rom_api.flash_driver = &mock_flash_driver;
    erase_calls = 0;
    last_erase_start = 0;
    last_erase_len = 0;
}

static void teardown(void)
{
    munmap(mock_flash, MOCK_FLASH_SIZE);
}

/* Write 20 bytes starting 5 bytes into a 16-byte flash word: the write spans
 * two flash words (word 0 bytes 5..15, word 1 bytes 0..8). Byte-for-byte,
 * mock_flash[5 .. 24] must equal the 20 input bytes, and everything outside
 * that range must be untouched. Before the fix, address/len over-advanced by
 * start_off (5) after the first word, so mock_flash[16..20] were left as
 * 0xFF and the last 5 input bytes were never written anywhere. */
START_TEST(test_unaligned_write_spanning_two_words)
{
    uint8_t data[20];
    int i;
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;

    for (i = 0; i < 20; i++)
        data[i] = (uint8_t)(i + 1);

    ck_assert_int_eq(hal_flash_write(base + 5, data, 20), 0);

    for (i = 0; i < 5; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
    for (i = 0; i < 20; i++)
        ck_assert_uint_eq(mock_flash[5 + i], data[i]);
    for (i = 25; i < MOCK_FLASH_SIZE; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
}
END_TEST

/* A write that fits entirely inside a single flash word must still work:
 * the buggy and fixed forms agree here (the loop runs only once), so this
 * guards against a fix that breaks the common case. */
START_TEST(test_unaligned_write_single_word)
{
    uint8_t data[6];
    int i;
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;

    for (i = 0; i < 6; i++)
        data[i] = (uint8_t)(0xA0 + i);

    ck_assert_int_eq(hal_flash_write(base + 3, data, 6), 0);

    for (i = 0; i < 3; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
    for (i = 0; i < 6; i++)
        ck_assert_uint_eq(mock_flash[3 + i], data[i]);
    for (i = 9; i < MOCK_FLASH_SIZE; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
}
END_TEST

/* An aligned write longer than one flash word takes the bulk fast path
 * (FLASH_ProgramPhrase over data + w) for the first 16 bytes, then a
 * partial-word tail for the rest. The fast path must advance the data source
 * index "w" by the bulk length; if it does not, the tail re-reads the input
 * from the start and programs the wrong bytes. Write 24 bytes at an aligned
 * address: mock_flash[0..23] must equal the input. Before the fix,
 * mock_flash[16..23] held data[0..7] instead of data[16..23]. */
START_TEST(test_aligned_write_bulk_then_tail)
{
    uint8_t data[24];
    int i;
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;

    for (i = 0; i < 24; i++)
        data[i] = (uint8_t)(i + 1);

    ck_assert_int_eq(hal_flash_write(base, data, 24), 0);

    for (i = 0; i < 24; i++)
        ck_assert_uint_eq(mock_flash[i], data[i]);
    for (i = 24; i < MOCK_FLASH_SIZE; i++)
        ck_assert_uint_eq(mock_flash[i], 0xFF);
}
END_TEST

/* hal_flash_erase() must hand FLASH_EraseSector the byte length
 * unconverted (the MCXA ROM API takes lengthInBytes, not a sector
 * count). */
START_TEST(test_erase_passes_byte_length){
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;

    ck_assert_int_eq(hal_flash_erase(base, 4096), 0);
    ck_assert_int_eq(erase_calls, 1);
    ck_assert_uint_eq(last_erase_start, base);
    ck_assert_uint_eq(last_erase_len, 4096);

    erase_calls = 0;
    ck_assert_int_eq(hal_flash_erase(base, 8192), 0);
    ck_assert_int_eq(erase_calls, 1);
    ck_assert_uint_eq(last_erase_len, 8192);
}
END_TEST

/* A zero-length erase request must be rejected without touching the ROM
 * API. */
START_TEST(test_erase_zero_len){
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;

    ck_assert_int_eq(hal_flash_erase(base, 0), -1);
    ck_assert_int_eq(erase_calls, 0);
}
END_TEST

START_TEST(test_erase_negative_len){
    uint32_t base = (uint32_t)(uintptr_t)mock_flash;

    ck_assert_int_eq(hal_flash_erase(base, -1), -1);
    ck_assert_int_eq(erase_calls, 0);
}
END_TEST

Suite *flash_write_suite(void)
{
    Suite *s = suite_create("flash-write-mcxa");
    TCase *tc = tcase_create("flash-write-mcxa");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_unaligned_write_spanning_two_words);
    tcase_add_test(tc, test_unaligned_write_single_word);
    tcase_add_test(tc, test_aligned_write_bulk_then_tail);
    tcase_add_test(tc, test_erase_passes_byte_length);
    tcase_add_test(tc, test_erase_zero_len);
    tcase_add_test(tc, test_erase_negative_len);

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
