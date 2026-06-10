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

/* Minimal FIT with a single /images/kernel-1 node whose `data` property
 * declares len=0xFFFFFFFF. There is no `load` (and no `compression`), so
 * fit_load_image_inner() takes the pass-through branch. Before the
 * fdt_next_tag() length check, the oversized len wrapped the cursor
 * arithmetic, slipped past the bounds check, and was handed back as
 * *lenp = -1 - which update_ram.c then aliased into a ~4GB memcpy size.
 * The loader must instead fail closed (return NULL). */
static const uint8_t fit_data_len_overflow[] = {
    /* header */
    0xd0, 0x0d, 0xfe, 0xed, /* magic */
    0x00, 0x00, 0x00, 0x81, /* totalsize = 129 */
    0x00, 0x00, 0x00, 0x38, /* off_dt_struct = 56 */
    0x00, 0x00, 0x00, 0x7c, /* off_dt_strings = 124 */
    0x00, 0x00, 0x00, 0x28, /* off_mem_rsvmap = 40 */
    0x00, 0x00, 0x00, 0x11, /* version = 17 */
    0x00, 0x00, 0x00, 0x10, /* last_comp_version = 16 */
    0x00, 0x00, 0x00, 0x00, /* boot_cpuid_phys */
    0x00, 0x00, 0x00, 0x05, /* size_dt_strings = 5 */
    0x00, 0x00, 0x00, 0x44, /* size_dt_struct = 68 */
    /* mem_rsvmap terminator (offset 40) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* struct block (offset 56) */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE root */
    0x00, 0x00, 0x00, 0x00,                         /* "" */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE images */
    0x69, 0x6d, 0x61, 0x67, 0x65, 0x73, 0x00, 0x00, /* "images\0\0" */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE kernel-1 */
    0x6b, 0x65, 0x72, 0x6e, 0x65, 0x6c, 0x2d, 0x31,
    0x00, 0x00, 0x00, 0x00,                         /* "kernel-1\0\0\0\0" */
    0x00, 0x00, 0x00, 0x03,                         /* FDT_PROP */
    0xff, 0xff, 0xff, 0xff,                         /* len = 0xFFFFFFFF */
    0x00, 0x00, 0x00, 0x00,                         /* nameoff = 0 ("data") */
    0x00, 0x00, 0x00, 0x00,                         /* data (4 bytes) */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE kernel-1 */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE images */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE root */
    0x00, 0x00, 0x00, 0x09,                         /* FDT_END */
    /* strings block (offset 124) */
    0x64, 0x61, 0x74, 0x61, 0x00,                   /* "data\0" */
};

START_TEST(test_fit_load_image_rejects_oversized_prop_len)
{
    static uint8_t fit_scratch[sizeof(fit_data_len_overflow)];
    int len = 0;
    void *ret;

    memcpy(fit_scratch, fit_data_len_overflow, sizeof(fit_scratch));

    ret = fit_load_image_ex(fit_scratch, "kernel-1", &len, 64 * 1024);

    /* Must fail closed: never return a live pointer with a negative
     * length that a caller could turn into a giant memcpy size. */
    ck_assert_ptr_null(ret);
}
END_TEST

/* off_dt_strings=4, size_dt_strings=0xFFFFFFFC: sum overflows uint32_t to 0.
 * Before the fix, fdt_data_size_() returned 0 and fdt_shrink() silently set
 * totalsize=0.  After the fix fdt_shrink() must return an error and leave
 * totalsize unchanged. */
START_TEST(test_fdt_shrink_rejects_dt_strings_area_overflow)
{
    static uint8_t buf[256];
    int rc;

    memset(buf, 0, sizeof(buf));
    fdt_set_totalsize(buf, sizeof(buf));
    fdt_set_off_dt_strings(buf, 4);
    fdt_set_size_dt_strings(buf, 0xFFFFFFFC);

    rc = fdt_shrink(buf);

    ck_assert_int_lt(rc, 0);
    ck_assert_uint_eq(fdt_totalsize(buf), sizeof(buf));
}
END_TEST

static Suite *fdt_suite(void)
{
    Suite *s = suite_create("fdt");
    TCase *tc = tcase_create("fdt");

    tcase_add_test(tc, test_fdt_get_string_rejects_out_of_range_offset);
    tcase_add_test(tc, test_fdt_get_string_returns_string_with_valid_offset);
    tcase_add_test(tc, test_fit_load_image_rejects_oversized_prop_len);
    tcase_add_test(tc, test_fdt_shrink_rejects_dt_strings_area_overflow);
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
