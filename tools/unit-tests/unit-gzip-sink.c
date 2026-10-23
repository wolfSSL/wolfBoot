/* unit-gzip-sink.c
 *
 * unit tests for the sink entry point of the wolfBoot gzip inflater
 * (wolfBoot_gunzip_sink in src/gzip.c), which decompresses to a callback
 * instead of to a buffer so the output need not be addressable.
 *
 * Fixtures come from zlib so the LZ77 window can be chosen; host gzip(1)
 * always uses 32KB and cannot produce the bounded-window streams this
 * decoder exists for.
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
#include <zlib.h>

#include "gzip.h"

/* Pull in the implementation under test directly */
#include "../../src/gzip.c"

#define SINK_MAX_OUT (512U * 1024U)

/* ------------------------------------------------------------------------- */
/* Fixtures                                                                  */
/* ------------------------------------------------------------------------- */

/* Compress with an explicit window so the decoder's window bound can be
 * exercised; level 0 emits stored blocks. */
static uint8_t *gz_deflate_window(const uint8_t *in, uint32_t in_len,
    int window_bits, int level, uint32_t *out_len)
{
    z_stream zs;
    uint8_t *out;
    uLong cap = compressBound(in_len) + 64;

    out = malloc(cap);
    ck_assert_ptr_nonnull(out);
    memset(&zs, 0, sizeof(zs));
    ck_assert_int_eq(deflateInit2(&zs, level, Z_DEFLATED, 16 + window_bits, 8,
        Z_DEFAULT_STRATEGY), Z_OK);
    zs.next_in = (Bytef *)in;
    zs.avail_in = in_len;
    zs.next_out = out;
    zs.avail_out = (uInt)cap;
    ck_assert_int_eq(deflate(&zs, Z_FINISH), Z_STREAM_END);
    ck_assert_int_eq(deflateEnd(&zs), Z_OK);
    *out_len = (uint32_t)zs.total_out;
    return out;
}

/* Deterministic, partly compressible. */
static void fill_mixed(uint8_t *p, uint32_t len, unsigned seed)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (i % 97 < 60) ? (uint8_t)('a' + (i % 23))
                             : (uint8_t)((seed >> 16) & 0xFF);
    }
}

/* ------------------------------------------------------------------------- */
/* Sink that records what it was handed                                      */
/* ------------------------------------------------------------------------- */

struct capture {
    uint8_t *buf;
    uint32_t cap;
    uint32_t next_off;  /* offset the next call must start at */
    uint32_t calls;
    uint32_t fail_at;   /* 0 = never fail */
    int      out_of_order;
    int      overflow;
};

static int capture_write(void *ctx, uint32_t off, const uint8_t *buf,
    uint32_t len)
{
    struct capture *c = (struct capture *)ctx;

    c->calls++;
    if ((c->fail_at != 0) && (c->calls >= c->fail_at)) {
        return -1;
    }
    /* The contract is in order, contiguous and exactly once. */
    if (off != c->next_off) {
        c->out_of_order = 1;
        return -1;
    }
    if ((off + len) > c->cap) {
        c->overflow = 1;
        return -1;
    }
    memcpy(c->buf + off, buf, len);
    c->next_off = off + len;
    return 0;
}

static void capture_init(struct capture *c, uint32_t cap)
{
    memset(c, 0, sizeof(*c));
    c->buf = malloc(cap ? cap : 1);
    ck_assert_ptr_nonnull(c->buf);
    c->cap = cap;
}

static void capture_free(struct capture *c)
{
    free(c->buf);
    c->buf = NULL;
}

/* Inflate `comp` through the sink and compare against `expect`. */
static int sink_roundtrip(const uint8_t *comp, uint32_t comp_len,
    const uint8_t *expect, uint32_t expect_len, int window_bits,
    uint32_t out_max)
{
    uint8_t *scratch;
    struct capture c;
    uint32_t out_len = 0xFFFFFFFFu;
    int ret;

    scratch = malloc(GZIP_SINK_BUF_SIZE(GZIP_SINK_WINDOW_MAX_BITS));
    ck_assert_ptr_nonnull(scratch);
    capture_init(&c, expect_len);

    ret = wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, scratch,
        (uint32_t)window_bits, capture_write, &c, out_max, &out_len);

    if (ret == 0) {
        ck_assert_int_eq(c.out_of_order, 0);
        ck_assert_int_eq(c.overflow, 0);
        ck_assert_uint_eq(out_len, expect_len);
        ck_assert_uint_eq(c.next_off, expect_len);
        if (expect_len > 0) {
            ck_assert_mem_eq(c.buf, expect, expect_len);
        }
    }
    capture_free(&c);
    free(scratch);
    return ret;
}

/* ------------------------------------------------------------------------- */
/* A source that hands the stream over a piece at a time                     */
/* ------------------------------------------------------------------------- */

struct source {
    const uint8_t *data;
    uint32_t len;        /* what the source will actually serve */
    uint32_t claimed;    /* what the decoder is told the stream is */
    uint32_t calls;
    uint32_t fail_at;    /* 0 = never fail */
    uint32_t next_off;   /* reads must not go backwards */
    int      out_of_order;
    int      over_read;
};

static int source_read(void *ctx, uint32_t off, uint8_t *buf, uint32_t len)
{
    struct source *src = (struct source *)ctx;

    src->calls++;
    if ((src->fail_at != 0) && (src->calls >= src->fail_at)) {
        return -1;
    }
    if (off < src->next_off) {
        src->out_of_order = 1;
        return -1;
    }
    if ((off + len) > src->len) {
        src->over_read = 1;
        return -1;
    }
    memcpy(buf, src->data + off, len);
    src->next_off = off;
    return 0;
}

/* Inflate through a reader and compare, as sink_roundtrip() does in place. */
static int reader_roundtrip(const uint8_t *comp, uint32_t comp_len,
    const uint8_t *expect, uint32_t expect_len, int window_bits,
    uint32_t claimed, uint32_t fail_at)
{
    uint8_t *scratch;
    struct capture c;
    struct source src;
    uint32_t out_len = 0xFFFFFFFFu;
    int ret;

    scratch = malloc(GZIP_SINK_BUF_SIZE(GZIP_SINK_WINDOW_MAX_BITS));
    ck_assert_ptr_nonnull(scratch);
    capture_init(&c, expect_len);
    memset(&src, 0, sizeof(src));
    src.data = comp;
    src.len = comp_len;
    src.claimed = (claimed == 0) ? comp_len : claimed;
    src.fail_at = fail_at;

    ret = wolfBoot_gunzip_sink(NULL, src.claimed, source_read, &src,
        scratch, (uint32_t)window_bits, capture_write, &c, SINK_MAX_OUT,
        &out_len);

    ck_assert_int_eq(src.out_of_order, 0);
    ck_assert_int_eq(src.over_read, 0);
    if (ret == 0) {
        ck_assert_uint_eq(out_len, expect_len);
        if (expect_len > 0) {
            ck_assert_mem_eq(c.buf, expect, expect_len);
        }
    }
    capture_free(&c);
    free(scratch);
    return ret;
}

/* ------------------------------------------------------------------------- */
/* Positive cases                                                            */
/* ------------------------------------------------------------------------- */

/* The same stream through both entry points must produce the same bytes. */
START_TEST(test_sink_matches_flat_decoder)
{
    static const uint32_t sizes[] = { 0, 1, 300, 5000, 70000, 400000 };
    uint8_t *raw, *comp, *flat;
    uint32_t comp_len, flat_len;
    unsigned si;
    int bits;

    raw = malloc(400000);
    flat = malloc(400000);
    ck_assert_ptr_nonnull(raw);
    ck_assert_ptr_nonnull(flat);
    fill_mixed(raw, 400000, 1);

    for (bits = GZIP_SINK_WINDOW_MIN_BITS; bits <= GZIP_SINK_WINDOW_MAX_BITS;
            bits++) {
        for (si = 0; si < (sizeof(sizes) / sizeof(sizes[0])); si++) {
            comp = gz_deflate_window(raw, sizes[si], bits, 9, &comp_len);

            ck_assert_int_eq(wolfBoot_gunzip(comp, comp_len, flat, 400000,
                &flat_len), 0);
            ck_assert_uint_eq(flat_len, sizes[si]);

            ck_assert_int_eq(sink_roundtrip(comp, comp_len, flat, flat_len,
                bits, SINK_MAX_OUT), 0);
            free(comp);
        }
    }
    free(flat);
    free(raw);
}
END_TEST

/* A stored block can be 64KB, larger than the scratch buffer, so it is
 * drained mid-copy rather than bounded up front. */
START_TEST(test_sink_stored_blocks)
{
    uint8_t *raw, *comp;
    uint32_t comp_len, i;
    unsigned seed = 7;

    raw = malloc(300000);
    ck_assert_ptr_nonnull(raw);
    for (i = 0; i < 300000; i++) {
        seed = seed * 1103515245u + 12345u;
        raw[i] = (uint8_t)(seed >> 16);
    }
    comp = gz_deflate_window(raw, 300000, GZIP_SINK_WINDOW_MIN_BITS, 0,
        &comp_len);

    ck_assert_int_eq(sink_roundtrip(comp, comp_len, raw, 300000,
        GZIP_SINK_WINDOW_MIN_BITS, SINK_MAX_OUT), 0);

    free(comp);
    free(raw);
}
END_TEST

/* Matches reaching a full window back must still resolve once the window
 * has started sliding. */
START_TEST(test_sink_max_distance_matches)
{
    uint8_t *raw, *comp;
    uint32_t comp_len, i;

    raw = malloc(300000);
    ck_assert_ptr_nonnull(raw);
    for (i = 0; i < 300000; i++) {
        raw[i] = (uint8_t)(i % 251);
    }
    comp = gz_deflate_window(raw, 300000, GZIP_SINK_WINDOW_MAX_BITS, 9,
        &comp_len);

    ck_assert_int_eq(sink_roundtrip(comp, comp_len, raw, 300000,
        GZIP_SINK_WINDOW_MAX_BITS, SINK_MAX_OUT), 0);

    free(comp);
    free(raw);
}
END_TEST

/* ------------------------------------------------------------------------- */
/* Negative cases                                                            */
/* ------------------------------------------------------------------------- */

/* The scratch buffer holds two windows, so a stream using a larger window
 * than declared could be decoded by accident. It must be refused instead,
 * because the manifest's window is what sized this build's buffer. */
START_TEST(test_sink_rejects_distance_beyond_window)
{
    uint8_t *raw, *comp;
    uint32_t comp_len, i;
    unsigned seed = 99;

    raw = malloc(28000);
    ck_assert_ptr_nonnull(raw);
    for (i = 0; i < 28000; i++) {
        seed = seed * 1103515245u + 12345u;
        raw[i] = (uint8_t)(seed >> 16);
    }
    /* Repeat an incompressible run 20000 bytes on, which can only be coded
     * as a distance far beyond a 4KB window. */
    memcpy(raw + 20000, raw, 8000);
    comp = gz_deflate_window(raw, 28000, GZIP_SINK_WINDOW_MAX_BITS, 9,
        &comp_len);

    ck_assert_int_eq(sink_roundtrip(comp, comp_len, raw, 28000,
        GZIP_SINK_WINDOW_MAX_BITS, SINK_MAX_OUT), 0);
    ck_assert_int_eq(sink_roundtrip(comp, comp_len, raw, 28000,
        GZIP_SINK_WINDOW_MIN_BITS, SINK_MAX_OUT), WOLFBOOT_GZIP_E_DISTANCE);

    free(comp);
    free(raw);
}
END_TEST

/* out_max is the capacity of the real destination, so it is enforced even
 * though the decoder never holds the whole output. */
START_TEST(test_sink_out_max_enforced)
{
    uint8_t *raw, *comp;
    uint32_t comp_len;

    raw = malloc(100000);
    ck_assert_ptr_nonnull(raw);
    fill_mixed(raw, 100000, 3);
    comp = gz_deflate_window(raw, 100000, 14, 9, &comp_len);

    ck_assert_int_eq(sink_roundtrip(comp, comp_len, raw, 100000, 14, 100000),
        0);
    ck_assert_int_eq(sink_roundtrip(comp, comp_len, raw, 100000, 14, 99999),
        WOLFBOOT_GZIP_E_OUTPUT);

    free(comp);
    free(raw);
}
END_TEST

START_TEST(test_sink_rejects_corrupt_and_truncated)
{
    uint8_t *raw, *comp;
    uint32_t comp_len;

    raw = malloc(50000);
    ck_assert_ptr_nonnull(raw);
    fill_mixed(raw, 50000, 5);

    comp = gz_deflate_window(raw, 50000, 14, 9, &comp_len);
    ck_assert_int_ne(sink_roundtrip(comp, comp_len - 9, raw, 50000, 14,
        SINK_MAX_OUT), 0);
    comp[comp_len / 2] ^= 0x40;
    ck_assert_int_ne(sink_roundtrip(comp, comp_len, raw, 50000, 14,
        SINK_MAX_OUT), 0);

    free(comp);
    free(raw);
}
END_TEST

/* A sink that cannot take the bytes must abort the inflate, not be ignored. */
START_TEST(test_sink_write_failure_propagates)
{
    uint8_t *raw, *comp, *scratch;
    struct capture c;
    uint32_t comp_len, out_len;

    raw = malloc(200000);
    ck_assert_ptr_nonnull(raw);
    fill_mixed(raw, 200000, 11);
    comp = gz_deflate_window(raw, 200000, 12, 9, &comp_len);
    scratch = malloc(GZIP_SINK_BUF_SIZE(12));
    ck_assert_ptr_nonnull(scratch);

    capture_init(&c, 200000);
    c.fail_at = 2;
    ck_assert_int_eq(wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, scratch,
        12, capture_write, &c, SINK_MAX_OUT, &out_len),
        WOLFBOOT_GZIP_E_OUTPUT);
    capture_free(&c);

    free(scratch);
    free(comp);
    free(raw);
}
END_TEST

START_TEST(test_sink_param_validation)
{
    uint8_t *raw, *comp, *scratch;
    struct capture c;
    uint32_t comp_len, out_len;

    raw = malloc(1000);
    ck_assert_ptr_nonnull(raw);
    fill_mixed(raw, 1000, 13);
    comp = gz_deflate_window(raw, 1000, 14, 9, &comp_len);
    scratch = malloc(GZIP_SINK_BUF_SIZE(GZIP_SINK_WINDOW_MAX_BITS));
    ck_assert_ptr_nonnull(scratch);
    capture_init(&c, 1000);

    ck_assert_int_eq(wolfBoot_gunzip_sink(NULL, comp_len, NULL, NULL, scratch,
        14, capture_write, &c, SINK_MAX_OUT, &out_len),
        WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, NULL,
        14, capture_write, &c, SINK_MAX_OUT, &out_len),
        WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, scratch,
        14, NULL, &c, SINK_MAX_OUT, &out_len), WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, scratch,
        14, capture_write, &c, SINK_MAX_OUT, NULL),
        WOLFBOOT_GZIP_E_PARAM);
    /* Outside the window range the scratch buffer was sized for. */
    ck_assert_int_eq(wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, scratch,
        GZIP_SINK_WINDOW_MIN_BITS - 1, capture_write, &c, SINK_MAX_OUT,
        &out_len), WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip_sink(comp, comp_len, NULL, NULL, scratch,
        GZIP_SINK_WINDOW_MAX_BITS + 1, capture_write, &c, SINK_MAX_OUT,
        &out_len), WOLFBOOT_GZIP_E_PARAM);

    capture_free(&c);
    free(scratch);
    free(comp);
    free(raw);
}
END_TEST

/* A stream pulled in through a callback must decode to the same bytes as
 * the same stream read in place. This is the net for the input refactor:
 * the parser's bounds now mean "left in the stream", not "left in the
 * buffer", and getting one wrong truncates instead of rejecting. */
START_TEST(test_reader_matches_in_place)
{
    static const uint32_t sizes[] = { 0, 1, 300, 5000, 70000, 400000 };
    uint8_t *raw, *comp, *flat;
    uint32_t comp_len, flat_len;
    unsigned si;
    int bits;

    raw = malloc(400000);
    flat = malloc(400000);
    ck_assert_ptr_nonnull(raw);
    ck_assert_ptr_nonnull(flat);
    fill_mixed(raw, 400000, 21);

    for (bits = GZIP_SINK_WINDOW_MIN_BITS; bits <= GZIP_SINK_WINDOW_MAX_BITS;
            bits++) {
        for (si = 0; si < (sizeof(sizes) / sizeof(sizes[0])); si++) {
            comp = gz_deflate_window(raw, sizes[si], bits, 9, &comp_len);
            ck_assert_int_eq(wolfBoot_gunzip(comp, comp_len, flat, 400000,
                &flat_len), 0);
            ck_assert_int_eq(reader_roundtrip(comp, comp_len, flat, flat_len,
                bits, 0, 0), 0);
            free(comp);
        }
    }
    free(flat);
    free(raw);
}
END_TEST

/* Stored blocks are copied straight from the input, so they exercise the
 * refill inside the copy rather than through the bit reader. */
START_TEST(test_reader_stored_blocks)
{
    uint8_t *raw, *comp;
    uint32_t comp_len, i;
    unsigned seed = 31;

    raw = malloc(300000);
    ck_assert_ptr_nonnull(raw);
    for (i = 0; i < 300000; i++) {
        seed = seed * 1103515245u + 12345u;
        raw[i] = (uint8_t)(seed >> 16);
    }
    comp = gz_deflate_window(raw, 300000, GZIP_SINK_WINDOW_MIN_BITS, 0,
        &comp_len);
    ck_assert_int_eq(reader_roundtrip(comp, comp_len, raw, 300000,
        GZIP_SINK_WINDOW_MIN_BITS, 0, 0), 0);
    free(comp);
    free(raw);
}
END_TEST

/* A source that stops early, or that was described as longer than it is,
 * must be rejected rather than yielding a short image. */
START_TEST(test_reader_truncation_is_rejected)
{
    uint8_t *raw, *comp;
    uint32_t comp_len;

    raw = malloc(200000);
    ck_assert_ptr_nonnull(raw);
    fill_mixed(raw, 200000, 23);
    comp = gz_deflate_window(raw, 200000, 14, 9, &comp_len);

    /* The reader fails part way through. */
    ck_assert_int_ne(reader_roundtrip(comp, comp_len, raw, 200000, 14, 0, 3),
        0);
    /* The stream is declared shorter than it is. */
    ck_assert_int_ne(reader_roundtrip(comp, comp_len, raw, 200000, 14,
        comp_len - 64, 0), 0);

    free(comp);
    free(raw);
}
END_TEST

/* Alternating compressible and incompressible runs make deflate emit both
 * Huffman and stored blocks, so the stored decoder and the block driver each
 * read s->in_end straight after the Huffman decoder returned. That is where
 * a cursor the decoder failed to write back shows up. */
START_TEST(test_reader_mixed_block_types)
{
    uint8_t *raw, *comp;
    uint32_t comp_len, i;
    unsigned seed = 77;
    int bits;

    raw = malloc(200000);
    ck_assert_ptr_nonnull(raw);
    for (i = 0; i < 200000; i++) {
        if ((i / 8192) % 2 == 0) {
            raw[i] = (uint8_t)('a' + (i % 3));
        }
        else {
            seed = seed * 1103515245u + 12345u;
            raw[i] = (uint8_t)(seed >> 16);
        }
    }
    for (bits = GZIP_SINK_WINDOW_MIN_BITS; bits <= GZIP_SINK_WINDOW_MAX_BITS;
            bits++) {
        comp = gz_deflate_window(raw, 200000, bits, 9, &comp_len);
        ck_assert_int_eq(reader_roundtrip(comp, comp_len, raw, 200000, bits,
            0, 0), 0);
        free(comp);
        /* Level 0 is all stored blocks, which refill inside the copy. */
        comp = gz_deflate_window(raw, 200000, bits, 0, &comp_len);
        ck_assert_int_eq(reader_roundtrip(comp, comp_len, raw, 200000, bits,
            0, 0), 0);
        free(comp);
    }
    free(raw);
}
END_TEST

START_TEST(test_reader_param_validation)
{
    uint8_t *scratch;
    struct capture c;
    struct source src;
    uint32_t out_len;
    uint8_t dummy[8];

    scratch = malloc(GZIP_SINK_BUF_SIZE(GZIP_SINK_WINDOW_MAX_BITS));
    ck_assert_ptr_nonnull(scratch);
    capture_init(&c, 8);
    memset(&src, 0, sizeof(src));
    memset(dummy, 0, sizeof(dummy));

    /* Neither input form, and both at once, are equally wrong. */
    ck_assert_int_eq(wolfBoot_gunzip_sink(NULL, 8, NULL, NULL, scratch, 14,
        capture_write, &c, SINK_MAX_OUT, &out_len), WOLFBOOT_GZIP_E_PARAM);
    ck_assert_int_eq(wolfBoot_gunzip_sink(dummy, 8, source_read, &src,
        scratch, 14, capture_write, &c, SINK_MAX_OUT, &out_len),
        WOLFBOOT_GZIP_E_PARAM);

    capture_free(&c);
    free(scratch);
}
END_TEST

/* ------------------------------------------------------------------------- */

static Suite *gzip_sink_suite(void)
{
    Suite *s = suite_create("wolfBoot-gzip-sink");
    TCase *tc = tcase_create("sink");

    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_sink_matches_flat_decoder);
    tcase_add_test(tc, test_sink_stored_blocks);
    tcase_add_test(tc, test_sink_max_distance_matches);
    tcase_add_test(tc, test_sink_rejects_distance_beyond_window);
    tcase_add_test(tc, test_sink_out_max_enforced);
    tcase_add_test(tc, test_sink_rejects_corrupt_and_truncated);
    tcase_add_test(tc, test_sink_write_failure_propagates);
    tcase_add_test(tc, test_sink_param_validation);
    tcase_add_test(tc, test_reader_matches_in_place);
    tcase_add_test(tc, test_reader_stored_blocks);
    tcase_add_test(tc, test_reader_truncation_is_rejected);
    tcase_add_test(tc, test_reader_mixed_block_types);
    tcase_add_test(tc, test_reader_param_validation);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    SRunner *sr = srunner_create(gzip_sink_suite());
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed ? 1 : 0;
}
