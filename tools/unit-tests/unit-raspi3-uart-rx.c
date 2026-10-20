/* unit-raspi3-uart-rx.c
 *
 * Exercises the real uart_rx() from hal/raspi3.c against an emulated
 * PL011 register block. The register accessors are absolute MMIO
 * pointers, so the Makefile extracts the function verbatim and they are
 * redefined over a local array below before it is included.
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
#include <string.h>

/* Emulated register block, indexed by register offset. */
static unsigned int uart_regs[0x20];

#define UART0_DR        ((volatile unsigned int*)&uart_regs[0x00 / 4])
#define UART0_RSR       ((volatile unsigned int*)&uart_regs[0x04 / 4])
#define UART0_FR        ((volatile unsigned int*)&uart_regs[0x18 / 4])

#define RXFE            0x10

#include "raspi3_uart_rx_extract.h"

static void setup(void)
{
    memset(uart_regs, 0, sizeof(uart_regs));
}

static void teardown(void)
{
}

START_TEST(test_empty_fifo_returns_zero)
{
    uint8_t c = 0xA5;

    *UART0_FR = RXFE;
    *UART0_DR = 'x';

    ck_assert_int_eq(uart_rx(&c), 0);
    ck_assert_uint_eq(c, 0xA5);
}
END_TEST

START_TEST(test_character_received)
{
    uint8_t c = 0;

    *UART0_FR = 0;
    *UART0_DR = 'a';

    ck_assert_int_eq(uart_rx(&c), 1);
    ck_assert_uint_eq(c, 'a');
}
END_TEST

/* Bits above the data field must not reach the caller. The receive
 * error field at [11:8] is covered by test_receive_error_dropped. */
START_TEST(test_status_bits_masked_off)
{
    uint8_t c = 0;

    *UART0_FR = 0;
    *UART0_DR = 0xF000U | 'b';

    ck_assert_int_eq(uart_rx(&c), 1);
    ck_assert_uint_eq(c, 'b');
}
END_TEST

START_TEST(test_receive_error_dropped)
{
    const unsigned int err[4] = { 0x800, 0x400, 0x200, 0x100 };
    uint8_t c;
    int i;

    for (i = 0; i < 4; i++) {
        c = 0xA5;
        *UART0_FR = 0;
        *UART0_DR = err[i] | 'c';
        *UART0_RSR = 0xF;

        ck_assert_int_eq(uart_rx(&c), 0);
        ck_assert_uint_eq(c, 0xA5);
        ck_assert_uint_eq(*UART0_RSR, 0);
    }
}
END_TEST

Suite *raspi3_uart_rx_suite(void)
{
    Suite *s = suite_create("raspi3-uart-rx");
    TCase *tc = tcase_create("raspi3-uart-rx");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_empty_fifo_returns_zero);
    tcase_add_test(tc, test_character_received);
    tcase_add_test(tc, test_status_bits_masked_off);
    tcase_add_test(tc, test_receive_error_dropped);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = raspi3_uart_rx_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
