/* unit-t10xx-flash-status.c
 *
 * Regression test for F-11033: hal_flash_write() and hal_flash_erase()
 * in hal/nxp_t10xx.c discarded the return value of
 * hal_flash_status_wait(), so a program or erase that timed out (the
 * NOR stuck mid-operation) still reported success to the caller.
 *
 * The real functions are extracted by the Makefile and run against a
 * mock QPI status model: offset 0 of a sector reports the DQ status
 * byte (toggling while busy, 0x44 after a program, 0x4C after an
 * erase). udelay() is a no-op, so a stuck device burns the full poll
 * budget (200 ms / 1.1 s worth of iterations) in milliseconds.
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

/* 16-bit QPI configuration, as used by the T10xx targets. */
#define FLASH_BASE_ADDR   0
#define FLASH_PAGE_SIZE   1024
#define FLASH_SECTOR_SIZE (128 * 1024)
#define FLASH_CFI_WIDTH   16

#define AMD_CMD_ERASE_START        0x80
#define AMD_CMD_ERASE_SECTOR       0x30
#define AMD_CMD_UNLOCK_START       0xAA
#define AMD_CMD_UNLOCK_ACK         0x55
#define AMD_CMD_WRITE_TO_BUFFER    0x25
#define AMD_CMD_WRITE_BUFFER_CONFIRM 0x29
#define AMD_STATUS_TOGGLE          0x40

#define FLASH_UNLOCK_ADDR1 0x555
#define FLASH_UNLOCK_ADDR2 0x2AA

void udelay(uint32_t us);

/* ---- mock QPI status model ------------------------------------------- */

typedef enum t10xx_mock_state {
    ST_IDLE = 0,
    ST_PROGRAMMING,
    ST_ERASING,
    ST_PROG_DONE,
    ST_ERASE_DONE,
} t10xx_mock_state;

#define MOCK_SECTOR_BYTES 64
#define NSECTORS 4

static uint8_t g_flash[NSECTORS * MOCK_SECTOR_BYTES];
static int g_state[NSECTORS];
static int g_stuck[NSECTORS];
static int g_unlocks;

static void mock_reset(int stuck)
{
    int s;

    memset(g_flash, 0, sizeof(g_flash));
    for (s = 0; s < NSECTORS; s++) {
        g_state[s] = ST_IDLE;
        g_stuck[s] = stuck;
    }
    g_unlocks = 0;
}

/* The real macros dereference the memory-mapped flash window; the test
 * routes them through the model so command writes drive the state.
 * 16-bit variant: every index is a word index (byte offset = 2 * n),
 * and the 8-bit write replicates the byte across the 16-bit word. */
static uint8_t mock_read8(uint32_t sector, uint32_t n)
{
    uint32_t byte_off = 2 * n;

    if (n == 0 &&
        ((g_state[sector] == ST_PROGRAMMING) ||
         (g_state[sector] == ST_ERASING))) {
        if (!g_stuck[sector]) {
            /* Complete on the first status poll after the command. */
            g_state[sector] = (g_state[sector] == ST_PROGRAMMING) ?
                ST_PROG_DONE : ST_ERASE_DONE;
            return (g_state[sector] == ST_PROG_DONE) ? 0x44 : 0x4C;
        }
        return AMD_STATUS_TOGGLE; /* busy, never completes */
    }
    if (n == 0) {
        return 0x4C; /* idle: satisfies program (0x44) and erase (0x4C) */
    }
    if (byte_off < MOCK_SECTOR_BYTES) {
        return g_flash[sector * MOCK_SECTOR_BYTES + byte_off];
    }
    return 0;
}

static uint16_t mock_read16(uint32_t sector, uint32_t n)
{
    uint32_t byte_off = 2 * n;

    return (uint16_t)(g_flash[sector * MOCK_SECTOR_BYTES + byte_off] |
                      ((uint16_t)g_flash[sector * MOCK_SECTOR_BYTES +
                                          byte_off + 1] << 8));
}

static void mock_write8(uint32_t sector, uint32_t n, uint8_t val)
{
    uint32_t byte_off = 2 * n;

    if (n == FLASH_UNLOCK_ADDR1 && val == AMD_CMD_UNLOCK_START) {
        g_unlocks++;
        return;
    }
    if (n == FLASH_UNLOCK_ADDR2 && val == AMD_CMD_UNLOCK_ACK) {
        return;
    }
    if (n == 0 && val == AMD_CMD_ERASE_SECTOR) {
        g_state[sector] = ST_ERASING;
        return;
    }
    if (val == AMD_CMD_WRITE_BUFFER_CONFIRM) {
        g_state[sector] = ST_PROGRAMMING;
        return;
    }
    if (byte_off < MOCK_SECTOR_BYTES) {
        g_flash[sector * MOCK_SECTOR_BYTES + byte_off] = val;
        g_flash[sector * MOCK_SECTOR_BYTES + byte_off + 1] = val;
    }
}

static void mock_write16(uint32_t sector, uint32_t n, uint16_t val)
{
    uint32_t byte_off = 2 * n;

    if (byte_off + 1 < MOCK_SECTOR_BYTES) {
        g_flash[sector * MOCK_SECTOR_BYTES + byte_off] = (uint8_t)(val & 0xFF);
        g_flash[sector * MOCK_SECTOR_BYTES + byte_off + 1] =
            (uint8_t)(val >> 8);
    }
}

#define FLASH_IO8_READ(sec, n)  mock_read8((sec), (n))
#define FLASH_IO8_WRITE(sec, n, val) mock_write8((sec), (n), (val))
#define FLASH_IO16_READ(sec, n) mock_read16((sec), (n))
#define FLASH_IO16_WRITE(sec, n, val) mock_write16((sec), (n), (val))

/* The real functions from hal/nxp_t10xx.c (extracted by the Makefile). */
static void hal_flash_unlock_sector(uint32_t sector);
#include "t10xx_flash_status_extract.h"

void udelay(uint32_t us)
{
    (void)us; /* no real delay: a stuck device burns the poll budget fast */
}

START_TEST (test_flash_write_success)
{
    uint8_t data[8];
    int i;
    int ret;

    mock_reset(0);
    for (i = 0; i < 8; i++)
        data[i] = (uint8_t)(0xA0 + i);

    ret = hal_flash_write(0, data, sizeof(data));
    ck_assert_int_eq(ret, 0);
    ck_assert_int_gt(g_unlocks, 0);
    ck_assert_mem_eq(g_flash, data, 8);
}
END_TEST

START_TEST (test_flash_write_timeout_returns_error)
{
    uint8_t data[8] = { 0 };
    int ret;

    /* Device stuck mid-program: the status never settles. */
    mock_reset(1);

    ret = hal_flash_write(0, data, sizeof(data));
    ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST (test_flash_erase_success)
{
    int ret;

    mock_reset(0);

    ret = hal_flash_erase(0, FLASH_SECTOR_SIZE);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_gt(g_unlocks, 0);
}
END_TEST

START_TEST (test_flash_erase_timeout_returns_error)
{
    int ret;

    /* Device stuck mid-erase: the status never settles. */
    mock_reset(1);

    ret = hal_flash_erase(0, FLASH_SECTOR_SIZE);
    ck_assert_int_eq(ret, -1);
}
END_TEST

Suite *t10xx_flash_status_suite(void)
{
    Suite *s  = suite_create("t10xx flash status");
    TCase *tc = tcase_create("flash-op-timeout");

    tcase_add_test(tc, test_flash_write_success);
    tcase_add_test(tc, test_flash_write_timeout_returns_error);
    tcase_add_test(tc, test_flash_erase_success);
    tcase_add_test(tc, test_flash_erase_timeout_returns_error);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = t10xx_flash_status_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
