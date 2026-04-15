#include <check.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>

static int mock_tpm_init_rc;
static int start_calls;
static jmp_buf exit_env;

void hal_init(void)
{
}

uint16_t spi_flash_probe(void)
{
    return 0;
}

int wolfBoot_tpm2_init(void)
{
    return mock_tpm_init_rc;
}

void wolfBoot_start(void)
{
    start_calls++;
    longjmp(exit_env, 2);
}

void wolfBoot_hook_panic(void)
{
    longjmp(exit_env, 1);
}

#include "../../src/loader.c"

static void setup(void)
{
    mock_tpm_init_rc = 0;
    start_calls = 0;
}

START_TEST(test_loader_panics_when_tpm_init_fails)
{
    int exit_reason;

    mock_tpm_init_rc = -1;
    exit_reason = setjmp(exit_env);
    if (exit_reason == 0) {
        ck_assert_int_eq(loader_main(), 0);
    }

    ck_assert_int_eq(exit_reason, 1);
    ck_assert_int_eq(start_calls, 0);
}
END_TEST

START_TEST(test_loader_starts_boot_when_tpm_init_succeeds)
{
    int exit_reason;

    exit_reason = setjmp(exit_env);
    if (exit_reason == 0) {
        ck_assert_int_eq(loader_main(), 0);
    }

    ck_assert_int_eq(exit_reason, 2);
    ck_assert_int_eq(start_calls, 1);
}
END_TEST

static Suite *loader_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("loader");
    tc = tcase_create("loader_main");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_loader_panics_when_tpm_init_fails);
    tcase_add_test(tc, test_loader_starts_boot_when_tpm_init_succeeds);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = loader_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
