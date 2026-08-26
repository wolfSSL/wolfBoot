/* unit-sdhci-acmd41-timeout.c
 *
 * Regression test for F-11032: sdcard_card_full_init() polled ACMD41
 * in an unbounded do/while until the card set OCR ready, so a card
 * that answers every ACMD41 without ever setting the bit held the
 * bootloader in the loop forever. (F-7984 bounded the separate
 * DATA0/CMD13 waits in sdhci_wait_busy(); this is the OCR readiness
 * path.)
 *
 * The real driver is compiled from the generated sdhci_host.c
 * (identical to src/sdhci.c except the three x86-incompatible asm
 * statements are blanked and sdhci_read() is renamed to
 * sdhci_read_hw()); the controller is scripted through the host
 * register file. The card model sets OCR READY after a configurable
 * number of ACMD41 polls (0 = never). The command-write cap turns the
 * pre-fix infinite loop into an abort instead of a hung CI.
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

/* Command-write cap: the pre-fix ACMD41 loop never terminates, so the
 * cap aborts the test (a failure) instead of hanging the build. The
 * fixed loop reaches the shipped 30 s budget in well under the cap. */
static uint32_t g_cmd_writes;
#define CMD_WRITE_CAP 50000

/* Card model: after g_ready_after ACMD41 polls the card sets OCR
 * ready (0 = the card never becomes ready). */
static uint32_t g_ready_after;
static uint32_t g_acmd41_polls;

uint32_t sdhci_reg_read(uint32_t offset)
{
    return g_sdhci_regs[offset / sizeof(uint32_t)];
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    if (offset == SDHCI_SRS12) {
        /* interrupt status register: write-1-to-clear */
        g_sdhci_regs[offset / sizeof(uint32_t)] &= ~val;
        return;
    }

    g_sdhci_regs[offset / sizeof(uint32_t)] = val;

    if (offset == SDHCI_SRS03) {
        uint32_t idx;

        /* the scripted controller completes every command without
         * error; the response is read from the preset SRS04 */
        g_sdhci_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_CC;

        g_cmd_writes++;
        if (g_cmd_writes > CMD_WRITE_CAP)
            ck_abort_msg("ACMD41 poll ran away: %u commands",
                         g_cmd_writes);

        idx = (val & SDHCI_SRS03_CIDX_MASK) >> SDHCI_SRS03_CIDX_SHIFT;
        if (idx == SD_ACMD41_SEND_OP_COND && g_ready_after > 0) {
            g_acmd41_polls++;
            if (g_acmd41_polls >= g_ready_after) {
                g_sdhci_regs[SDHCI_SRS04 / sizeof(uint32_t)] |=
                    SDCARD_REG_OCR_READY;
                g_ready_after = 0;
            }
        }
    }
}

/* Timer: advance by g_timer_step microseconds on every read, so the
 * shipped SDCARD_ACMD41_TIMEOUT_MS is exercised as built. */
static uint64_t g_timer_us;
static uint64_t g_timer_step = 10000;
uint64_t hal_get_timer_us(void)
{
    g_timer_us += g_timer_step;
    return g_timer_us;
}

/* Counts watchdog services performed inside the poll loop. */
static unsigned int g_wdt_pets;
void sdhci_platform_wdt_pet(void)
{
    g_wdt_pets++;
}

/* Platform hooks the SD init path touches. */
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

/* The real driver (see the Makefile for the transforms). */
#include "sdhci_host.c"

/* The data path is not under test; the stub ends the init sequence
 * after the ACMD41 loop, the bus-width setup and the SCR read. */
int sdhci_read(uint32_t cmd_index, uint32_t block_addr, uint32_t *dst,
    uint32_t sz)
{
    (void)cmd_index; (void)block_addr; (void)dst; (void)sz;
    return -1;
}

/* Script the controller for a card present at 3.3 V: every command
 * completes immediately (SRS12 CC preset), responses come from the
 * preset SRS04 (READY_FOR_DATA for R1, voltage bits for the OCR),
 * the host is 3.3-V capable, and no 1.8-V / UHS features are
 * advertised so the init stays on the plain path. */
static void script_card_present(void)
{
    g_sdhci_regs[SDHCI_SRS12 / sizeof(uint32_t)] = SDHCI_SRS12_CC;
    g_sdhci_regs[SDHCI_SRS04 / sizeof(uint32_t)] =
        (1U << 8) | SDCARD_REG_OCR_3_3_3_4;
    g_sdhci_regs[SDHCI_SRS16 / sizeof(uint32_t)] = SDHCI_SRS16_VS33;
    g_sdhci_regs[SDHCI_SRS18 / sizeof(uint32_t)] = 0; /* no 1.8V current */
}

static void setup(void)
{
    memset(g_sdhci_regs, 0, sizeof(g_sdhci_regs));
    g_timer_us = 0;
    g_timer_step = 10000; /* 10 ms per timer read */
    g_wdt_pets = 0;
    g_cmd_writes = 0;
    g_acmd41_polls = 0;
    g_ready_after = 0;
}

static void teardown(void)
{
}

/* A card that answers ACMD41 forever without setting OCR ready must
 * time out with an error instead of holding the boot. Pre-fix the
 * loop never terminates and the command-write cap aborts. */
START_TEST(test_acmd41_never_ready_times_out)
{
    int ret;

    script_card_present();
    g_ready_after = 0; /* never ready */

    ret = sdcard_card_full_init();

    ck_assert_int_eq(ret, -1);
    /* the poll ran to the shipped budget, so it must have petted */
    ck_assert_uint_gt(g_wdt_pets, 0);
    /* and stayed far below the runaway cap */
    ck_assert_uint_lt(g_cmd_writes, CMD_WRITE_CAP);
}
END_TEST

/* A card that reports ready after a few polls must initialize: the
 * loop exits promptly (well under the cap) and the sequence proceeds
 * to the end of the init path (the stubbed SCR read ends it). */
START_TEST(test_acmd41_ready_after_polls)
{
    int ret;

    script_card_present();
    g_ready_after = 5;

    ret = sdcard_card_full_init();

    /* the loop must have exited on OCR ready, not on the timeout */
    ck_assert_uint_eq(g_acmd41_polls, 5);
    ck_assert_uint_lt(g_cmd_writes, 1000);
    /* the init ran to the stubbed data path, which fails by design */
    ck_assert_int_eq(ret, -1);
}
END_TEST

Suite *sdhci_acmd41_suite(void)
{
    Suite *s = suite_create("sdhci-acmd41-timeout");
    TCase *tc = tcase_create("acmd41-timeout");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_acmd41_never_ready_times_out);
    tcase_add_test(tc, test_acmd41_ready_after_polls);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = sdhci_acmd41_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
