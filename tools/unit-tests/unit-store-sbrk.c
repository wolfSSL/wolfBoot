/* unit-store-sbrk.c
 *
 * Unit tests for store allocator helper.
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
#include <limits.h>
#include <stdint.h>

#include "../../src/store_sbrk.h"

START_TEST(test_sbrk_first_call_advances_heap)
{
    uint8_t heap_buf[32];
    uint8_t *heap = NULL;
    void *ret;

    ret = wolfboot_store_sbrk(5, &heap, heap_buf, sizeof(heap_buf));

    ck_assert_ptr_eq(ret, heap_buf);
    ck_assert_ptr_eq(heap, heap_buf + 8);
}
END_TEST

START_TEST(test_sbrk_rejects_overflow)
{
    uint8_t heap_buf[16];
    uint8_t *heap = NULL;
    void *ret;

    ret = wolfboot_store_sbrk(8, &heap, heap_buf, sizeof(heap_buf));
    ck_assert_ptr_eq(ret, heap_buf);

    ret = wolfboot_store_sbrk(16, &heap, heap_buf, sizeof(heap_buf));
    ck_assert_ptr_eq(ret, (void *)-1);
    ck_assert_ptr_eq(heap, heap_buf + 8);
}
END_TEST

START_TEST(test_sbrk_rejects_alignment_overflow)
{
    uint8_t heap_buf[16];
    uint8_t *heap = NULL;
    void *ret;

    ret = wolfboot_store_sbrk(UINT_MAX - 1U, &heap, heap_buf, sizeof(heap_buf));

    ck_assert_ptr_eq(ret, (void *)-1);
    ck_assert_ptr_eq(heap, NULL);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("store-sbrk");
    TCase *tcase = tcase_create("store_sbrk");

    tcase_add_test(tcase, test_sbrk_first_call_advances_heap);
    tcase_add_test(tcase, test_sbrk_rejects_overflow);
    tcase_add_test(tcase, test_sbrk_rejects_alignment_overflow);
    suite_add_tcase(s, tcase);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
