/* unit-flash-is-erased.c
 *
 * Unit tests for the weak hal_flash_is_erased() and ext_flash_is_erased()
 * defaults in src/libwolfboot.c. Built for both flag polarities, so the
 * erased value is 0xFF by default and 0x00 with WOLFBOOT_FLAGS_INVERT.
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
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <check.h>
#include "libwolfboot.c"

#define AREA_SIZE 200
/* Internal flash is at a fixed low address, because haladdr_t has 32 bits in
 * unit tests */
#define INT_FLASH_ADDR 0xCF100000UL
static uint8_t *g_int;
static uint8_t g_ext[AREA_SIZE];
static int g_ext_reads;
static int g_ext_fail;

/* HAL mocks that libwolfboot.c needs to link */
int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}
int hal_flash_erase(haladdr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}
void hal_flash_unlock(void) {}
void hal_flash_lock(void) {}
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}
int ext_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}
void ext_flash_unlock(void) {}
void ext_flash_lock(void) {}

/* External flash is g_ext, addressed from 0 */
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    g_ext_reads++;
    if (g_ext_fail)
        return -1;
    memcpy(data, g_ext + address, len);
    return len;
}

static void setup(void)
{
    memset(g_int, FLASH_BYTE_ERASED, AREA_SIZE);
    memset(g_ext, FLASH_BYTE_ERASED, sizeof(g_ext));
    g_ext_reads = 0;
    g_ext_fail = 0;
}

static void teardown(void)
{
}

START_TEST(test_hal_default)
{
    haladdr_t a = (haladdr_t)INT_FLASH_ADDR;

    ck_assert_int_eq(hal_flash_is_erased(a, AREA_SIZE), 1);
    ck_assert_int_eq(hal_flash_is_erased(a, 0), 1);

    /* One bit away from the erased value */
    g_int[150] = (uint8_t)(FLASH_BYTE_ERASED ^ 0x01);
    ck_assert_int_eq(hal_flash_is_erased(a, AREA_SIZE), 0);
    ck_assert_int_eq(hal_flash_is_erased(a, 150), 1);
    ck_assert_int_eq(hal_flash_is_erased(a + 150, 1), 0);
    ck_assert_int_eq(hal_flash_is_erased(a + 151, AREA_SIZE - 151), 1);

    /* The value of the other polarity is not erased */
    g_int[150] = (uint8_t)~FLASH_BYTE_ERASED;
    ck_assert_int_eq(hal_flash_is_erased(a + 150, 1), 0);
}
END_TEST

/* Reads in chunks through ext_flash_read(), across the chunk boundary */
START_TEST(test_ext_default)
{
    ck_assert_int_eq(ext_flash_is_erased(0, AREA_SIZE), 1);
    ck_assert_int_gt(g_ext_reads, 1);

    g_ext[33] = (uint8_t)(FLASH_BYTE_ERASED ^ 0x80);
    ck_assert_int_eq(ext_flash_is_erased(0, AREA_SIZE), 0);
    ck_assert_int_eq(ext_flash_is_erased(0, 33), 1);
    ck_assert_int_eq(ext_flash_is_erased(30, 4), 0);
    ck_assert_int_eq(ext_flash_is_erased(34, 100), 1);

    g_ext_fail = 1;
    ck_assert_int_lt(ext_flash_is_erased(0, 4), 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-flash-is-erased");
    TCase *tc = tcase_create("flash-is-erased");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_hal_default);
    tcase_add_test(tc, test_ext_default);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s;
    SRunner *sr;

    g_int = mmap((void *)INT_FLASH_ADDR, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (g_int == MAP_FAILED)
        return 1;
    s = wolfboot_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
