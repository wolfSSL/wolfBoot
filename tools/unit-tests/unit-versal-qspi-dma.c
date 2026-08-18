/* unit-versal-qspi-dma.c
 *
 * Regression test for F-7982: the DMA RX path of qspi_transfer() in
 * hal/versal.c. For an unaligned destination or a length not divisible
 * by four, the transfer runs through the 4096-byte dma_tmpbuf. If the
 * requested rxLen exceeded the temp buffer, dmaLen was truncated to
 * sizeof(dma_tmpbuf) (so only that much was DMA'd), but the code then
 * copied the full rxLen out with memcpy(rxData, dmaPtr, rxLen) - reading
 * past the end of dma_tmpbuf and reporting the whole read as successful.
 *
 * The test compiles the real hal/versal.c (as versal_host.c, generated
 * by the Makefile). The generated copy differs from the real file only
 * in:
 *   - ARM asm statements blanked (cannot assemble on x86; the cache
 *     maintenance helpers become no-ops, which is harmless because the
 *     DMA is emulated below);
 *   - VERSAL_QSPI_BASE rebased onto g_vqspi_regs (real register
 *     offsets are preserved);
 *   - the real qspi_dma_wait() renamed to qspi_dma_wait_hw() so this
 *     file can provide a qspi_dma_wait() that performs the DMA move the
 *     hardware would do (flash -> GQSPIDMA_DST, GQSPIDMA_SIZE bytes).
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

/* Register file for the rebased VERSAL_QSPI_BASE (see versal_host.h). */
static uint32_t g_vqspi_regs[0x4000 / sizeof(uint32_t)];

/* Register macros (offsets unchanged, base rebased). */
#include "versal_host.h"

/* Emulated flash: a continuous byte stream. The DMA reads from here. */
#define G_FLASH_SZ (64 * 1024)
static uint8_t g_flash[G_FLASH_SZ];
static uint8_t *g_flash_cur;

/* DMA moves performed by the emulated qspi_dma_wait(). */
#define MAX_MOVES 8
struct dma_move {
    uint64_t dst;
    uint32_t size;
};
static struct dma_move g_moves[MAX_MOVES];
static int g_moves_n;

/* Emulated DMA completion: perform the transfer the GQSPIDMA_* registers
 * describe. The real qspi_dma_wait() (renamed to qspi_dma_wait_hw in
 * versal_host.c) only polls the DONE bit; the move itself is hardware. */
static int qspi_dma_wait(void)
{
    uint64_t dst = (((uint64_t)GQSPIDMA_DST_MSB) << 32) | GQSPIDMA_DST;
    uint32_t size = GQSPIDMA_SIZE;

    if (g_moves_n < MAX_MOVES) {
        g_moves[g_moves_n].dst = dst;
        g_moves[g_moves_n].size = size;
        g_moves_n++;
    }
    if (g_flash_cur + size > g_flash + G_FLASH_SZ)
        return -1;
    memcpy((void *)dst, g_flash_cur, size);
    g_flash_cur += size;

    return 0;
}

/* The real HAL (identical to hal/versal.c apart from the transforms
 * documented above). */
#include "versal_host.c"

static QspiDev_t g_dev;

static void setup(void)
{
    int i;

    memset(g_vqspi_regs, 0, sizeof(g_vqspi_regs));
    /* GenFIFO never full / always empty; TX FIFO never full. */
    GQSPI_ISR = GQSPI_IXR_GEN_FIFO_NOT_FULL | GQSPI_IXR_GEN_FIFO_EMPTY;

    for (i = 0; i < G_FLASH_SZ; i++)
        g_flash[i] = (uint8_t)((i * 7 + 13) & 0xFF);
    g_flash_cur = g_flash;

    memset(g_moves, 0, sizeof(g_moves));
    g_moves_n = 0;

    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.bus = GQSPI_GEN_FIFO_BUS_LOW;
    g_dev.cs = GQSPI_GEN_FIFO_CS_LOWER;
    g_dev.stripe = 0;
}

static void teardown(void)
{
}

/* Aligned destination, length a multiple of four: DMA goes straight into
 * rxData in one move (existing behavior, must not change). */
START_TEST(test_dma_aligned_large)
{
    static uint8_t XALIGNED(GQSPI_DMA_ALIGN) buf[8192 + GQSPI_DMA_ALIGN];
    static const uint8_t tx[5] = { 0xEB, 0x00, 0x00, 0x00, 0x00 };
    int i;

    ck_assert_int_eq(qspi_transfer(&g_dev, tx, sizeof(tx), buf, 8192,
        8, NULL, 0), 0);

    ck_assert_int_eq(g_moves_n, 1);
    ck_assert_uint_eq(g_moves[0].dst, (uint64_t)(uintptr_t)buf);
    ck_assert_uint_eq(g_moves[0].size, 8192);
    for (i = 0; i < 8192; i++)
        ck_assert_uint_eq(buf[i], g_flash[i]);
}
END_TEST

/* Unaligned destination, small read: single temp-buffer pass, only the
 * requested bytes copied out. */
START_TEST(test_dma_unaligned_small)
{
    static uint8_t XALIGNED(GQSPI_DMA_ALIGN) raw[GQSPI_DMA_TMPSZ +
        GQSPI_DMA_ALIGN];
    uint8_t *buf = raw + 1; /* unaligned */
    static const uint8_t tx[5] = { 0xEB, 0x00, 0x00, 0x00, 0x00 };
    int i;

    ck_assert_int_eq(qspi_transfer(&g_dev, tx, sizeof(tx), buf, 100,
        8, NULL, 0), 0);

    ck_assert_int_eq(g_moves_n, 1);
    ck_assert_uint_eq(g_moves[0].dst, (uint64_t)(uintptr_t)dma_tmpbuf);
    /* 100 bytes rounded up to a 64-byte (cache line) multiple */
    ck_assert_uint_eq(g_moves[0].size, 128);
    for (i = 0; i < 100; i++)
        ck_assert_uint_eq(buf[i], g_flash[i]);
}
END_TEST

/* Unaligned destination, read larger than the temp buffer: must be
 * processed in temp-buffer-sized passes, advancing the destination each
 * time, and never copy more than what was actually DMA'd. Pre-fix this
 * did one 4096-byte DMA and then memcpy'ed the full 9000 bytes out of
 * the 4096-byte dma_tmpbuf. */
START_TEST(test_dma_unaligned_oversized)
{
    static uint8_t XALIGNED(GQSPI_DMA_ALIGN) raw[9000 + 2 * GQSPI_DMA_ALIGN];
    uint8_t *buf = raw + 1; /* unaligned */
    static const uint8_t tx[5] = { 0xEB, 0x00, 0x00, 0x00, 0x00 };
    int i;

    ck_assert_int_eq(qspi_transfer(&g_dev, tx, sizeof(tx), buf, 9000,
        8, NULL, 0), 0);

    /* 4096 + 4096 + 808 (last DMA pass padded to 832) */
    ck_assert_int_eq(g_moves_n, 3);
    ck_assert_uint_eq(g_moves[0].size, 4096);
    ck_assert_uint_eq(g_moves[1].size, 4096);
    ck_assert_uint_eq(g_moves[2].size, 832);
    for (i = 0; i < 9000; i++)
        ck_assert_uint_eq(buf[i], g_flash[i]);
}
END_TEST

Suite *versal_qspi_dma_suite(void)
{
    Suite *s = suite_create("versal-qspi-dma");
    TCase *tc = tcase_create("versal-qspi-dma");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_dma_aligned_large);
    tcase_add_test(tc, test_dma_unaligned_small);
    tcase_add_test(tc, test_dma_unaligned_oversized);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = versal_qspi_dma_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
