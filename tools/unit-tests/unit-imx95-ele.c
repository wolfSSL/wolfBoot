/* unit-imx95-ele.c
 *
 * Host test for the i.MX95 stage 1 EdgeLock Enclave mailbox transport.
 *
 * mu_wait() and ele_call() are extracted verbatim from hal/imx95_a55_stage1.c
 * into imx95_ele_extract.h, so this exercises the shipped code rather than a
 * copy of it. Only what the transport sits on is stubbed: the MU registers, and
 * the generic timer that bounds the waits.
 *
 * The register model is bounds-checked. An access outside the MU's register
 * window is recorded rather than allowed to pass unnoticed, because on the SoC
 * that window is followed by other peripherals.
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define IMX95_ELE_MU_BASE   0x47530000UL

/* The last register the driver may touch is RR(7) at 0x29C. */
#define MU_WINDOW_BYTES     0x2A0U
#define MU_MODEL_WORDS      0x400U

#define NOINLINE __attribute__((noinline))

static NOINLINE uint32_t rd(uintptr_t a);
static NOINLINE void wr(uintptr_t a, uint32_t v);
static NOINLINE uint64_t timer_ticks(void);
#define ELE_BARRIER() do { } while (0)
#define ELE_TEST_CNTFRQ 24000000ULL

#include "imx95_ele_extract.h"

/* ---- the model ---------------------------------------------------------- */

static volatile struct {
    uint32_t reads;          /* register reads, so a spin cannot be elided */
    uint32_t par;           /* what ELE_MU_PAR reads back as */
    uint32_t tsr;           /* transmit-empty bits */
    uint32_t rsr;           /* receive-full bits */
    uint32_t stale;         /* words a previous owner left in the mailbox */
    uint32_t rr_empty;       /* RR reads with nothing pending */
    uint32_t reply[ELE_MAX_MSG];
    uint32_t reply_n;
    uint32_t sent[MU_MODEL_WORDS];
    uint32_t sent_n;
    uint32_t rr_pos;
    uint32_t oob;           /* accesses outside the MU register window */
    uintptr_t oob_addr;
    uint64_t ticks;
    uint64_t tick_step;     /* 0 models a system counter that is not running */
} mu;

static void mu_reset(void)
{
    memset((void *)&mu, 0, sizeof(mu));
    mu.par = (8U << 8) | 8U;    /* 8 transmit, 8 receive registers */
    mu.tsr = 0xFFU;             /* always room to transmit */
    mu.rsr = 0xFFU;             /* a reply word is always waiting */
    mu.tick_step = 1000ULL;
}

static NOINLINE uint32_t rd(uintptr_t a)
{
    uint32_t off = (uint32_t)(a - IMX95_ELE_MU_BASE);

    mu.reads++;
    if (off >= MU_WINDOW_BYTES) {
        mu.oob++;
        mu.oob_addr = a;
        return 0;
    }
    if (a == ELE_MU_PAR)
        return mu.par;
    if (a == ELE_MU_SR)
        return mu.stale ? (uint32_t)ELE_MU_SR_RDR : 0U;
    if (a == ELE_MU_TSR)
        return mu.tsr;
    if (a == ELE_MU_RSR)
        return mu.rsr;
    if (off >= 0x280U && off < 0x2A0U) {
        if (mu.stale) {
            mu.stale = mu.stale - 1U;
            return 0xDEADBEEFU;
        }
        /* The enclave only replies once the command has been transmitted.
         * Before that an RR read returns whatever the register held, which is
         * what the drain loop is reading past the words actually pending. */
        if (mu.sent_n > 0U && mu.rr_pos < mu.reply_n) {
            uint32_t k = mu.rr_pos;
            mu.rr_pos = k + 1U;
            return mu.reply[k];
        }
        mu.rr_empty++;
        return 0;
    }
    return 0;
}

static NOINLINE void wr(uintptr_t a, uint32_t v)
{
    uint32_t off = (uint32_t)(a - IMX95_ELE_MU_BASE);

    if (off >= MU_WINDOW_BYTES) {
        mu.oob++;
        mu.oob_addr = a;
        return;
    }
    if (off >= 0x200U && off < 0x220U && mu.sent_n < MU_MODEL_WORDS) {
        uint32_t k = mu.sent_n;
        mu.sent_n = k + 1U;
        mu.sent[k] = v;
    }
}

static NOINLINE uint64_t timer_ticks(void)
{
    mu.ticks += mu.tick_step;
    return mu.ticks;
}

/* ---- helpers ------------------------------------------------------------ */

static int fails;

static void check(int cond, const char *what)
{
    printf("  %-58s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond)
        fails++;
}

static uint32_t hdr(uint32_t tag, uint32_t cmd, uint32_t words, uint32_t ver)
{
    return (tag << 24) | (cmd << 16) | (words << 8) | ver;
}

/* Script a well-formed two-word reply to cmd, carrying indication ind. */
static void reply_ok(uint32_t cmd, uint32_t ind)
{
    mu.reply[0] = hdr(ELE_RESP_TAG, cmd, 2, ELE_VERSION);
    mu.reply[1] = ind;
    mu.reply_n = 2;
}

static int call_rng(uint32_t *resp)
{
    uint32_t msg[ELE_MAX_MSG];

    msg[0] = hdr(ELE_CMD_TAG, ELE_START_RNG, 1, ELE_VERSION);
    return ele_call(ELE_START_RNG, msg, 1, resp);
}

/* ---- tests -------------------------------------------------------------- */

static void test_success(void)
{
    uint32_t resp = 0;
    int ret;

    puts("a well-formed reply is accepted");
    mu_reset();
    reply_ok(ELE_START_RNG, ELE_OK);
    ret = call_rng(&resp);
    check(ret == ELE_CALL_OK, "returns ELE_CALL_OK");
    check(resp == ELE_OK, "reports the indication word");
    check(mu.sent_n == 1, "sends exactly one word");
    check(mu.oob == 0, "stays inside the MU register window");
}

static void test_rejected(void)
{
    uint32_t resp = 0;
    int ret;

    puts("a clean negative reply is REJECTED, not BROKEN");
    mu_reset();
    reply_ok(ELE_START_RNG, (ELE_IND_RNG_STOPPED << 8) | 0x29U);
    ret = call_rng(&resp);
    check(ret == ELE_CALL_REJECTED, "returns ELE_CALL_REJECTED");
    check(((resp >> 8) & 0xFFU) == ELE_IND_RNG_STOPPED,
          "passes the indication byte back for reporting");
}

static void test_reply_mismatch(void)
{
    int ret;

    puts("a reply that is not this command's reply is BROKEN");

    mu_reset();
    mu.reply[0] = hdr(0x12U, ELE_START_RNG, 2, ELE_VERSION);
    mu.reply[1] = ELE_OK;
    mu.reply_n = 2;
    ret = call_rng(NULL);
    check(ret == ELE_CALL_BROKEN, "wrong response tag");

    mu_reset();
    reply_ok(ELE_CNTR_AUTH, ELE_OK);
    ret = call_rng(NULL);
    check(ret == ELE_CALL_BROKEN, "reply echoes a different command");

    mu_reset();
    mu.reply[0] = hdr(ELE_RESP_TAG, ELE_START_RNG, 1, ELE_VERSION);
    mu.reply[1] = ELE_OK;
    mu.reply_n = 2;
    ret = call_rng(NULL);
    check(ret == ELE_CALL_BROKEN, "reply claims fewer than two words");

    mu_reset();
    mu.reply[0] = hdr(ELE_RESP_TAG, ELE_START_RNG, ELE_MAX_MSG + 1U,
                      ELE_VERSION);
    mu.reply[1] = ELE_OK;
    mu.reply_n = 2;
    ret = call_rng(NULL);
    check(ret == ELE_CALL_BROKEN, "reply claims more words than the mailbox");
}

static void test_stale_reply_is_drained(void)
{
    uint32_t resp = 0;

    puts("a reply left by a previous owner is drained first");
    mu_reset();
    mu.stale = 3;
    reply_ok(ELE_START_RNG, ELE_OK);
    check(call_rng(&resp) == ELE_CALL_OK, "the stale words are not misread");
    check(mu.stale == 0, "the mailbox was emptied");
    printf("    RR reads with nothing pending: %u\n", mu.rr_empty);
}

static void test_bad_msg_size(void)
{
    uint32_t msg[ELE_MAX_MSG] = { 0 };

    puts("the caller's word count is bounded");
    mu_reset();
    check(ele_call(ELE_START_RNG, msg, 0, NULL) == ELE_CALL_BROKEN,
          "zero words is rejected");
    mu_reset();
    check(ele_call(ELE_START_RNG, msg, ELE_MAX_MSG + 1U, NULL)
              == ELE_CALL_BROKEN,
          "more than ELE_MAX_MSG words is rejected");
}

/* The wait must terminate even when the generic timer is not counting. Nothing
 * in the boot chain guarantees the system counter is enabled by the time stage
 * 1 runs, and a stalled CNTPCT_EL0 makes every deadline unreachable. */
static void test_terminates_with_stalled_counter(void)
{
    int ret;

    puts("a stalled system counter must not hang the wait");
    printf("    (the no-clock fallback spins %lu times; this is not instant)\n",
           (unsigned long)ELE_MU_POLL_LOOPS);
    mu_reset();
    mu.tick_step = 0;       /* CNTPCT_EL0 never advances */
    mu.tsr = 0;             /* and the ELE never accepts a word */
    alarm(60);              /* a hang shows up as a killed test, not a wedge */
    ret = call_rng(NULL);
    alarm(0);
    check(ret == ELE_CALL_BROKEN, "returns ELE_CALL_BROKEN instead of spinning");
    /* If the host compiler elides the spin the bound is never really exercised,
     * and the test would pass without proving anything. */
    printf("    register reads during the wait: %lu\n", (unsigned long)mu.reads);
    check(mu.reads >= ELE_MU_POLL_LOOPS, "the bounded spin actually ran");
}

/* PAR is a hardware constant, but reading an unclocked or powered-down MU
 * returns all-ones rather than a sane register count. The driver only checks it
 * is non-zero, so the counts must still be bounded before they index TR/RR. */
static void test_par_is_bounded(void)
{
    uint32_t resp = 0;

    puts("a garbage PAR read must not index outside the MU window");
    mu_reset();
    mu.par = 0xFFFFFFFFU;
    mu.stale = 1;
    reply_ok(ELE_START_RNG, ELE_OK);
    (void)call_rng(&resp);
    if (mu.oob != 0)
        printf("    first out-of-window access at 0x%lx\n",
               (unsigned long)mu.oob_addr);
    check(mu.oob == 0, "no access beyond ELE_MU_RR(7)");
}

static void test_four_register_mailbox(void)
{
    uint32_t msg[ELE_MAX_MSG];
    uint32_t i;

    puts("a mailbox with four registers wraps instead of overrunning");
    mu_reset();
    mu.par = (4U << 8) | 4U;
    msg[0] = hdr(ELE_CMD_TAG, ELE_CNTR_AUTH, ELE_MAX_MSG, ELE_VERSION);
    for (i = 1; i < ELE_MAX_MSG; i++)
        msg[i] = 0x1000U + i;
    reply_ok(ELE_CNTR_AUTH, ELE_OK);
    check(ele_call(ELE_CNTR_AUTH, msg, ELE_MAX_MSG, NULL) == ELE_CALL_OK,
          "all eight words are sent over four registers");
    check(mu.sent_n == ELE_MAX_MSG, "no word is dropped");
    check(mu.oob == 0, "stays inside the MU register window");
}

int main(void)
{
    test_success();
    test_rejected();
    test_reply_mismatch();
    test_stale_reply_is_drained();
    test_bad_msg_size();
    test_par_is_bounded();
    test_four_register_mailbox();
    test_terminates_with_stalled_counter();

    printf("\n%s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}
