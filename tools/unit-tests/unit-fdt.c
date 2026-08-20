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

/* Minimal FDT whose root node carries a `compatible` property whose value has
 * no NUL terminator within its declared length (len=4, "AAAA").
 * fdt_node_offset_by_compatible() walks the (possibly multi-string) compatible
 * value with memchr(prop, '\0', len); */
static const uint8_t fdt_compatible_no_terminator[] = {
    /* header */
    0xd0, 0x0d, 0xfe, 0xed, /* magic */
    0x00, 0x00, 0x00, 0x63, /* totalsize = 99 */
    0x00, 0x00, 0x00, 0x38, /* off_dt_struct = 56 */
    0x00, 0x00, 0x00, 0x58, /* off_dt_strings = 88 */
    0x00, 0x00, 0x00, 0x28, /* off_mem_rsvmap = 40 */
    0x00, 0x00, 0x00, 0x11, /* version = 17 */
    0x00, 0x00, 0x00, 0x10, /* last_comp_version = 16 */
    0x00, 0x00, 0x00, 0x00, /* boot_cpuid_phys */
    0x00, 0x00, 0x00, 0x0b, /* size_dt_strings = 11 */
    0x00, 0x00, 0x00, 0x20, /* size_dt_struct = 32 */
    /* mem_rsvmap terminator (offset 40) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* struct block (offset 56) */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE root */
    0x00, 0x00, 0x00, 0x00,                         /* "" */
    0x00, 0x00, 0x00, 0x03,                         /* FDT_PROP */
    0x00, 0x00, 0x00, 0x04,                         /* len = 4 */
    0x00, 0x00, 0x00, 0x00,                         /* nameoff = 0 ("compatible") */
    0x41, 0x41, 0x41, 0x41,                         /* value "AAAA" -- no NUL */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE root */
    0x00, 0x00, 0x00, 0x09,                         /* FDT_END */
    /* strings block (offset 88) */
    0x63, 0x6f, 0x6d, 0x70, 0x61, 0x74, 0x69, 0x62, /* "compatib" */
    0x6c, 0x65, 0x00,                               /* "le\0" */
};

START_TEST(test_fdt_node_offset_by_compatible_terminates_on_unterminated_prop)
{
    int off;

    /* complen = strlen("foo") = 3 <= 4 = declared property length, so the
     * inner walk loop is entered; "foo" != "AAAA" so it does not match and
     * falls through to the memchr() advance  */
    off = fdt_node_offset_by_compatible(fdt_compatible_no_terminator, -1, "foo");

    ck_assert_int_lt(off, 0);
}
END_TEST

/* Build a minimal FDT in buf (zero-initialized, so padding bytes are
 * 0x00): root node with a `compatible` property whose raw value is
 * `len` bytes at `val`. */
static void build_compat_fdt(uint8_t *buf, size_t size,
    const uint8_t *val, uint32_t len)
{
    struct fdt_header *hdr;
    uint32_t *s;
    uint32_t val_aligned = (len + 3u) & ~3u;
    uint32_t struct_off = 0x40;
    uint32_t strings_off = 0x80;

    memset(buf, 0, size);

    hdr = (struct fdt_header *)buf;
    fdt_set_totalsize(hdr, 0x100);
    fdt_set_off_dt_struct(hdr, struct_off);
    fdt_set_off_dt_strings(hdr, strings_off);
    fdt_set_off_mem_rsvmap(hdr, 0x28);
    fdt_set_version(hdr, 17);
    fdt_set_last_comp_version(hdr, 16);
    fdt_set_size_dt_struct(hdr,
        8u + 12u + val_aligned + 4u + 4u); /* node hdr, prop, end, FDT_END */
    fdt_set_size_dt_strings(hdr, sizeof("compatible"));
    memcpy(buf + strings_off, "compatible", sizeof("compatible"));

    s = (uint32_t *)(buf + struct_off);
    s[0] = fdt32_to_cpu(FDT_BEGIN_NODE); /* root */
    s[1] = 0;                            /* empty name */
    s[2] = fdt32_to_cpu(FDT_PROP);
    s[3] = fdt32_to_cpu(len);
    s[4] = fdt32_to_cpu(0);              /* nameoff of "compatible" */
    memcpy(buf + struct_off + 8 + 12, val, len);
    s[5 + val_aligned / 4u] = fdt32_to_cpu(FDT_END_NODE);
    s[6 + val_aligned / 4u] = fdt32_to_cpu(FDT_END);
}

/* A compatible entry whose declared length equals the search string
 * (no room for a NUL) must not match: before the fix the comparison
 * read one byte past the declared length (the 4-byte alignment
 * padding, zero here) as if it were the terminator and accepted the
 * entry. */
START_TEST(test_fdt_compatible_unterminated_exact_len_no_match)
{
    static uint8_t buf[0x100];
    int off;

    build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc", 3);

    off = fdt_node_offset_by_compatible(buf, -1, "abc");
    ck_assert_int_lt(off, 0);
}
END_TEST

/* A properly terminated entry of exactly the search length matches. */
START_TEST(test_fdt_compatible_terminated_exact_len_match)
{
    static uint8_t buf[0x100];
    int off;

    build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);

    off = fdt_node_offset_by_compatible(buf, -1, "abc");
    ck_assert_int_eq(off, 0); /* the root node */
}
END_TEST

/* Multi-string compatible lists keep working: the second entry
 * matches. */
START_TEST(test_fdt_compatible_multi_string_list_match)
{
    static uint8_t buf[0x100];
    int off;

    build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"xy\0abc\0", 8);

    off = fdt_node_offset_by_compatible(buf, -1, "abc");
    ck_assert_int_eq(off, 0);
}
END_TEST

/* A terminated entry that merely starts with the search string does
 * not match (the entry is longer). */
START_TEST(test_fdt_compatible_prefix_entry_no_match)
{
    static uint8_t buf[0x100];
    int off;

    build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abcd\0", 5);

    off = fdt_node_offset_by_compatible(buf, -1, "abc");
    ck_assert_int_lt(off, 0);
}

static Suite *fdt_suite(void)
{
    Suite *s = suite_create("fdt");
    TCase *tc = tcase_create("fdt");
    /* Separate case with a hard timeout so an unterminated-property
     * regression is reported as a failure rather than hanging the suite. */
    TCase *tc_dos = tcase_create("fdt-dos");

    tcase_add_test(tc, test_fdt_get_string_rejects_out_of_range_offset);
    tcase_add_test(tc, test_fdt_get_string_returns_string_with_valid_offset);
    tcase_add_test(tc, test_fit_load_image_rejects_oversized_prop_len);
    tcase_add_test(tc, test_fdt_shrink_rejects_dt_strings_area_overflow);
    tcase_add_test(tc, test_fdt_compatible_unterminated_exact_len_no_match);
    tcase_add_test(tc, test_fdt_compatible_terminated_exact_len_match);
    tcase_add_test(tc, test_fdt_compatible_multi_string_list_match);
    tcase_add_test(tc, test_fdt_compatible_prefix_entry_no_match);
    suite_add_tcase(s, tc);

    tcase_set_timeout(tc_dos, 5);
    tcase_add_test(tc_dos,
        test_fdt_node_offset_by_compatible_terminates_on_unterminated_prop);
    suite_add_tcase(s, tc_dos);

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
