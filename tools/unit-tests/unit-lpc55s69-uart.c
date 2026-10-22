/* unit-lpc55s69-uart.c
 *
 * Unit tests for the LPC55S69 debug console in hal/lpc55s69.c.
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

/* lpc55s69.config layout, in place of target.h */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x200
#define WOLFBOOT_PARTITION_SIZE              0x30000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x10000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x40000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x70000
#define ARCH_FLASH_OFFSET                    0x0
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256

#include "fsl_device_registers.h"

static USART_Type mock_usart0;
static SYSCON_Type mock_syscon;
static IOCON_Type mock_iocon;
static RNG_Type mock_rng;

/* Before fsl_common.h: the inline clock gates in fsl_clock.h expand these */
#undef USART0
#undef SYSCON
#undef IOCON
#undef RNG
#define USART0 (&mock_usart0)
#define SYSCON (&mock_syscon)
#define IOCON  (&mock_iocon)
#define RNG    (&mock_rng)

#include "fsl_common.h"
#include "fsl_iap.h"
#include "fsl_iap_ffr.h"
#include "fsl_rng.h"
#include "fsl_usart.h"

#define MOCK_TX_MAX 64

uint32_t mock_primask;
int mock_barriers;
NVIC_Type mock_nvic;
SysTick_Type mock_systick;

static clock_attach_id_t attached_clk;
static int attach_calls;
static int fc0_unreset;
static usart_config_t init_cfg;
static uint32_t init_clk_hz;
static USART_Type *init_base;
static int init_calls;
static uint8_t tx[MOCK_TX_MAX];
static unsigned int tx_len;
static int tx_bad_base;

void CLOCK_AttachClk(clock_attach_id_t connection)
{
    attached_clk = connection;
    attach_calls++;
}

void RESET_ClearPeripheralReset(reset_ip_name_t peripheral)
{
    if (peripheral == kFC0_RST_SHIFT_RSTn)
        fc0_unreset++;
}

void USART_GetDefaultConfig(usart_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->baudRate_Bps = 9600U;
}

status_t USART_Init(USART_Type *base, const usart_config_t *config,
        uint32_t srcClock_Hz)
{
    init_base = base;
    init_cfg = *config;
    init_clk_hz = srcClock_Hz;
    init_calls++;
    return kStatus_Success;
}

status_t USART_WriteBlocking(USART_Type *base, const uint8_t *data,
        size_t length)
{
    if (base != &mock_usart0)
        tx_bad_base = 1;
    while ((length > 0U) && (tx_len < MOCK_TX_MAX)) {
        tx[tx_len++] = *data++;
        length--;
    }
    return kStatus_Success;
}

/* hal_init (application side) links against these; the tests never call it */
status_t FLASH_Init(flash_config_t *config)
{
    (void)config;
    return kStatus_Fail;
}

status_t FFR_Init(flash_config_t *config)
{
    (void)config;
    return kStatus_Fail;
}

status_t FLASH_VerifyErase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes)
{
    (void)config;
    (void)start;
    (void)lengthInBytes;
    return kStatus_Fail;
}

status_t FLASH_Erase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes, uint32_t key)
{
    (void)config;
    (void)start;
    (void)lengthInBytes;
    (void)key;
    return kStatus_Fail;
}

status_t FLASH_Program(flash_config_t *config, uint32_t start,
        const uint8_t *src, uint32_t lengthInBytes)
{
    (void)config;
    (void)start;
    (void)src;
    (void)lengthInBytes;
    return kStatus_Fail;
}

void RESET_PeripheralReset(reset_ip_name_t peripheral)
{
    (void)peripheral;
}

void RNG_Init(RNG_Type *base)
{
    (void)base;
}

void uart_printf(const char *fmt, ...)
{
    (void)fmt;
}

#include "../../hal/lpc55s69.c"

static void setup(void)
{
    memset(&mock_syscon, 0, sizeof(mock_syscon));
    memset(&mock_iocon, 0, sizeof(mock_iocon));
    memset(&init_cfg, 0, sizeof(init_cfg));
    memset(tx, 0, sizeof(tx));
    tx_len = 0;
    tx_bad_base = 0;
    attach_calls = init_calls = fc0_unreset = 0;
    init_base = NULL;
}

START_TEST(test_uart_init_console_pins_and_baud)
{
    const uint32_t mux = IOCON_FUNC1 | IOCON_MODE_INACT | IOCON_DIGITAL_EN;

    uart_init();
    ck_assert_uint_eq(mock_iocon.PIO[0][29], mux);
    ck_assert_uint_eq(mock_iocon.PIO[0][30], mux);
    ck_assert_int_eq(attach_calls, 1);
    ck_assert_int_eq(attached_clk, kFRO12M_to_FLEXCOMM0);
    ck_assert_int_eq(fc0_unreset, 1);
    ck_assert_int_eq(init_calls, 1);
    ck_assert_ptr_eq(init_base, &mock_usart0);
    ck_assert_uint_eq(init_cfg.baudRate_Bps, 115200U);
    ck_assert(init_cfg.enableTx);
    ck_assert(init_cfg.enableRx);
    ck_assert_uint_eq(init_clk_hz, 12000000U);
}
END_TEST

START_TEST(test_uart_write_adds_cr_before_lf)
{
    uart_write("a\nb\n", 4);
    ck_assert_uint_eq(tx_len, 6);
    ck_assert_mem_eq(tx, "a\r\nb\r\n", 6);
    ck_assert_int_eq(tx_bad_base, 0);
}
END_TEST

START_TEST(test_uart_write_plain_and_bounds)
{
    uart_write("hello", 5);
    ck_assert_uint_eq(tx_len, 5);
    ck_assert_mem_eq(tx, "hello", 5);

    tx_len = 0;
    uart_write("\n\n", 2);
    ck_assert_uint_eq(tx_len, 4);
    ck_assert_mem_eq(tx, "\r\n\r\n", 4);

    tx_len = 0;
    uart_write("xyz", 0);
    uart_write(NULL, 3);
    ck_assert_uint_eq(tx_len, 0);
}
END_TEST

static Suite *lpc55s69_uart_suite(void)
{
    Suite *s = suite_create("lpc55s69-uart");
    TCase *tc = tcase_create("lpc55s69-uart");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_uart_init_console_pins_and_baud);
    tcase_add_test(tc, test_uart_write_adds_cr_before_lf);
    tcase_add_test(tc, test_uart_write_plain_and_bounds);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = lpc55s69_uart_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
