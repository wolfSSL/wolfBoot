/* unit-gzip.c
 *
 * unit tests for the wolfBoot native gzip inflater (src/gzip.c).
 *
 * Positive cases round-trip a corpus through host gzip(1) and back through
 * wolfBoot_gunzip. Negative cases corrupt or truncate the gzip stream and
 * verify the inflater rejects it with the appropriate error code.
 *
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gzip.h"

/* Pull in the implementation under test directly */
#include "../../src/gzip.c"

/* ------------------------------------------------------------------------- */
/* Helpers: gzip(1) on the host produces our test fixtures                   */
/* ------------------------------------------------------------------------- */

static uint8_t *gz_compress_buf(const uint8_t *in, size_t in_len, size_t *out_len)
{
    char in_path[64], out_path[64];
    char cmd[256];
    FILE *f = NULL;
    long n;
    uint8_t *buf = NULL;

    snprintf(in_path,  sizeof(in_path),  "/tmp/wb-gz-in-%d.bin",  getpid());
    snprintf(out_path, sizeof(out_path), "/tmp/wb-gz-out-%d.gz",  getpid());

    f = fopen(in_path, "wb");
    if (f == NULL) goto cleanup;
    if (in_len > 0) {
        if (fwrite(in, 1, in_len, f) != in_len) {
            fclose(f);
            f = NULL;
            goto cleanup;
        }
    }
    fclose(f);
    f = NULL;

    snprintf(cmd, sizeof(cmd), "gzip -nc %s > %s", in_path, out_path);
    if (system(cmd) != 0) goto cleanup;

    f = fopen(out_path, "rb");
    if (f == NULL) goto cleanup;
    if (fseek(f, 0, SEEK_END) != 0) goto cleanup;
    n = ftell(f);
    if (n < 0) goto cleanup;
    if (fseek(f, 0, SEEK_SET) != 0) goto cleanup;
    buf = (uint8_t*)malloc((size_t)n);
    if (buf == NULL) goto cleanup;
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
        goto cleanup;
    }
    *out_len = (size_t)n;

cleanup:
    if (f != NULL) fclose(f);
    unlink(in_path);
    unlink(out_path);
    return buf;
}

static void roundtrip_check(const uint8_t *input, size_t in_len)
{
    uint8_t *gz_data = NULL;
    size_t gz_len = 0;
    uint8_t *out;
    uint32_t out_len = 0;
    int rc;

    gz_data = gz_compress_buf(input, in_len, &gz_len);
    ck_assert_ptr_nonnull(gz_data);

    out = (uint8_t*)malloc(in_len + 16); /* +16 to make 0-byte case allocable */
    ck_assert_ptr_nonnull(out);

    rc = wolfBoot_gunzip(gz_data, (uint32_t)gz_len,
                         out, (uint32_t)(in_len + 16), &out_len);
    ck_assert_int_eq(rc, 0);
    ck_assert_uint_eq((unsigned)out_len, (unsigned)in_len);
    if (in_len > 0) {
        ck_assert_int_eq(memcmp(out, input, in_len), 0);
    }

    free(out);
    free(gz_data);
}

/* ------------------------------------------------------------------------- */
/* Positive round-trip tests                                                 */
/* ------------------------------------------------------------------------- */

START_TEST(test_roundtrip_empty)
{
    roundtrip_check((const uint8_t*)"", 0);
}
END_TEST

START_TEST(test_roundtrip_short_text)
{
    const char *s = "Hello, World!\n";
    roundtrip_check((const uint8_t*)s, strlen(s));
}
END_TEST

START_TEST(test_roundtrip_zeros)
{
    /* Highly compressible -> exercises long back-references */
    static uint8_t buf[16 * 1024];
    memset(buf, 0, sizeof(buf));
    roundtrip_check(buf, sizeof(buf));
}
END_TEST

START_TEST(test_roundtrip_repeated_text)
{
    /* Exercises fixed-Huffman + back-references */
    static uint8_t buf[8 * 1024];
    const char *t = "The quick brown fox jumps over the lazy dog. ";
    size_t tl = strlen(t);
    size_t off = 0;
    while (off + tl < sizeof(buf)) {
        memcpy(buf + off, t, tl);
        off += tl;
    }
    roundtrip_check(buf, off);
}
END_TEST

START_TEST(test_roundtrip_pseudo_random)
{
    /* Near-incompressible -> exercises dynamic Huffman + literals */
    static uint8_t buf[128 * 1024];
    uint32_t state = 0xDEADBEEFU;
    int i;
    for (i = 0; i < (int)sizeof(buf); i++) {
        state = state * 1103515245U + 12345U;
        buf[i] = (uint8_t)(state >> 16);
    }
    roundtrip_check(buf, sizeof(buf));
}
END_TEST

START_TEST(test_roundtrip_kernel_sized)
{
    /* ~2 MB of structured-but-varied data, similar in nature to a kernel.
     * Catches accumulator / long-running issues. */
    static uint8_t buf[2 * 1024 * 1024];
    int i;
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = (uint8_t)((i * 31) ^ (i >> 4));
    }
    roundtrip_check(buf, sizeof(buf));
}
END_TEST

/* ------------------------------------------------------------------------- */
/* Negative tests                                                            */
/* ------------------------------------------------------------------------- */

START_TEST(test_neg_bad_magic)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[64]; uint32_t out_len = 0;
    int rc;
    const char *s = "abc";

    gz = gz_compress_buf((const uint8_t*)s, 3, &gz_len);
    ck_assert_ptr_nonnull(gz);
    gz[0] ^= 0xFF; /* corrupt magic byte */
    rc = wolfBoot_gunzip(gz, (uint32_t)gz_len, out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_FORMAT);
    free(gz);
}
END_TEST

START_TEST(test_neg_bad_method)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[64]; uint32_t out_len = 0;
    int rc;
    const char *s = "abc";

    gz = gz_compress_buf((const uint8_t*)s, 3, &gz_len);
    ck_assert_ptr_nonnull(gz);
    gz[2] = 9; /* CM != 8 */
    rc = wolfBoot_gunzip(gz, (uint32_t)gz_len, out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_FORMAT);
    free(gz);
}
END_TEST

START_TEST(test_neg_reserved_flags)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[64]; uint32_t out_len = 0;
    int rc;
    const char *s = "abc";

    gz = gz_compress_buf((const uint8_t*)s, 3, &gz_len);
    ck_assert_ptr_nonnull(gz);
    gz[3] |= 0x80; /* set a reserved FLG bit */
    rc = wolfBoot_gunzip(gz, (uint32_t)gz_len, out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_FORMAT);
    free(gz);
}
END_TEST

START_TEST(test_neg_truncated_header)
{
    uint8_t buf[5] = { 0x1F, 0x8B, 0x08, 0x00, 0x00 }; /* < 10 bytes */
    uint8_t out[64]; uint32_t out_len = 0;
    int rc = wolfBoot_gunzip(buf, sizeof(buf), out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_TRUNCATED);
}
END_TEST

START_TEST(test_neg_truncated_stream)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[64]; uint32_t out_len = 0;
    int rc;
    const char *s = "Hello, World";

    gz = gz_compress_buf((const uint8_t*)s, strlen(s), &gz_len);
    ck_assert_ptr_nonnull(gz);
    /* Lop off the trailer + a couple bytes so the deflate body is incomplete */
    rc = wolfBoot_gunzip(gz, (uint32_t)(gz_len - 12),
                         out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_TRUNCATED);
    free(gz);
}
END_TEST

START_TEST(test_neg_bad_crc32)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[64]; uint32_t out_len = 0;
    int rc;
    const char *s = "Hello, World!";

    gz = gz_compress_buf((const uint8_t*)s, strlen(s), &gz_len);
    ck_assert_ptr_nonnull(gz);
    /* Flip a bit in the 4-byte CRC32 (8 bytes from end) */
    gz[gz_len - 8] ^= 0x01;
    rc = wolfBoot_gunzip(gz, (uint32_t)gz_len, out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_CRC32);
    free(gz);
}
END_TEST

START_TEST(test_neg_bad_isize)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[64]; uint32_t out_len = 0;
    int rc;
    const char *s = "Hello, World!";

    gz = gz_compress_buf((const uint8_t*)s, strlen(s), &gz_len);
    ck_assert_ptr_nonnull(gz);
    /* Flip a bit in the 4-byte ISIZE (last 4 bytes) */
    gz[gz_len - 1] ^= 0x80;
    rc = wolfBoot_gunzip(gz, (uint32_t)gz_len, out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_ISIZE);
    free(gz);
}
END_TEST

START_TEST(test_neg_output_overflow)
{
    uint8_t *gz; size_t gz_len;
    uint8_t out[8]; uint32_t out_len = 0;
    int rc;
    /* Compressed bytes will easily exceed 8 raw output bytes when expanded */
    static uint8_t input[1024];
    memset(input, 'A', sizeof(input));

    gz = gz_compress_buf(input, sizeof(input), &gz_len);
    ck_assert_ptr_nonnull(gz);
    rc = wolfBoot_gunzip(gz, (uint32_t)gz_len, out, sizeof(out), &out_len);
    ck_assert_int_eq(rc, WOLFBOOT_GZIP_E_OUTPUT);
    free(gz);
}
END_TEST

START_TEST(test_neg_null_args)
{
    uint8_t buf[16];
    uint32_t out_len;
    ck_assert_int_eq(wolfBoot_gunzip(NULL, 0, buf, sizeof(buf), &out_len),
                     WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip(buf, 0, NULL, sizeof(buf), &out_len),
                     WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip(buf, 0, buf, sizeof(buf), NULL),
                     WOLFBOOT_GZIP_E_PARAM);
}
END_TEST

/* ------------------------------------------------------------------------- */
/* Test runner                                                               */
/* ------------------------------------------------------------------------- */

static Suite *gzip_suite(void)
{
    Suite *s = suite_create("gzip");
    TCase *tc_pos = tcase_create("roundtrip");
    TCase *tc_neg = tcase_create("negative");

    /* The 2 MB test pushes past the default 4-second per-test budget */
    tcase_set_timeout(tc_pos, 30);
    tcase_set_timeout(tc_neg, 10);

    tcase_add_test(tc_pos, test_roundtrip_empty);
    tcase_add_test(tc_pos, test_roundtrip_short_text);
    tcase_add_test(tc_pos, test_roundtrip_zeros);
    tcase_add_test(tc_pos, test_roundtrip_repeated_text);
    tcase_add_test(tc_pos, test_roundtrip_pseudo_random);
    tcase_add_test(tc_pos, test_roundtrip_kernel_sized);

    tcase_add_test(tc_neg, test_neg_bad_magic);
    tcase_add_test(tc_neg, test_neg_bad_method);
    tcase_add_test(tc_neg, test_neg_reserved_flags);
    tcase_add_test(tc_neg, test_neg_truncated_header);
    tcase_add_test(tc_neg, test_neg_truncated_stream);
    tcase_add_test(tc_neg, test_neg_bad_crc32);
    tcase_add_test(tc_neg, test_neg_bad_isize);
    tcase_add_test(tc_neg, test_neg_output_overflow);
    tcase_add_test(tc_neg, test_neg_null_args);

    suite_add_tcase(s, tc_pos);
    suite_add_tcase(s, tc_neg);
    return s;
}

int main(void)
{
    int failed;
    SRunner *sr = srunner_create(gzip_suite());
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed ? 1 : 0;
}
