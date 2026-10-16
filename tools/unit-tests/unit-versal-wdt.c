/* unit-versal-wdt.c
 *
 * Unit tests for the Versal system watchdog (WWDT) driver in hal/versal.c.
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
#include <string.h>

/* Register file, with the generic-mode page at 0x1000/0x2000 included */
#define WWDT_REGS_SIZE      0x2010
static uint32_t mock_regs[WWDT_REGS_SIZE / sizeof(uint32_t)];
static uint32_t shadow[WWDT_REGS_SIZE / sizeof(uint32_t)];

#define WATCHDOG_BASE       ((uintptr_t)mock_regs)
#define WATCHDOG_TIMEOUT_MS 1000U
#define WATCHDOG_CLK_HZ     100000000UL
#define TIMER_CLK_FREQ      100000000UL
#define RAMFUNCTION

/* QSPI register file the generated header rebases onto host memory */
static uint32_t g_vqspi_regs[0x900 / sizeof(uint32_t)];

#include "versal_host.h"

#define R(off) mock_regs[(off) / sizeof(uint32_t)]
#define S(off) shadow[(off) / sizeof(uint32_t)]

#define wolfBoot_printf(...) do {} while (0)

/* Emulated watchdog. The driver ends every register write with dsb(), so that
 * is where the model applies the hardware's rules: the window registers ignore
 * writes unless MWR[MWC] is set; a restart or a disable outside the open
 * window is a bad event and the disable is not honored; nor is one while
 * FCR[WDP] is set; and enabling or restarting begins the first window, open at
 * once when FWR is 0. A restart inside the open window writes ESR back
 * unchanged, so the tests assert on whether the driver unlocked the space to
 * attempt one at all. */
static int wdt_enabled;
static int wdt_open;
static int wdt_mwc;
static int wdt_bad_events;
static int wdt_blocked_writes;
static int wdt_mwc_writes;
static int wdt_generic_refresh;
static int wdt_wdp_cleared;
static uint32_t wdt_fcv;

/* Polling model for the wait on the open window */
#define WDT_MAX_POLLS   50
static int wdt_polls;
static int wdt_polls_to_open;

static void wdt_commit_esr(uint32_t written)
{
    uint32_t esr = 0;

    if (wdt_enabled) {
        esr |= WWDT_ESR_WEN;
    }
    if (wdt_open) {
        esr |= WWDT_ESR_WSW;
    }
    /* WINT and WRP are write-1-to-clear */
    esr |= S(WWDT_ESR_OFFSET) & (WWDT_ESR_WINT | WWDT_ESR_WRP) & ~written;
    esr |= (wdt_fcv << WWDT_ESR_FCV_SHIFT) & WWDT_ESR_FCV_MASK;
    R(WWDT_ESR_OFFSET) = esr;
    S(WWDT_ESR_OFFSET) = esr;
}

static void wdt_window_start(void)
{
    wdt_open = (S(WWDT_FWR_OFFSET) == 0) ? 1 : 0;
}

static void wdt_sync(void)
{
    uint32_t esr, fcr;

    if (R(WWDT_MWR_OFFSET) != S(WWDT_MWR_OFFSET)) {
        if (R(WWDT_MWR_OFFSET) & WWDT_MWR_MWC) {
            wdt_mwc = 1;
            wdt_mwc_writes++;
        }
        else {
            wdt_mwc = 0;
        }
        S(WWDT_MWR_OFFSET) = R(WWDT_MWR_OFFSET);
    }

    /* The generic-mode page needs no unlock */
    if (R(WWDT_GWRR_OFFSET) != S(WWDT_GWRR_OFFSET)) {
        if (R(WWDT_GWRR_OFFSET) & WWDT_GWRR_REFRESH) {
            wdt_generic_refresh++;
        }
        R(WWDT_GWRR_OFFSET) = 0;
        S(WWDT_GWRR_OFFSET) = 0;
    }
    S(WWDT_GWCSR_OFFSET) = R(WWDT_GWCSR_OFFSET);
    S(WWDT_GWOR_OFFSET) = R(WWDT_GWOR_OFFSET);

    if (!wdt_mwc) {
        if (R(WWDT_ESR_OFFSET) != S(WWDT_ESR_OFFSET) ||
                R(WWDT_FCR_OFFSET) != S(WWDT_FCR_OFFSET) ||
                R(WWDT_FWR_OFFSET) != S(WWDT_FWR_OFFSET) ||
                R(WWDT_SWR_OFFSET) != S(WWDT_SWR_OFFSET)) {
            wdt_blocked_writes++;
            R(WWDT_ESR_OFFSET) = S(WWDT_ESR_OFFSET);
            R(WWDT_FCR_OFFSET) = S(WWDT_FCR_OFFSET);
            R(WWDT_FWR_OFFSET) = S(WWDT_FWR_OFFSET);
            R(WWDT_SWR_OFFSET) = S(WWDT_SWR_OFFSET);
        }
        return;
    }

    S(WWDT_FWR_OFFSET) = R(WWDT_FWR_OFFSET);
    S(WWDT_SWR_OFFSET) = R(WWDT_SWR_OFFSET);
    fcr = R(WWDT_FCR_OFFSET);
    if ((S(WWDT_FCR_OFFSET) & WWDT_FCR_WDP) && !(fcr & WWDT_FCR_WDP)) {
        wdt_wdp_cleared = 1;
    }
    S(WWDT_FCR_OFFSET) = fcr;

    esr = R(WWDT_ESR_OFFSET);
    if (esr == S(WWDT_ESR_OFFSET)) {
        return;
    }

    if ((esr & WWDT_ESR_WEN) && !wdt_enabled) {
        wdt_enabled = 1;
        wdt_mwc = 0;
        R(WWDT_MWR_OFFSET) &= ~(uint32_t)WWDT_MWR_MWC;
        S(WWDT_MWR_OFFSET) = R(WWDT_MWR_OFFSET);
        wdt_window_start();
    }
    else if (!(esr & WWDT_ESR_WEN) && wdt_enabled) {
        if ((fcr & WWDT_FCR_WDP) || !wdt_open) {
            wdt_bad_events++;
        }
        else {
            wdt_enabled = 0;
            wdt_open = 0;
        }
    }
    else if ((esr & WWDT_ESR_WSW) && wdt_enabled) {
        if (!wdt_open) {
            wdt_bad_events++;
        }
        else {
            wdt_window_start();
        }
    }
    wdt_commit_esr(esr);
}

#define dsb() wdt_sync()

static uint64_t timer_deadline_us(uint32_t us)
{
    (void)us;
    wdt_polls = 0;
    return 0;
}

static int timer_expired(uint64_t deadline)
{
    (void)deadline;
    wdt_polls++;
    if (wdt_polls_to_open >= 0 && wdt_polls >= wdt_polls_to_open) {
        wdt_open = 1;
        wdt_commit_esr(0);
    }
    return (wdt_polls > WDT_MAX_POLLS);
}

#include "versal_wdt_extract.h"

static void reset_mock(void)
{
    memset(mock_regs, 0, sizeof(mock_regs));
    memset(shadow, 0, sizeof(shadow));
    wdt_enabled = 0;
    wdt_open = 0;
    wdt_mwc = 0;
    wdt_bad_events = 0;
    wdt_blocked_writes = 0;
    wdt_mwc_writes = 0;
    wdt_generic_refresh = 0;
    wdt_wdp_cleared = 0;
    wdt_fcv = 0;
    wdt_polls = 0;
    wdt_polls_to_open = 3;
}

/* Put the model in the state a CDO-armed watchdog leaves behind */
static void mock_armed(uint32_t fwr, int open)
{
    wdt_enabled = 1;
    wdt_open = open;
    R(WWDT_FWR_OFFSET) = fwr;
    S(WWDT_FWR_OFFSET) = fwr;
    wdt_commit_esr(0);
}

START_TEST(test_arm_from_cold)
{
    reset_mock();

    ck_assert_int_eq(versal_wwdt_arm(1000), 0);
    ck_assert_int_eq(wdt_enabled, 1);
    /* No closed window, so every later restart is legal */
    ck_assert_uint_eq(R(WWDT_FWR_OFFSET), 0);
    ck_assert_uint_eq(R(WWDT_SWR_OFFSET), 1000UL * (WATCHDOG_CLK_HZ / 1000UL));
    ck_assert_int_eq(wdt_open, 1);
    ck_assert_int_eq(wdt_bad_events, 0);
    ck_assert_int_eq(wdt_blocked_writes, 0);
    /* The space was unlocked before the window registers were written */
    ck_assert_int_ge(wdt_mwc_writes, 1);
}
END_TEST

START_TEST(test_arm_count_is_clamped)
{
    reset_mock();

    /* 60 s at 100 MHz is 6e9 ticks, past the 32-bit window count */
    ck_assert_int_eq(versal_wwdt_arm(60000), 0);
    ck_assert_uint_eq(R(WWDT_SWR_OFFSET), (uint32_t)WWDT_COUNT_MAX);
}
END_TEST

START_TEST(test_arm_over_running_waits_for_open_window)
{
    reset_mock();
    /* Armed by the CDO with a closed window ahead of it */
    mock_armed(0x4000, 0);

    ck_assert_int_eq(versal_wwdt_arm(1000), 0);
    ck_assert_int_eq(wdt_enabled, 1);
    ck_assert_uint_eq(R(WWDT_FWR_OFFSET), 0);
    ck_assert_int_eq(wdt_bad_events, 0);
    ck_assert_int_eq(wdt_blocked_writes, 0);
}
END_TEST

START_TEST(test_arm_refuses_qa_mode)
{
    reset_mock();
    mock_armed(0, 1);
    R(WWDT_FCR_OFFSET) = WWDT_FCR_WM;
    S(WWDT_FCR_OFFSET) = WWDT_FCR_WM;

    /* Q&A mode needs a token handshake wolfBoot does not take part in */
    ck_assert_int_ne(versal_wwdt_arm(1000), 0);
    ck_assert_int_eq(wdt_enabled, 1);
    ck_assert_uint_eq(R(WWDT_MWR_OFFSET), 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_arm_over_write_protected_clears_protection)
{
    reset_mock();
    mock_armed(0, 1);
    R(WWDT_FCR_OFFSET) = WWDT_FCR_WDP;
    S(WWDT_FCR_OFFSET) = WWDT_FCR_WDP;

    /* WDP blocks clearing WEN, so the bring-down has to drop it or the
     * reprogram lands on a watchdog that never actually stopped */
    ck_assert_int_eq(versal_wwdt_arm(1000), 0);
    ck_assert_int_eq(wdt_wdp_cleared, 1);
    ck_assert_int_eq(wdt_enabled, 1);
    ck_assert_uint_eq(R(WWDT_FWR_OFFSET), 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_arm_fails_when_running_dog_cannot_be_stopped)
{
    reset_mock();
    mock_armed(0, 1);
    R(WWDT_MWR_OFFSET) = WWDT_MWR_AEN;
    S(WWDT_MWR_OFFSET) = WWDT_MWR_AEN;

    ck_assert_int_ne(versal_wwdt_arm(1000), 0);
    /* Reported, not papered over with a half-programmed timer */
    ck_assert_uint_eq(R(WWDT_FWR_OFFSET), 0);
    ck_assert_uint_eq(R(WWDT_SWR_OFFSET), 0);
    ck_assert_int_eq(wdt_enabled, 1);
}
END_TEST

START_TEST(test_feed_in_open_window_kicks)
{
    reset_mock();
    mock_armed(0, 1);

    wolfBoot_watchdog_feed();
    ck_assert_int_eq(wdt_mwc_writes, 1);
    ck_assert_int_eq(wdt_bad_events, 0);
    ck_assert_int_eq(wdt_enabled, 1);
}
END_TEST

START_TEST(test_feed_in_closed_window_is_suppressed)
{
    reset_mock();
    mock_armed(0x4000, 0);

    /* A restart here would be a bad event, and the timer does not need one */
    wolfBoot_watchdog_feed();
    ck_assert_int_eq(wdt_mwc_writes, 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_feed_in_qa_mode_is_suppressed)
{
    reset_mock();
    mock_armed(0, 1);
    R(WWDT_FCR_OFFSET) = WWDT_FCR_WM;
    S(WWDT_FCR_OFFSET) = WWDT_FCR_WM;

    /* Q&A mode restarts through a token handshake, not a WSW write */
    wolfBoot_watchdog_feed();
    ck_assert_int_eq(wdt_mwc_writes, 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_feed_when_disabled_does_nothing)
{
    reset_mock();

    wolfBoot_watchdog_feed();
    ck_assert_int_eq(wdt_mwc_writes, 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_stop_in_open_window)
{
    reset_mock();
    mock_armed(0, 1);

    ck_assert_int_eq(versal_wwdt_stop(), 0);
    ck_assert_int_eq(wdt_enabled, 0);
    ck_assert_uint_eq(R(WWDT_ESR_OFFSET) & WWDT_ESR_WEN, 0);
    ck_assert_int_eq(wdt_bad_events, 0);
    ck_assert_int_eq(wdt_blocked_writes, 0);
}
END_TEST

START_TEST(test_stop_waits_for_open_window)
{
    reset_mock();
    mock_armed(0x4000, 0);
    wdt_polls_to_open = 5;

    /* The hardware ignores a disable in the closed window, so the driver has
     * to wait for the open one rather than write and hope */
    ck_assert_int_eq(versal_wwdt_stop(), 0);
    ck_assert_int_eq(wdt_enabled, 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_stop_clears_write_protection_first)
{
    reset_mock();
    mock_armed(0, 1);
    R(WWDT_FCR_OFFSET) = WWDT_FCR_WDP;
    S(WWDT_FCR_OFFSET) = WWDT_FCR_WDP;

    ck_assert_int_eq(versal_wwdt_stop(), 0);
    ck_assert_int_eq(wdt_wdp_cleared, 1);
    ck_assert_int_eq(wdt_enabled, 0);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_stop_refused_when_always_enabled)
{
    reset_mock();
    mock_armed(0, 1);
    R(WWDT_MWR_OFFSET) = WWDT_MWR_AEN;
    S(WWDT_MWR_OFFSET) = WWDT_MWR_AEN;

    ck_assert_int_ne(versal_wwdt_stop(), 0);
    ck_assert_int_eq(wdt_enabled, 1);
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_stop_times_out_when_window_never_opens)
{
    reset_mock();
    mock_armed(0x4000, 0);
    wdt_polls_to_open = -1;

    ck_assert_int_ne(versal_wwdt_stop(), 0);
    ck_assert_int_eq(wdt_enabled, 1);
    /* Reported as a failure rather than attempted and silently ignored */
    ck_assert_int_eq(wdt_bad_events, 0);
}
END_TEST

START_TEST(test_stop_when_already_disabled)
{
    reset_mock();

    ck_assert_int_eq(versal_wwdt_stop(), 0);
    ck_assert_int_eq(wdt_mwc_writes, 0);
}
END_TEST

START_TEST(test_generic_mode_arm_feed_and_stop)
{
    reset_mock();
    R(WWDT_GWCSR_OFFSET) = WWDT_GWCSR_GWEN;
    S(WWDT_GWCSR_OFFSET) = WWDT_GWCSR_GWEN;

    /* Generic mode stays generic: the window registers are left alone */
    ck_assert_int_eq(versal_wwdt_arm(1000), 0);
    ck_assert_uint_eq(R(WWDT_GWOR_OFFSET),
        1000UL * (WATCHDOG_CLK_HZ / 1000UL));
    ck_assert_uint_eq(R(WWDT_GWCSR_OFFSET) & WWDT_GWCSR_GWEN,
        WWDT_GWCSR_GWEN);
    ck_assert_int_eq(wdt_generic_refresh, 1);
    ck_assert_uint_eq(R(WWDT_MWR_OFFSET), 0);

    wolfBoot_watchdog_feed();
    ck_assert_int_eq(wdt_generic_refresh, 2);

    ck_assert_int_eq(versal_wwdt_stop(), 0);
    ck_assert_uint_eq(R(WWDT_GWCSR_OFFSET) & WWDT_GWCSR_GWEN, 0);
}
END_TEST

Suite *versal_wdt_suite(void)
{
    Suite *s = suite_create("versal-wdt");
    TCase *tc = tcase_create("wwdt");

    tcase_add_test(tc, test_arm_from_cold);
    tcase_add_test(tc, test_arm_count_is_clamped);
    tcase_add_test(tc, test_arm_over_running_waits_for_open_window);
    tcase_add_test(tc, test_arm_refuses_qa_mode);
    tcase_add_test(tc, test_arm_over_write_protected_clears_protection);
    tcase_add_test(tc, test_arm_fails_when_running_dog_cannot_be_stopped);
    tcase_add_test(tc, test_feed_in_open_window_kicks);
    tcase_add_test(tc, test_feed_in_closed_window_is_suppressed);
    tcase_add_test(tc, test_feed_in_qa_mode_is_suppressed);
    tcase_add_test(tc, test_feed_when_disabled_does_nothing);
    tcase_add_test(tc, test_stop_in_open_window);
    tcase_add_test(tc, test_stop_waits_for_open_window);
    tcase_add_test(tc, test_stop_clears_write_protection_first);
    tcase_add_test(tc, test_stop_refused_when_always_enabled);
    tcase_add_test(tc, test_stop_times_out_when_window_never_opens);
    tcase_add_test(tc, test_stop_when_already_disabled);
    tcase_add_test(tc, test_generic_mode_arm_feed_and_stop);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = versal_wdt_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
