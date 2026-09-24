/* unit-nrf54l-uart-write.c
 *
 * Regression test for F-12884: uart_write_device() in hal/nrf54l.c
 * stopped its conversion loop when the 128-byte buffer filled and did
 * a single raw write, silently truncating any longer input (earlier
 * still when newline expansion consumed the spare slot). The writer
 * must flush the buffer when full and keep converting the rest.
 *
 * The real function is extracted by the Makefile; uart_write_raw() is
 * a recording mock.
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
#include <string.h>

#define UART_WRITE_BUF_SIZE 128

/* Recording mock for the DMA raw writer (prototype first: the extracted
 * function calls it before the definition below). */
#define OUT_CAP 8192
static uint8_t g_out[OUT_CAP];
static int g_out_len;
static int g_writes;

static void uart_write_raw(int device, const char* buf, unsigned int sz);

/* The real function from hal/nrf54l.c (extracted by the Makefile). */
#include "nrf54l_uart_write_extract.h"

static void uart_write_raw(int device, const char* buf, unsigned int sz)
{
    (void)device;
    ck_assert_int_le(g_out_len + (int)sz, OUT_CAP);
    memcpy(g_out + g_out_len, buf, sz);
    g_out_len += (int)sz;
    g_writes++;
}

static void setup(void)
{
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;
    g_writes = 0;
}

static void teardown(void)
{
}

/* Expected transform: drop \r, expand \n to \r\n. */
static int build_expected(const char* in, int inlen, uint8_t* out)
{
    int n = 0;
    int i;

    for (i = 0; i < inlen; i++) {
        if (in[i] == '\r')
            continue;
        if (in[i] == '\n')
            out[n++] = '\r';
        out[n++] = (uint8_t)in[i];
    }
    return n;
}

/* A plain 300-byte line: pre-fix everything past byte 128 was dropped. */
START_TEST(test_write_300_no_truncation){
    char in[300];
    uint8_t exp[300];
    int explen;
    int i;

    for (i = 0; i < 300; i++)
        in[i] = (char)('a' + (i % 26));
    explen = build_expected(in, 300, exp);

    uart_write_device(0, in, 300);

    ck_assert_int_eq(g_out_len, explen);
    ck_assert_int_eq(g_writes, 3);
    ck_assert_int_eq(memcmp(g_out, exp, explen), 0);
}
END_TEST

/* 200 newlines: every one expands to CRLF, 400 bytes out. Pre-fix the
 * expansion stopped early at the buffer boundary. */
START_TEST(test_write_newline_expansion)
{
    char in[200];
    uint8_t exp[400];
    int explen;
    int i;

    memset(in, '\n', sizeof(in));
    explen = build_expected(in, 200, exp);

    uart_write_device(0, in, 200);

    ck_assert_int_eq(g_out_len, explen);
    ck_assert_int_eq(memcmp(g_out, exp, explen), 0);
}
END_TEST

/* Mixed content with \r\n pairs straddling the 128-byte boundary: the
 * flush must never split a CRLF pair across writes inconsistently, and
 * the reassembled stream must match the transform exactly. */
START_TEST(test_write_mixed_boundary)
{
    char in[260];
    uint8_t exp[520];
    int explen;
    int i;

    for (i = 0; i < 260; i++)
        in[i] = (i % 13 == 0) ? '\r' : (i % 7 == 0) ? '\n' : (char)('A' + i);
    explen = build_expected(in, 260, exp);

    uart_write_device(0, in, 260);

    ck_assert_int_eq(g_out_len, explen);
    ck_assert_int_eq(memcmp(g_out, exp, explen), 0);
}
END_TEST

/* Short input stays a single write: the common path is unchanged. */
START_TEST(test_write_short_single)
{
    const char* in = "boot ok\n";
    uint8_t exp[32];
    int explen;

    explen = build_expected(in, (int)strlen(in), exp);

    uart_write_device(0, in, (unsigned int)strlen(in));

    ck_assert_int_eq(g_writes, 1);
    ck_assert_int_eq(g_out_len, explen);
    ck_assert_int_eq(memcmp(g_out, exp, explen), 0);
}
END_TEST

Suite *nrf54l_uart_write_suite(void)
{
    Suite *s = suite_create("nrf54l-uart-write");
    TCase *tc = tcase_create("nrf54l-uart-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_300_no_truncation);
    tcase_add_test(tc, test_write_newline_expansion);
    tcase_add_test(tc, test_write_mixed_boundary);
    tcase_add_test(tc, test_write_short_single);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = nrf54l_uart_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
