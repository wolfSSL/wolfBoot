/* unit-mcxw-flash.c
 *
 * Unit tests for the MCX W71 flash driver in hal/mcxw.c, against a model of
 * the FMU command handshake, built against the NXP MCUXpresso SDK headers.
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

/* MCX W layout (config/examples/mcxw-tz.config) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x2000
#define WOLFBOOT_PARTITION_SIZE              0x58000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x32000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x8A000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0xE2000

#include "fsl_common.h"

FMU_Type *mock_fmu_regs(void);
SMSCM_Type *mock_smscm_regs(void);
MCM_Type *mock_mcm_regs(void);
volatile uint32_t *mock_flash_ptr(uint32_t address);

/* Point the SDK peripheral instances at the mocks below */
#undef FMU0
#undef SMSCM
#undef MCM
#define FMU0  (mock_fmu_regs())
#define SMSCM (mock_smscm_regs())
#define MCM   (mock_mcm_regs())
#define MCXW_FLASH_PTR(a) mock_flash_ptr(a)

#include "../../hal/mcxw.c"

#define MOCK_MAX_CMDS          64
/* FSTAT bit the HAL never writes: its absence marks a fresh HAL store */
#define MOCK_FSTAT_SENTINEL    (1U << 30)
#define MOCK_ERR_ACCERR        0x20U
#define MOCK_ERR_PVIOL         0x10U
#define MOCK_SMSCM_RESET       0x00000013U
#define MOCK_STAGE_EMPTY       0xA5A5A5A5U
#define MOCK_CCIF_POLL_LIMIT   1000
/* Polls after staging before PERDY rises: only a real wait loop sees it */
#define MOCK_PERDY_POLLS       3
#define TEST_BASE              0x00040000U

uint32_t mock_primask;
int mock_barriers;

static uint8_t mock_flash[MCXW_FLASH_SIZE];
static uint32_t staging[MCXW_FLASH_PHRASE_WORDS];
static uint32_t staged_addr;

static FMU_Type fmu;
static uint32_t fmu_state;
/* FMU command phases: busy after launch, then PEWEN, then PERDY once the
 * phrase is staged; each step needs a status poll, as on the hardware */
enum { FMU_IDLE, FMU_LAUNCHED, FMU_WRITE_ENABLED, FMU_READY };
static int fmu_phase;
static int staged_polls;
static int ccif_polls;
static int cmd_count;
static int exec_count;
static uint32_t cmd_code[MOCK_MAX_CMDS];
static uint32_t cmd_addr[MOCK_MAX_CMDS];
static int fail_exec_at;
static int fail_launch_at;
static int fail_stage_at;
static int irq_enabled_during_cmd;

static SMSCM_Type smscm;
static MCM_Type mcm;
static int cache_clears;
static int code_cache_clears;

volatile uint32_t *mock_flash_ptr(uint32_t address)
{
    if ((address & MCXW_FLASH_SECURE_ALIAS) != 0U) {
        /* Command data stores: captured for the FMU model */
        staged_addr = address;
        memset(staging, (int)(MOCK_STAGE_EMPTY & 0xFFU), sizeof(staging));
        return staging;
    }
    ck_assert_uint_lt(address, MCXW_FLASH_SIZE);
    ck_assert_uint_eq(address % sizeof(uint32_t), 0);
    return (volatile uint32_t *)(void *)&mock_flash[address];
}

static void fmu_execute(void)
{
    uint32_t phys = staged_addr & ~MCXW_FLASH_SECURE_ALIAS;
    uint32_t cmd = fmu.FCCOB[0];
    const uint8_t *src = (const uint8_t *)staging;
    uint32_t i;

    if (exec_count < MOCK_MAX_CMDS) {
        cmd_code[exec_count] = cmd;
        cmd_addr[exec_count] = staged_addr;
    }
    exec_count++;
    if (exec_count == fail_exec_at) {
        fmu_state = FMU_FSTAT_CCIF_MASK | MOCK_ERR_ACCERR;
        return;
    }
    if (cmd == MCXW_FMU_CMD_PROGRAM_PHRASE) {
        ck_assert_uint_eq(phys % MCXW_FLASH_PHRASE_SIZE, 0);
        ck_assert_uint_le(phys + MCXW_FLASH_PHRASE_SIZE, MCXW_FLASH_SIZE);
        /* ECC flash: never program a phrase that is not erased */
        for (i = 0; i < MCXW_FLASH_PHRASE_SIZE; i++)
            ck_assert_uint_eq(mock_flash[phys + i], 0xFF);
        for (i = 0; i < MCXW_FLASH_PHRASE_SIZE; i++)
            mock_flash[phys + i] &= src[i];
    }
    else if (cmd == MCXW_FMU_CMD_ERASE_SECTOR) {
        ck_assert_uint_eq(phys % MCXW_FLASH_SECTOR_SIZE, 0);
        ck_assert_uint_le(phys + MCXW_FLASH_SECTOR_SIZE, MCXW_FLASH_SIZE);
        for (i = 0; i < MCXW_FLASH_PHRASE_WORDS; i++)
            ck_assert_uint_eq(staging[i], 0U);
        memset(&mock_flash[phys], 0xFF, MCXW_FLASH_SECTOR_SIZE);
    }
    else {
        ck_abort_msg("unexpected FMU command 0x%x", cmd);
    }
    fmu_state = FMU_FSTAT_CCIF_MASK;
}

static int staged_words(void)
{
    int n = 0;
    uint32_t i;

    for (i = 0; i < MCXW_FLASH_PHRASE_WORDS; i++) {
        if (staging[i] != MOCK_STAGE_EMPTY)
            n++;
    }
    return n;
}

static void fmu_store(uint32_t w)
{
    if ((w & FMU_FSTAT_CCIF_MASK) != 0U) {
        ck_assert_int_eq(fmu_phase, FMU_IDLE);
        if (mock_primask == 0U)
            irq_enabled_during_cmd = 1;
        cmd_count++;
        if (cmd_count == fail_launch_at) {
            fmu_state = FMU_FSTAT_CCIF_MASK | MOCK_ERR_PVIOL;
            return;
        }
        fmu_phase = FMU_LAUNCHED;
        fmu_state = 0;
        staged_polls = 0;
        ccif_polls = 0;
    }
    else if ((w & FMU_FSTAT_PERDY_MASK) != 0U) {
        if (fmu_phase != FMU_READY)
            ck_abort_msg("PERDY cleared before the FMU raised it");
        fmu_phase = FMU_IDLE;
        fmu_execute();
    }
    else {
        fmu_state &= ~(w & MCXW_FSTAT_CLEAR_MASK);
    }
}

static void fmu_poll(void)
{
    if (fmu_phase == FMU_LAUNCHED) {
        if (staged_words() != 0)
            ck_abort_msg("phrase written before PEWEN was set");
        fmu_phase = FMU_WRITE_ENABLED;
        fmu_state = FMU_FSTAT_PEWEN(1);
    }
    else if (fmu_phase == FMU_WRITE_ENABLED &&
            staged_words() == (int)MCXW_FLASH_PHRASE_WORDS) {
        if (cmd_count == fail_stage_at) {
            /* Error after staging: the FMU aborts without raising PERDY */
            fmu_phase = FMU_IDLE;
            fmu_state = FMU_FSTAT_CCIF_MASK | MOCK_ERR_ACCERR;
        }
        else if (++staged_polls >= MOCK_PERDY_POLLS) {
            fmu_phase = FMU_READY;
            fmu_state |= FMU_FSTAT_PERDY_MASK;
        }
    }
    else if (fmu_phase == FMU_READY &&
            ++ccif_polls > MOCK_CCIF_POLL_LIMIT) {
        ck_abort_msg("waiting on CCIF without clearing PERDY");
    }
}

FMU_Type *mock_fmu_regs(void)
{
    if ((fmu.FSTAT & MOCK_FSTAT_SENTINEL) == 0U)
        fmu_store(fmu.FSTAT);
    else
        fmu_poll();
    fmu.FSTAT = fmu_state | MOCK_FSTAT_SENTINEL;
    return &fmu;
}

SMSCM_Type *mock_smscm_regs(void)
{
    if ((smscm.OCMDR0 & SMSCM_OCMDR0_OCMCF2_MASK) ==
            SMSCM_OCMDR0_OCMCF2(MCXW_OCMCF2_CACHE_CLEAR)) {
        cache_clears++;
    }
    return &smscm;
}

MCM_Type *mock_mcm_regs(void)
{
    if ((mcm.CPCR2 & MCM_CPCR2_CCBC_MASK) != 0U) {
        code_cache_clears++;
        mcm.CPCR2 &= ~MCM_CPCR2_CCBC_MASK;
    }
    return &mcm;
}

static void reset_mocks(void)
{
    memset(mock_flash, 0xFF, sizeof(mock_flash));
    memset(&fmu, 0, sizeof(fmu));
    fmu_state = FMU_FSTAT_CCIF_MASK;
    fmu.FSTAT = fmu_state | MOCK_FSTAT_SENTINEL;
    fmu_phase = FMU_IDLE;
    cmd_count = 0;
    exec_count = 0;
    fail_exec_at = 0;
    fail_launch_at = 0;
    fail_stage_at = 0;
    irq_enabled_during_cmd = 0;
    smscm.OCMDR0 = MOCK_SMSCM_RESET;
    mcm.CPCR2 = 0;
    cache_clears = 0;
    code_cache_clears = 0;
    mock_primask = 0;
}

static void fill(uint8_t *buf, uint32_t len, uint8_t seed)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(seed + i);
}

START_TEST(test_write_aligned_phrase)
{
    uint8_t data[MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x10);
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), 0);
    ck_assert_int_eq(exec_count, 1);
    ck_assert_uint_eq(cmd_code[0], MCXW_FMU_CMD_PROGRAM_PHRASE);
    /* Command stores go through the secure alias */
    ck_assert_uint_eq(cmd_addr[0], TEST_BASE | MCXW_FLASH_SECURE_ALIAS);
    ck_assert_mem_eq(&mock_flash[TEST_BASE], data, sizeof(data));
}
END_TEST

START_TEST(test_write_unaligned_keeps_neighbours)
{
    const uint8_t data[3] = { 0x11, 0x22, 0x33 };
    uint32_t i;

    reset_mocks();
    ck_assert_int_eq(hal_flash_write(TEST_BASE + 5, data, sizeof(data)), 0);
    ck_assert_int_eq(exec_count, 1);
    ck_assert_uint_eq(cmd_addr[0] & ~MCXW_FLASH_SECURE_ALIAS, TEST_BASE);
    for (i = 0; i < MCXW_FLASH_PHRASE_SIZE; i++) {
        if (i >= 5 && i < 8)
            ck_assert_uint_eq(mock_flash[TEST_BASE + i], data[i - 5]);
        else
            ck_assert_uint_eq(mock_flash[TEST_BASE + i], 0xFF);
    }
}
END_TEST

START_TEST(test_write_spans_phrases)
{
    uint8_t data[20];

    reset_mocks();
    fill(data, sizeof(data), 0x40);
    ck_assert_int_eq(hal_flash_write(TEST_BASE + 0xC, data, sizeof(data)), 0);
    ck_assert_int_eq(exec_count, 2);
    ck_assert_uint_eq(cmd_addr[0] & ~MCXW_FLASH_SECURE_ALIAS, TEST_BASE);
    ck_assert_uint_eq(cmd_addr[1] & ~MCXW_FLASH_SECURE_ALIAS,
        TEST_BASE + MCXW_FLASH_PHRASE_SIZE);
    ck_assert_mem_eq(&mock_flash[TEST_BASE + 0xC], data, sizeof(data));
    ck_assert_uint_eq(mock_flash[TEST_BASE + 0xB], 0xFF);
    ck_assert_uint_eq(mock_flash[TEST_BASE + 0xC + sizeof(data)], 0xFF);
}
END_TEST

START_TEST(test_write_erased_data_is_skipped)
{
    uint8_t data[2 * MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    memset(data, 0xFF, sizeof(data));
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), 0);
    ck_assert_int_eq(cmd_count, 0);
}
END_TEST

START_TEST(test_write_same_data_is_skipped)
{
    uint8_t data[MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x70);
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), 0);
    ck_assert_int_eq(hal_flash_write(TEST_BASE + 4, data + 4, 8), 0);
    ck_assert_int_eq(cmd_count, 1);
}
END_TEST

START_TEST(test_write_reprogram_rejected)
{
    uint8_t first[4] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t second[4] = { 0xA1, 0xA2, 0xA3, 0xA4 };

    reset_mocks();
    ck_assert_int_eq(hal_flash_write(TEST_BASE, first, sizeof(first)), 0);
    /* Different bytes of the same, already programmed, phrase */
    ck_assert_int_eq(hal_flash_write(TEST_BASE + 8, second, sizeof(second)),
        -1);
    ck_assert_int_eq(cmd_count, 1);
    ck_assert_mem_eq(&mock_flash[TEST_BASE], first, sizeof(first));
    ck_assert_uint_eq(mock_flash[TEST_BASE + 8], 0xFF);
}
END_TEST

START_TEST(test_write_error_stops_loop)
{
    uint8_t data[3 * MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x20);
    fail_exec_at = 2;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), -1);
    ck_assert_int_eq(exec_count, 2);
    ck_assert_int_eq(fmu_phase, FMU_IDLE);
}
END_TEST

START_TEST(test_write_launch_error_returns)
{
    uint8_t data[2 * MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x30);
    fail_launch_at = 1;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), -1);
    ck_assert_int_eq(cmd_count, 1);
    ck_assert_int_eq(exec_count, 0);
    ck_assert_uint_eq(mock_flash[TEST_BASE], 0xFF);
}
END_TEST

START_TEST(test_write_stage_error_stops_loop)
{
    uint8_t data[3 * MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x40);
    mock_primask = 1;
    fail_stage_at = 2;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), -1);
    ck_assert_int_eq(cmd_count, 2);
    ck_assert_int_eq(exec_count, 1);
    ck_assert_int_eq(fmu_phase, FMU_IDLE);
    ck_assert_uint_eq(mock_primask, 1);
    ck_assert_int_eq(cache_clears, 2);
    ck_assert_mem_eq(&mock_flash[TEST_BASE], data, MCXW_FLASH_PHRASE_SIZE);
    ck_assert_uint_eq(mock_flash[TEST_BASE + MCXW_FLASH_PHRASE_SIZE], 0xFF);
}
END_TEST

START_TEST(test_write_secure_alias_address)
{
    uint8_t data[MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x55);
    ck_assert_int_eq(hal_flash_write(TEST_BASE | MCXW_FLASH_SECURE_ALIAS,
        data, sizeof(data)), 0);
    ck_assert_mem_eq(&mock_flash[TEST_BASE], data, sizeof(data));
}
END_TEST

START_TEST(test_write_bounds)
{
    uint8_t data[2 * MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x01);
    ck_assert_int_eq(hal_flash_write(MCXW_FLASH_SIZE, data, 1), -1);
    ck_assert_int_eq(hal_flash_write(MCXW_FLASH_SIZE - MCXW_FLASH_PHRASE_SIZE,
        data, sizeof(data)), -1);
    ck_assert_int_eq(hal_flash_write(0x20000000U, data, 1), -1);
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, -1), -1);
    ck_assert_int_eq(hal_flash_write(TEST_BASE, NULL, 4), -1);
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, 0), 0);
    /* The NXP PROD_DATA sector is never written */
    ck_assert_int_eq(hal_flash_write(MCXW_FLASH_PROD_DATA, data, 1), -1);
    ck_assert_int_eq(hal_flash_write(MCXW_FLASH_PROD_DATA - 4, data, 8), -1);
    ck_assert_int_eq(cmd_count, 0);
    ck_assert_int_eq(hal_flash_write(MCXW_FLASH_PROD_DATA -
        MCXW_FLASH_PHRASE_SIZE, data, MCXW_FLASH_PHRASE_SIZE), 0);
}
END_TEST

START_TEST(test_primask_restored)
{
    uint8_t data[MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x66);
    mock_primask = 0;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), 0);
    ck_assert_uint_eq(mock_primask, 0);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, MCXW_FLASH_SECTOR_SIZE), 0);
    ck_assert_uint_eq(mock_primask, 0);

    mock_primask = 1;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), 0);
    ck_assert_uint_eq(mock_primask, 1);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, MCXW_FLASH_SECTOR_SIZE), 0);
    ck_assert_uint_eq(mock_primask, 1);

    ck_assert_int_eq(irq_enabled_during_cmd, 0);
}
END_TEST

START_TEST(test_cache_flushed_per_command)
{
    uint8_t data[2 * MCXW_FLASH_PHRASE_SIZE];

    reset_mocks();
    fill(data, sizeof(data), 0x77);
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, sizeof(data)), 0);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, MCXW_FLASH_SECTOR_SIZE), 0);
    (void)mock_mcm_regs();
    ck_assert_int_eq(exec_count, 3);
    ck_assert_int_eq(cache_clears, 3);
    ck_assert_int_eq(code_cache_clears, 3);
    /* The flash cache configuration is restored after each flush */
    ck_assert_uint_eq(smscm.OCMDR0, MOCK_SMSCM_RESET);
}
END_TEST

START_TEST(test_erase_sectors)
{
    reset_mocks();
    memset(&mock_flash[TEST_BASE], 0x00, 2 * MCXW_FLASH_SECTOR_SIZE + 1);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, 2 * MCXW_FLASH_SECTOR_SIZE),
        0);
    ck_assert_int_eq(exec_count, 2);
    ck_assert_uint_eq(cmd_code[0], MCXW_FMU_CMD_ERASE_SECTOR);
    ck_assert_uint_eq(cmd_addr[0], TEST_BASE | MCXW_FLASH_SECURE_ALIAS);
    ck_assert_uint_eq(cmd_addr[1],
        (TEST_BASE + MCXW_FLASH_SECTOR_SIZE) | MCXW_FLASH_SECURE_ALIAS);
    ck_assert_uint_eq(mock_flash[TEST_BASE], 0xFF);
    ck_assert_uint_eq(mock_flash[TEST_BASE + 2 * MCXW_FLASH_SECTOR_SIZE - 1],
        0xFF);
    /* The next sector is untouched */
    ck_assert_uint_eq(mock_flash[TEST_BASE + 2 * MCXW_FLASH_SECTOR_SIZE],
        0x00);
}
END_TEST

START_TEST(test_erase_unaligned_covers_range)
{
    reset_mocks();
    /* Starts near the end of one sector and reaches into the next */
    ck_assert_int_eq(hal_flash_erase(TEST_BASE + MCXW_FLASH_SECTOR_SIZE - 0x100,
        0x200), 0);
    ck_assert_int_eq(exec_count, 2);
    ck_assert_uint_eq(cmd_addr[0] & ~MCXW_FLASH_SECURE_ALIAS, TEST_BASE);
    ck_assert_uint_eq(cmd_addr[1] & ~MCXW_FLASH_SECURE_ALIAS,
        TEST_BASE + MCXW_FLASH_SECTOR_SIZE);
}
END_TEST

START_TEST(test_erase_error_stops_loop)
{
    reset_mocks();
    fail_exec_at = 2;
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, 4 * MCXW_FLASH_SECTOR_SIZE),
        -1);
    ck_assert_int_eq(exec_count, 2);
}
END_TEST

START_TEST(test_erase_bounds)
{
    reset_mocks();
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, 0), -1);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, -1), -1);
    ck_assert_int_eq(hal_flash_erase(MCXW_FLASH_SIZE, 1), -1);
    ck_assert_int_eq(hal_flash_erase(MCXW_FLASH_SIZE - MCXW_FLASH_SECTOR_SIZE,
        2 * MCXW_FLASH_SECTOR_SIZE), -1);
    ck_assert_int_eq(hal_flash_erase(MCXW_FLASH_PROD_DATA,
        MCXW_FLASH_SECTOR_SIZE), -1);
    ck_assert_int_eq(hal_flash_erase(MCXW_FLASH_PROD_DATA -
        MCXW_FLASH_SECTOR_SIZE, 2 * MCXW_FLASH_SECTOR_SIZE), -1);
    ck_assert_int_eq(cmd_count, 0);
    ck_assert_int_eq(hal_flash_erase(MCXW_FLASH_PROD_DATA -
        MCXW_FLASH_SECTOR_SIZE, MCXW_FLASH_SECTOR_SIZE), 0);
    ck_assert_uint_eq(cmd_addr[0] & ~MCXW_FLASH_SECURE_ALIAS,
        MCXW_FLASH_PROD_DATA - MCXW_FLASH_SECTOR_SIZE);
}
END_TEST

Suite *mcxw_flash_suite(void)
{
    Suite *s = suite_create("mcxw-flash");
    TCase *tc = tcase_create("mcxw-flash");

    tcase_add_test(tc, test_write_aligned_phrase);
    tcase_add_test(tc, test_write_unaligned_keeps_neighbours);
    tcase_add_test(tc, test_write_spans_phrases);
    tcase_add_test(tc, test_write_erased_data_is_skipped);
    tcase_add_test(tc, test_write_same_data_is_skipped);
    tcase_add_test(tc, test_write_reprogram_rejected);
    tcase_add_test(tc, test_write_error_stops_loop);
    tcase_add_test(tc, test_write_launch_error_returns);
    tcase_add_test(tc, test_write_stage_error_stops_loop);
    tcase_add_test(tc, test_write_secure_alias_address);
    tcase_add_test(tc, test_write_bounds);
    tcase_add_test(tc, test_primask_restored);
    tcase_add_test(tc, test_cache_flushed_per_command);
    tcase_add_test(tc, test_erase_sectors);
    tcase_add_test(tc, test_erase_unaligned_covers_range);
    tcase_add_test(tc, test_erase_error_stops_loop);
    tcase_add_test(tc, test_erase_bounds);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = mcxw_flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
