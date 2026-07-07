/* unit-xspi-tfd-index.c
 *
 * Unit tests for the LS1028A XSPI TX FIFO fill indexing.
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
#include <string.h>

#include "nxp_ls1028a.h"

/* Back the XSPI register space with host memory instead of MMIO. */
#undef XSPI_BASE
static uint32_t g_xspi_regs[0x400 / sizeof(uint32_t)];
#define XSPI_BASE ((uintptr_t)g_xspi_regs)

#define WORD0 0xA1B2C3D4u
#define WORD1 0x11223344u

static void xspi_store(uint32_t *addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static void fill_burst_bytestride(const uint8_t *data)
{
    uint32_t j = 0, tx = 0;

    for (j = 0; j < XSPI_IP_WM_SIZE; j += 4) {
        memcpy(&tx, data, 4);
        data += 4;
        xspi_store((uint32_t *)XSPI_TFD_BASE + j, tx);
    }
}

static void fill_burst_wordindex(const uint8_t *data)
{
    uint32_t j = 0, tx = 0;

    for (j = 0; j < XSPI_IP_WM_SIZE; j += 4) {
        memcpy(&tx, data, 4);
        data += 4;
        xspi_store((uint32_t *)XSPI_TFD_BASE + (j / 4), tx);
    }
}

static void make_payload(uint8_t *payload)
{
    uint32_t w0 = WORD0, w1 = WORD1;

    memcpy(payload, &w0, 4);
    memcpy(payload + 4, &w1, 4);
}

START_TEST(test_bytestride_loses_second_word)
{
    uint8_t payload[XSPI_IP_WM_SIZE];

    memset(g_xspi_regs, 0, sizeof(g_xspi_regs));
    make_payload(payload);
    fill_burst_bytestride(payload);

    ck_assert_uint_eq(XSPI_TFD(0), WORD0);
    ck_assert_uint_ne(XSPI_TFD(1), WORD1);
    ck_assert_uint_eq(XSPI_TFD(4), WORD1);
}
END_TEST

START_TEST(test_wordindex_preserves_burst)
{
    uint8_t payload[XSPI_IP_WM_SIZE];

    memset(g_xspi_regs, 0, sizeof(g_xspi_regs));
    make_payload(payload);
    fill_burst_wordindex(payload);

    ck_assert_uint_eq(XSPI_TFD(0), WORD0);
    ck_assert_uint_eq(XSPI_TFD(1), WORD1);
    ck_assert_uint_eq(XSPI_TFD(4), 0u);
}
END_TEST

Suite *xspi_tfd_suite(void)
{
    Suite *s = suite_create("xspi-tfd-index");
    TCase *tc = tcase_create("tx-fifo-fill");

    tcase_add_test(tc, test_bytestride_loses_second_word);
    tcase_add_test(tc, test_wordindex_preserves_burst);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = xspi_tfd_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
