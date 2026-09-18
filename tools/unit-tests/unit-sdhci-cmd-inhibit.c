/* unit-sdhci-cmd-inhibit.c
 *
 * Two card-initialization defects on the SDHCI command path.
 *
 * 1. An unbounded spin on Command Inhibit (CMD). A command that times out
 *    leaves CICMD set in SRS09, and only a CMD-line reset clears it, but
 *    sdhci_send_cmd_internal() waited for it with a bare
 *        while ((SDHCI_REG(SDHCI_SRS09) & SDHCI_SRS09_CICMD) != 0);
 *    both before sending and after an error. So the first failed command hung
 *    the boot instead of returning: the caller never saw the error and could
 *    not retry, report or fall back. Observed on an i.MX 8QuadMax MEK, where a
 *    warm reboot after Linux had used UHS-I stopped dead after the CMD8 error
 *    line with no further output.
 *
 * 2. An SD v1.x card does not implement CMD8 and never answers it. Treating
 *    that silence as fatal rejected every legacy card. Nothing in the init
 *    path reads the CMD8 response - ACMD41 carries HCS, which a v1.x card
 *    ignores - so a missing answer must not abort initialization.
 *
 * The real driver is compiled from the generated sdhci_host.c. A register-read
 * cap turns the pre-fix infinite spin into a test failure instead of a hung
 * build, and the ACMD41 poll count is the signal that init got past CMD8.
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

static uint32_t g_sdhci_regs[0x400 / sizeof(uint32_t)];

/* Read cap. The pre-fix CICMD spin issues no commands, so only a read cap can
 * turn it into a failure rather than a hang. The bounded wait is
 * SDHCI_INHIBIT_TIMEOUT iterations, so allow generously more than that. */
static unsigned long g_reads;
#define READ_CAP 40000000UL

/* Card / controller model. */
static int g_cmd8_never;      /* v1.x: CMD8 is never answered */
static int g_cmd8_needs_18v;  /* left in UHS: answered only at 1.8V */
static int g_cicmd_stuck;     /* the command line never goes idle */
static int g_cmd8_mangled;    /* CMD8 IS answered, but the link corrupts it */
static int g_reset_never;     /* the reset bit itself never self-clears */
static int g_inhibit_sticky;  /* reset completes, but CICMD stays set */
static unsigned int g_cmd8_attempts;
static unsigned int g_acmd41_polls;
static unsigned int g_ready_after;

uint32_t sdhci_reg_read(uint32_t offset)
{
    if (++g_reads > READ_CAP)
        ck_abort_msg("register read loop ran away: %lu reads", g_reads);

    if (offset == SDHCI_SRS09 && g_cicmd_stuck)
        return g_sdhci_regs[offset / sizeof(uint32_t)] | SDHCI_SRS09_CICMD;

    return g_sdhci_regs[offset / sizeof(uint32_t)];
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    uint32_t idx;

    if (offset == SDHCI_SRS12) {
        g_sdhci_regs[offset / sizeof(uint32_t)] &= ~val;
        return;
    }

    /* A CMD-line reset is what clears the inhibit on real hardware; model
     * that so the fixed driver can make progress after resetting. */
    if (offset == SDHCI_SRS11 && (val & SDHCI_SRS11_RESET_DAT_CMD) != 0) {
        if (g_reset_never) {
            /* Controller wedged: the reset bit never self-clears. */
            g_sdhci_regs[offset / sizeof(uint32_t)] = val;
            return;
        }
        /* The reset bit self-clears as on real hardware. Whether that also
         * releases the inhibit is the thing under test. */
        if (!g_inhibit_sticky)
            g_cicmd_stuck = 0;
        g_sdhci_regs[offset / sizeof(uint32_t)] =
            val & ~(uint32_t)SDHCI_SRS11_RESET_DAT_CMD;
        return;
    }

    g_sdhci_regs[offset / sizeof(uint32_t)] = val;

    if (offset != SDHCI_SRS03)
        return;

    idx = (val & SDHCI_SRS03_CIDX_MASK) >> SDHCI_SRS03_CIDX_SHIFT;

    if (idx == SD_CMD8_SEND_IF_COND) {
        int at_18v = (g_sdhci_regs[SDHCI_SRS15 / sizeof(uint32_t)]
                      & SDHCI_SRS15_V18SE) != 0;

        g_cmd8_attempts++;
        if (g_cmd8_mangled) {
            /* The card DID answer; the link mangled the response. Not a
             * timeout, so not a legacy card. */
            g_sdhci_regs[SDHCI_SRS12 / sizeof(uint32_t)] |=
                SDHCI_SRS12_EINT | SDHCI_SRS12_ECCRC | SDHCI_SRS12_ECI;
            return;
        }
        if (g_cmd8_never || (g_cmd8_needs_18v && !at_18v)) {
            g_sdhci_regs[SDHCI_SRS12 / sizeof(uint32_t)] |=
                SDHCI_SRS12_EINT | SDHCI_SRS12_ECT;
            return;
        }
    }

    g_sdhci_regs[SDHCI_SRS12 / sizeof(uint32_t)] |= SDHCI_SRS12_CC;

    if (idx == SD_ACMD41_SEND_OP_COND) {
        g_acmd41_polls++;
        if (g_ready_after > 0 && g_acmd41_polls >= g_ready_after) {
            g_sdhci_regs[SDHCI_SRS04 / sizeof(uint32_t)] |=
                SDCARD_REG_OCR_READY;
            g_ready_after = 0;
        }
    }
}

static uint64_t g_timer_us;
uint64_t hal_get_timer_us(void)
{
    g_timer_us += 10000;
    return g_timer_us;
}

/* Counted: the bounded recovery waits must keep servicing the watchdog, or on
 * a platform whose watchdog cannot be disabled (the PolarFire MSS pair) a long
 * recovery is cut short by a chip reset. */
static unsigned long g_wdt_pets;
void sdhci_platform_wdt_pet(void) { g_wdt_pets++; }
void sdhci_platform_init(void) { }
void sdhci_platform_irq_init(void) { }
void sdhci_platform_set_bus_mode(int is_emmc) { (void)is_emmc; }

#include "sdhci_host.c"

int sdhci_read(uint32_t cmd_index, uint32_t block_addr, uint32_t *dst,
    uint32_t sz)
{
    (void)cmd_index; (void)block_addr; (void)dst; (void)sz;
    return -1;
}

static void script_card_present(void)
{
    g_sdhci_regs[SDHCI_SRS12 / sizeof(uint32_t)] = SDHCI_SRS12_CC;
    g_sdhci_regs[SDHCI_SRS04 / sizeof(uint32_t)] =
        (1U << 8) | SDCARD_REG_OCR_3_3_3_4;
    g_sdhci_regs[SDHCI_SRS16 / sizeof(uint32_t)] = SDHCI_SRS16_VS33;
    g_sdhci_regs[SDHCI_SRS18 / sizeof(uint32_t)] = 0;
}

static void setup(void)
{
    memset(g_sdhci_regs, 0, sizeof(g_sdhci_regs));
    g_timer_us = 0;
    g_reads = 0;
    g_cmd8_never = 0;
    g_cmd8_needs_18v = 0;
    g_cmd8_mangled = 0;
    g_reset_never = 0;
    g_inhibit_sticky = 0;
    g_wdt_pets = 0;
    g_cicmd_stuck = 0;
    g_cmd8_attempts = 0;
    g_acmd41_polls = 0;
    g_ready_after = 3;
}

static void teardown(void) { }

/* A command line that never goes idle must not hold the boot. Pre-fix this
 * spins for ever and the read cap aborts; fixed, the bounded wait resets the
 * lines and initialization returns. */
START_TEST(test_command_inhibit_does_not_hang)
{
    script_card_present();
    g_cicmd_stuck = 1;

    (void)sdcard_card_full_init();

    /* the point is that control returned at all */
    ck_assert_uint_lt(g_reads, READ_CAP);
    /* and that the driver cleared the inhibit with a reset */
    ck_assert_int_eq(g_cicmd_stuck, 0);
}
END_TEST

/* An SD v1.x card never answers CMD8. Initialization must continue: the
 * response is unused, and ACMD41 is only reached if CMD8 did not abort. */
START_TEST(test_cmd8_absent_is_not_fatal)
{
    script_card_present();
    g_cmd8_never = 1;

    (void)sdcard_card_full_init();

#ifndef SDHCI_UHS_RECOVER_ON_INIT
    /* one attempt; with the opt-in enabled the 1.8V retry adds a second */
    ck_assert_uint_eq(g_cmd8_attempts, 1);
#endif
    ck_assert_uint_gt(g_acmd41_polls, 0);
}
END_TEST

/* A normal card answers CMD8 and initialization proceeds as before. */
START_TEST(test_cmd8_answered_normally)
{
    script_card_present();

    (void)sdcard_card_full_init();

    ck_assert_uint_eq(g_cmd8_attempts, 1);
    ck_assert_uint_gt(g_acmd41_polls, 0);
}
END_TEST

#ifdef SDHCI_UHS_RECOVER_ON_INIT
/* With the opt-in enabled, a card that answers CMD8 only at 1.8V must be
 * recovered: the host switches signaling, the retry is answered, and the
 * host is left at 1.8V to match the card. */
START_TEST(test_uhs_recover_on_init)
{
    script_card_present();
    g_cmd8_needs_18v = 1;

    (void)sdcard_card_full_init();

    ck_assert_uint_ge(g_cmd8_attempts, 2);
    ck_assert_int_ne((g_sdhci_regs[SDHCI_SRS15 / sizeof(uint32_t)]
                      & SDHCI_SRS15_V18SE), 0);
    ck_assert_uint_gt(g_acmd41_polls, 0);
}
END_TEST

/* A card that answers at neither voltage must roll the host back to 3.3V. */
START_TEST(test_uhs_recover_rolls_back)
{
    script_card_present();
    g_cmd8_never = 1;

    (void)sdcard_card_full_init();

    ck_assert_uint_ge(g_cmd8_attempts, 2);
    ck_assert_int_eq((g_sdhci_regs[SDHCI_SRS15 / sizeof(uint32_t)]
                      & SDHCI_SRS15_V18SE), 0);
    ck_assert_uint_gt(g_acmd41_polls, 0);
}
END_TEST
#endif

/* A mangled CMD8 response is NOT a legacy card.
 *
 * Silence means the card does not implement CMD8; a CRC or index error means
 * it answered and the link corrupted the answer. Excusing the second would
 * run the whole of initialization over a bus already known to be broken, so
 * init must fail instead of continuing. */
START_TEST(test_cmd8_bus_error_is_still_fatal)
{
    int rc;

    script_card_present();
    g_cmd8_mangled = 1;

    rc = sdcard_card_full_init();

    ck_assert_int_ne(rc, 0);
    ck_assert_uint_gt(g_cmd8_attempts, 0);
    /* Never reached ACMD41: the failure was propagated, not swallowed. */
    ck_assert_uint_eq(g_acmd41_polls, 0);
}
END_TEST

/* A reset that completes without releasing the inhibit must not hang.
 *
 * This is the gap that bounding only the reset leaves: the reset bit
 * self-clears, so a bounded reset looks successful, and a caller that then
 * waits unbounded on CICMD/CIDAT spins for ever. The read cap in this
 * harness is what fails the test if it does. */
START_TEST(test_inhibit_that_survives_reset_does_not_hang)
{
    int rc;

    script_card_present();
    g_cicmd_stuck = 1;
    g_inhibit_sticky = 1;

    rc = sdcard_card_full_init();

    ck_assert_int_ne(rc, 0);
    /* and the long wait serviced the watchdog rather than starving it */
    ck_assert_uint_gt(g_wdt_pets, 0);
}
END_TEST

/* A reset whose own bit never self-clears must also be reported. */
START_TEST(test_reset_that_never_clears_does_not_hang)
{
    int rc;

    script_card_present();
    g_cicmd_stuck = 1;
    g_reset_never = 1;

    rc = sdcard_card_full_init();

    ck_assert_int_ne(rc, 0);
    ck_assert_uint_gt(g_wdt_pets, 0);
}
END_TEST

Suite *sdhci_cmd_inhibit_suite(void)
{
    Suite *s = suite_create("sdhci-cmd-inhibit");
    TCase *tc = tcase_create("cmd-inhibit");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_command_inhibit_does_not_hang);
    tcase_add_test(tc, test_cmd8_absent_is_not_fatal);
    tcase_add_test(tc, test_cmd8_answered_normally);
    tcase_add_test(tc, test_cmd8_bus_error_is_still_fatal);
    tcase_add_test(tc, test_inhibit_that_survives_reset_does_not_hang);
    tcase_add_test(tc, test_reset_that_never_clears_does_not_hang);
#ifdef SDHCI_UHS_RECOVER_ON_INIT
    tcase_add_test(tc, test_uhs_recover_on_init);
    tcase_add_test(tc, test_uhs_recover_rolls_back);
#endif
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = sdhci_cmd_inhibit_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
