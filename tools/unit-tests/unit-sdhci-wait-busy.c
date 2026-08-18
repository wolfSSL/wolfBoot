/* unit-sdhci-wait-busy.c
 *
 * Regression test for F-7984: sdhci_wait_busy() had no timeout in
 * either its DATA0 polling loop or its repeated CMD13 loop (the
 * in-code TODO acknowledged it). A removed card, a controller fault,
 * or a card stuck in the programming state left wolfBoot spinning
 * forever instead of returning an I/O error.
 *
 * The test compiles the real src/sdhci.c (as sdhci_host.c, generated
 * by the Makefile: asm blanked, sdhci_read renamed to sdhci_read_hw)
 * and scripts the controller through the host register file:
 *   - the DATA0 level (SDHCI_SRS09_DAT0_LVL) stays low for the
 *     data-line timeout case;
 *   - the command always "completes" (SDHCI_SRS12_CC preset) with the
 *     READY_FOR_DATA bit (bit 8) of the response register
 *     (SDHCI_SRS04) cleared, so every CMD13 reports DEVICE_BUSY.
 * The emulated hal_get_timer_us() advances by a settable step per read,
 * so the shipped SDHCI_WAIT_BUSY_TIMEOUT_MS is exercised as built --
 * a 1 ms step reaches the 30 s budget in ~30k iterations -- rather than
 * against a shrunken stand-in value. Pre-fix, both timeout cases never
 * return (the test runner's timeout kills them).
 *
 * The stub also counts sdhci_platform_wdt_pet() calls: a bounded wait
 * this long must service the watchdog or it becomes a reset rather than
 * the clean I/O error it is meant to be.
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

#include "sdhci.h"
#include "disk.h"

/* Host register file for the platform-provided register accessors. */
static uint32_t g_sdhci_regs[0x400 / sizeof(uint32_t)];

uint32_t sdhci_reg_read(uint32_t offset)
{
    return g_sdhci_regs[offset / sizeof(uint32_t)];
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    g_sdhci_regs[offset / sizeof(uint32_t)] = val;
}

/* Timer: advance by g_timer_step microseconds on every read, so a test
 * can reach the shipped timeout in a sane number of iterations. */
static uint64_t g_timer_us;
static uint64_t g_timer_step = 1;
uint64_t hal_get_timer_us(void)
{
    g_timer_us += g_timer_step;
    return g_timer_us;
}

/* Counts watchdog services performed inside the busy loops. */
static unsigned int g_wdt_pets;
void sdhci_platform_wdt_pet(void)
{
    g_wdt_pets++;
}

/* Platform hooks the SD init path touches; irrelevant here. */
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

/* The real sdhci_read() is renamed to sdhci_read_hw() in the generated
 * copy; the data path is not under test, so this stub just satisfies
 * the disk_read()/disk_write() references. */
int sdhci_read(uint32_t cmd_index, uint32_t block_addr, uint32_t *dst,
    uint32_t sz)
{
    (void)cmd_index; (void)block_addr; (void)dst; (void)sz;
    return -1;
}

/* The real driver (see the Makefile for the transforms). */
#include "sdhci_host.c"

static void setup(void)
{
    memset(g_sdhci_regs, 0, sizeof(g_sdhci_regs));
    g_timer_us = 0;
    g_wdt_pets = 0;
    /* 1 ms per timer read: the shipped 30 s budget is reached in ~30k
     * iterations, so the real value is what gets exercised. */
    g_timer_step = 1000;
}

static void teardown(void)
{
}

/* A card stuck with DATA0 low must time out instead of hanging. */
START_TEST(test_wait_busy_dat0_timeout)
{
    /* DAT0 low: the card asserts the data line (programming). */
    g_sdhci_regs[SDHCI_SRS09 / 4] &= ~SDHCI_SRS09_DAT0_LVL;

    ck_assert_int_eq(sdhci_wait_busy(1), -1);
    /* the wait ran to the shipped budget, so it must have petted */
    ck_assert_uint_gt(g_wdt_pets, 0);
}
END_TEST

/* A card whose CMD13 keeps reporting DEVICE_BUSY must time out. */
START_TEST(test_wait_busy_cmd13_timeout)
{
    /* DAT0 high (not busy), so only the CMD13 loop runs. The command
     * always completes; the response READY_FOR_DATA bit stays clear,
     * so sdhci_cmd() returns DEVICE_BUSY every time. */
    g_sdhci_regs[SDHCI_SRS09 / 4] |= SDHCI_SRS09_DAT0_LVL;
    g_sdhci_regs[SDHCI_SRS12 / 4] = SDHCI_SRS12_CC;
    g_sdhci_regs[SDHCI_SRS04 / 4] = 0;

    ck_assert_int_eq(sdhci_wait_busy(0), -1);
    ck_assert_uint_gt(g_wdt_pets, 0);
}
END_TEST

/* A ready card returns success promptly. */
START_TEST(test_wait_busy_ready)
{
    /* DAT0 high; the first CMD13 response has READY_FOR_DATA set, so
     * sdhci_cmd() returns 0 and the loop exits. */
    g_sdhci_regs[SDHCI_SRS09 / 4] |= SDHCI_SRS09_DAT0_LVL;
    g_sdhci_regs[SDHCI_SRS12 / 4] = SDHCI_SRS12_CC;
    g_sdhci_regs[SDHCI_SRS04 / 4] = 1U << 8;

    ck_assert_int_eq(sdhci_wait_busy(1), 0);
}
END_TEST

Suite *sdhci_wait_busy_suite(void)
{
    Suite *s = suite_create("sdhci-wait-busy");
    TCase *tc = tcase_create("sdhci-wait-busy");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_wait_busy_dat0_timeout);
    tcase_add_test(tc, test_wait_busy_cmd13_timeout);
    tcase_add_test(tc, test_wait_busy_ready);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = sdhci_wait_busy_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
