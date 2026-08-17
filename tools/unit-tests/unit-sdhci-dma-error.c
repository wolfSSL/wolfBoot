/* unit-sdhci-dma-error.c
 *
 * Regression test: a failed SDMA data transfer must be reported as an error.
 *
 * sdhci_transfer() waits for Transfer Complete on the SDMA path and sets
 * status = -1 if that wait times out. The post-transfer "check for errors"
 * block then runs unconditionally, and a wolfBoot-side wait timeout does not
 * necessarily leave an error bit set in SRS12. Before the fix, the CMD12
 * (stop transmission) result overwrote that -1, so the function returned
 * success for a transfer that never moved any data.
 *
 * The caller then treats a partially filled buffer as a good read. On a
 * verified-boot target that surfaces much later as an image integrity
 * failure rather than as the I/O error it actually is -- and where
 * anti-rollback is enabled, the good lower-versioned slot is refused too,
 * leaving nothing bootable.
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

/* When 0, a data transfer never signals Transfer Complete -- the SDMA wait
 * times out, which is the condition under test. When 1, the transfer
 * completes normally (positive control). */
static int mock_complete_transfer;

/* Monotonic, so udelay() in the included sdhci.c always terminates. A
 * constant timer makes its "while (hal_get_timer_us() < end)" loop spin
 * forever if any code path under test ever calls it. */
uint64_t hal_get_timer_us(void)
{
    static uint64_t now;
    return ++now;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
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

    /* SRS12 is write-1-to-clear */
    if (offset == SDHCI_SRS12) {
        *reg &= ~val;
        return;
    }

    *reg = val;

    if (offset == SDHCI_SRS03) {
        if (val & SDHCI_SRS03_DPS) {
            /* Data-present command. Signal Transfer Complete only when the
             * test asks for it. No error bits are set either way: this
             * reproduces a wait timeout with a clean SRS12, which is what
             * made the old code discard the error. */
            if (mock_complete_transfer) {
                mock_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_TC;
            }
        }
        else {
            /* Command without data (e.g. CMD12 stop) completes normally */
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

/* Large enough to take the SDMA path (>= SDHCI_DMA_THRESHOLD) */
#define TEST_XFER_SZ (8U * 1024U)

static uint32_t test_buf[TEST_XFER_SZ / sizeof(uint32_t)];

static void reset_mock_state(int complete)
{
    memset(mock_regs, 0, sizeof(mock_regs));
    memset(test_buf, 0, sizeof(test_buf));
    mock_complete_transfer = complete;
    g_mmc_irq_pending = 0;
    g_mmc_irq_status = 0;
}

/* The bug: SDMA never completes, SRS12 carries no error bit, and the CMD12
 * issued afterwards succeeds -- which used to overwrite the error. */
START_TEST(test_sdma_timeout_is_reported)
{
    int ret;

    reset_mock_state(0);
    ret = sdhci_read(MMC_CMD18_READ_MULTIPLE, 0, test_buf, TEST_XFER_SZ);
    ck_assert_msg(ret != 0,
        "sdhci_read must fail when the SDMA transfer never completes "
        "(returned %d)", ret);
}
END_TEST

/* Positive control: with Transfer Complete signalled the same path succeeds,
 * so the assertions above are detecting the timeout and not simply a mock
 * that can never succeed. */
START_TEST(test_completed_transfer_succeeds)
{
    int ret;

    reset_mock_state(1);
    ret = sdhci_read(MMC_CMD18_READ_MULTIPLE, 0, test_buf, TEST_XFER_SZ);
    ck_assert_msg(ret == 0,
        "sdhci_read must succeed when the transfer completes (returned %d)",
        ret);
}
END_TEST

Suite *sdhci_dma_suite(void)
{
    Suite *s = suite_create("sdhci-dma-error");
    TCase *tc = tcase_create("dma-error");

    /* The timeout path spins a large retry count before giving up */
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_sdma_timeout_is_reported);
    tcase_add_test(tc, test_completed_transfer_succeeds);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr = srunner_create(sdhci_dma_suite());

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (fails == 0) ? 0 : 1;
}
