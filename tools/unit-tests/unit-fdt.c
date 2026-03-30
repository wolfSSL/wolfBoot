/* unit-fdt.c
 *
 * Unit tests for flattened device tree helpers.
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
