/* unit-zynqmp-efuse-guards.c
 *
 * Coverage for the ZynqMP fail-secure guards that decide whether an
 * irreversible eFuse write or a BBRAM key program is allowed to proceed.
 * These paths are MMIO-bound, so the HAL's pmu_mmio_* accessors are backed by
 * a sparse fake register map here and the functions under test are extracted
 * verbatim from hal/zynq.c by the Makefile.
 *
 * What is covered, and why each matters:
 *  - zynqmp_efuse_prog_allowed(): an unread SysMon supply channel reads back
 *    zero and must block. Note zero is refused twice over -- by the explicit
 *    unread check and, redundantly, by the lower range bound -- so this
 *    asserts the property rather than isolating either check.
 *  - zynqmp_efuse_write_bit(): bounds, then brick-fuse rejection, then the
 *    supply gate. The brick rejection must win even when the gate would pass.
 *  - zynqmp_sec_policy_check(): an empty required-mask must fail rather than
 *    pass vacuously.
 *  - zynqmp_bbram_program(): NULL and wrong key size must be refused before
 *    any register access, and a CRC mismatch must be reported as failure.
 *
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
#include <stddef.h>
#include <string.h>

#define WOLFBOOT_ZYNQMP_FSBL_SEC
#define WOLFBOOT_ZYNQMP_BBRAM
#define WOLFBOOT_ZYNQMP_EFUSE_WRITE

#include "hal/zynq.h"

/* ---- fake register file -------------------------------------------------
 * Sparse: anything never written reads back as zero, which is also what real
 * hardware does for an unconverted SysMon channel. */
#define FAKE_REGS 64
static struct { uint32_t addr; uint32_t val; int used; int writes; } fake[FAKE_REGS];
static int efuse_model;          /* emulate the eFuse controller */
static int efuse_fail_tbits;     /* present a bad T-bit pattern */
static int efuse_fail_pgm;       /* report PGM_ERR instead of PGM_DONE */
static int efuse_fail_cache;     /* never complete the cache reload */
static int bbram_model;     /* emulate the BBRAM state machine */
static int bbram_force_crc_fail;  /* withhold AES_CRC_PASS */
static int bbram_no_pgm_mode;    /* refuse to latch PGM_MODE */

static void fake_reset(void)
{
    memset(fake, 0, sizeof(fake));
    bbram_model = 0;
    bbram_force_crc_fail = 0;
    bbram_no_pgm_mode = 0;
    efuse_model = 0;
    efuse_fail_tbits = 0;
    efuse_fail_pgm = 0;
    efuse_fail_cache = 0;
}

static uint32_t *fake_slot(uint32_t addr)
{
    int i;
    for (i = 0; i < FAKE_REGS; i++) {
        if (fake[i].used && fake[i].addr == addr) {
            return &fake[i].val;
        }
    }
    for (i = 0; i < FAKE_REGS; i++) {
        if (!fake[i].used) {
            fake[i].used = 1;
            fake[i].addr = addr;
            fake[i].val = 0;
            return &fake[i].val;
        }
    }
    return NULL;
}

static void fake_set(uint32_t addr, uint32_t val)
{
    uint32_t *p = fake_slot(addr);
    if (p != NULL) {
        *p = val;
    }
}

static uint32_t fake_get(uint32_t addr)
{
    uint32_t *p = fake_slot(addr);
    return (p != NULL) ? *p : 0;
}

/* How many times the code under test wrote this register */
static int fake_writes(uint32_t addr)
{
    int i;
    for (i = 0; i < FAKE_REGS; i++) {
        if (fake[i].used && fake[i].addr == addr) {
            return fake[i].writes;
        }
    }
    return 0;
}

/* How many registers have been touched at all (read or written) */
static int fake_slots_used(void)
{
    int i, n = 0;
    for (i = 0; i < FAKE_REGS; i++) {
        if (fake[i].used) {
            n++;
        }
    }
    return n;
}

static void fake_mark_write(uint32_t addr)
{
    int i;
    for (i = 0; i < FAKE_REGS; i++) {
        if (fake[i].used && fake[i].addr == addr) {
            fake[i].writes++;
            return;
        }
    }
}

/* ---- HAL accessors the extracted code calls ---------------------------- */
uint32_t pmu_mmio_read(uint32_t addr)
{
    return fake_get(addr);
}

uint32_t pmu_mmio_write(uint32_t addr, uint32_t val)
{
    fake_set(addr, val);
    fake_mark_write(addr);

    if (efuse_model) {
        if (addr == ZYNQMP_EFUSE_PGM_ADDR) {
            fake_set(ZYNQMP_EFUSE_ISR, efuse_fail_pgm ?
                ZYNQMP_EFUSE_ISR_PGM_ERR : ZYNQMP_EFUSE_ISR_PGM_DONE);
        }
        else if (addr == ZYNQMP_EFUSE_CACHE_LOAD) {
            uint32_t st = fake_get(ZYNQMP_EFUSE_STATUS);

            if (efuse_fail_cache) {
                /* Reload still in flight, with a stale done flag from an
                 * earlier one. Only a wait that also requires CACHE_LOAD to
                 * be clear rejects this. */
                fake_set(ZYNQMP_EFUSE_STATUS, st |
                    ZYNQMP_EFUSE_STATUS_CACHE_LOAD |
                    ZYNQMP_EFUSE_STATUS_CACHE_DONE);
            }
            else {
                st &= ~(uint32_t)ZYNQMP_EFUSE_STATUS_CACHE_LOAD;
                fake_set(ZYNQMP_EFUSE_STATUS,
                    st | ZYNQMP_EFUSE_STATUS_CACHE_DONE);
            }
        }
    }

    if (bbram_model) {
        uint32_t sts = fake_get(ZYNQMP_BBRAM_STS);
        if (addr == ZYNQMP_BBRAM_CTRL &&
                (val & ZYNQMP_BBRAM_CTRL_ZEROIZE) != 0) {
            fake_set(ZYNQMP_BBRAM_STS, sts | ZYNQMP_BBRAM_STS_ZEROIZED);
        }
        else if (addr == ZYNQMP_BBRAM_PGM_MODE) {
            if (val == ZYNQMP_BBRAM_PGM_MODE_MAGIC && !bbram_no_pgm_mode) {
                fake_set(ZYNQMP_BBRAM_STS, sts | ZYNQMP_BBRAM_STS_PGM_MODE);
            }
            else {
                fake_set(ZYNQMP_BBRAM_STS,
                    sts & ~(uint32_t)ZYNQMP_BBRAM_STS_PGM_MODE);
            }
        }
        else if (addr == ZYNQMP_BBRAM_AES_CRC) {
            /* Check the submitted CRC against the words actually written to
             * the key registers, as the controller does. A deleted or
             * misaddressed key write then fails here on its own. */
            uint32_t kw[ZYNQMP_BBRAM_KEY_WORDS];
            uint32_t k;

            for (k = 0; k < ZYNQMP_BBRAM_KEY_WORDS; k++) {
                kw[k] = fake_get(ZYNQMP_BBRAM_KEY_0 +
                    k * (uint32_t)sizeof(uint32_t));
            }
            sts |= ZYNQMP_BBRAM_STS_AES_CRC_DONE;
            if (!bbram_force_crc_fail && val == zynqmp_bbram_key_crc(kw)) {
                sts |= ZYNQMP_BBRAM_STS_AES_CRC_PASS;
            }
            fake_set(ZYNQMP_BBRAM_STS, sts);
        }
    }
    return 0;
}

uint32_t pmu_mmio_writemask(uint32_t addr, uint32_t mask, uint32_t val)
{
    uint32_t cur = fake_get(addr);
    fake_set(addr, (cur & ~mask) | (val & mask));
    fake_mark_write(addr);
    return 0;
}

int pmu_mmio_wait(uint32_t addr, uint32_t wait_mask, uint32_t wait_val,
    uint32_t tries)
{
    (void)tries;
    return ((fake_get(addr) & wait_mask) == wait_val) ? 0 : -1;
}

/* Defined in the extract header below; the fake controller needs it to check
 * the CRC against the key registers rather than a value the test supplies. */
uint32_t zynqmp_bbram_key_crc(const uint32_t* key);

/* printf and secret-wipe stubs */
static int printf_calls;
#define wolfBoot_printf(...) do { printf_calls++; } while (0)
static void wc_ForceZero(void *p, unsigned long n) { memset(p, 0, (size_t)n); }

/* hal/zynq.h only defines the mode the driver selects; mode 1 is the
 * leftover state this test puts the controller in. */
#define ZYNQMP_EFUSE_CFG_MARGIN_1_RD_FOR_TEST 0x01

#include "efuse_guards_extract.h"

/* Put the supply channels inside their accepted windows. */
static void sysmon_healthy(void)
{
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_TEMP * 4, ZYNQMP_SYSMON_C_TO_RAW(30));
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSAUX * 4, ZYNQMP_SYSMON_MV_TO_RAW(1800));
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSINTLP * 4, ZYNQMP_SYSMON_MV_TO_RAW(850));
}

/* Set up a controller that would accept a program request. */
static void efuse_healthy(void)
{
    efuse_model = 1;
    fake_set(ZYNQMP_EFUSE_STATUS, efuse_fail_tbits ? 0 :
        ZYNQMP_EFUSE_STATUS_TBITS_ALL);
}

START_TEST(test_prog_allowed_gate){
    fake_reset();
    sysmon_healthy();
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), 0);

    /* In-range but below zero: ZYNQMP_SYSMON_C_TO_RAW casts to int64_t so a
     * negative temperature converts correctly. Only an accept-path assertion
     * catches that; the reject cases pass either way. */
    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_TEMP * 4, ZYNQMP_SYSMON_C_TO_RAW(-30));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), 0);

    /* An unread supply channel reads zero and must block. The explicit
     * unread check and the lower range bound both catch this; the assertion
     * is on the outcome, which is what matters for an irreversible write. */
    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSAUX * 4, 0);
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSINTLP * 4, 0);
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    /* Out-of-window supplies and temperature */
    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSAUX * 4, ZYNQMP_SYSMON_MV_TO_RAW(1500));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSINTLP * 4, ZYNQMP_SYSMON_MV_TO_RAW(1100));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_TEMP * 4, ZYNQMP_SYSMON_C_TO_RAW(130));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    /* the opposite bound of each range, so no check is one-sided */
    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_TEMP * 4, ZYNQMP_SYSMON_C_TO_RAW(-50));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSAUX * 4, ZYNQMP_SYSMON_MV_TO_RAW(2100));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    fake_reset();
    sysmon_healthy();
    fake_set(ZYNQMP_SYSMON_BASE + ZYNQMP_SYSMON_PS_OFFSET +
        ZYNQMP_SYSMON_CH_VCC_PSINTLP * 4, ZYNQMP_SYSMON_MV_TO_RAW(600));
    ck_assert_int_eq(zynqmp_efuse_prog_allowed(), -1);

    /* a rejection must say why */
    printf_calls = 0;
    (void)zynqmp_efuse_prog_allowed();
    ck_assert_int_gt(printf_calls, 0);
}
END_TEST

START_TEST(test_write_bit_guards){
    fake_reset();
    sysmon_healthy();
    efuse_healthy();

    /* out of range coordinates */
    ck_assert_int_eq(zynqmp_efuse_write_bit(4, 0, 0), -1);
    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 64, 0), -1);
    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 0, 32), -1);

    /* brick fuses refused even though the supply gate would pass */
    ck_assert_int_eq(zynqmp_efuse_write_bit(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 5), -1);
    ck_assert_int_eq(zynqmp_efuse_write_bit(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 11), -1);

    /* a user fuse is permitted when the gate passes */
    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), 0);

    /* and refused when the gate does not */
    fake_reset();
    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), -1);
}
END_TEST

START_TEST(test_sec_policy_check){
    fake_reset();
    fake_set(ZYNQMP_EFUSE_STATUS, ZYNQMP_EFUSE_STATUS_CACHE_DONE);
    fake_set(ZYNQMP_EFUSE_SEC_CTRL, ZYNQMP_EFUSE_SEC_CTRL_RSA_EN);

    /* an empty mask must not pass vacuously */
    ck_assert_int_eq(zynqmp_sec_policy_check(0), -1);
    /* satisfied policy passes */
    ck_assert_int_eq(
        zynqmp_sec_policy_check(ZYNQMP_EFUSE_SEC_CTRL_RSA_EN), 0);
    /* missing bit fails */
    ck_assert_int_eq(
        zynqmp_sec_policy_check(ZYNQMP_EFUSE_SEC_CTRL_JTAG_DIS), -1);

    /* a realistic caller ORs several bits together */
    fake_reset();
    fake_set(ZYNQMP_EFUSE_STATUS, ZYNQMP_EFUSE_STATUS_CACHE_DONE);
    fake_set(ZYNQMP_EFUSE_SEC_CTRL, ZYNQMP_EFUSE_SEC_CTRL_RSA_EN |
        ZYNQMP_EFUSE_SEC_CTRL_ENC_ONLY | ZYNQMP_EFUSE_SEC_CTRL_JTAG_DIS);
    ck_assert_int_eq(zynqmp_sec_policy_check(
        ZYNQMP_EFUSE_SEC_CTRL_RSA_EN | ZYNQMP_EFUSE_SEC_CTRL_ENC_ONLY), 0);
    /* a partial match must fail, not pass on the bits that are present */
    ck_assert_int_eq(zynqmp_sec_policy_check(
        ZYNQMP_EFUSE_SEC_CTRL_RSA_EN | ZYNQMP_EFUSE_SEC_CTRL_SEC_LOCK), -1);
    /* RSA_EN is multi-bit: a partially fused field must not satisfy it */
    fake_set(ZYNQMP_EFUSE_SEC_CTRL, 1UL << 11);
    ck_assert_int_eq(
        zynqmp_sec_policy_check(ZYNQMP_EFUSE_SEC_CTRL_RSA_EN), -1);

    /* cache not loaded must fail rather than read stale zeros as a pass */
    fake_reset();
    fake_set(ZYNQMP_EFUSE_SEC_CTRL, ZYNQMP_EFUSE_SEC_CTRL_RSA_EN);
    ck_assert_int_eq(
        zynqmp_sec_policy_check(ZYNQMP_EFUSE_SEC_CTRL_RSA_EN), -1);

    /* A reload still in flight with a stale done flag is an unfinished
     * snapshot, not a loaded cache, and must not be read as one. */
    fake_reset();
    fake_set(ZYNQMP_EFUSE_STATUS, ZYNQMP_EFUSE_STATUS_CACHE_LOAD |
        ZYNQMP_EFUSE_STATUS_CACHE_DONE);
    fake_set(ZYNQMP_EFUSE_SEC_CTRL, ZYNQMP_EFUSE_SEC_CTRL_RSA_EN);
    ck_assert_int_eq(
        zynqmp_sec_policy_check(ZYNQMP_EFUSE_SEC_CTRL_RSA_EN), -1);
}
END_TEST

START_TEST(test_bbram_program_guards){
    uint8_t key[ZYNQMP_BBRAM_KEY_SZ];
    uint32_t words[ZYNQMP_BBRAM_KEY_WORDS];
    unsigned i;

    for (i = 0; i < sizeof(key); i++) {
        key[i] = (uint8_t)i;
    }

    /* refused before any register access */
    fake_reset();
    ck_assert_int_eq(zynqmp_bbram_program(NULL, ZYNQMP_BBRAM_KEY_SZ), -1);
    ck_assert_int_eq(zynqmp_bbram_program(key, ZYNQMP_BBRAM_KEY_SZ - 1), -1);
    ck_assert_int_eq(zynqmp_bbram_program(key, 0), -1);
    /* Argument validation must happen before anything touches the
     * controller: moving the zeroize earlier would destroy a resident key. */
    ck_assert_int_eq(fake_slots_used(), 0);

    /* full sequence with a controller that accepts the CRC */
    fake_reset();
    bbram_model = 1;
    zynqmp_bbram_key_words(key, words);
    ck_assert_int_eq(zynqmp_bbram_program(key, ZYNQMP_BBRAM_KEY_SZ), 0);
    /* The reversed words must actually reach the key registers. Programming
     * them forward still passes the controller CRC but yields a key the CSU
     * cannot decrypt with, so assert the order, not just the outcome. */
    for (i = 0; i < ZYNQMP_BBRAM_KEY_WORDS; i++) {
        ck_assert_uint_eq(
            fake_get(ZYNQMP_BBRAM_KEY_0 + i * (uint32_t)sizeof(uint32_t)),
            words[i]);
    }
    /* KEY_0 holds the LAST four key bytes, KEY_7 the first */
    ck_assert_uint_eq(fake_get(ZYNQMP_BBRAM_KEY_0), 0x1C1D1E1FU);
    ck_assert_uint_eq(fake_get(ZYNQMP_BBRAM_KEY_0 +
        7 * (uint32_t)sizeof(uint32_t)), 0x00010203U);
    /* programming mode is left on every path */
    ck_assert_uint_eq(fake_get(ZYNQMP_BBRAM_PGM_MODE), 0);

    /* zeroize never completing must abort before any key word is written */
    fake_reset();
    bbram_model = 0;
    ck_assert_int_eq(zynqmp_bbram_program(key, ZYNQMP_BBRAM_KEY_SZ), -1);
    ck_assert_int_eq(fake_writes(ZYNQMP_BBRAM_AES_CRC), 0);

    /* programming mode never latching must abort too */
    fake_reset();
    bbram_model = 1;
    bbram_no_pgm_mode = 1;
    ck_assert_int_eq(zynqmp_bbram_program(key, ZYNQMP_BBRAM_KEY_SZ), -1);
    ck_assert_int_eq(fake_writes(ZYNQMP_BBRAM_AES_CRC), 0);
    bbram_no_pgm_mode = 0;

    /* a controller that rejects the CRC must be reported as failure */
    fake_reset();
    bbram_model = 1;
    bbram_force_crc_fail = 1;
    ck_assert_int_eq(zynqmp_bbram_program(key, ZYNQMP_BBRAM_KEY_SZ), -1);
    ck_assert_uint_eq(fake_get(ZYNQMP_BBRAM_PGM_MODE), 0);
}
END_TEST

START_TEST(test_bbram_zeroize_and_status){
    uint32_t sts = 0xFFFFFFFFU;

    fake_reset();
    bbram_model = 1;
    ck_assert_int_eq(zynqmp_bbram_zeroize(), 0);

    ck_assert_int_eq(zynqmp_bbram_status(NULL), -1);
    ck_assert_int_eq(zynqmp_bbram_status(&sts), 0);
    ck_assert_uint_eq(sts & ZYNQMP_BBRAM_STS_ZEROIZED,
        ZYNQMP_BBRAM_STS_ZEROIZED);

    /* a controller that never reports zeroized must fail, not hang */
    fake_reset();
    bbram_model = 0;
    ck_assert_int_eq(zynqmp_bbram_zeroize(), -1);
}
END_TEST

/* A rejected request must never reach the controller at all, in either
 * build. This is the property that keeps a bad coordinate from becoming an
 * irreversible write. */
START_TEST(test_rejected_requests_never_strobe){
    fake_reset();
    sysmon_healthy();
    efuse_healthy();

    /* bounds and brick rejection happen before any register is touched */
    fake_reset();
    (void)zynqmp_efuse_write_bit(0, 64, 0);
    (void)zynqmp_efuse_write_bit(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 5);
    ck_assert_int_eq(fake_slots_used(), 0);

    fake_reset();
    sysmon_healthy();
    efuse_healthy();
    (void)zynqmp_efuse_write_bit(0, 64, 0);                 /* bad row */
    (void)zynqmp_efuse_write_bit(0, 0, 32);                 /* bad column */
    (void)zynqmp_efuse_write_bit(ZYNQMP_EFUSE_PAGE_SEC_CTRL,
        ZYNQMP_EFUSE_ROW_SEC_CTRL, 5);                      /* brick fuse */

    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_PGM_ADDR), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_WR_LOCK), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_CFG), 0);

    /* and when the supply gate refuses */
    fake_reset();
    efuse_healthy();
    (void)zynqmp_efuse_write_bit(0, 8, 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_PGM_ADDR), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_WR_LOCK), 0);
}
END_TEST

#ifndef ZYNQMP_EFUSE_BURN
/* Report-only must touch nothing on the controller. */
START_TEST(test_report_only_writes_nothing){
    fake_reset();
    sysmon_healthy();
    efuse_healthy();

    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), 0);

    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_PGM_ADDR), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_WR_LOCK), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_CFG), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_CACHE_LOAD), 0);
}
END_TEST
#else
/* Every burn exit path must clear PGM_EN and re-lock the controller, so the
 * OTP registers are never left writable. */
static void assert_controller_secured(void)
{
    ck_assert_uint_eq(fake_get(ZYNQMP_EFUSE_CFG) & ZYNQMP_EFUSE_CFG_PGM_EN, 0);
    ck_assert_uint_eq(fake_get(ZYNQMP_EFUSE_WR_LOCK),
        ZYNQMP_EFUSE_WR_LOCK_VAL);
    ck_assert_int_ge(fake_writes(ZYNQMP_EFUSE_WR_LOCK), 2);
}

START_TEST(test_burn_success_path){
    fake_reset();
    sysmon_healthy();
    efuse_healthy();

    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), 0);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_PGM_ADDR), 1);
    /* page 0, row 8, col 0 */
    ck_assert_uint_eq(fake_get(ZYNQMP_EFUSE_PGM_ADDR), 0x100);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_CACHE_LOAD), 1);
    assert_controller_secured();
}
END_TEST

/* The whole margin field must be cleared before mode 2 is selected: OR-ing
 * onto a leftover mode 1 would select mode 3. */
START_TEST(test_burn_sets_margin_mode_2){
    fake_reset();
    sysmon_healthy();
    efuse_healthy();
    /* leave the controller in margin mode 1 */
    fake_set(ZYNQMP_EFUSE_CFG,
        (uint32_t)ZYNQMP_EFUSE_CFG_MARGIN_1_RD_FOR_TEST
            << ZYNQMP_EFUSE_CFG_MARGIN_RD_SHIFT);

    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), 0);

    ck_assert_uint_eq((fake_get(ZYNQMP_EFUSE_CFG) &
            ZYNQMP_EFUSE_CFG_MARGIN_RD_MASK) >>
            ZYNQMP_EFUSE_CFG_MARGIN_RD_SHIFT,
        ZYNQMP_EFUSE_CFG_MARGIN_2_RD);
}
END_TEST

/* Every field of the one-shot program address must be packed correctly, not
 * just the row. */
START_TEST(test_burn_address_encoding){
    fake_reset();
    sysmon_healthy();
    efuse_healthy();
    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 31), 0);
    ck_assert_uint_eq(fake_get(ZYNQMP_EFUSE_PGM_ADDR), 0x11F);

    fake_reset();
    sysmon_healthy();
    efuse_healthy();
    ck_assert_int_eq(zynqmp_efuse_write_bit(1, 9, 7), 0);
    ck_assert_uint_eq(fake_get(ZYNQMP_EFUSE_PGM_ADDR), 0x927);

    fake_reset();
    sysmon_healthy();
    efuse_healthy();
    ck_assert_int_eq(zynqmp_efuse_write_bit(3, 63, 31), 0);
    ck_assert_uint_eq(fake_get(ZYNQMP_EFUSE_PGM_ADDR), 0x1FFF);
}
END_TEST

START_TEST(test_burn_bad_tbits_refuses){
    fake_reset();
    sysmon_healthy();
    efuse_fail_tbits = 1;
    efuse_healthy();

    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), -1);
    /* refused before the strobe */
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_PGM_ADDR), 0);
    assert_controller_secured();
}
END_TEST

START_TEST(test_burn_program_timeout){
    fake_reset();
    sysmon_healthy();
    efuse_fail_pgm = 1;
    efuse_healthy();

    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), -1);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_PGM_ADDR), 1);
    /* a failed program must not be followed by a cache reload */
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_CACHE_LOAD), 0);
    assert_controller_secured();
}
END_TEST

START_TEST(test_burn_cache_reload_timeout){
    fake_reset();
    sysmon_healthy();
    efuse_fail_cache = 1;
    efuse_healthy();

    ck_assert_int_eq(zynqmp_efuse_write_bit(0, 8, 0), -1);
    ck_assert_int_eq(fake_writes(ZYNQMP_EFUSE_CACHE_LOAD), 1);
    assert_controller_secured();
}
END_TEST
#endif /* ZYNQMP_EFUSE_BURN */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot zynqmp efuse guards");
    TCase *tc = tcase_create("efuse-guards");

    tcase_add_test(tc, test_prog_allowed_gate);
    tcase_add_test(tc, test_write_bit_guards);
    tcase_add_test(tc, test_sec_policy_check);
    tcase_add_test(tc, test_bbram_program_guards);
    tcase_add_test(tc, test_bbram_zeroize_and_status);
    tcase_add_test(tc, test_rejected_requests_never_strobe);
#ifndef ZYNQMP_EFUSE_BURN
    tcase_add_test(tc, test_report_only_writes_nothing);
#else
    tcase_add_test(tc, test_burn_success_path);
    tcase_add_test(tc, test_burn_sets_margin_mode_2);
    tcase_add_test(tc, test_burn_address_encoding);
    tcase_add_test(tc, test_burn_bad_tbits_refuses);
    tcase_add_test(tc, test_burn_program_timeout);
    tcase_add_test(tc, test_burn_cache_reload_timeout);
#endif
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? 0 : 1;
}
