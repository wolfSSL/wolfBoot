/* unit-x86-uart.c
 *
 * Regression test for F-12885: uart_init() in hal/x86_uart.c derived the
 * stop-bit field from an always-zero local variable instead of the
 * requested stop count, so the UART was always configured for one stop
 * bit (and stop values other than 1/2 were silently accepted).
 *
 * The real driver is linked in; io_write8()/io_read8() are recording
 * mocks standing in for the port I/O primitives.
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
#include <stdlib.h>
#include <string.h>

#include <uart_drv.h>

/* Default port base in hal/x86_uart.c; LCR is register 3. */
#define TEST_LCR_PORT (0x3f8 + 3)

static uint8_t last_lcr;
static int lcr_writes;

void io_write8(uint16_t port, uint8_t value)
{
    if (port == TEST_LCR_PORT) {
        last_lcr = value;
        lcr_writes++;
    }
}

uint8_t io_read8(uint16_t port)
{
    (void)port;
    return 0x20;
}

static void setup(void)
{
    last_lcr = 0;
    lcr_writes = 0;
}

static void teardown(void)
{
}

/* 115200 8-N-1: LCR = DATA_8_BIT (0x03). */
START_TEST(test_uart_init_one_stop_bit)
{
    ck_assert_int_eq(uart_init(115200, 8, 'N', 1), 0);
    ck_assert_int_gt(lcr_writes, 0);
    ck_assert_uint_eq(last_lcr, 0x03);
}
END_TEST

/* 115200 8-N-2: LCR = DATA_8_BIT | (1 << 2) = 0x07. Pre-fix the
 * stop-bit field was always zero and the LCR came out 0x03. */
START_TEST(test_uart_init_two_stop_bits)
{
    ck_assert_int_eq(uart_init(115200, 8, 'N', 2), 0);
    ck_assert_int_gt(lcr_writes, 0);
    ck_assert_uint_eq(last_lcr, 0x07);
}
END_TEST

/* 115200 5-O-2: LCR = DATA_5_BIT | (1 << 2) | (PARITY_ODD << 3) = 0x0C. */
START_TEST(test_uart_init_parity_and_stops)
{
    ck_assert_int_eq(uart_init(115200, 5, 'O', 2), 0);
    ck_assert_int_gt(lcr_writes, 0);
    ck_assert_uint_eq(last_lcr, 0x0C);
}
END_TEST

/* Stop counts other than 1 and 2 are not representable in the LCR. */
START_TEST(test_uart_init_bad_stop_count)
{
    ck_assert_int_eq(uart_init(115200, 8, 'N', 0), -1);
    ck_assert_int_eq(uart_init(115200, 8, 'N', 3), -1);
    ck_assert_int_eq(lcr_writes, 0);
}
END_TEST

/* Existing rejections must be untouched. */
START_TEST(test_uart_init_bad_args)
{
    ck_assert_int_eq(uart_init(0, 8, 'N', 1), -1);
    ck_assert_int_eq(uart_init(115200, 9, 'N', 1), -1);
    ck_assert_int_eq(uart_init(115200, 8, 'X', 1), -1);
    ck_assert_int_eq(lcr_writes, 0);
}
END_TEST

int main(void)
{
    SRunner *sr;
    Suite *s = suite_create("x86_uart");
    TCase *tc = tcase_create("uart_init");
    int failures;

    tcase_add_unchecked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_uart_init_one_stop_bit);
    tcase_add_test(tc, test_uart_init_two_stop_bits);
    tcase_add_test(tc, test_uart_init_parity_and_stops);
    tcase_add_test(tc, test_uart_init_bad_stop_count);
    tcase_add_test(tc, test_uart_init_bad_args);
    suite_add_tcase(s, tc);

    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
