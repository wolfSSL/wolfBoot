/* unit-fdt.c
 *
 * Unit tests for flattened device tree helpers.
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

#include "../../include/fdt.h"

void wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
}

START_TEST(test_fdt_get_string_rejects_out_of_range_offset)
{
    struct {
        struct fdt_header hdr;
        char strings[8];
        char after[4];
    } blob;
    int len = 1234;
    const char *s;

    memset(&blob, 0, sizeof(blob));
    fdt_set_off_dt_strings(&blob, sizeof(blob.hdr));
    fdt_set_size_dt_strings(&blob, sizeof(blob.strings));
    memcpy(blob.strings, "chosen", sizeof("chosen"));
    blob.after[0] = 'X';
    blob.after[1] = '\0';

    s = fdt_get_string(&blob, (int)sizeof(blob.strings), &len);

    ck_assert_ptr_null(s);
    ck_assert_int_eq(len, -FDT_ERR_BADOFFSET);
}
END_TEST

START_TEST(test_fdt_get_string_returns_string_with_valid_offset)
{
    struct {
        struct fdt_header hdr;
        char strings[16];
    } blob;
    int len = -1;
    const char *s;

    memset(&blob, 0, sizeof(blob));
    fdt_set_off_dt_strings(&blob, sizeof(blob.hdr));
    fdt_set_size_dt_strings(&blob, sizeof(blob.strings));
    memcpy(blob.strings, "serial\0console\0", 15);

    s = fdt_get_string(&blob, 7, &len);

    ck_assert_ptr_nonnull(s);
    ck_assert_str_eq(s, "console");
    ck_assert_int_eq(len, 7);
}
END_TEST

static Suite *fdt_suite(void)
{
    Suite *s = suite_create("fdt");
    TCase *tc = tcase_create("fdt");

    tcase_add_test(tc, test_fdt_get_string_rejects_out_of_range_offset);
    tcase_add_test(tc, test_fdt_get_string_returns_string_with_valid_offset);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = fdt_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
