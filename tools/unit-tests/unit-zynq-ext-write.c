/* unit-zynq-ext-write.c
 *
 * Regression test: ext_flash_write() in hal/zynq.c chunked by length
 * alone, ignoring the start address's offset inside the device page. A
 * write starting mid-page sent a full-page Page Program across the
 * boundary, and NOR wraps the write pointer, so the excess clobbered
 * the start of the page.
 *
 * As with unit-zynq-erase-loop the HAL cannot be built on the host, so
 * the Makefile extracts ext_flash_write() and runs it against emulated
 * qspi_* calls. The emulated NOR models the wrap, so pre-fix the
 * corruption is visible in the flash image.
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

/* The extracted function is a RAMFUNCTION; on the host that is nothing. */
#define RAMFUNCTION

/* Xilinx / board constants the function uses (board build tree values).
 * QSPI NOR page program size is 256. */
#define GQSPI_CODE_SUCCESS        0
#define GQSPI_GEN_FIFO_MODE_SPI   0
#define PAGE_PROG_CMD             0x02U
#define FLASH_PAGE_SIZE           256
#define GQPI_USE_4BYTE_ADDR       0
#define wolfBoot_printf(...)      do {} while (0)

/* The driver keeps its device state in a static mDev; the only member
 * the write loop reads is stripe (dual-parallel address scaling). */
typedef struct {
    int stripe;
} QspiDev_t;
static QspiDev_t mDev;

/* Emulated QSPI device: records every operation and programs an
 * emulated NOR that wraps the write pointer at page boundaries, like
 * real NOR flash. */
#define EMU_MAX_OPS 64
enum {
    EMU_WEN,
    EMU_XFER,
    EMU_WAIT,
    EMU_WDIS,
};
struct emu_op {
    int kind;
    uint8_t cmd[16];
    uint32_t cmdsz;
    uint32_t txsz;
};
static struct emu_op g_ops[EMU_MAX_OPS];
static int g_ops_n;
/* Force the operation at this index to fail (one-shot, -1 = none). */
static int g_fail_at_op = -1;

#define NOR_SIZE (4 * 1024)
static uint8_t g_nor[NOR_SIZE];

static int emu_ret(void)
{
    if (g_fail_at_op >= 0 && (g_ops_n - 1) == g_fail_at_op) {
        g_fail_at_op = -1;
        return -1;
    }
    return GQSPI_CODE_SUCCESS;
}

static int qspi_write_enable(QspiDev_t *dev)
{
    (void)dev;
    g_ops[g_ops_n].kind = EMU_WEN;
    g_ops_n++;
    return emu_ret();
}

static int qspi_transfer(QspiDev_t *pDev,
    const uint8_t *cmdData, uint32_t cmdSz,
    const uint8_t *txData, uint32_t txSz,
    uint8_t *rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    (void)pDev; (void)rxData; (void)rxSz; (void)dummySz; (void)mode;

    if (g_ops_n < EMU_MAX_OPS) {
        uint32_t n = cmdSz < sizeof(g_ops[0].cmd) ? cmdSz
                                                  : sizeof(g_ops[0].cmd);

        g_ops[g_ops_n].kind = EMU_XFER;
        memcpy(g_ops[g_ops_n].cmd, cmdData, n);
        g_ops[g_ops_n].cmdsz = cmdSz;
        g_ops[g_ops_n].txsz = txSz;

        /* Model the Page Program: the write pointer starts at the
         * command address and wraps at the physical page boundary,
         * clobbering the page start for a crossing program. */
        if (cmdSz >= 4 && cmdData[0] == PAGE_PROG_CMD) {
            uint32_t addr = ((uint32_t)cmdData[1] << 16) |
                            ((uint32_t)cmdData[2] << 8) |
                            (uint32_t)cmdData[3];
            uint32_t base = addr & ~(uint32_t)(FLASH_PAGE_SIZE - 1);
            uint32_t i;

            for (i = 0; i < txSz; i++)
                g_nor[base + ((addr + i) & (FLASH_PAGE_SIZE - 1))] =
                    txData[i];
        }
    }
    g_ops_n++;
    return emu_ret();
}

static int qspi_wait_ready(QspiDev_t *dev)
{
    (void)dev;
    g_ops[g_ops_n].kind = EMU_WAIT;
    g_ops_n++;
    return emu_ret();
}

static void qspi_write_disable(QspiDev_t *dev)
{
    (void)dev;
    if (g_ops_n < EMU_MAX_OPS)
        g_ops[g_ops_n].kind = EMU_WDIS;
    g_ops_n++;
}

/* The real ext_flash_write() from hal/zynq.c (extracted by the
 * Makefile). */
#include "zynq_write_extract.h"

static void setup(void)
{
    memset(g_ops, 0, sizeof(g_ops));
    g_ops_n = 0;
    g_fail_at_op = -1;
    mDev.stripe = 0;
    memset(g_nor, 0xFF, sizeof(g_nor));
}

static void teardown(void)
{
}

static void fill(uint8_t *buf, size_t len, uint8_t base)
{
    size_t i;

    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(base + i);
}

/* Page-aligned multi-page write: three well-formed Page Programs,
 * data lands, nothing else is touched. */
START_TEST(test_write_page_aligned)
{
    uint8_t data[600];
    uint32_t xfers = 0;
    int i;

    fill(data, sizeof(data), 0x10);

    ck_assert_int_eq(ext_flash_write(0, data, 600), GQSPI_CODE_SUCCESS);

    for (i = 0; i < g_ops_n; i++) {
        if (g_ops[i].kind == EMU_XFER) {
            xfers++;
            ck_assert_uint_eq(g_ops[i].cmd[0], PAGE_PROG_CMD);
        }
    }
    ck_assert_uint_eq(xfers, 3);
    ck_assert_uint_eq(g_nor[599], data[599]);
    for (i = 0; i < 600; i++)
        ck_assert_uint_eq(g_nor[i], data[i]);
    for (i = 600; i < NOR_SIZE; i++)
        ck_assert_uint_eq(g_nor[i], 0xFF);
}
END_TEST

/* A write that crosses page boundaries must not clobber the starts
 * of the pages it wraps into. Pre-fix, the 256-byte program starting
 * 56 bytes before the boundary wrapped and overwrote g_nor[0..55]. */
START_TEST(test_write_crossing_page_no_wrap)
{
    uint8_t data[300];
    uint32_t xfers = 0;
    int i;

    /* Pre-seed the whole flash with a pattern. */
    memset(g_nor, 0x5A, sizeof(g_nor));
    fill(data, sizeof(data), 0x60);

    ck_assert_int_eq(ext_flash_write(200, data, 300), GQSPI_CODE_SUCCESS);

    /* Every Page Program must fit in its physical page. */
    for (i = 0; i < g_ops_n; i++) {
        if (g_ops[i].kind == EMU_XFER) {
            uint32_t addr = ((uint32_t)g_ops[i].cmd[1] << 16) |
                            ((uint32_t)g_ops[i].cmd[2] << 8) |
                            (uint32_t)g_ops[i].cmd[3];

            xfers++;
            ck_assert_int_le((int)(addr % FLASH_PAGE_SIZE) +
                (int)g_ops[i].txsz, FLASH_PAGE_SIZE);
        }
    }
    /* Chunks: (200, 56) to the page edge, then the remaining 244
     * bytes in one program at the next page start. */
    ck_assert_uint_eq(xfers, 2);
    ck_assert_uint_eq(g_ops[1].txsz, 56);
    ck_assert_uint_eq(g_ops[5].txsz, 244);
    ck_assert_uint_eq((((uint32_t)g_ops[5].cmd[1] << 16) |
        ((uint32_t)g_ops[5].cmd[2] << 8) | (uint32_t)g_ops[5].cmd[3]),
        256);

    /* The written bytes. */
    for (i = 0; i < 300; i++)
        ck_assert_uint_eq(g_nor[200 + i], data[i]);

    /* The page starts a wrapping program would clobber. */
    for (i = 0; i < 200; i++)
        ck_assert_uint_eq(g_nor[i], 0x5A);
    for (i = 500; i < NOR_SIZE; i++)
        ck_assert_uint_eq(g_nor[i], 0x5A);
}
END_TEST

/* A failed write-enable must stop the loop and report the error. */
START_TEST(test_write_stops_on_write_enable_failure)
{
    uint8_t data[600];

    /* ops: WEN(0) XFER(1) WAIT(2) WDIS(3) WEN(4) <- fails here */
    g_fail_at_op = 4;

    ck_assert_int_eq(ext_flash_write(0, data, 600), -1);
    ck_assert_int_eq(g_ops_n, 5);
}
END_TEST

/* A failed transfer must stop the loop and report the error. */
START_TEST(test_write_stops_on_transfer_failure)
{
    /* ops: WEN(0) XFER(1) <- fails here; the loop breaks without a
     * further operation (the next write re-issues Write Enable). */
    uint8_t data[600];

    g_fail_at_op = 1;

    ck_assert_int_eq(ext_flash_write(0, data, 600), -1);
    ck_assert_int_eq(g_ops_n, 2);
    ck_assert_int_eq(g_ops[0].kind, EMU_WEN);
    ck_assert_int_eq(g_ops[1].kind, EMU_XFER);
}
END_TEST

Suite *zynq_ext_write_suite(void)
{
    Suite *s = suite_create("zynq-ext-write");
    TCase *tc = tcase_create("zynq-ext-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_page_aligned);
    tcase_add_test(tc, test_write_crossing_page_no_wrap);
    tcase_add_test(tc, test_write_stops_on_write_enable_failure);
    tcase_add_test(tc, test_write_stops_on_transfer_failure);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = zynq_ext_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
