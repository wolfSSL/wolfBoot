/* unit-rw612-uart.c
 *
 * Unit tests for the RW612 debug console in hal/rw612.c (DEBUG_UART): the
 * FLEXCOMM3 clock, pin mux and USART setup, and the newline to CRLF output.
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

/* RW612 layout (config/examples/rw612.config), in place of target.h */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x1000
#define WOLFBOOT_PARTITION_SIZE              0x100000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x08100000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x08200000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x08300000

#include "fsl_common.h"

static USART_Type mock_usart3;
static MCI_IO_MUX_Type mock_iomux;
static SOCCIU_Type mock_socctrl;
static AON_SOC_CIU_Type mock_aon;

/* Before fsl_io_mux.h: its inline IO_MUX_SetPinMux() expands these */
#undef USART3
#undef MCI_IO_MUX
#undef SOCCTRL
#undef AON_SOC_CIU
#define USART3      (&mock_usart3)
#define MCI_IO_MUX  (&mock_iomux)
#define SOCCTRL     (&mock_socctrl)
#define AON_SOC_CIU (&mock_aon)

#include "fsl_io_mux.h"
#include "fsl_usart.h"

#define MOCK_TX_MAX 64

uint32_t mock_primask;
int mock_barriers;

static clock_attach_id_t attached_clk;
static int attach_calls;
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
    if (base != &mock_usart3)
        tx_bad_base = 1;
    while ((length > 0U) && (tx_len < MOCK_TX_MAX)) {
        tx[tx_len++] = *data++;
        length--;
    }
    return kStatus_Success;
}

#include "../../hal/rw612.c"

static void setup(void)
{
    memset(&mock_iomux, 0xFF, sizeof(mock_iomux));
    memset(&mock_socctrl, 0, sizeof(mock_socctrl));
    memset(&mock_aon, 0, sizeof(mock_aon));
    memset(&init_cfg, 0, sizeof(init_cfg));
    memset(tx, 0, sizeof(tx));
    tx_len = 0;
    tx_bad_base = 0;
    attach_calls = init_calls = 0;
    init_base = NULL;
    init_clk_hz = 0;
}

START_TEST(test_uart_init_flexcomm3_115200)
{
    uart_init();
    ck_assert_int_eq(attach_calls, 1);
    ck_assert_int_eq(attached_clk, kSFRO_to_FLEXCOMM3);
    ck_assert_int_eq(init_calls, 1);
    ck_assert_ptr_eq(init_base, &mock_usart3);
    ck_assert_uint_eq(init_cfg.baudRate_Bps, 115200U);
    ck_assert(init_cfg.enableTx);
    ck_assert(init_cfg.enableRx);
    ck_assert_uint_eq(init_clk_hz, 16000000U);
}
END_TEST

START_TEST(test_uart_init_muxes_fc3_pins)
{
    /* FC3 USART data is GPIO24/GPIO26, enabled in the always-on domain */
    uart_init();
    ck_assert_uint_eq(mock_iomux.GPIO_GRP0 & 0x05000000U, 0U);
    ck_assert_uint_ne(mock_iomux.FC3, 0U);
    ck_assert_uint_eq(mock_aon.MCI_IOMUX_EN0 & 0x05000000U, 0x05000000U);
}
END_TEST

START_TEST(test_uart_write_crlf)
{
    static const char out[] = "\r\nab\r\n\r\nc";

    uart_write("\nab\n\nc", 6);
    ck_assert_int_eq(tx_bad_base, 0);
    ck_assert_uint_eq(tx_len, sizeof(out) - 1U);
    ck_assert_mem_eq(tx, out, sizeof(out) - 1U);
}
END_TEST

START_TEST(test_uart_write_plain_and_empty)
{
    uart_write("abc", 3);
    ck_assert_uint_eq(tx_len, 3);
    ck_assert_mem_eq(tx, "abc", 3);
    uart_write("x", 0);
    ck_assert_uint_eq(tx_len, 3);
}
END_TEST

static Suite *rw612_uart_suite(void)
{
    Suite *s = suite_create("rw612-uart");
    TCase *tc = tcase_create("uart");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_uart_init_flexcomm3_115200);
    tcase_add_test(tc, test_uart_init_muxes_fc3_pins);
    tcase_add_test(tc, test_uart_write_crlf);
    tcase_add_test(tc, test_uart_write_plain_and_empty);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr = srunner_create(rw612_uart_suite());

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails == 0 ? 0 : 1;
}
