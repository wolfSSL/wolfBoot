/* unit-psoc6-flash.c
 *
 * Unit tests for the PSoC 6 internal flash driver and console in
 * hal/psoc6.c, against a model of main and work flash, built against the
 * Infineon mtb-pdl-cat1 headers.
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

/* PSoC 6 layout (config/examples/cypsoc6.config) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x1000
#define WOLFBOOT_PARTITION_SIZE              0xE0000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x10020000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x10100000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x101E0000

#include "cy_device_headers.h"
#include "cy_scb_uart.h"

const void *mock_flash_ptr(uint32_t address);
#define PSOC6_FLASH_PTR(a) mock_flash_ptr(a)
/* The PDL's inline Put writes the TX FIFO register; record the bytes */
uint32_t mock_uart_put(CySCB_Type *base, uint32_t data);
#define Cy_SCB_UART_Put mock_uart_put

#include "../../hal/psoc6.c"

#define MOCK_MAX_OPS   64
#define MOCK_TX_MAX    64
#define ROW            PSOC6_FLASH_ROW_SIZE
#define SUBSECTOR      (ROW * 8U)
#define SECTOR         (ROW * 512U)
#define TEST_ROW       0x10040000U

enum { OP_WRITE, OP_PROGRAM, OP_ERASE_ROW, OP_ERASE_SUBSECTOR,
    OP_ERASE_SECTOR };

uint32_t mock_primask;
int mock_barriers;
int mock_wfi;
NVIC_Type mock_nvic;
SCB_Type mock_scb;
SysTick_Type mock_systick;

static uint8_t mock_main[PSOC6_FLASH_SIZE];
static uint8_t mock_wflash[PSOC6_WFLASH_SIZE];
static struct {
    int op;
    uint32_t addr;
} ops[MOCK_MAX_OPS];
static int n_ops;
static int cache_clears;
static int fail_at_op;
static char tx[MOCK_TX_MAX];
static int n_tx;

void mock_wfi_hook(void)
{
}

void CY_ASSERT_HANDLER(void)
{
    ck_abort_msg("PDL assert");
}

static int resets;

void NVIC_SystemReset(void)
{
    resets++;
}

static uint8_t *mock_phys(uint32_t address)
{
    if ((address >= PSOC6_FLASH_BASE) &&
        (address < PSOC6_FLASH_BASE + PSOC6_FLASH_SIZE)) {
        return &mock_main[address - PSOC6_FLASH_BASE];
    }
    if ((address >= PSOC6_WFLASH_BASE) &&
        (address < PSOC6_WFLASH_BASE + PSOC6_WFLASH_SIZE)) {
        return &mock_wflash[address - PSOC6_WFLASH_BASE];
    }
    ck_abort_msg("flash access outside the model: 0x%08x", address);
    return NULL;
}

const void *mock_flash_ptr(uint32_t address)
{
    return mock_phys(address);
}

static cy_en_flashdrv_status_t mock_op(int op, uint32_t addr, uint32_t size)
{
    ck_assert_int_lt(n_ops, MOCK_MAX_OPS);
    ck_assert_uint_eq(addr % size, 0);
    ops[n_ops].op = op;
    ops[n_ops].addr = addr;
    n_ops++;
    if (n_ops == fail_at_op) {
        return CY_FLASH_DRV_ERR_UNC;
    }
    if (op != OP_WRITE) {
        memset(mock_phys(addr), 0, size);
    }
    return CY_FLASH_DRV_SUCCESS;
}

cy_en_flashdrv_status_t Cy_Flash_WriteRow(uint32_t rowAddr,
    const uint32_t* data)
{
    cy_en_flashdrv_status_t st = mock_op(OP_WRITE, rowAddr, ROW);

    if (st == CY_FLASH_DRV_SUCCESS) {
        memcpy(mock_phys(rowAddr), data, ROW);
    }
    return st;
}

cy_en_flashdrv_status_t Cy_Flash_ProgramRow(uint32_t rowAddr,
    const uint32_t* data)
{
    const uint8_t *p = mock_phys(rowAddr);
    cy_en_flashdrv_status_t st;
    uint32_t i;

    /* The PDL requires an erased row */
    for (i = 0; i < ROW; i++)
        ck_assert_uint_eq(p[i], 0);
    st = mock_op(OP_PROGRAM, rowAddr, ROW);
    if (st == CY_FLASH_DRV_SUCCESS) {
        memcpy(mock_phys(rowAddr), data, ROW);
    }
    return st;
}

cy_en_flashdrv_status_t Cy_Flash_EraseRow(uint32_t rowAddr)
{
    return mock_op(OP_ERASE_ROW, rowAddr, ROW);
}

cy_en_flashdrv_status_t Cy_Flash_EraseSubsector(uint32_t subSectorAddr)
{
    return mock_op(OP_ERASE_SUBSECTOR, subSectorAddr, SUBSECTOR);
}

cy_en_flashdrv_status_t Cy_Flash_EraseSector(uint32_t sectorAddr)
{
    return mock_op(OP_ERASE_SECTOR, sectorAddr, SECTOR);
}

void Cy_SysLib_ClearFlashCacheAndBuffer(void)
{
    cache_clears++;
}

uint32_t mock_uart_put(CySCB_Type *base, uint32_t data)
{
    (void)base;
    ck_assert_int_lt(n_tx, MOCK_TX_MAX);
    tx[n_tx++] = (char)data;
    return 1UL;
}

static void fill(uint32_t address, uint8_t seed, uint32_t len)
{
    uint8_t *p = mock_phys(address);
    uint32_t i;

    for (i = 0; i < len; i++) {
        p[i] = (uint8_t)(seed + i);
    }
}

static void setup(void)
{
    memset(mock_main, 0, sizeof(mock_main));
    memset(mock_wflash, 0, sizeof(mock_wflash));
    memset(ops, 0, sizeof(ops));
    n_ops = 0;
    cache_clears = 0;
    fail_at_op = 0;
    n_tx = 0;
}

START_TEST(test_range_ok)
{
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE, ROW), 1);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE,
        PSOC6_FLASH_SIZE), 1);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE +
        PSOC6_FLASH_SIZE - ROW, ROW), 1);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE +
        PSOC6_FLASH_SIZE - ROW, ROW + 1U), 0);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE - ROW, ROW), 0);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE, 0), 0);
    ck_assert_int_eq(psoc6_flash_range_ok(0xFFFFFE00U, 0x400U), 0);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BASE + ROW,
        0xFFFFFFFFU), 0);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_WFLASH_BASE,
        PSOC6_WFLASH_SIZE), 1);
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_WFLASH_BASE,
        PSOC6_WFLASH_SIZE + 1U), 0);
    /* The second-bank alias is only valid with DUALBANK_SWAP */
    ck_assert_int_eq(psoc6_flash_range_ok(PSOC6_FLASH_BANK2_BASE, ROW), 0);
}
END_TEST

START_TEST(test_write_full_row)
{
    uint8_t data[ROW];
    uint32_t i;

    for (i = 0; i < ROW; i++)
        data[i] = (uint8_t)(i * 3U + 1U);
    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, ROW), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(ops[0].op, OP_PROGRAM);
    ck_assert_uint_eq(ops[0].addr, TEST_ROW);
    ck_assert_int_eq(cache_clears, 1);
    ck_assert_mem_eq(mock_phys(TEST_ROW), data, ROW);
}
END_TEST

START_TEST(test_rewrite_programmed_row_erases)
{
    uint8_t data[ROW];

    fill(TEST_ROW, 0x21, ROW);
    memset(data, 0x42, sizeof(data));
    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, ROW), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(ops[0].op, OP_WRITE);
    ck_assert_mem_eq(mock_phys(TEST_ROW), data, ROW);
}
END_TEST

START_TEST(test_write_unaligned_head_preserves_row)
{
    uint8_t before[ROW];
    uint8_t data[10];

    fill(TEST_ROW, 0x40, ROW);
    memcpy(before, mock_phys(TEST_ROW), ROW);
    memset(data, 0xA5, sizeof(data));

    ck_assert_int_eq(hal_flash_write(TEST_ROW + 100U, data, sizeof(data)), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_uint_eq(ops[0].addr, TEST_ROW);
    ck_assert_mem_eq(mock_phys(TEST_ROW), before, 100);
    ck_assert_mem_eq(mock_phys(TEST_ROW + 100U), data, sizeof(data));
    ck_assert_mem_eq(mock_phys(TEST_ROW + 110U), before + 110, ROW - 110U);
}
END_TEST

START_TEST(test_write_partial_tail_preserves_row)
{
    uint8_t before[ROW];
    uint8_t data[ROW + 188U];

    fill(TEST_ROW + ROW, 0x11, ROW);
    memcpy(before, mock_phys(TEST_ROW + ROW), ROW);
    memset(data, 0x5A, sizeof(data));

    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, sizeof(data)), 0);
    ck_assert_int_eq(n_ops, 2);
    ck_assert_uint_eq(ops[0].addr, TEST_ROW);
    ck_assert_uint_eq(ops[1].addr, TEST_ROW + ROW);
    ck_assert_mem_eq(mock_phys(TEST_ROW), data, sizeof(data));
    ck_assert_mem_eq(mock_phys(TEST_ROW + sizeof(data)), before + 188,
        ROW - 188U);
}
END_TEST

START_TEST(test_write_across_row_boundary)
{
    uint8_t data[40];

    memset(data, 0x3C, sizeof(data));
    ck_assert_int_eq(hal_flash_write(TEST_ROW + ROW - 20U, data,
        sizeof(data)), 0);
    ck_assert_int_eq(n_ops, 2);
    ck_assert_uint_eq(ops[0].addr, TEST_ROW);
    ck_assert_uint_eq(ops[1].addr, TEST_ROW + ROW);
    ck_assert_mem_eq(mock_phys(TEST_ROW + ROW - 20U), data, sizeof(data));
    ck_assert_uint_eq(*(uint8_t*)mock_phys(TEST_ROW + ROW - 21U), 0);
    ck_assert_uint_eq(*(uint8_t*)mock_phys(TEST_ROW + ROW + 20U), 0);
}
END_TEST

START_TEST(test_write_unchanged_row_skipped)
{
    uint8_t data[ROW];

    fill(TEST_ROW, 0x77, ROW);
    memcpy(data, mock_phys(TEST_ROW), ROW);
    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, ROW), 0);
    ck_assert_int_eq(n_ops, 0);
    ck_assert_int_eq(cache_clears, 0);
}
END_TEST

START_TEST(test_write_rejects_bad_args)
{
    uint8_t data[ROW];

    memset(data, 1, sizeof(data));
    ck_assert_int_eq(hal_flash_write(TEST_ROW, NULL, ROW), -1);
    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, 0), -1);
    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, -4), -1);
    ck_assert_int_eq(hal_flash_write(PSOC6_FLASH_BASE - ROW, data, ROW), -1);
    ck_assert_int_eq(hal_flash_write(PSOC6_FLASH_BASE + PSOC6_FLASH_SIZE -
        10U, data, 20), -1);
    ck_assert_int_eq(hal_flash_write(PSOC6_FLASH_BANK2_BASE, data, ROW), -1);
    ck_assert_int_eq(n_ops, 0);
}
END_TEST

/* Built as the application with RAM_CODE: the HAL still provides arch_reboot */
START_TEST(test_app_arch_reboot_resets)
{
    resets = 0;
    arch_reboot();
    ck_assert_int_eq(resets, 1);
}
END_TEST

START_TEST(test_write_failure_stops)
{
    uint8_t data[3U * ROW];

    memset(data, 0x99, sizeof(data));
    fail_at_op = 2;
    ck_assert_int_eq(hal_flash_write(TEST_ROW, data, sizeof(data)), -1);
    ck_assert_int_eq(n_ops, 2);
}
END_TEST

START_TEST(test_write_work_flash)
{
    uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    ck_assert_int_eq(hal_flash_write(PSOC6_WFLASH_BASE + 4U, data,
        sizeof(data)), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_uint_eq(ops[0].addr, PSOC6_WFLASH_BASE);
    ck_assert_mem_eq(mock_phys(PSOC6_WFLASH_BASE + 4U), data, sizeof(data));
}
END_TEST

START_TEST(test_erase_picks_largest_unit)
{
    fill(TEST_ROW, 1, SECTOR + SUBSECTOR + ROW);
    ck_assert_int_eq(hal_flash_erase(TEST_ROW, SECTOR + SUBSECTOR + ROW), 0);
    ck_assert_int_eq(n_ops, 3);
    ck_assert_int_eq(ops[0].op, OP_ERASE_SECTOR);
    ck_assert_uint_eq(ops[0].addr, TEST_ROW);
    ck_assert_int_eq(ops[1].op, OP_ERASE_SUBSECTOR);
    ck_assert_uint_eq(ops[1].addr, TEST_ROW + SECTOR);
    ck_assert_int_eq(ops[2].op, OP_ERASE_ROW);
    ck_assert_uint_eq(ops[2].addr, TEST_ROW + SECTOR + SUBSECTOR);
    ck_assert_int_eq(cache_clears, 1);
}
END_TEST

START_TEST(test_erase_unaligned_start_uses_rows)
{
    fill(TEST_ROW + ROW, 1, SUBSECTOR);
    ck_assert_int_eq(hal_flash_erase(TEST_ROW + ROW, SUBSECTOR), 0);
    ck_assert_int_eq(n_ops, 8);
    ck_assert_int_eq(ops[0].op, OP_ERASE_ROW);
    ck_assert_int_eq(ops[6].op, OP_ERASE_ROW);
    ck_assert_uint_eq(ops[6].addr, TEST_ROW + 7U * ROW);
    ck_assert_int_eq(ops[7].op, OP_ERASE_ROW);
    ck_assert_uint_eq(ops[7].addr, TEST_ROW + SUBSECTOR);
}
END_TEST

START_TEST(test_erase_skips_blank)
{
    fill(TEST_ROW + SUBSECTOR + 8U, 1, 1);
    ck_assert_int_eq(hal_flash_erase(TEST_ROW, 2U * SUBSECTOR), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(ops[0].op, OP_ERASE_SUBSECTOR);
    ck_assert_uint_eq(ops[0].addr, TEST_ROW + SUBSECTOR);
}
END_TEST

START_TEST(test_erase_rounds_len_up)
{
    fill(TEST_ROW, 1, ROW);
    ck_assert_int_eq(hal_flash_erase(TEST_ROW, 100), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(ops[0].op, OP_ERASE_ROW);
    ck_assert_uint_eq(*(uint8_t*)mock_phys(TEST_ROW + ROW - 1U), 0);
}
END_TEST

START_TEST(test_erase_rejects_bad_args)
{
    ck_assert_int_eq(hal_flash_erase(TEST_ROW + 4U, ROW), -1);
    ck_assert_int_eq(hal_flash_erase(TEST_ROW, 0), -1);
    ck_assert_int_eq(hal_flash_erase(TEST_ROW, -ROW), -1);
    ck_assert_int_eq(hal_flash_erase(PSOC6_FLASH_BASE + PSOC6_FLASH_SIZE -
        ROW, ROW + 1), -1);
    ck_assert_int_eq(hal_flash_erase(PSOC6_FLASH_BANK2_BASE, ROW), -1);
    ck_assert_int_eq(n_ops, 0);
}
END_TEST

START_TEST(test_erase_failure_stops)
{
    fill(TEST_ROW, 1, 3U * ROW);
    fail_at_op = 1;
    ck_assert_int_eq(hal_flash_erase(TEST_ROW + ROW, 2U * ROW), -1);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(cache_clears, 1);
}
END_TEST

START_TEST(test_uart_divider)
{
    uint64_t div_x32 = ((uint64_t)(PSOC6_UART_DIV_INT + 1U) * 32U) +
        PSOC6_UART_DIV_FRAC;
    uint64_t baud = ((uint64_t)PSOC6_CLK_PERI_HZ * 32U) /
        (div_x32 * PSOC6_UART_OVERSAMPLE);
    uint64_t err = (baud > PSOC6_UART_BAUD) ? baud - PSOC6_UART_BAUD :
        PSOC6_UART_BAUD - baud;

    ck_assert_uint_eq(PSOC6_UART_DIV_INT, 53);
    ck_assert_uint_eq(PSOC6_UART_DIV_FRAC, 8);
    ck_assert_uint_lt(PSOC6_UART_DIV_FRAC, 32);
    /* Under 0.1 % baud error */
    ck_assert_uint_lt(err * 1000U, PSOC6_UART_BAUD);
}
END_TEST

START_TEST(test_uart_write_crlf)
{
    uart_write("a\nb\n", 4);
    ck_assert_int_eq(n_tx, 6);
    ck_assert_mem_eq(tx, "a\r\nb\r\n", 6);
}
END_TEST

Suite *psoc6_flash_suite(void)
{
    Suite *s = suite_create("psoc6-flash");
    TCase *tc = tcase_create("flash");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_range_ok);
    tcase_add_test(tc, test_write_full_row);
    tcase_add_test(tc, test_rewrite_programmed_row_erases);
    tcase_add_test(tc, test_write_unaligned_head_preserves_row);
    tcase_add_test(tc, test_write_partial_tail_preserves_row);
    tcase_add_test(tc, test_write_across_row_boundary);
    tcase_add_test(tc, test_write_unchanged_row_skipped);
    tcase_add_test(tc, test_write_rejects_bad_args);
    tcase_add_test(tc, test_write_failure_stops);
    tcase_add_test(tc, test_app_arch_reboot_resets);
    tcase_add_test(tc, test_write_work_flash);
    tcase_add_test(tc, test_erase_picks_largest_unit);
    tcase_add_test(tc, test_erase_unaligned_start_uses_rows);
    tcase_add_test(tc, test_erase_skips_blank);
    tcase_add_test(tc, test_erase_rounds_len_up);
    tcase_add_test(tc, test_erase_rejects_bad_args);
    tcase_add_test(tc, test_erase_failure_stops);
    tcase_add_test(tc, test_uart_divider);
    tcase_add_test(tc, test_uart_write_crlf);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = psoc6_flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
