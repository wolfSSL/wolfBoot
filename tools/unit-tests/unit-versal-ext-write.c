/* unit-versal-ext-write.c
 *
 * Regression test: ext_flash_write() in hal/versal.c chunked by length
 * alone, ignoring where the start address sat inside the physical page.
 * A write starting mid-page sent a full-page Page Program across the
 * boundary, and NOR wraps the write pointer, so the excess clobbered
 * the start of the page.
 *
 * The QspiDev_t typedef and the qspi_* helpers are file-local, so the
 * Makefile extracts the struct and ext_flash_write() verbatim and runs
 * them against emulated qspi_* calls. The emulated NOR models the wrap,
 * so pre-fix the corruption is visible in the flash image.
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

/* The constants the extracted function uses (FLASH_PAGE_SIZE, the
 * page-program opcode, flash sizing); the generated host copy of
 * hal/versal.h is host-clean. */
#include "versal_host.h"

/* The real QspiDev_t from hal/versal.c (extracted by the Makefile);
 * the emulated qspi_* below take it by pointer. */
#include "versal_qspidev_extract.h"

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
    uint8_t cmd[5];
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
    return 0;
}

static int qspi_write_enable(QspiDev_t *dev)
{
    (void)dev;
    g_ops[g_ops_n].kind = EMU_WEN;
    g_ops_n++;
    return emu_ret();
}

static int qspi_transfer(QspiDev_t *dev, const uint8_t *txData,
    uint32_t txLen, uint8_t *rxData, uint32_t rxLen, uint32_t dummyClocks,
    const uint8_t *writeData, uint32_t writeLen)
{
    (void)dev; (void)rxData; (void)rxLen; (void)dummyClocks;

    g_ops[g_ops_n].kind = EMU_XFER;
    if (g_ops_n < EMU_MAX_OPS && txData != NULL)
        memcpy(g_ops[g_ops_n].cmd, txData,
            txLen < 5 ? txLen : 5);
    g_ops[g_ops_n].txsz = writeLen;

    /* Model the Page Program: the write pointer starts at the command
     * address (4-byte, big-endian) and wraps at the physical page
     * boundary, clobbering the page start for a crossing program. */
    if (txData != NULL && txLen == 5 &&
        txData[0] == FLASH_CMD_PAGE_PROG_4B) {
        uint32_t addr = ((uint32_t)txData[1] << 24) |
                        ((uint32_t)txData[2] << 16) |
                        ((uint32_t)txData[3] << 8) |
                        (uint32_t)txData[4];
        uint32_t base = addr & ~(uint32_t)(FLASH_PAGE_SIZE - 1);
        uint32_t i;

        for (i = 0; i < writeLen; i++)
            g_nor[base + ((addr + i) & (FLASH_PAGE_SIZE - 1))] =
                writeData[i];
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

/* The driver state the extracted function reads. */
static QspiDev_t qspiDev;
static int qspi_initialized;

/* Debug logging for the QSPI driver (real: hal/versal.c). */
#define QSPI_DEBUG_PRINTF(...) do {} while(0)

/* The real ext_flash_write() from hal/versal.c (extracted by the
 * Makefile). */
#include "versal_ext_write_extract.h"

static void setup(void)
{
    memset(g_ops, 0, sizeof(g_ops));
    g_ops_n = 0;
    g_fail_at_op = -1;
    memset(g_nor, 0xFF, sizeof(g_nor));

    qspiDev.stripe = 0;
    qspi_initialized = 1;
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

static uint32_t prog_addr(const struct emu_op *op)
{
    return ((uint32_t)op->cmd[1] << 24) |
           ((uint32_t)op->cmd[2] << 16) |
           ((uint32_t)op->cmd[3] << 8) |
           (uint32_t)op->cmd[4];
}

/* Page-aligned multi-page write: well-formed programs, data lands,
 * nothing else is touched. */
START_TEST(test_write_page_aligned)
{
    uint8_t data[600];
    uint32_t xfers = 0;
    int i;

    fill(data, sizeof(data), 0x10);

    ck_assert_int_eq(ext_flash_write(0, data, 600), 0);

    for (i = 0; i < g_ops_n; i++) {
        if (g_ops[i].kind == EMU_XFER) {
            xfers++;
            ck_assert_uint_eq(g_ops[i].cmd[0], FLASH_CMD_PAGE_PROG_4B);
            ck_assert_uint_eq(prog_addr(&g_ops[i]), 256 * (xfers - 1));
        }
    }
    ck_assert_uint_eq(xfers, 3);
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

    ck_assert_int_eq(ext_flash_write(200, data, 300), 0);

    /* Every Page Program must fit in its physical page. */
    for (i = 0; i < g_ops_n; i++) {
        if (g_ops[i].kind == EMU_XFER) {
            uint32_t addr = prog_addr(&g_ops[i]);

            xfers++;
            ck_assert_int_le((int)(addr % FLASH_PAGE_SIZE) +
                (int)g_ops[i].txsz, FLASH_PAGE_SIZE);
        }
    }
    /* Chunks: (200, 56) to the page edge, then the remaining 244
     * bytes from the next page start. */
    ck_assert_uint_eq(xfers, 2);
    ck_assert_uint_eq(g_ops[1].txsz, 56);
    ck_assert_uint_eq(g_ops[5].txsz, 244);
    ck_assert_uint_eq(prog_addr(&g_ops[5]), 256);

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

/* An uninitialized QSPI must reject the write. */
START_TEST(test_write_uninitialized_rejected)
{
    uint8_t data[16];

    qspi_initialized = 0;
    ck_assert_int_eq(ext_flash_write(0, data, sizeof(data)), -1);
    ck_assert_int_eq(g_ops_n, 0);
}
END_TEST

/* A failed transfer must stop the loop and report the error. */
START_TEST(test_write_stops_on_transfer_failure)
{
    uint8_t data[600];

    /* ops: WEN(0) XFER(1) <- fails here */
    g_fail_at_op = 1;

    ck_assert_int_eq(ext_flash_write(0, data, 600), -1);
    ck_assert_int_eq(g_ops_n, 2);
    ck_assert_int_eq(g_ops[0].kind, EMU_WEN);
    ck_assert_int_eq(g_ops[1].kind, EMU_XFER);
}
END_TEST

Suite *versal_ext_write_suite(void)
{
    Suite *s = suite_create("versal-ext-write");
    TCase *tc = tcase_create("versal-ext-write");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_page_aligned);
    tcase_add_test(tc, test_write_crossing_page_no_wrap);
    tcase_add_test(tc, test_write_uninitialized_rejected);
    tcase_add_test(tc, test_write_stops_on_transfer_failure);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = versal_ext_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
