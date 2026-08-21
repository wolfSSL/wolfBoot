/* unit-sdhci-disk-unaligned.c
 *
 * Unit tests for unaligned disk I/O via sdhci.c.
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

#define DISK_SDCARD 1
/* Enable the opt-in bounded multi-block write settle-wait with a tiny bound so
 * the withheld-TC path (H1) is reachable and fast in the test. */
#define SDHCI_WRITE_SETTLE_SPINS 16U

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "sdhci.h"

static uint32_t mock_regs[0x260 / sizeof(uint32_t)];
static uint8_t mock_disk[SDHCI_BLOCK_SIZE * 4];

struct transfer_state {
    int active;
    int is_read;
    uint32_t block_addr;
    uint32_t word_index;
    uint32_t word_count;
};

static struct transfer_state xfer;

/* Test knobs for the multi-block write settle-wait path (H1). */
static int mock_withhold_write_tc;    /* don't raise TC on a write data phase */
static int mock_fail_after_write_dps; /* fail non-data cmds after a write DPS */
static int mock_write_dps_seen;       /* a write data phase has been issued */

uint64_t hal_get_timer_us(void)
{
    static uint64_t now;
    return ++now;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
    if (offset == SDHCI_SRS08 && xfer.active && xfer.is_read) {
        uint32_t pos = xfer.block_addr * SDHCI_BLOCK_SIZE + (xfer.word_index * 4);
        uint32_t val;

        memcpy(&val, &mock_disk[pos], sizeof(val));
        xfer.word_index++;
        return val;
    }

    return mock_regs[offset / sizeof(uint32_t)];
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    uint32_t *reg = &mock_regs[offset / sizeof(uint32_t)];

    if (offset == SDHCI_SRS08 && xfer.active && !xfer.is_read) {
        uint32_t pos = xfer.block_addr * SDHCI_BLOCK_SIZE + (xfer.word_index * 4);

        memcpy(&mock_disk[pos], &val, sizeof(val));
        xfer.word_index++;
        return;
    }

    if (offset == SDHCI_SRS11) {
        *reg = val | (val & SDHCI_SRS11_ICE ? SDHCI_SRS11_ICS : 0);
        *reg &= ~SDHCI_SRS11_RESET_DAT_CMD;
        return;
    }

    if (offset == SDHCI_SRS12) {
        *reg &= ~val;
        return;
    }

    *reg = val;

    if (offset == SDHCI_SRS03) {
        if (val & SDHCI_SRS03_DPS) {
            xfer.active = 1;
            xfer.is_read = (val & SDHCI_SRS03_DTDS) != 0;
            xfer.block_addr = mock_regs[SDHCI_SRS02 / sizeof(uint32_t)];
            xfer.word_index = 0;
            xfer.word_count = SDHCI_BLOCK_SIZE / sizeof(uint32_t);
            if (xfer.is_read) {
                mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |=
                    (SDHCI_SRS12_BRR | SDHCI_SRS12_TC);
            }
            else {
                mock_write_dps_seen = 1;
                mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_BWR;
                /* Optionally withhold TC to exercise the bounded settle-wait. */
                if (!mock_withhold_write_tc) {
                    mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_TC;
                }
            }
        }
        else {
            /* Non-data command (CMD13/CMD12/...). Optionally fail once a write
             * data phase has occurred, to exercise the post-timeout
             * "completion NOT confirmed" path. */
            if (mock_fail_after_write_dps && mock_write_dps_seen) {
                mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_EINT;
            }
            else {
                mock_regs[SDHCI_SRS04 / sizeof(uint32_t)] = (1U << 8);
                mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_CC;
            }
        }
    }
}

void sdhci_platform_init(void)
{
}

void sdhci_platform_irq_init(void)
{
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
}

#include "../../src/sdhci.c"

static void reset_mock_state(void)
{
    uint32_t i;

    memset(mock_regs, 0, sizeof(mock_regs));
    memset(&xfer, 0, sizeof(xfer));
    mock_withhold_write_tc = 0;
    mock_fail_after_write_dps = 0;
    mock_write_dps_seen = 0;
    for (i = 0; i < sizeof(mock_disk); i++) {
        mock_disk[i] = (uint8_t)i;
    }
}

START_TEST(test_disk_read_unaligned_spans_blocks)
{
    uint8_t out[SDHCI_BLOCK_SIZE];
    uint8_t expected[SDHCI_BLOCK_SIZE];

    reset_mock_state();

    memcpy(expected, &mock_disk[SDHCI_BLOCK_SIZE - 1], 1);
    memcpy(expected + 1, &mock_disk[SDHCI_BLOCK_SIZE], SDHCI_BLOCK_SIZE - 1);

    ck_assert_int_eq(disk_read(0, SDHCI_BLOCK_SIZE - 1, sizeof(out), out), 0);
    ck_assert_mem_eq(out, expected, sizeof(out));
}
END_TEST

START_TEST(test_disk_write_unaligned_spans_blocks)
{
    uint8_t in[SDHCI_BLOCK_SIZE];
    uint8_t before[sizeof(mock_disk)];
    uint32_t i;

    reset_mock_state();
    memcpy(before, mock_disk, sizeof(before));

    for (i = 0; i < sizeof(in); i++) {
        in[i] = (uint8_t)(0xA0U + (i & 0x1FU));
    }

    ck_assert_int_eq(disk_write(0, SDHCI_BLOCK_SIZE - 1, sizeof(in), in), 0);
    ck_assert_mem_eq(mock_disk, before, SDHCI_BLOCK_SIZE - 1);
    ck_assert_uint_eq(mock_disk[SDHCI_BLOCK_SIZE - 1], in[0]);
    ck_assert_mem_eq(&mock_disk[SDHCI_BLOCK_SIZE], in + 1, SDHCI_BLOCK_SIZE - 1);
    ck_assert_uint_eq(mock_disk[(2 * SDHCI_BLOCK_SIZE) - 1],
        before[(2 * SDHCI_BLOCK_SIZE) - 1]);
}
END_TEST

/* A byte offset whose block address (offset / 512) does not fit in the 32-bit
 * SD command argument must be rejected, not silently truncated/wrapped to an
 * in-range sector. Here offset / 512 == 0x100000000, which truncates to 0. */
START_TEST(test_disk_read_rejects_block_addr_overflow)
{
    uint8_t out[SDHCI_BLOCK_SIZE];
    uint64_t start = (uint64_t)0x100000000ULL * SDHCI_BLOCK_SIZE;

    reset_mock_state();

    /* Must fail rather than wrap and read sector 0 (mock_disk[0..]). */
    ck_assert_int_ne(disk_read(0, start, sizeof(out), out), 0);
}
END_TEST

START_TEST(test_disk_write_rejects_block_addr_overflow)
{
    uint8_t in[SDHCI_BLOCK_SIZE];
    uint8_t before[sizeof(mock_disk)];
    uint64_t start = (uint64_t)0x100000000ULL * SDHCI_BLOCK_SIZE;

    reset_mock_state();
    memcpy(before, mock_disk, sizeof(before));
    memset(in, 0x5A, sizeof(in));

    /* Must fail rather than wrap and overwrite sector 0. */
    ck_assert_int_ne(disk_write(0, start, sizeof(in), in), 0);
    ck_assert_mem_eq(mock_disk, before, sizeof(before));
}
END_TEST

/* H1: a multi-block (CMD25) write whose controller withholds TC until CMD12
 * must not hang - the bounded settle-wait fires and completion is re-confirmed
 * via CMD13. Returns success because the card side completes. */
START_TEST(test_multiblock_write_tc_withheld_confirms)
{
    uint32_t buf[(2 * SDHCI_BLOCK_SIZE) / sizeof(uint32_t)];
    uint32_t i;

    reset_mock_state();
    for (i = 0; i < sizeof(buf) / sizeof(buf[0]); i++)
        buf[i] = 0xC5A50000U + i;

    mock_withhold_write_tc = 1;

    ck_assert_int_eq(
        sdhci_write(MMC_CMD25_WRITE_MULTIPLE, 0, buf, sizeof(buf)), 0);
}
END_TEST

/* H1: same withheld-TC bound, but the card fails to confirm completion (the
 * CMD12 after the data phase errors). The write must be reported as FAILED, not
 * silently landed - distinguishing it from the confirmed case above. */
START_TEST(test_multiblock_write_tc_withheld_unconfirmed_fails)
{
    uint32_t buf[(2 * SDHCI_BLOCK_SIZE) / sizeof(uint32_t)];
    uint32_t i;

    reset_mock_state();
    for (i = 0; i < sizeof(buf) / sizeof(buf[0]); i++)
        buf[i] = 0x5A5A0000U + i;

    mock_withhold_write_tc = 1;
    mock_fail_after_write_dps = 1;

    ck_assert_int_ne(
        sdhci_write(MMC_CMD25_WRITE_MULTIPLE, 0, buf, sizeof(buf)), 0);
}
END_TEST

Suite *sdhci_disk_suite(void)
{
    Suite *s = suite_create("sdhci-disk");
    TCase *tc = tcase_create("unaligned");

    tcase_add_test(tc, test_disk_read_unaligned_spans_blocks);
    tcase_add_test(tc, test_disk_write_unaligned_spans_blocks);
    tcase_add_test(tc, test_disk_read_rejects_block_addr_overflow);
    tcase_add_test(tc, test_disk_write_rejects_block_addr_overflow);
    tcase_add_test(tc, test_multiblock_write_tc_withheld_confirms);
    tcase_add_test(tc, test_multiblock_write_tc_withheld_unconfirmed_fails);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = sdhci_disk_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
