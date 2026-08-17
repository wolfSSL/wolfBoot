/* unit-zynq-erase-loop.c
 *
 * Regression test for F-7980: ext_flash_erase() in hal/zynq.c initializes
 * idx once before the multi-sector while (len > 0) loop and never resets
 * it, even though cmd is memset to zero each iteration. From the second
 * sector on, the erase command is written at cmd[idx] (past the start of
 * the buffer): in three-byte address mode the flash receives leading zero
 * bytes before the opcode (malformed command, erase silently fails); in
 * four-byte address mode the writes run past the end of the 8-byte cmd
 * buffer. The loop also kept going - and could return success - after a
 * failed write-enable, transfer, or ready wait.
 *
 * hal/zynq.c cannot be compiled on the host (it needs the Xilinx SDK
 * headers, which are provided by the board build tree), but the bug is
 * entirely in the ext_flash_erase() loop, so the Makefile extracts that
 * one function verbatim from hal/zynq.c (zynq_erase_extract.h) and runs
 * it here against emulated qspi_* functions that record every operation.
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

/* The extracted function is a RAMFUNCTION; on the host that is nothing. */
#define RAMFUNCTION

/* Xilinx / board constants the function uses (board build tree values). */
#define GQSPI_CODE_SUCCESS        0
#define GQSPI_GEN_FIFO_MODE_SPI   0
#define SEC_ERASE_CMD             0xD8U
#define WOLFBOOT_SECTOR_SIZE      0x10000
#define GQPI_USE_4BYTE_ADDR       0
#define wolfBoot_printf(...)      do {} while (0)

/* The driver keeps its device state in a static mDev; the only member
 * the erase loop reads is stripe (dual-parallel address scaling). */
typedef struct {
    int stripe;
} QspiDev_t;
static QspiDev_t mDev;

/* Emulated QSPI device: records every operation the driver issues. */
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
};
static struct emu_op g_ops[EMU_MAX_OPS];
static int g_ops_n;
/* Force the operation at this index to fail (one-shot, -1 = none). */
static int g_fail_at_op = -1;

/* g_ops_n has already been incremented by the operation that just ran,
 * so the index of that operation is g_ops_n - 1. */
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
    (void)pDev; (void)txData; (void)txSz; (void)rxData; (void)rxSz;
    (void)dummySz; (void)mode;
    if (g_ops_n < EMU_MAX_OPS) {
        uint32_t n = cmdSz < sizeof(g_ops[0].cmd) ? cmdSz : sizeof(g_ops[0].cmd);

        g_ops[g_ops_n].kind = EMU_XFER;
        memcpy(g_ops[g_ops_n].cmd, cmdData, n);
        g_ops[g_ops_n].cmdsz = cmdSz;
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

/* The real ext_flash_erase() from hal/zynq.c (extracted by the Makefile). */
#include "zynq_erase_extract.h"

static void setup(void)
{
    memset(g_ops, 0, sizeof(g_ops));
    g_ops_n = 0;
    g_fail_at_op = -1;
    mDev.stripe = 0;
}

static void teardown(void)
{
}

/* A multi-sector erase must issue a well-formed command per sector:
 * WEN, then the opcode at cmd[0] plus the sector address, then wait and
 * write-disable. Pre-fix, the second sector's command started at cmd[4]
 * (three-byte mode): four leading zero bytes before the opcode. */
START_TEST(test_erase_multisector)
{
    uint32_t s;

    ck_assert_int_eq(ext_flash_erase(0, 3 * WOLFBOOT_SECTOR_SIZE),
        GQSPI_CODE_SUCCESS);

    ck_assert_int_eq(g_ops_n, 12);
    for (s = 0; s < 3; s++) {
        ck_assert_int_eq(g_ops[4 * s].kind, EMU_WEN);
        ck_assert_int_eq(g_ops[4 * s + 1].kind, EMU_XFER);
        ck_assert_uint_eq(g_ops[4 * s + 1].cmdsz, 4);
        ck_assert_uint_eq(g_ops[4 * s + 1].cmd[0], SEC_ERASE_CMD);
        ck_assert_uint_eq(g_ops[4 * s + 1].cmd[1],
            (s * WOLFBOOT_SECTOR_SIZE) >> 16 & 0xFF);
        ck_assert_uint_eq(g_ops[4 * s + 1].cmd[2],
            (s * WOLFBOOT_SECTOR_SIZE) >> 8 & 0xFF);
        ck_assert_uint_eq(g_ops[4 * s + 1].cmd[3],
            (s * WOLFBOOT_SECTOR_SIZE) & 0xFF);
        ck_assert_int_eq(g_ops[4 * s + 2].kind, EMU_WAIT);
        ck_assert_int_eq(g_ops[4 * s + 3].kind, EMU_WDIS);
    }
}
END_TEST

/* A failed write-enable must stop the loop and report the error, not
 * skip the sector and keep erasing (pre-fix the loop advanced to the
 * next sector and could return success overall). */
START_TEST(test_erase_stops_on_write_enable_failure)
{
    /* ops: WEN(0) XFER(1) WAIT(2) WDIS(3) WEN(4) <- fails here */
    g_fail_at_op = 4;

    ck_assert_int_eq(ext_flash_erase(0, 3 * WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(g_ops_n, 5);
}
END_TEST

/* A failed transfer must stop the loop and report the error. */
START_TEST(test_erase_stops_on_transfer_failure)
{
    /* ops: WEN(0) XFER(1) <- fails here; then WDIS, stop */
    g_fail_at_op = 1;

    ck_assert_int_eq(ext_flash_erase(0, 3 * WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(g_ops_n, 3);
    ck_assert_int_eq(g_ops[0].kind, EMU_WEN);
    ck_assert_int_eq(g_ops[1].kind, EMU_XFER);
    ck_assert_int_eq(g_ops[2].kind, EMU_WDIS);
}
END_TEST

/* A failed ready-wait must stop the loop and report the error. */
START_TEST(test_erase_stops_on_wait_failure)
{
    /* ops: WEN(0) XFER(1) WAIT(2) <- fails here; then WDIS, stop */
    g_fail_at_op = 2;

    ck_assert_int_eq(ext_flash_erase(0, 3 * WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(g_ops_n, 4);
    ck_assert_int_eq(g_ops[0].kind, EMU_WEN);
    ck_assert_int_eq(g_ops[1].kind, EMU_XFER);
    ck_assert_int_eq(g_ops[2].kind, EMU_WAIT);
    ck_assert_int_eq(g_ops[3].kind, EMU_WDIS);
}
END_TEST

Suite *zynq_erase_suite(void)
{
    Suite *s = suite_create("zynq-erase-loop");
    TCase *tc = tcase_create("zynq-erase-loop");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_erase_multisector);
    tcase_add_test(tc, test_erase_stops_on_write_enable_failure);
    tcase_add_test(tc, test_erase_stops_on_transfer_failure);
    tcase_add_test(tc, test_erase_stops_on_wait_failure);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = zynq_erase_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
