/* unit-wolfhal-uart-flash.c
 *
 * Tests uart_write and the internal flash calls of hal/wolfhal.c on mock
 * wolfHAL devices, for a board without and with internal flash.
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

#include <wolfHAL/wolfHAL.h>

/* From hal.h and wolfboot.h, which need a target. */
typedef uintptr_t haladdr_t;
#define RAMFUNCTION

/* Mock UART: keeps the bytes sent and counts the sends. */

#define UART_LOG_SIZE 64

static uint8_t uart_log[UART_LOG_SIZE];
static size_t  uart_len;
static int     uart_sends;

static whal_Error MockUartSend(whal_Uart *d, const void *data, size_t sz)
{
    (void)d;
    uart_sends++;
    if (sz > UART_LOG_SIZE - uart_len) {
        return WHAL_EINVAL;
    }
    memcpy(uart_log + uart_len, data, sz);
    uart_len += sz;
    return WHAL_SUCCESS;
}

static const whal_UartDriver mockUartDriver = {
    .Send = MockUartSend,
};

static whal_Uart mock_uart = { 0, &mockUartDriver, NULL };
#define BOARD_UART_DEV (&mock_uart)
#define DEBUG_UART

#ifdef UNIT_INTERNAL_FLASH
/* Mock internal flash: records the whal_Flash calls. */

static int         fl_locks, fl_unlocks, fl_writes, fl_erases;
static size_t      fl_addr, fl_len;
static const void *fl_data;
static whal_Error  fl_ret;

static whal_Error MockFlashLock(whal_Flash *d, size_t addr, size_t len)
{
    (void)d;
    (void)addr;
    (void)len;
    fl_locks++;
    return WHAL_SUCCESS;
}

static whal_Error MockFlashUnlock(whal_Flash *d, size_t addr, size_t len)
{
    (void)d;
    (void)addr;
    (void)len;
    fl_unlocks++;
    return WHAL_SUCCESS;
}

static whal_Error MockFlashWrite(whal_Flash *d, size_t addr,
                                 const void *data, size_t sz)
{
    (void)d;
    fl_writes++;
    fl_addr = addr;
    fl_data = data;
    fl_len = sz;
    return fl_ret;
}

static whal_Error MockFlashErase(whal_Flash *d, size_t addr, size_t sz)
{
    (void)d;
    fl_erases++;
    fl_addr = addr;
    fl_len = sz;
    return fl_ret;
}

static const whal_FlashDriver mockFlashDriver = {
    .Lock = MockFlashLock,
    .Unlock = MockFlashUnlock,
    .Write = MockFlashWrite,
    .Erase = MockFlashErase,
};

static whal_Flash mock_flash = { 0, &mockFlashDriver, NULL };
#define BOARD_FLASH_DEV (&mock_flash)
#else
/* As on the VEK385: no internal flash, and '\n' is sent as "\r\n". */
#define WOLFHAL_UART_CRLF
#endif /* UNIT_INTERNAL_FLASH */

/* The hal/wolfhal.c code under test, copied by the Makefile. */
#include "wolfhal_uart_flash_extract.h"

/* The UART got exactly these bytes, in this number of sends. */
static void uart_expect(const char *want, int sends)
{
    size_t n = strlen(want);

    ck_assert_uint_eq(uart_len, n);
    ck_assert_int_eq(memcmp(uart_log, want, n), 0);
    ck_assert_int_eq(uart_sends, sends);
}

/* Only len bytes are sent. */
START_TEST(test_uart_write_uses_len)
{
    uart_write("abcdef", 2);
    uart_expect("ab", 1);
}
END_TEST

#ifdef WOLFHAL_UART_CRLF
START_TEST(test_uart_write_crlf_lines)
{
    uart_write("ab\ncd\n", 6);
    uart_expect("ab\r\ncd\r\n", 4);
}
END_TEST

/* A newline first, two in a row, and text after the last one. */
START_TEST(test_uart_write_crlf_edges)
{
    uart_write("\n\nx", 3);
    uart_expect("\r\n\r\nx", 3);
}
END_TEST

/* A newline as the last of the len bytes; the rest is not sent. */
START_TEST(test_uart_write_crlf_stops_at_len)
{
    uart_write("a\nb\n", 2);
    uart_expect("a\r\n", 2);
}
END_TEST

START_TEST(test_uart_write_crlf_no_newline)
{
    uart_write("abc", 3);
    uart_expect("abc", 1);
}
END_TEST

/* len 0 sends nothing. */
START_TEST(test_uart_write_crlf_empty)
{
    uart_write("abc", 0);
    uart_expect("", 0);
}
END_TEST
#else
/* Without WOLFHAL_UART_CRLF the bytes go out unchanged, in one send. */
START_TEST(test_uart_write_plain)
{
    uart_write("ab\ncd\n", 6);
    uart_expect("ab\ncd\n", 1);
}
END_TEST
#endif /* WOLFHAL_UART_CRLF */

#ifdef UNIT_INTERNAL_FLASH
START_TEST(test_hal_flash_passes_requests)
{
    uint8_t b[4] = { 1, 2, 3, 4 };

    hal_flash_unlock();
    ck_assert_int_eq(fl_unlocks, 1);
    ck_assert_int_eq(hal_flash_write(0x8001000, b, 4), 0);
    ck_assert_int_eq(fl_writes, 1);
    ck_assert_uint_eq(fl_addr, 0x8001000);
    ck_assert_uint_eq(fl_len, 4);
    ck_assert_ptr_eq(fl_data, b);
    ck_assert_int_eq(hal_flash_erase(0x8002000, 0x800), 0);
    ck_assert_int_eq(fl_erases, 1);
    ck_assert_uint_eq(fl_addr, 0x8002000);
    ck_assert_uint_eq(fl_len, 0x800);
    hal_flash_lock();
    ck_assert_int_eq(fl_locks, 1);
}
END_TEST

START_TEST(test_hal_flash_driver_error)
{
    uint8_t b[4] = { 1, 2, 3, 4 };

    fl_ret = WHAL_EINVAL;
    ck_assert_int_ne(hal_flash_write(0x8001000, b, 4), 0);
    ck_assert_int_ne(hal_flash_erase(0x8002000, 0x800), 0);
}
END_TEST
#else
/* No internal flash: unlock and lock do nothing, write and erase fail. */
START_TEST(test_hal_flash_no_internal_flash)
{
    uint8_t b[4] = { 1, 2, 3, 4 };

    hal_flash_unlock();
    ck_assert_int_eq(hal_flash_write(0x1000, b, 4), -1);
    ck_assert_int_eq(hal_flash_erase(0x1000, 0x1000), -1);
    hal_flash_lock();
}
END_TEST
#endif /* UNIT_INTERNAL_FLASH */

static void setup(void)
{
    memset(uart_log, 0, sizeof(uart_log));
    uart_len = 0;
    uart_sends = 0;
#ifdef UNIT_INTERNAL_FLASH
    fl_locks = 0;
    fl_unlocks = 0;
    fl_writes = 0;
    fl_erases = 0;
    fl_addr = 0;
    fl_len = 0;
    fl_data = NULL;
    fl_ret = WHAL_SUCCESS;
#endif
}

static void teardown(void)
{
}

Suite *wolfhal_uart_flash_suite(void)
{
    Suite *s = suite_create("wolfhal-uart-flash");
    TCase *tc = tcase_create("wolfhal-uart-flash");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_uart_write_uses_len);
#ifdef WOLFHAL_UART_CRLF
    tcase_add_test(tc, test_uart_write_crlf_lines);
    tcase_add_test(tc, test_uart_write_crlf_edges);
    tcase_add_test(tc, test_uart_write_crlf_stops_at_len);
    tcase_add_test(tc, test_uart_write_crlf_no_newline);
    tcase_add_test(tc, test_uart_write_crlf_empty);
#else
    tcase_add_test(tc, test_uart_write_plain);
#endif
#ifdef UNIT_INTERNAL_FLASH
    tcase_add_test(tc, test_hal_flash_passes_requests);
    tcase_add_test(tc, test_hal_flash_driver_error);
#else
    tcase_add_test(tc, test_hal_flash_no_internal_flash);
#endif
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfhal_uart_flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
