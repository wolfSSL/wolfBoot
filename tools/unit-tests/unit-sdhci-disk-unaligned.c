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
            mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= xfer.is_read ?
                (SDHCI_SRS12_BRR | SDHCI_SRS12_TC) :
                (SDHCI_SRS12_BWR | SDHCI_SRS12_TC);
        }
        else {
            mock_regs[SDHCI_SRS04 / sizeof(uint32_t)] = (1U << 8);
            mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_CC;
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

Suite *sdhci_disk_suite(void)
{
    Suite *s = suite_create("sdhci-disk");
    TCase *tc = tcase_create("unaligned");

    tcase_add_test(tc, test_disk_read_unaligned_spans_blocks);
    tcase_add_test(tc, test_disk_write_unaligned_spans_blocks);
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
