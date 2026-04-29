/* unit-tpm-api-names.c
 *
 * Unit tests for TPM API string helpers.
 */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SPI_CS_TPM
#define SPI_CS_TPM 1
#endif

#include "tpm.h"

struct small_buf {
    char text[4];
    char guard[4];
};

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

const char* TPM2_GetAlgName(TPM_ALG_ID alg)
{
    (void)alg;
    return NULL;
}

const char* TPM2_GetRCString(int rc)
{
    (void)rc;
    return NULL;
}

#include "../../src/tpm.c"

static void setup_small_buf(struct small_buf* buf)
{
    memset(buf->text, 'X', sizeof(buf->text));
    memcpy(buf->guard, "END", 4);
}

START_TEST(test_wolfBoot_tpm2_get_alg_name_bounds_unknown_fallback)
{
    struct small_buf buf;
    const char* ret;

    setup_small_buf(&buf);

    ret = wolfBoot_tpm2_get_alg_name((TPM_ALG_ID)0xFFFF, buf.text,
        (int)sizeof(buf.text));

    ck_assert_ptr_eq(ret, buf.text);
    ck_assert_str_eq(buf.guard, "END");
    ck_assert_int_eq(buf.text[sizeof(buf.text) - 1], '\0');
}
END_TEST

START_TEST(test_wolfBoot_tpm2_get_rc_string_bounds_unknown_fallback)
{
    struct small_buf buf;
    const char* ret;

    setup_small_buf(&buf);

    ret = wolfBoot_tpm2_get_rc_string(-1, buf.text, (int)sizeof(buf.text));

    ck_assert_ptr_eq(ret, buf.text);
    ck_assert_str_eq(buf.guard, "END");
    ck_assert_int_eq(buf.text[sizeof(buf.text) - 1], '\0');
}
END_TEST

static Suite* tpm_api_names_suite(void)
{
    Suite* s;
    TCase* tc;

    s = suite_create("TPM API names");
    tc = tcase_create("fallback_bounds");
    tcase_add_test(tc, test_wolfBoot_tpm2_get_alg_name_bounds_unknown_fallback);
    tcase_add_test(tc, test_wolfBoot_tpm2_get_rc_string_bounds_unknown_fallback);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s;
    SRunner* sr;
    int failed;

    s = tpm_api_names_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
