/* unit-sdhci-write-tc-timeout.c
 *
 * A write that ends with both Transfer Complete and Data Timeout Error set
 * has completed: the SDHCI specification gives Transfer Complete priority.
 * The BCM2712 eMMC controller reports every polled write this way. The
 * driver must accept it, wait for the card to release DAT0 before the next
 * command, and still fail a Data Timeout Error that arrives alone.
 * SDHCI_WRITE_SINGLE_BLOCK must turn a multi-block disk_write() into one
 * CMD24 per block.
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
#define SDHCI_SDMA_DISABLED
#define SDHCI_WRITE_SINGLE_BLOCK
#define SDHCI_TC_OVER_DATA_TIMEOUT

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "sdhci.h"

#define TEST_BLOCKS 4U

static uint32_t mock_regs[0x260 / sizeof(uint32_t)];

/* SRS12 bits a data command raises, on top of Buffer Write Ready */
static uint32_t mock_data_status;
/* Reads of SRS09 that still show DAT0 low (card busy) after a data command */
static int mock_busy_reads;
static int mock_busy_left;
static int mock_cmd24_count;
static int mock_cmd25_count;
/* Set when a command was issued while the card still held DAT0 low */
static int mock_cmd_while_busy;

uint64_t hal_get_timer_us(void)
{
    static uint64_t now;
    return ++now;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
    if (offset == SDHCI_SRS09) {
        if (mock_busy_left > 0) {
            mock_busy_left--;
            return 0;
        }
        return SDHCI_SRS09_DAT0_LVL;
    }
    return mock_regs[offset / sizeof(uint32_t)];
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    uint32_t *reg = &mock_regs[offset / sizeof(uint32_t)];

    /* Reset bits self-clear, and the internal clock reports stable */
    if (offset == SDHCI_SRS11) {
        *reg = val | (val & SDHCI_SRS11_ICE ? SDHCI_SRS11_ICS : 0);
        *reg &= ~SDHCI_SRS11_RESET_DAT_CMD;
        return;
    }

    /* SRS12 is write-1-to-clear; Error Interrupt follows the error bits */
    if (offset == SDHCI_SRS12) {
        *reg &= ~val;
        if ((*reg & SDHCI_SRS12_ERR_STAT) == 0) {
            *reg &= ~SDHCI_SRS12_EINT;
        }
        return;
    }

    *reg = val;

    if (offset == SDHCI_SRS03) {
        uint32_t idx = (val >> SDHCI_SRS03_CIDX_SHIFT) & 0x3F;

        if (mock_busy_left > 0) {
            mock_cmd_while_busy = 1;
        }
        if (val & SDHCI_SRS03_DPS) {
            if (idx == MMC_CMD24_WRITE_SINGLE) {
                mock_cmd24_count++;
            }
            if (idx == MMC_CMD25_WRITE_MULTIPLE) {
                mock_cmd25_count++;
            }
            mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |=
                SDHCI_SRS12_BWR | mock_data_status;
            mock_busy_left = mock_busy_reads;
        }
        else {
            /* Command without data (CMD13) completes, card ready for data */
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

static uint32_t test_buf[(TEST_BLOCKS * SDHCI_BLOCK_SIZE) / sizeof(uint32_t)];

static void reset_mock_state(uint32_t data_status, int busy_reads)
{
    memset(mock_regs, 0, sizeof(mock_regs));
    memset(test_buf, 0xA5, sizeof(test_buf));
    mock_data_status = data_status;
    mock_busy_reads = busy_reads;
    mock_busy_left = 0;
    mock_cmd24_count = 0;
    mock_cmd25_count = 0;
    mock_cmd_while_busy = 0;
}

START_TEST(test_tc_with_data_timeout_is_success)
{
    int ret;

    reset_mock_state(SDHCI_SRS12_TC | SDHCI_SRS12_EDT | SDHCI_SRS12_EINT, 3);
    ret = sdhci_write(MMC_CMD24_WRITE_SINGLE, 0, test_buf, SDHCI_BLOCK_SIZE);
    ck_assert_int_eq(ret, 0);
    /* the busy on DAT0 was waited out before the status poll */
    ck_assert_int_eq(mock_busy_left, 0);
    ck_assert_int_eq(mock_cmd_while_busy, 0);
    ck_assert_uint_eq(mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] &
        SDHCI_SRS12_EDT, 0);
}
END_TEST

START_TEST(test_data_timeout_alone_is_error)
{
    int ret;

    reset_mock_state(SDHCI_SRS12_EDT | SDHCI_SRS12_EINT, 0);
    ret = sdhci_write(MMC_CMD24_WRITE_SINGLE, 0, test_buf, SDHCI_BLOCK_SIZE);
    ck_assert_int_ne(ret, 0);
}
END_TEST

START_TEST(test_tc_with_other_error_is_error)
{
    int ret;

    reset_mock_state(SDHCI_SRS12_TC | SDHCI_SRS12_EDT | SDHCI_SRS12_EDCRC |
        SDHCI_SRS12_EINT, 0);
    ret = sdhci_write(MMC_CMD24_WRITE_SINGLE, 0, test_buf, SDHCI_BLOCK_SIZE);
    ck_assert_int_ne(ret, 0);
}
END_TEST

START_TEST(test_disk_write_uses_single_blocks)
{
    int ret;

    reset_mock_state(SDHCI_SRS12_TC | SDHCI_SRS12_EDT | SDHCI_SRS12_EINT, 3);
    ret = disk_write(0, 0, sizeof(test_buf), (const uint8_t *)test_buf);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(mock_cmd24_count, (int)TEST_BLOCKS);
    ck_assert_int_eq(mock_cmd25_count, 0);
    ck_assert_int_eq(mock_cmd_while_busy, 0);
}
END_TEST

Suite *sdhci_write_suite(void)
{
    Suite *s = suite_create("sdhci-write-tc-timeout");
    TCase *tc = tcase_create("write-tc-timeout");

    tcase_add_test(tc, test_tc_with_data_timeout_is_success);
    tcase_add_test(tc, test_data_timeout_alone_is_error);
    tcase_add_test(tc, test_tc_with_other_error_is_error);
    tcase_add_test(tc, test_disk_write_uses_single_blocks);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr = srunner_create(sdhci_write_suite());

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (fails == 0) ? 0 : 1;
}
