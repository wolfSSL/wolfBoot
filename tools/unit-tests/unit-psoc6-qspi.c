/* unit-psoc6-qspi.c
 *
 * Unit tests for the PSoC 6 QSPI (SMIF) external flash driver in
 * hal/psoc6.c, against a model of the SMIF memory slot API, built against
 * the Infineon mtb-pdl-cat1 headers.
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

/* PSoC 6 QSPI layout (config/examples/cypsoc6-qspi.config) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x40000
#define WOLFBOOT_PARTITION_SIZE              0x180000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x10040000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x18000000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x101C0000

#include "cy_device_headers.h"
#include "cy_gpio.h"

/* The PDL's inline HSIOM accessor writes registers; route it to a mock */
void mock_set_hsiom(GPIO_PRT_Type *base, uint32_t pinNum, en_hsiom_sel_t value);
#define Cy_GPIO_SetHSIOM mock_set_hsiom

#include "../../hal/psoc6.c"

#define QSPI_BASE      CY_XIP_BASE
#define QSPI_SIZE      0x04000000U
#define QSPI_ERASE     0x40000U
#define QSPI_PAGE      0x100U
#define MOCK_MAX_OPS   16

enum { OP_READ, OP_WRITE, OP_ERASE };

uint32_t mock_primask;
int mock_barriers;
int mock_wfi;
NVIC_Type mock_nvic;
SCB_Type mock_scb;
SysTick_Type mock_systick;

static struct {
    int op;
    uint32_t offset;
    uint32_t len;
    const uint8_t *src;
    uint8_t *dst;
} ops[MOCK_MAX_OPS];
static int n_ops;
static cy_en_smif_status_t next_status;

void mock_wfi_hook(void)
{
}

void CY_ASSERT_HANDLER(void)
{
    ck_abort_msg("PDL assert");
}

void NVIC_SystemReset(void)
{
}

static cy_en_smif_status_t record(int op, uint32_t offset, uint32_t len)
{
    ck_assert_int_lt(n_ops, MOCK_MAX_OPS);
    ops[n_ops].op = op;
    ops[n_ops].offset = offset;
    ops[n_ops].len = len;
    n_ops++;
    return next_status;
}

cy_en_smif_status_t Cy_SMIF_MemWrite(SMIF_Type *base,
    cy_stc_smif_mem_config_t const *memConfig, uint32_t address,
    uint8_t const writeBuff[], uint32_t length,
    cy_stc_smif_context_t const *context)
{
    (void)base;
    (void)context;
    ck_assert_ptr_eq(memConfig, &mem_sfdp_0);
    ops[n_ops].src = writeBuff;
    return record(OP_WRITE, address, length);
}

cy_en_smif_status_t Cy_SMIF_MemRead(SMIF_Type *base,
    cy_stc_smif_mem_config_t const *memConfig, uint32_t address,
    uint8_t readBuff[], uint32_t length, cy_stc_smif_context_t const *context)
{
    (void)base;
    (void)context;
    ck_assert_ptr_eq(memConfig, &mem_sfdp_0);
    ops[n_ops].dst = readBuff;
    return record(OP_READ, address, length);
}

cy_en_smif_status_t Cy_SMIF_MemEraseSector(SMIF_Type *base,
    cy_stc_smif_mem_config_t const *memConfig, uint32_t address,
    uint32_t length, cy_stc_smif_context_t const *context)
{
    (void)base;
    (void)context;
    ck_assert_ptr_eq(memConfig, &mem_sfdp_0);
    return record(OP_ERASE, address, length);
}

/* Only the hal_ext_flash_init() test may reach these */
static int in_init;
#define INIT_ONLY() \
    do { if (!in_init) ck_abort_msg("unexpected PDL call in a host test"); \
    } while (0)
void mock_set_hsiom(GPIO_PRT_Type *base, uint32_t pinNum, en_hsiom_sel_t value)
{ (void)base; (void)pinNum; (void)value; INIT_ONLY(); }
cy_en_gpio_status_t Cy_GPIO_Pin_Init(GPIO_PRT_Type *base, uint32_t pinNum,
    const cy_stc_gpio_pin_config_t *config)
{ (void)base; (void)pinNum; (void)config; INIT_ONLY(); return CY_GPIO_SUCCESS; }
cy_en_sysclk_status_t Cy_SysClk_ClkHfSetSource(uint32_t clkHf,
    cy_en_clkhf_in_sources_t source)
{ (void)clkHf; (void)source; INIT_ONLY(); return CY_SYSCLK_SUCCESS; }
cy_en_sysclk_status_t Cy_SysClk_ClkHfSetDivider(uint32_t clkHf,
    cy_en_clkhf_dividers_t divider)
{ (void)clkHf; (void)divider; INIT_ONLY(); return CY_SYSCLK_SUCCESS; }
cy_en_sysclk_status_t Cy_SysClk_ClkHfEnable(uint32_t clkHf)
{ (void)clkHf; INIT_ONLY(); return CY_SYSCLK_SUCCESS; }
cy_en_smif_status_t Cy_SMIF_Init(SMIF_Type *base,
    cy_stc_smif_config_t const *config, uint32_t timeout,
    cy_stc_smif_context_t *context)
{ (void)base; (void)config; (void)timeout; (void)context; INIT_ONLY();
  return CY_SMIF_SUCCESS; }
cy_en_smif_status_t Cy_SMIF_MemInit(SMIF_Type *base,
    cy_stc_smif_block_config_t const * blockConfig,
    cy_stc_smif_context_t *context)
{ (void)base; (void)context; INIT_ONLY();
  ck_assert_ptr_eq(blockConfig, &smifBlockConfig_sfdp);
  return CY_SMIF_SUCCESS; }
void Cy_SysLib_Delay(uint32_t milliseconds)
{ (void)milliseconds; ck_abort_msg("SMIF init retried"); }
void Cy_SMIF_Enable(SMIF_Type *base, cy_stc_smif_context_t *context)
{ (void)base; (void)context; INIT_ONLY(); }
#define NOT_CALLED() ck_abort_msg("unexpected PDL call in a host test")
const cy_stc_device_t *cy_device;
cy_en_flashdrv_status_t Cy_Flash_WriteRow(uint32_t rowAddr,
    const uint32_t* data)
{ (void)rowAddr; (void)data; NOT_CALLED(); return 0; }
cy_en_flashdrv_status_t Cy_Flash_ProgramRow(uint32_t rowAddr,
    const uint32_t* data)
{ (void)rowAddr; (void)data; NOT_CALLED(); return 0; }
cy_en_flashdrv_status_t Cy_Flash_EraseRow(uint32_t rowAddr)
{ (void)rowAddr; NOT_CALLED(); return 0; }
cy_en_flashdrv_status_t Cy_Flash_EraseSubsector(uint32_t subSectorAddr)
{ (void)subSectorAddr; NOT_CALLED(); return 0; }
cy_en_flashdrv_status_t Cy_Flash_EraseSector(uint32_t sectorAddr)
{ (void)sectorAddr; NOT_CALLED(); return 0; }
void Cy_SysLib_ClearFlashCacheAndBuffer(void)
{ NOT_CALLED(); }

static void setup(void)
{
    memset(ops, 0, sizeof(ops));
    n_ops = 0;
    next_status = CY_SMIF_SUCCESS;
    dev_sfdp_0.eraseSize = QSPI_ERASE;
    dev_sfdp_0.programSize = QSPI_PAGE;
    dev_sfdp_0.memSize = QSPI_SIZE;
    smif_blk_config = &smifBlockConfig_sfdp;
    in_init = 0;
}

START_TEST(test_write_maps_offset)
{
    uint8_t data[7] = { 1, 2, 3, 4, 5, 6, 7 };

    ck_assert_int_eq(ext_flash_write(QSPI_BASE + 0x123U, data, 7), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(ops[0].op, OP_WRITE);
    ck_assert_uint_eq(ops[0].offset, 0x123U);
    ck_assert_uint_eq(ops[0].len, 7);
    ck_assert_ptr_eq(ops[0].src, data);
}
END_TEST

START_TEST(test_write_rejects_out_of_range)
{
    uint8_t data[16];

    memset(data, 0, sizeof(data));
    ck_assert_int_eq(ext_flash_write(QSPI_BASE - 1U, data, 1), -1);
    ck_assert_int_eq(ext_flash_write(QSPI_BASE + QSPI_SIZE - 8U, data, 16),
        -1);
    ck_assert_int_eq(ext_flash_write(QSPI_BASE + QSPI_SIZE, data, 1), -1);
    ck_assert_int_eq(ext_flash_write(QSPI_BASE, data, 0), -1);
    ck_assert_int_eq(ext_flash_write(QSPI_BASE, NULL, 4), -1);
    ck_assert_int_eq(n_ops, 0);
    ck_assert_int_eq(ext_flash_write(QSPI_BASE + QSPI_SIZE - 16U, data, 16),
        0);
}
END_TEST

START_TEST(test_write_error)
{
    uint8_t data[4] = { 0 };

    next_status = CY_SMIF_EXCEED_TIMEOUT;
    ck_assert_int_eq(ext_flash_write(QSPI_BASE, data, 4), -1);
}
END_TEST

START_TEST(test_read_returns_len)
{
    uint8_t buf[32];

    ck_assert_int_eq(ext_flash_read(QSPI_BASE + 0x40000U, buf, 32), 32);
    ck_assert_int_eq(ops[0].op, OP_READ);
    ck_assert_uint_eq(ops[0].offset, 0x40000U);
    ck_assert_ptr_eq(ops[0].dst, buf);
    next_status = CY_SMIF_BUSY;
    ck_assert_int_eq(ext_flash_read(QSPI_BASE, buf, 32), -1);
    ck_assert_int_eq(ext_flash_read(QSPI_BASE + QSPI_SIZE, buf, 1), -1);
    ck_assert_int_eq(ext_flash_read(QSPI_BASE, NULL, 1), -1);
}
END_TEST

START_TEST(test_erase_whole_sectors)
{
    ck_assert_int_eq(ext_flash_erase(QSPI_BASE + QSPI_ERASE,
        2 * QSPI_ERASE), 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(ops[0].op, OP_ERASE);
    ck_assert_uint_eq(ops[0].offset, QSPI_ERASE);
    ck_assert_uint_eq(ops[0].len, 2 * QSPI_ERASE);
}
END_TEST

START_TEST(test_erase_rejects_partial_sectors)
{
    /* The PDL would round these out and erase data around the request */
    ck_assert_int_eq(ext_flash_erase(QSPI_BASE + 0x1000U, QSPI_ERASE), -1);
    ck_assert_int_eq(ext_flash_erase(QSPI_BASE, 0x1000), -1);
    ck_assert_int_eq(ext_flash_erase(QSPI_BASE, 0), -1);
    ck_assert_int_eq(ext_flash_erase(QSPI_BASE + QSPI_SIZE, QSPI_ERASE), -1);
    ck_assert_int_eq(n_ops, 0);
}
END_TEST

/* hal_ext_flash_init() fails when the SFDP erase size does not fit the
 * wolfBoot sector, before any flash operation */
START_TEST(test_init_checks_geometry)
{
    in_init = 1;
    ck_assert_int_eq(hal_ext_flash_init(), CY_SMIF_SUCCESS);
    dev_sfdp_0.eraseSize = 0x80000U;
    ck_assert_int_eq(hal_ext_flash_init(), CY_SMIF_BAD_PARAM);
    dev_sfdp_0.eraseSize = 0x30000U;
    ck_assert_int_eq(hal_ext_flash_init(), CY_SMIF_BAD_PARAM);
    ck_assert_int_eq(n_ops, 0);
}
END_TEST

START_TEST(test_geometry)
{
    ck_assert_int_eq(psoc6_qspi_geometry_ok(0x40000U), 1);
    ck_assert_int_eq(psoc6_qspi_geometry_ok(0x1000U), 1);
    ck_assert_int_eq(psoc6_qspi_geometry_ok(0x80000U), 0);
    ck_assert_int_eq(psoc6_qspi_geometry_ok(0x30000U), 0);
    ck_assert_int_eq(psoc6_qspi_geometry_ok(0), 0);
}
END_TEST

Suite *psoc6_qspi_suite(void)
{
    Suite *s = suite_create("psoc6-qspi");
    TCase *tc = tcase_create("qspi");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_write_maps_offset);
    tcase_add_test(tc, test_write_rejects_out_of_range);
    tcase_add_test(tc, test_write_error);
    tcase_add_test(tc, test_read_returns_len);
    tcase_add_test(tc, test_erase_whole_sectors);
    tcase_add_test(tc, test_erase_rejects_partial_sectors);
    tcase_add_test(tc, test_init_checks_geometry);
    tcase_add_test(tc, test_geometry);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = psoc6_qspi_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
