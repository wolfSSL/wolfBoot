/* unit-x86-paging-oob.c
 *
 * Regression test for OOB memset in x86_paging_setup_ptp when pool is
 * exhausted.  The guard must fire for any call where page_table_page_used
 * >= WOLFBOOT_PTP_NUM, not only the exact == boundary.  With the old
 * single-hlt panic() the guard check can be bypassed on a second call,
 * writing 4 KB one page past the end of page_table_pages[].
 */

#include <check.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

static jmp_buf panic_jmp;
static int panic_count = 0;

/* Satisfies __attribute__((noreturn)) via longjmp; allows test to continue
 * after a guarded panic call without executing the code that follows it. */
__attribute__((noreturn)) void panic(void)
{
    panic_count++;
    longjmp(panic_jmp, 1);
}

/* paging.c includes <printf.h> which maps wolfBoot_printf to fprintf(stderr,
 * ...) on Linux, so no extra stub is needed.  The cr3-reading static function
 * x86_paging_get_paget_table_root() is compiled but never called here. */
#include "../../src/x86/paging.c"

static void reset_pool(void)
{
    page_table_page_used = 0;
    panic_count = 0;
    memset(page_table_pages, 0, sizeof(page_table_pages));
}

/* Verify that x86_paging_setup_ptp triggers panic for ANY call when the pool
 * is full (page_table_page_used >= WOLFBOOT_PTP_NUM), not only the first. */
START_TEST(test_ptp_guard_triggers_on_full_pool)
{
    uint64_t e = 0;

    reset_pool();
    page_table_page_used = WOLFBOOT_PTP_NUM;

    /* An over-limit call must trigger panic (longjmp). */
    if (setjmp(panic_jmp) == 0) {
        x86_paging_setup_ptp(&e);
        /* Reaching here means the guard was bypassed — the bug is present. */
        ck_abort_msg("panic not triggered for over-limit allocation (== guard bypassed)");
    }
    ck_assert_int_eq(panic_count, 1);

    /* page_table_page_used must not have advanced past WOLFBOOT_PTP_NUM;
     * with the fix the counter is checked before being incremented. */
    ck_assert_int_le(page_table_page_used, WOLFBOOT_PTP_NUM);

    /* A second over-limit call must also trigger panic (>= guard). */
    if (setjmp(panic_jmp) == 0) {
        x86_paging_setup_ptp(&e);
        ck_abort_msg("panic not triggered on second over-limit allocation");
    }
    ck_assert_int_eq(panic_count, 2);
}
END_TEST

static Suite *paging_oob_suite(void)
{
    Suite *s = suite_create("x86_paging_oob");
    TCase *tc = tcase_create("setup_ptp_guard");
    tcase_add_test(tc, test_ptp_guard_triggers_on_full_pool);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = paging_oob_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
