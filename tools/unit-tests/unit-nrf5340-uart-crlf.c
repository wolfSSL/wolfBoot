/* unit-nrf5340-uart-crlf.c
 *
 * Regression test for F-12883: nrf5340_uart_crlf() (hal/nrf5340_uart.c,
 * extracted from hal/nrf5340.c uart_write) advanced the buffer pointer to
 * the newline instead of the byte after it after emitting CRLF, while
 * shrinking the size as if the newline had been consumed. On multiline input
 * it reprocessed the newline (emitting extra CRLFs) and dropped the text
 * that followed it: "abc\ndef\n" came out as "abc\r\n" plus four stray
 * CRLFs, with "def" lost.
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
 * along with wolfBoot; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "../../hal/nrf5340_uart.c"

static char cap[256];
static int caplen;

static void sink(const char* c, unsigned int sz)
{
    memcpy(cap + caplen, c, sz);
    caplen += (int)sz;
}

static void reset_cap(void)
{
    caplen = 0;
    cap[0] = '\0';
}

START_TEST(test_crlf_multiline)
{
    reset_cap();
    nrf5340_uart_crlf("abc\ndef\n", 8, sink);
    /* both lines preserved, each CRLF-terminated, nothing dropped */
    ck_assert_str_eq(cap, "abc\r\ndef\r\n");
}

START_TEST(test_crlf_single_line_no_nl)
{
    reset_cap();
    nrf5340_uart_crlf("hello", 5, sink);
    ck_assert_str_eq(cap, "hello");
}

START_TEST(test_crlf_single_line_with_nl)
{
    reset_cap();
    nrf5340_uart_crlf("hello\n", 6, sink);
    ck_assert_str_eq(cap, "hello\r\n");
}

START_TEST(test_crlf_leading_nl)
{
    reset_cap();
    nrf5340_uart_crlf("\nabc", 4, sink);
    ck_assert_str_eq(cap, "\r\nabc");
}

START_TEST(test_crlf_consecutive_nl)
{
    reset_cap();
    nrf5340_uart_crlf("a\n\nb\n", 5, sink);
    ck_assert_str_eq(cap, "a\r\n\r\nb\r\n");
}

int main(void)
{
    Suite* s;
    TCase* tc;
    SRunner* sr;
    int failed;

    s = suite_create("nrf5340-uart-crlf");
    tc = tcase_create("crlf");
    tcase_add_test(tc, test_crlf_multiline);
    tcase_add_test(tc, test_crlf_single_line_no_nl);
    tcase_add_test(tc, test_crlf_single_line_with_nl);
    tcase_add_test(tc, test_crlf_leading_nl);
    tcase_add_test(tc, test_crlf_consecutive_nl);
    suite_add_tcase(s, tc);
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? 0 : 1;
}
