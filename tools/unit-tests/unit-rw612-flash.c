/* unit-rw612-flash.c
 *
 * Unit tests for the RW612 FlexSPI NOR flash HAL (hal/rw612.c), run against
 * a mock boot ROM FlexSPI driver and a mock NOR array.
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
#include <sys/mman.h>

/* RW612 layout (config/examples/rw612*.config) with the 8KB logical sector
 * ML-DSA builds use over the 4KB NOR erase sector, in place of target.h */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x2000
#define WOLFBOOT_PARTITION_SIZE              0x100000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x08100000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x08200000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x08300000
#define WOLFBOOT_NSC_ADDRESS                 0x08060000
#define WOLFBOOT_NSC_SIZE                    0x2000
#define WOLFBOOT_KEYVAULT_ADDRESS            0x08400000
#define WOLFBOOT_KEYVAULT_SIZE               0x20000

#include "fsl_common.h"
#include "fsl_romapi_flexspi.h"

static FLEXSPI_Type mock_flexspi;
static CACHE64_CTRL_Type mock_cache64;
CACHE64_POLSEL_Type mock_cache64_polsel;
SOCCIU_Type mock_socctrl;
NVIC_Type mock_nvic;
uint32_t mock_primask;
int mock_barriers;
static int ahb_rx_clears;
static int cache_invalidations;
static uint32_t cache64_seen;

FLEXSPI_Type *mock_flexspi_regs(void)
{
    if ((mock_flexspi.AHBCR & FLEXSPI_AHBCR_CLRAHBRXBUF_MASK) != 0U)
        ahb_rx_clears++;
    return &mock_flexspi;
}

CACHE64_CTRL_Type *mock_cache64_regs(void)
{
    cache64_seen |= mock_cache64.CCR;
    if ((mock_cache64.CCR & CACHE64_CTRL_CCR_GO_MASK) != 0U) {
        cache_invalidations++;
        mock_cache64.CCR &= ~CACHE64_CTRL_CCR_GO_MASK;
    }
    return &mock_cache64;
}

/* The HAL reads 32-bit ROM table pointers, so the mock tables live below 4GB */
static uint8_t *mock_rom;
static uint8_t mock_fcb[sizeof(flexspi_nor_config_t)];

#define RW612_ROM_API_TREE_A0 ((uint32_t)(uintptr_t)mock_rom)
#define RW612_ROM_API_TREE_A1 ((uint32_t)(uintptr_t)(mock_rom + 0x40))
#define RW612_FCB_ADDRESS     ((uintptr_t)mock_fcb)

/* Point the SDK peripheral instances at the mocks above */
#undef FLEXSPI
#undef CACHE64_CTRL0
#undef CACHE64_POLSEL0
#undef SOCCTRL
#define FLEXSPI         (mock_flexspi_regs())
#define CACHE64_CTRL0   (mock_cache64_regs())
#define CACHE64_POLSEL0 (&mock_cache64_polsel)
#define SOCCTRL         (&mock_socctrl)

#include "../../hal/rw612.c"

#ifdef MAP_32BIT
#define MOCK_ROM_HINT   NULL
#define MOCK_ROM_FLAGS  (MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT)
#else
#define MOCK_ROM_HINT   ((void *)0x10000000UL)
#define MOCK_ROM_FLAGS  (MAP_PRIVATE | MAP_ANONYMOUS)
#endif

#define MOCK_NOR_SIZE   0x8000
#define MOCK_MAX_CALLS  16
#define TEST_BASE       0x08001000U

static uint8_t mock_nor[MOCK_NOR_SIZE];
static flexspi_nor_flash_driver_t *drv_a0;
static flexspi_nor_flash_driver_t *drv_a1;

static int init_calls;
static int program_calls;
static int erase_calls;
static status_t init_ret;
static int fail_program_at;
static int fail_erase_at;
static int init_cfg_ok;
static int rom_irq_enabled;
static uint32_t program_dst[MOCK_MAX_CALLS];
static uint32_t erase_addr[MOCK_MAX_CALLS];

static status_t mock_init(uint32_t instance, flexspi_nor_config_t *config)
{
    (void)instance;
    init_calls++;
    if (mock_primask == 0U)
        rom_irq_enabled = 1;
    init_cfg_ok = (memcmp(config, mock_fcb, sizeof(*config)) == 0);
    return init_ret;
}

static status_t mock_page_program(uint32_t instance,
        flexspi_nor_config_t *config, uint32_t dst, const uint32_t *src,
        bool keepState)
{
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;

    (void)instance; (void)config; (void)keepState;
    if (mock_primask == 0U)
        rom_irq_enabled = 1;
    if (program_calls < MOCK_MAX_CALLS)
        program_dst[program_calls] = dst;
    program_calls++;
    if (program_calls == fail_program_at)
        return kStatus_Fail;
    ck_assert_uint_eq(dst % RW612_FLASH_PAGE_SIZE, 0);
    ck_assert_uint_le(dst + RW612_FLASH_PAGE_SIZE, MOCK_NOR_SIZE);
    /* NOR programming can only clear bits */
    for (i = 0; i < RW612_FLASH_PAGE_SIZE; i++)
        mock_nor[dst + i] &= p[i];
    return kStatus_Success;
}

static status_t mock_erase_sector(uint32_t instance,
        flexspi_nor_config_t *config, uint32_t address)
{
    (void)instance; (void)config;
    if (mock_primask == 0U)
        rom_irq_enabled = 1;
    if (erase_calls < MOCK_MAX_CALLS)
        erase_addr[erase_calls] = address;
    erase_calls++;
    if (erase_calls == fail_erase_at)
        return kStatus_Fail;
    ck_assert_uint_eq(address % RW612_FLASH_SECTOR_SIZE, 0);
    ck_assert_uint_le(address + RW612_FLASH_SECTOR_SIZE, MOCK_NOR_SIZE);
    memset(mock_nor + address, 0xFF, RW612_FLASH_SECTOR_SIZE);
    return kStatus_Success;
}

static void setup(void)
{
    uint32_t i;

    mock_rom = mmap(MOCK_ROM_HINT, 4096, PROT_READ | PROT_WRITE,
            MOCK_ROM_FLAGS, -1, 0);
    ck_assert_ptr_ne(mock_rom, MAP_FAILED);
    ck_assert_uint_lt((uintptr_t)mock_rom, 0xFFFFF000UL);
    drv_a0 = (flexspi_nor_flash_driver_t *)(mock_rom + 0x100);
    drv_a1 = (flexspi_nor_flash_driver_t *)(mock_rom + 0x200);
    drv_a0->init = drv_a1->init = mock_init;
    drv_a0->page_program = drv_a1->page_program = mock_page_program;
    drv_a0->erase_sector = drv_a1->erase_sector = mock_erase_sector;
    ((uint32_t *)(uintptr_t)RW612_ROM_API_TREE_A0)[RW612_ROM_API_FLEXSPI_IDX] =
        (uint32_t)(uintptr_t)drv_a0;
    ((uint32_t *)(uintptr_t)RW612_ROM_API_TREE_A1)[RW612_ROM_API_FLEXSPI_IDX] =
        (uint32_t)(uintptr_t)drv_a1;

    for (i = 0; i < sizeof(mock_fcb); i++)
        mock_fcb[i] = (uint8_t)i;
    memset(mock_nor, 0xFF, sizeof(mock_nor));
    *(uint32_t *)&mock_socctrl.CHIP_INFO = 1U;
    mock_primask = 0U;
    mock_barriers = 0;
    memset(&mock_flexspi, 0, sizeof(mock_flexspi));
    memset(&mock_cache64, 0, sizeof(mock_cache64));
    ahb_rx_clears = cache_invalidations = 0;
    cache64_seen = 0;
    rom_flexspi = NULL;
    init_calls = program_calls = erase_calls = 0;
    init_ret = kStatus_Success;
    fail_program_at = fail_erase_at = 0;
    init_cfg_ok = 0;
    rom_irq_enabled = 0;
}

static void teardown(void)
{
    munmap(mock_rom, 4096);
}

START_TEST(test_write_unaligned_keeps_page_neighbours)
{
    uint8_t data[8];
    int i;

    memset(mock_nor + 0x1000, 0x5A, 16);
    for (i = 0; i < 8; i++)
        data[i] = (uint8_t)(0xA0 + i);

    ck_assert_int_eq(hal_flash_write(TEST_BASE + 0x20, data, 8), 0);
    ck_assert_int_eq(program_calls, 1);
    ck_assert_uint_eq(program_dst[0], 0x1000);
    for (i = 0; i < 16; i++)
        ck_assert_uint_eq(mock_nor[0x1000 + i], 0x5A);
    for (i = 0; i < 8; i++)
        ck_assert_uint_eq(mock_nor[0x1020 + i], data[i]);
    for (i = 0x1028; i < 0x1100; i++)
        ck_assert_uint_eq(mock_nor[i], 0xFF);
}
END_TEST

START_TEST(test_write_spanning_pages)
{
    uint8_t data[20];
    int i;

    for (i = 0; i < 20; i++)
        data[i] = (uint8_t)(i + 1);

    ck_assert_int_eq(hal_flash_write(TEST_BASE + 250, data, 20), 0);
    ck_assert_int_eq(program_calls, 2);
    ck_assert_uint_eq(program_dst[0], 0x1000);
    ck_assert_uint_eq(program_dst[1], 0x1100);
    for (i = 0; i < 20; i++)
        ck_assert_uint_eq(mock_nor[0x1000 + 250 + i], data[i]);
    ck_assert_uint_eq(mock_nor[0x1000 + 249], 0xFF);
    ck_assert_uint_eq(mock_nor[0x1000 + 270], 0xFF);
}
END_TEST

START_TEST(test_write_secure_alias)
{
    uint8_t data[4] = { 0x11, 0x22, 0x33, 0x44 };

    ck_assert_int_eq(hal_flash_write(0x18001010U, data, 4), 0);
    ck_assert_int_eq(program_calls, 1);
    ck_assert_uint_eq(program_dst[0], 0x1000);
    ck_assert_mem_eq(mock_nor + 0x1010, data, 4);
}
END_TEST

START_TEST(test_write_invalid_len)
{
    uint8_t data[1] = { 0 };

    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, -1), -1);
    ck_assert_int_eq(init_calls, 0);
    ck_assert_int_eq(program_calls, 0);
}
END_TEST

START_TEST(test_write_stops_on_failure)
{
    uint8_t data[600];

    memset(data, 0, sizeof(data));
    fail_program_at = 1;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, (int)sizeof(data)), -1);
    ck_assert_int_eq(program_calls, 1);
    ck_assert_uint_eq(mock_primask, 0);
}
END_TEST

START_TEST(test_init_once_with_fcb_copy)
{
    uint8_t data[4] = { 0 };

    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, 4), 0);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, WOLFBOOT_SECTOR_SIZE), 0);
    ck_assert_int_eq(init_calls, 1);
    ck_assert_int_eq(init_cfg_ok, 1);
    ck_assert_ptr_eq(rom_flexspi, drv_a1);
}
END_TEST

START_TEST(test_init_selects_a0_tree)
{
    *(uint32_t *)&mock_socctrl.CHIP_INFO = 0U;
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, WOLFBOOT_SECTOR_SIZE), 0);
    ck_assert_ptr_eq(rom_flexspi, drv_a0);
}
END_TEST

START_TEST(test_init_failure_retries)
{
    uint8_t data[4] = { 0 };

    init_ret = kStatus_Fail;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, 4), -1);
    ck_assert_int_eq(program_calls, 0);
    ck_assert_ptr_null(rom_flexspi);
    ck_assert_uint_eq(mock_primask, 0);

    init_ret = kStatus_Success;
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, 4), 0);
    ck_assert_int_eq(init_calls, 2);
    ck_assert_int_eq(program_calls, 1);
}
END_TEST

START_TEST(test_erase_rounds_to_sectors)
{
    memset(mock_nor + 0x1000, 0x00, 3 * RW612_FLASH_SECTOR_SIZE);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE + RW612_FLASH_SECTOR_SIZE / 2,
            RW612_FLASH_SECTOR_SIZE), 0);
    ck_assert_int_eq(erase_calls, 2);
    ck_assert_uint_eq(erase_addr[0], 0x1000);
    ck_assert_uint_eq(erase_addr[1], 0x1000 + RW612_FLASH_SECTOR_SIZE);
    ck_assert_uint_eq(mock_nor[0x1000 + 2 * RW612_FLASH_SECTOR_SIZE], 0x00);
}
END_TEST

START_TEST(test_erase_covers_whole_logical_sector)
{
    int i;

    memset(mock_nor + 0x1000, 0x00, WOLFBOOT_SECTOR_SIZE + 1);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, WOLFBOOT_SECTOR_SIZE), 0);
    ck_assert_int_eq(erase_calls,
            (int)(WOLFBOOT_SECTOR_SIZE / RW612_FLASH_SECTOR_SIZE));
    for (i = 0; i < WOLFBOOT_SECTOR_SIZE; i++)
        ck_assert_uint_eq(mock_nor[0x1000 + i], 0xFF);
    ck_assert_uint_eq(mock_nor[0x1000 + WOLFBOOT_SECTOR_SIZE], 0x00);
}
END_TEST

START_TEST(test_erase_invalid_len)
{
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, 0), -1);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, -1), -1);
    ck_assert_int_eq(init_calls, 0);
    ck_assert_int_eq(erase_calls, 0);
}
END_TEST

START_TEST(test_erase_stops_on_failure)
{
    fail_erase_at = 1;
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, 2 * WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_calls, 1);
    ck_assert_uint_eq(mock_primask, 0);
}
END_TEST

START_TEST(test_primask_preserved)
{
    uint8_t data[300];

    memset(data, 0, sizeof(data));
    ck_assert_int_eq(hal_flash_write(TEST_BASE, data, (int)sizeof(data)), 0);
    ck_assert_uint_eq(mock_primask, 0);
    ck_assert_int_eq(rom_irq_enabled, 0);

    mock_primask = 1U;
    ck_assert_int_eq(hal_flash_write(TEST_BASE + 0x400, data,
            (int)sizeof(data)), 0);
    ck_assert_uint_eq(mock_primask, 1);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, WOLFBOOT_SECTOR_SIZE), 0);
    ck_assert_uint_eq(mock_primask, 1);
}
END_TEST

START_TEST(test_cache_flushed_after_each_rom_call)
{
    uint8_t data[20] = { 0 };
    const uint32_t inv = CACHE64_CTRL_CCR_INVW0_MASK |
        CACHE64_CTRL_CCR_INVW1_MASK;
    int rom_ops;

    ck_assert_int_eq(hal_flash_write(TEST_BASE + 250, data, 20), 0);
    ck_assert_int_eq(hal_flash_erase(TEST_BASE, WOLFBOOT_SECTOR_SIZE), 0);
    rom_ops = init_calls + program_calls + erase_calls;
    ck_assert_int_eq(rom_ops,
            3 + (int)(WOLFBOOT_SECTOR_SIZE / RW612_FLASH_SECTOR_SIZE));
    ck_assert_int_eq(ahb_rx_clears, rom_ops);
    ck_assert_int_eq(cache_invalidations, rom_ops);
    ck_assert_int_eq(mock_barriers, rom_ops);
    ck_assert_uint_eq(cache64_seen & inv, inv);
    ck_assert_uint_eq(mock_cache64.CCR & inv, 0);
    ck_assert_uint_eq(mock_flexspi.AHBCR & FLEXSPI_AHBCR_CLRAHBRXBUF_MASK, 0);
}
END_TEST

static Suite *rw612_flash_suite(void)
{
    Suite *s = suite_create("rw612-flash");
    TCase *tc = tcase_create("rw612-flash");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_unaligned_keeps_page_neighbours);
    tcase_add_test(tc, test_write_spanning_pages);
    tcase_add_test(tc, test_write_secure_alias);
    tcase_add_test(tc, test_write_invalid_len);
    tcase_add_test(tc, test_write_stops_on_failure);
    tcase_add_test(tc, test_init_once_with_fcb_copy);
    tcase_add_test(tc, test_init_selects_a0_tree);
    tcase_add_test(tc, test_init_failure_retries);
    tcase_add_test(tc, test_erase_rounds_to_sectors);
    tcase_add_test(tc, test_erase_covers_whole_logical_sector);
    tcase_add_test(tc, test_erase_invalid_len);
    tcase_add_test(tc, test_erase_stops_on_failure);
    tcase_add_test(tc, test_primask_preserved);
    tcase_add_test(tc, test_cache_flushed_after_each_rom_call);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = rw612_flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
