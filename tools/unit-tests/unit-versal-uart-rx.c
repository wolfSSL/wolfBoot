/* unit-versal-uart-rx.c
 *
 * Exercises the real uart_rx() from hal/versal.c against an emulated
 * PL011 register block. The receive path cannot be exercised on the
 * VMK180 on hand, whose console FT4232H does not enumerate.
 *
 * UART_REG() resolves to an absolute MMIO address, so the Makefile
 * extracts the function verbatim and it is redefined over a local
 * array below before the function is included.
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

/* The PL011 register offsets and bit masks, from hal/versal.h. */
#include "versal_host.h"

/* Emulated register block, indexed by register offset. */
static uint32_t uart_regs[0x20];

#undef UART_REG
#define UART_REG(offset)        (uart_regs[(offset) / 4])

#include "versal_uart_rx_extract.h"

static void setup(void)
{
    memset(uart_regs, 0, sizeof(uart_regs));
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

static void teardown(void)
{
}

/* PLM configures the port for its own log output, which needs transmit
 * only, and at EL2 uart_init() leaves it that way. */
START_TEST(test_receive_enabled_without_disturbing_port)
{
    uint8_t c = 0;

    UART_CR = UART_CR_UARTEN | UART_CR_TXE;
    UART_IBRD = 81;
    UART_FBRD = 24;
    UART_LCR = UART_LCR_WLEN_8 | UART_LCR_FEN;
    UART_FR = UART_FR_RXFE;

    ck_assert_int_eq(uart_rx(&c), 0);
    ck_assert(UART_CR & UART_CR_RXE);
    ck_assert(UART_CR & UART_CR_UARTEN);
    ck_assert(UART_CR & UART_CR_TXE);
    ck_assert_uint_eq(UART_IBRD, 81);
    ck_assert_uint_eq(UART_FBRD, 24);
    ck_assert_uint_eq(UART_LCR, UART_LCR_WLEN_8 | UART_LCR_FEN);
}
END_TEST

START_TEST(test_empty_fifo_returns_zero)
{
    uint8_t c = 0xA5;

    UART_FR = UART_FR_RXFE;
    UART_DR = 'x';

    ck_assert_int_eq(uart_rx(&c), 0);
    ck_assert_uint_eq(c, 0xA5);
}
END_TEST

START_TEST(test_character_received)
{
    uint8_t c = 0;

    UART_FR = 0;
    UART_DR = 'a';

    ck_assert_int_eq(uart_rx(&c), 1);
    ck_assert_uint_eq(c, 'a');
}
END_TEST

/* Bits above the data field must not reach the caller. The receive
 * error field at [11:8] is covered by test_receive_error_dropped. */
START_TEST(test_status_bits_masked_off)
{
    uint8_t c = 0;

    UART_FR = 0;
    UART_DR = 0xF000UL | 'b';

    ck_assert_int_eq(uart_rx(&c), 1);
    ck_assert_uint_eq(c, 'b');
}
END_TEST

START_TEST(test_receive_error_dropped)
{
    const uint32_t err[4] = {
        UART_DR_OE, UART_DR_BE, UART_DR_PE, UART_DR_FE
    };
    uint8_t c;
    int i;

    for (i = 0; i < 4; i++) {
        c = 0xA5;
        UART_FR = 0;
        UART_DR = err[i] | 'c';
        UART_RSR = 0xF;

        ck_assert_int_eq(uart_rx(&c), 0);
        ck_assert_uint_eq(c, 0xA5);
        ck_assert_uint_eq(UART_RSR, 0);
    }
}
END_TEST

Suite *versal_uart_rx_suite(void)
{
    Suite *s = suite_create("versal-uart-rx");
    TCase *tc = tcase_create("versal-uart-rx");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_receive_enabled_without_disturbing_port);
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
    Suite *s = versal_uart_rx_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
