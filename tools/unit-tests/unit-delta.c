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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "delta.h"
#define WC_RSA_BLINDING
#include "delta.c"

#define SRC_SIZE 4096
#define PATCH_SIZE 8192
#define DST_SIZE 4096
#define DIFF_SIZE 8192
#define DELTA_OFFSET_LIMIT (1U << 24)


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
    int sector_size_ret;
    size_t sector_size;
    int ret;

    sector_size_ret = wb_diff_get_sector_size();
    ck_assert_int_gt(sector_size_ret, BLOCK_HDR_SIZE);
    sector_size = (size_t)sector_size_ret;

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

START_TEST(test_wb_diff_preserves_trailing_header_margin_for_escape)
{
    WB_DIFF_CTX diff_ctx;
    uint8_t src_a[64] = {0};
    uint8_t src_b[64] = {0};
    uint8_t patch[BLOCK_HDR_SIZE + 2] = {0};
    int ret;

    src_b[0] = ESC;

    ret = wb_diff_init(&diff_ctx, src_a, sizeof(src_a), src_b, 1);
    ck_assert_int_eq(ret, 0);

    ret = wb_diff(&diff_ctx, patch, BLOCK_HDR_SIZE + 1);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(patch[0], 0);
}
END_TEST

START_TEST(test_wb_diff_preserves_main_loop_header_margin_for_escape)
{
    WB_DIFF_CTX diff_ctx;
    uint8_t src_a[64] = {0};
    uint8_t src_b[64] = {0};
    uint8_t patch[BLOCK_HDR_SIZE + 2] = {0};
    int ret;

    memset(src_b, 0x5a, BLOCK_HDR_SIZE + 1);
    src_b[0] = ESC;

    ret = wb_diff_init(&diff_ctx, src_a, sizeof(src_a), src_b, BLOCK_HDR_SIZE + 1);
    ck_assert_int_eq(ret, 0);

    ret = wb_diff(&diff_ctx, patch, BLOCK_HDR_SIZE + 1);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(patch[0], 0);
}
END_TEST

START_TEST(test_wb_diff_rejects_match_offsets_beyond_24_bits)
{
    WB_DIFF_CTX diff_ctx;
    uint8_t *src_a;
    uint8_t src_b[BLOCK_HDR_SIZE + 1] = {0};
    uint8_t patch[DELTA_BLOCK_SIZE] = {0};
    size_t src_a_size = DELTA_OFFSET_LIMIT + BLOCK_HDR_SIZE;
    int ret;

    src_a = calloc(1, src_a_size);
    ck_assert_ptr_nonnull(src_a);

    memset(src_a + DELTA_OFFSET_LIMIT, 0x5a, BLOCK_HDR_SIZE);
    memset(src_b, 0x5a, BLOCK_HDR_SIZE);

    ret = wb_diff_init(&diff_ctx, src_a, src_a_size, src_b, sizeof(src_b));
    ck_assert_int_eq(ret, 0);

    ret = wb_diff(&diff_ctx, patch, sizeof(patch));
    ck_assert_int_eq(ret, -1);

    free(src_a);
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
    if (size > 100) {
        src_b[100] = src_a[100] + 1;
    }
    if (size > 200) {
        src_b[200] = src_a[200] + 2;
    }

    /* 10-bytes difference across two blocks */
    for (int i = 1020; i < 1040 && (size_t)i < size; ++i) {
        src_b[i] = src_a[i] + 3;
    }

    if (size > 510) {
        src_a[510] = ESC;
    }
    if (size > 1022) {
        src_b[1022] = ESC;
    }

}

static uint8_t pattern_byte(uint32_t seed, size_t index)
{
    uint32_t value = seed ^ (uint32_t)index;

    value *= 1664525U;
    value += 1013904223U;
    value ^= (uint32_t)(index >> 8);
    value ^= (uint32_t)(index >> 16);

    if ((uint8_t)value == ESC) {
        value ^= 0x55U;
    }
    return (uint8_t)value;
}

static void fill_pattern(uint8_t *dst, size_t size, uint32_t seed)
{
    size_t i;

    for (i = 0; i < size; ++i) {
        dst[i] = pattern_byte(seed, i);
    }
}

static uint32_t run_roundtrip_case(const uint8_t *src_a, uint32_t size_a,
    const uint8_t *src_b, uint32_t size_b, uint32_t patch_capacity)
{
    WB_DIFF_CTX diff_ctx;
    WB_PATCH_CTX patch_ctx;
    uint8_t *src_a_copy;
    uint8_t *patch;
    uint8_t *patched_dst;
    uint8_t *new_patch;
    uint8_t block[DELTA_BLOCK_SIZE] = {0};
    uint32_t p_written = 0;
    uint32_t dst_written = 0;
    int ret;

    src_a_copy = malloc(size_a);
    patch = malloc(patch_capacity);
    patched_dst = malloc(size_b);
    ck_assert_ptr_nonnull(src_a_copy);
    ck_assert_ptr_nonnull(patch);
    ck_assert_ptr_nonnull(patched_dst);

    memcpy(src_a_copy, src_a, size_a);

    ret = wb_diff_init(&diff_ctx, src_a_copy, size_a, (uint8_t *)src_b, size_b);
    ck_assert_int_eq(ret, 0);

    for (;;) {
        uint32_t remaining = patch_capacity - p_written;

        ck_assert_uint_ge(remaining, BLOCK_HDR_SIZE);
        ret = wb_diff(&diff_ctx, patch + p_written,
            remaining > DELTA_BLOCK_SIZE ? DELTA_BLOCK_SIZE : remaining);
        ck_assert_int_ge(ret, 0);
        if (ret == 0) {
            if (diff_ctx.off_b < size_b) {
                patch_capacity += DELTA_BLOCK_SIZE;
                new_patch = realloc(patch, patch_capacity);
                ck_assert_ptr_nonnull(new_patch);
                patch = new_patch;
                continue;
            }
            break;
        }
        p_written += (uint32_t)ret;
        ck_assert_uint_le(p_written, patch_capacity);
    }

    ck_assert_uint_eq(diff_ctx.off_b, size_b);
    ck_assert_uint_gt(p_written, 0);

    ret = wb_patch_init(&patch_ctx, src_a_copy, size_a, patch, p_written);
    ck_assert_int_eq(ret, 0);

    for (;;) {
        ret = wb_patch(&patch_ctx, block, sizeof(block));
        ck_assert_int_ge(ret, 0);
        if (ret == 0) {
            break;
        }
        ck_assert_uint_le(dst_written + (uint32_t)ret, size_b);
        memcpy(patched_dst + dst_written, block, (uint32_t)ret);
        dst_written += (uint32_t)ret;
    }

    ck_assert_uint_eq(dst_written, size_b);
    ck_assert_int_eq(memcmp(patched_dst, src_b, size_b), 0);

    free(patched_dst);
    free(patch);
    free(src_a_copy);
    return p_written;
}

START_TEST(test_wb_patch_and_diff)
{
    uint8_t src_a[SRC_SIZE];
    uint8_t src_b[SRC_SIZE];
    uint32_t p_written = 0;

    initialize_buffers(src_a, src_b, SRC_SIZE);

    p_written = run_roundtrip_case(src_a, SRC_SIZE, src_b, SRC_SIZE, PATCH_SIZE);
    printf("patch size: %u\n", p_written);
}
END_TEST

START_TEST(test_wb_patch_and_diff_identical_images)
{
    uint8_t src_a[SRC_SIZE];
    uint32_t p_written;

    fill_pattern(src_a, sizeof(src_a), 0x12345678U);

    p_written = run_roundtrip_case(src_a, sizeof(src_a), src_a, sizeof(src_a),
        PATCH_SIZE);
    ck_assert_uint_lt(p_written, 64);
}
END_TEST

START_TEST(test_wb_patch_and_diff_completely_different_images)
{
    uint8_t src_a[SRC_SIZE];
    uint8_t src_b[SRC_SIZE];

    fill_pattern(src_a, sizeof(src_a), 0x11111111U);
    fill_pattern(src_b, sizeof(src_b), 0x22222222U);

    (void)run_roundtrip_case(src_a, sizeof(src_a), src_b, sizeof(src_b),
        sizeof(src_b) + DELTA_BLOCK_SIZE);
}
END_TEST

START_TEST(test_wb_patch_and_diff_all_escape_images)
{
    uint8_t src_a[SRC_SIZE];
    uint8_t src_b[SRC_SIZE];

    memset(src_a, ESC, sizeof(src_a));
    memset(src_b, ESC, sizeof(src_b));

    (void)run_roundtrip_case(src_a, sizeof(src_a), src_b, sizeof(src_b),
        PATCH_SIZE);
}
END_TEST

START_TEST(test_wb_patch_and_diff_multi_sector_images)
{
    int sector_size_ret;
    uint32_t size;
    uint8_t *src_a;
    uint8_t *src_b;

    sector_size_ret = wb_diff_get_sector_size();
    ck_assert_int_gt(sector_size_ret, BLOCK_HDR_SIZE);
    size = (uint32_t)(3 * sector_size_ret + 37);

    src_a = malloc(size);
    src_b = malloc(size);
    ck_assert_ptr_nonnull(src_a);
    ck_assert_ptr_nonnull(src_b);

    fill_pattern(src_a, size, 0xA5A5A5A5U);
    memcpy(src_b, src_a, size);
    src_b[sector_size_ret - 1] ^= 0x11;
    src_b[sector_size_ret] ^= 0x22;
    src_b[(2 * sector_size_ret) + 5] = ESC;

    (void)run_roundtrip_case(src_a, size, src_b, size, size + DELTA_BLOCK_SIZE);

    free(src_b);
    free(src_a);
}
END_TEST

START_TEST(test_wb_diff_get_sector_size_rejects_values_above_16bit)
{
    const char *saved = getenv("WOLFBOOT_SECTOR_SIZE");
    char *saved_copy = saved ? strdup(saved) : NULL;
    pid_t pid;
    int status = 0;

    ck_assert_int_eq(setenv("WOLFBOOT_SECTOR_SIZE", "0x20000", 1), 0);
    pid = fork();
    ck_assert_int_ne(pid, -1);

    if (pid == 0) {
        (void)wb_diff_get_sector_size();
        _exit(0);
    }

    ck_assert_int_eq(waitpid(pid, &status, 0), pid);
    ck_assert_int_eq(WIFEXITED(status), 1);
    ck_assert_int_eq(WEXITSTATUS(status), 6);

    if (saved_copy != NULL) {
        ck_assert_int_eq(setenv("WOLFBOOT_SECTOR_SIZE", saved_copy, 1), 0);
        free(saved_copy);
    }
    else {
        ck_assert_int_eq(unsetenv("WOLFBOOT_SECTOR_SIZE"), 0);
    }
}
END_TEST

START_TEST(test_wb_patch_and_diff_size_changing_update)
{
    uint8_t src_a[2048];
    uint8_t src_b[3077];

    fill_pattern(src_a, sizeof(src_a), 0x31415926U);
    fill_pattern(src_b, sizeof(src_b), 0x27182818U);
    memcpy(src_b + 512, src_a, sizeof(src_a));
    src_b[0] = ESC;
    src_b[sizeof(src_b) - 1] ^= 0x5A;

    (void)run_roundtrip_case(src_a, sizeof(src_a), src_b, sizeof(src_b),
        sizeof(src_b) + DELTA_BLOCK_SIZE);
}
END_TEST

START_TEST(test_wb_patch_and_diff_single_byte_difference)
{
    uint8_t src_a[SRC_SIZE];
    uint8_t src_b[SRC_SIZE];
    uint32_t p_written;

    fill_pattern(src_a, sizeof(src_a), 0x0BADB002U);
    memcpy(src_b, src_a, sizeof(src_a));
    src_b[1537] ^= 0x01;

    p_written = run_roundtrip_case(src_a, sizeof(src_a), src_b, sizeof(src_b),
        PATCH_SIZE);
    ck_assert_uint_lt(p_written, 64);
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
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_preserves_trailing_header_margin_for_escape);
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_preserves_main_loop_header_margin_for_escape);
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_rejects_match_offsets_beyond_24_bits);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff_identical_images);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff_completely_different_images);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff_all_escape_images);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff_multi_sector_images);
    tcase_add_test(tc_wolfboot_delta, test_wb_diff_get_sector_size_rejects_values_above_16bit);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff_size_changing_update);
    tcase_add_test(tc_wolfboot_delta, test_wb_patch_and_diff_single_byte_difference);
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
