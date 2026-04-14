/* unit-max-space.c
 *
 * Unit test for WOLFBOOT_MAX_SPACE macro arithmetic in image.h
 */

#include <check.h>
#include <stdint.h>

#define TRAILER_SKIP 0
#include "image.h"

START_TEST(test_wolfboot_max_space_formula)
{
    uint32_t expected = (uint32_t)(WOLFBOOT_PARTITION_SIZE -
        (TRAILER_SKIP + sizeof(uint32_t) +
        ((WOLFBOOT_PARTITION_SIZE + 1U) / (WOLFBOOT_SECTOR_SIZE * 8U))));
    uint32_t actual = (uint32_t)WOLFBOOT_MAX_SPACE;

    ck_assert_uint_eq(actual, expected);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("WOLFBOOT_MAX_SPACE");
    tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_wolfboot_max_space_formula);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? 0 : 1;
}
