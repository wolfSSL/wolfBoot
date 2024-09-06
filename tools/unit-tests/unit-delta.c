/* unit-delta.c
 *
 * unit tests for delta updates module
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include <stdio.h>

#include "delta.h"
#define WC_RSA_BLINDING
#include "delta.c"

#define SRC_SIZE 4096
#define PATCH_SIZE 8192
#define DST_SIZE 4096
#define DIFF_SIZE 8192



START_TEST(test_wb_patch_init_invalid)
{
    WB_PATCH_CTX ctx;
    uint8_t src[SRC_SIZE] = {0};
    uint8_t patch[PATCH_SIZE] = {0};

    ck_assert_int_eq(wb_patch_init(NULL, src, SRC_SIZE, patch, PATCH_SIZE), -1);
    ck_assert_int_eq(wb_patch_init(&ctx, src, 0, patch, PATCH_SIZE), -1);
    ck_assert_int_eq(wb_patch_init(&ctx, src, SRC_SIZE, patch, 0), -1);
}
END_TEST


START_TEST(test_wb_diff_init_invalid)
{
    WB_DIFF_CTX ctx;
    uint8_t src_a[SRC_SIZE] = {0};
    uint8_t src_b[SRC_SIZE] = {0};

    ck_assert_int_eq(wb_diff_init(NULL, src_a, SRC_SIZE, src_b, SRC_SIZE), -1);
    ck_assert_int_eq(wb_diff_init(&ctx, src_a, 0, src_b, SRC_SIZE), -1);
    ck_assert_int_eq(wb_diff_init(&ctx, src_a, SRC_SIZE, src_b, 0), -1);
}
END_TEST

static void initialize_buffers(uint8_t *src_a, uint8_t *src_b)
{
    uint32_t pseudo_rand = 0;
    uint8_t tmp[128];
    for (int i = 0; i < SRC_SIZE; ++i) {
        src_a[i] = pseudo_rand % 256;
        src_b[i] = pseudo_rand % 256;
        if ((i % 100) == 42) {
            src_b[i] -= 1;
        }
        pseudo_rand *= 1664525;
        pseudo_rand += 1013904223;
        pseudo_rand ^= ~(i);
    }

    /* Introduce differences */
    src_b[100] = src_a[100] + 1;
    src_b[200] = src_a[200] + 2;

    /* 10-bytes difference across two blocks */
    for (int i = 1020; i < 1040; ++i) {
        src_b[i] = src_a[i] + 3;
    }


    /* Copy a sequence from A to B, behind */
    src_a[510] = ESC;
    memcpy(src_b + 4090, src_a + 500, 20);


    /* Copy a sequence from B to itself, ahead */
    src_b[1022] = ESC;
    memcpy(tmp, src_b + 1020, 30);
    memcpy(src_b + 7163, tmp, 30);

}

START_TEST(test_wb_patch_and_diff)
{
    WB_DIFF_CTX diff_ctx;
    WB_PATCH_CTX patch_ctx;
    uint8_t src_a[SRC_SIZE];
    uint8_t src_b[SRC_SIZE];
    uint8_t patch[PATCH_SIZE];
    uint8_t patched_dst[DST_SIZE];
    int ret;
    int i;
    uint32_t p_written = 0;


    initialize_buffers(src_a, src_b);

    ret = wb_diff_init(&diff_ctx, src_a, SRC_SIZE, src_b, SRC_SIZE);
    ck_assert_int_eq(ret, 0);

    /* Create the patch */
    for (i = 0; i < SRC_SIZE; i += DELTA_BLOCK_SIZE) {
        ret = wb_diff(&diff_ctx, patch + p_written, DELTA_BLOCK_SIZE);
        ck_assert_int_ge(ret, 0); /* Should not be 0 until patch is over*/
        if (ret == 0)
            break;
        p_written += ret;
    }
    ck_assert_int_gt(p_written, 0); /* Should not be 0 */

    printf("patch size: %u\n", p_written);
    ret = wb_patch_init(&patch_ctx, src_a, SRC_SIZE, patch, p_written);
    ck_assert_int_eq(ret, 0);

    /* Apply the patch */
    for (i = 0; i < SRC_SIZE;)
    {
        ret = wb_patch(&patch_ctx, patched_dst + i, DELTA_BLOCK_SIZE);
        ck_assert_int_ge(ret, 0); /* Should not be 0 until patch is over*/
        if (ret == 0)
            break;
        i += ret;
    }
    ck_assert_int_gt(i, 0); /* Should not be 0 */
    ck_assert_int_eq(i, SRC_SIZE); // The patched length should match the buffer size

    /* Verify that the patched destination matches src_b */
    for (int i = 0; i < SRC_SIZE; ++i) {
        ck_assert_uint_eq(patched_dst[i], src_b[i]);
    }
}
END_TEST


Suite *patch_diff_suite(void)
{
    Suite *s;
    TCase *tc_wolfboot_delta;

    s = suite_create("PatchDiff");

    /* Core test case */
    tc_wolfboot_delta = tcase_create("wolfboot-delta");

    tcase_add_test(tc_wolfboot_delta, test_wb_patch_init_invalid);
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_init_invalid);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff);
    suite_add_tcase(s, tc_wolfboot_delta);

    return s;
}

int main(void)
{
    int ret;
    Suite *s;
    SRunner *sr;

    s = patch_diff_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    ret = srunner_ntests_failed(sr);
    srunner_free(sr);

    return ret;
}
