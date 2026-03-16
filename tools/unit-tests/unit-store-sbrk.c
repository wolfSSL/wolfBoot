/* unit-store-sbrk.c
 *
 * Unit tests for store allocator helper.
 */

#include <check.h>
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

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("store-sbrk");
    TCase *tcase = tcase_create("store_sbrk");

    tcase_add_test(tcase, test_sbrk_first_call_advances_heap);
    tcase_add_test(tcase, test_sbrk_rejects_overflow);
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
