/* unit-enc-key.c
 *
 * Unit tests for encrypted key storage on small flash erase units.
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
 * along with wolfBoot; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
const char ENCRYPT_KEY[] = "0123456789abcdef0123456789abcdef0123456789abcdef";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
#include "encrypt.h"
#include "libwolfboot.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>

const char *argv0;

#include "unit-mock-flash.c"

static void reset_erase_log(void)
{
    mock_flash_erase_count = 0;
    mock_flash_erase_violations = 0;
}

static void mark_expected_range(uint8_t *expected,
        uintptr_t address, size_t length)
{
    uintptr_t unit = address & ~(MOCK_FLASH_ERASE_UNIT - 1);
    uintptr_t last = (address + length - 1) & ~(MOCK_FLASH_ERASE_UNIT - 1);

    while (unit <= last) {
        size_t index = (unit - WOLFBOOT_PARTITION_BOOT_ADDRESS) /
            MOCK_FLASH_ERASE_UNIT;
        expected[index] = 1;
        unit += MOCK_FLASH_ERASE_UNIT;
    }
}

static void check_erase_log(void)
{
    uint8_t expected[WOLFBOOT_PARTITION_SIZE / MOCK_FLASH_ERASE_UNIT] = { 0 };
    uintptr_t base = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uintptr_t key_addr = base + ENCRYPT_TMP_SECRET_OFFSET;
    uintptr_t magic_addr = key_addr - sizeof(uint32_t);
    size_t i, expected_count = 0;

#ifdef FLAGS_HOME
    mark_expected_range(expected, magic_addr -
        (PART_BOOT_ENDFLAGS - PART_UPDATE_ENDFLAGS), sizeof(uint32_t));
#endif
    mark_expected_range(expected, magic_addr, sizeof(uint32_t));
    mark_expected_range(expected, key_addr, ENCRYPT_KEY_SIZE);
    mark_expected_range(expected, key_addr + ENCRYPT_KEY_SIZE,
        ENCRYPT_NONCE_SIZE);

    for (i = 0; i < sizeof(expected); i++) {
        if (!expected[i])
            continue;
        expected_count++;
        ck_assert_msg(mock_flash_erased_address[expected_count - 1] ==
                base + i * MOCK_FLASH_ERASE_UNIT,
            "unexpected erase address at log index %lu",
            (unsigned long)(expected_count - 1));
        ck_assert_int_eq(mock_flash_erased_length[expected_count - 1],
            MOCK_FLASH_ERASE_UNIT);
    }
    ck_assert_int_eq(mock_flash_erase_count, expected_count);
    ck_assert_int_eq(mock_flash_erase_violations, 0);
}

static void check_flash_contents(const uint8_t *key, const uint8_t *nonce,
        int erased)
{
    const uint8_t *boot = (const uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uintptr_t base = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uintptr_t key_addr = base + ENCRYPT_TMP_SECRET_OFFSET;
    uintptr_t magic_addr = key_addr - sizeof(uint32_t);
    uint8_t magic[sizeof(uint32_t)];
    size_t i;

    XMEMCPY(magic, &wolfboot_magic_trail, sizeof(magic));
    for (i = 0; i < WOLFBOOT_PARTITION_SIZE; i++) {
        uint8_t expected = 0x5A;

#ifdef FLAGS_HOME
        uintptr_t flags_magic = magic_addr -
            (PART_BOOT_ENDFLAGS - PART_UPDATE_ENDFLAGS);
        if (i >= flags_magic - base &&
                i < flags_magic - base + sizeof(magic))
            expected = magic[i - (flags_magic - base)];
#endif
        if (i >= magic_addr - base &&
                i < magic_addr - base + sizeof(magic))
            expected = magic[i - (magic_addr - base)];
        if (i >= key_addr - base &&
                i < key_addr - base + ENCRYPT_KEY_SIZE)
            expected = erased ? 0xFF : key[i - (key_addr - base)];
        if (i >= key_addr - base + ENCRYPT_KEY_SIZE &&
                i < key_addr - base + ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE)
            expected = erased ? 0xFF :
                nonce[i - (key_addr - base) - ENCRYPT_KEY_SIZE];
        ck_assert_msg(boot[i] == expected,
            "unexpected byte at BOOT offset 0x%lx: got 0x%02x expected 0x%02x",
            (unsigned long)i, boot[i], expected);
    }
}

static void check_cache_scrubbed(void)
{
    size_t i;

    for (i = 0; i < sizeof(ENCRYPT_CACHE); i++)
        ck_assert_msg(ENCRYPT_CACHE[i] == 0,
            "encryption cache byte %lu was not scrubbed",
            (unsigned long)i);
}

START_TEST(test_set_and_erase_encrypt_key)
{
    static const uint8_t key[ENCRYPT_KEY_SIZE] = { 0x11 };
    static const uint8_t nonce[ENCRYPT_NONCE_SIZE] = { 0x22 };
    uint8_t *boot = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-enc-key-update.bin",
        (uint8_t *)MOCK_ADDRESS, WOLFBOOT_PARTITION_SIZE, NULL);
    ck_assert_int_ge(ret, 0);
    ret = mmap_file("/tmp/wolfboot-unit-enc-key-boot.bin",
        (uint8_t *)MOCK_ADDRESS_BOOT, WOLFBOOT_PARTITION_SIZE, NULL);
    ck_assert_int_ge(ret, 0);
    ret = mmap_file("/tmp/wolfboot-unit-enc-key-swap.bin",
        (uint8_t *)MOCK_ADDRESS_SWAP, WOLFBOOT_SECTOR_SIZE, NULL);
    ck_assert_int_ge(ret, 0);

    memset(boot, 0x5A, WOLFBOOT_PARTITION_SIZE);
    reset_erase_log();
    ret = wolfBoot_set_encrypt_key(key, nonce);
    ck_assert_int_eq(ret, 0);
    ck_assert(locked);
    check_erase_log();
    check_flash_contents(key, nonce, 0);
    check_cache_scrubbed();

    reset_erase_log();
    ret = wolfBoot_erase_encrypt_key();
    ck_assert_int_eq(ret, 0);
    ck_assert(locked);
    check_erase_log();
    check_flash_contents(key, nonce, 1);
    check_cache_scrubbed();

    reset_erase_log();
    hal_flash_erase_fail = 1;
    ret = wolfBoot_set_encrypt_key(key, nonce);
    ck_assert_int_eq(ret, -1);
    ck_assert(locked);
    check_cache_scrubbed();

    hal_flash_write_fail = 1;
    ret = wolfBoot_set_encrypt_key(key, nonce);
    ck_assert_int_eq(ret, -1);
    ck_assert(locked);
    check_cache_scrubbed();

    ret = wolfBoot_set_encrypt_key(key, nonce);
    ck_assert_int_eq(ret, 0);
    ck_assert(locked);
    check_cache_scrubbed();

    hal_flash_erase_fail = 1;
    ret = wolfBoot_erase_encrypt_key();
    ck_assert_int_eq(ret, -1);
    ck_assert(locked);
    check_cache_scrubbed();

    ret = wolfBoot_set_encrypt_key(key, nonce);
    ck_assert_int_eq(ret, 0);
    ck_assert(locked);
    check_cache_scrubbed();

    hal_flash_write_fail = 1;
    ret = wolfBoot_erase_encrypt_key();
    ck_assert_int_eq(ret, -1);
    ck_assert(locked);
    check_cache_scrubbed();
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *suite = suite_create("encrypted key erase unit");
    TCase *test_case = tcase_create("key storage");

    tcase_add_test(test_case, test_set_and_erase_encrypt_key);
    suite_add_tcase(suite, test_case);
    return suite;
}

int main(int argc, char *argv[])
{
    int failures;
    SRunner *runner;

    (void)argc;
    argv0 = strdup(argv[0]);
    runner = srunner_create(wolfboot_suite());
    srunner_run_all(runner, CK_NORMAL);
    failures = srunner_ntests_failed(runner);
    srunner_free(runner);
    free((void *)argv0);
    return failures;
}
