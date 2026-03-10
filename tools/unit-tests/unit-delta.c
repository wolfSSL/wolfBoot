/* unit-delta.c
 *
 * unit tests for delta updates module
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include <stdlib.h>

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

START_TEST(test_wb_patch_src_bounds_invalid)
{
    WB_PATCH_CTX patch_ctx;
    uint8_t src[SRC_SIZE] = {0};
    uint8_t patch[PATCH_SIZE] = {0};
    uint8_t dst[DELTA_BLOCK_SIZE] = {0};
    int ret;

    /* ESC + header with src_off beyond src_size */
    patch[0] = ESC;
    patch[1] = 0x00; /* off[0] */
    patch[2] = 0x10; /* off[1] -> 0x0010FF */
    patch[3] = 0xFF; /* off[2] */
    patch[4] = 0x00; /* sz[0] */
    patch[5] = 0x10; /* sz[1] -> 16 */

    ret = wb_patch_init(&patch_ctx, src, SRC_SIZE, patch, BLOCK_HDR_SIZE);
    ck_assert_int_eq(ret, 0);

    ret = wb_patch(&patch_ctx, dst, sizeof(dst));
    ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(test_wb_patch_resume_bounds_invalid)
{
    WB_PATCH_CTX patch_ctx;
    uint8_t src[SRC_SIZE] = {0};
    uint8_t patch[PATCH_SIZE] = {0};
    uint8_t dst[DELTA_BLOCK_SIZE] = {0};
    int ret;

    ret = wb_patch_init(&patch_ctx, src, SRC_SIZE, patch, BLOCK_HDR_SIZE);
    ck_assert_int_eq(ret, 0);

    patch_ctx.matching = 1;
    patch_ctx.blk_off = SRC_SIZE + 1;
    patch_ctx.blk_sz = 4;

    ret = wb_patch(&patch_ctx, dst, sizeof(dst));
    ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(test_wb_patch_resume_large_len)
{
    WB_PATCH_CTX patch_ctx;
    uint8_t src[SRC_SIZE] = {0};
    uint8_t patch[PATCH_SIZE] = {0};
    uint8_t dst[DST_SIZE] = {0};
    uint32_t len = 70000;
    int ret;

    src[0] = 0xA5;
    ret = wb_patch_init(&patch_ctx, src, SRC_SIZE, patch, BLOCK_HDR_SIZE);
    ck_assert_int_eq(ret, 0);

    patch_ctx.matching = 1;
    patch_ctx.blk_off = 0;
    patch_ctx.blk_sz = len;

    ret = wb_patch(&patch_ctx, dst, len);
    ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(test_wb_patch_trailing_escape_invalid)
{
    WB_PATCH_CTX patch_ctx;
    uint8_t src[SRC_SIZE] = {0};
    uint8_t patch[1] = {ESC};
    uint8_t dst[DELTA_BLOCK_SIZE] = {0};
    int ret;

    ret = wb_patch_init(&patch_ctx, src, SRC_SIZE, patch, sizeof(patch));
    ck_assert_int_eq(ret, 0);

    ret = wb_patch(&patch_ctx, dst, sizeof(dst));
    ck_assert_int_eq(ret, -1);
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

START_TEST(test_wb_diff_match_extends_to_src_b_end)
{
    WB_DIFF_CTX diff_ctx;
    uint8_t src_a[BLOCK_HDR_SIZE + 2] = {0};
    uint8_t src_b[BLOCK_HDR_SIZE + 1] = {0};
    uint8_t patch[DELTA_BLOCK_SIZE] = {0};
    int ret;

    memset(src_a, 0x41, sizeof(src_a));
    memset(src_b, 0x41, sizeof(src_b));

    ret = wb_diff_init(&diff_ctx, src_a, sizeof(src_a), src_b, sizeof(src_b));
    ck_assert_int_eq(ret, 0);

    ret = wb_diff(&diff_ctx, patch, sizeof(patch));
    ck_assert_int_gt(ret, 0);
}
END_TEST

START_TEST(test_wb_diff_self_match_extends_to_src_b_end)
{
    WB_DIFF_CTX diff_ctx;
    uint8_t *src_a;
    uint8_t *src_b;
    uint8_t patch[DELTA_BLOCK_SIZE] = {0};
    uint32_t sector_size;
    int ret;

    sector_size = wb_diff_get_sector_size();
    ck_assert_uint_gt(sector_size, BLOCK_HDR_SIZE);

    src_a = calloc(1, sector_size + BLOCK_HDR_SIZE);
    src_b = calloc(1, sector_size + BLOCK_HDR_SIZE + 1);
    ck_assert_ptr_nonnull(src_a);
    ck_assert_ptr_nonnull(src_b);

    ret = wb_diff_init(&diff_ctx, src_a, sector_size + BLOCK_HDR_SIZE,
            src_b, sector_size + BLOCK_HDR_SIZE + 1);
    ck_assert_int_eq(ret, 0);

    memset(src_a + sector_size, 0x11, BLOCK_HDR_SIZE);
    memset(src_b, 0x22, BLOCK_HDR_SIZE + 1);
    memset(src_b + sector_size, 0x22, BLOCK_HDR_SIZE + 1);
    diff_ctx.off_b = sector_size;

    ret = wb_diff(&diff_ctx, patch, sizeof(patch));
    ck_assert_int_gt(ret, 0);

    free(src_a);
    free(src_b);
}
END_TEST

static void initialize_buffers(uint8_t *src_a, uint8_t *src_b, size_t size)
{
    uint32_t pseudo_rand = 0;
    size_t i;

    for (i = 0; i < size; ++i) {
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

    src_a[510] = ESC;
    src_b[1022] = ESC;

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


    initialize_buffers(src_a, src_b, SRC_SIZE);

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
    for (i = 0; i < SRC_SIZE; ++i) {
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
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_src_bounds_invalid);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_resume_bounds_invalid);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_resume_large_len);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_trailing_escape_invalid);
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_match_extends_to_src_b_end);
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_self_match_extends_to_src_b_end);
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
