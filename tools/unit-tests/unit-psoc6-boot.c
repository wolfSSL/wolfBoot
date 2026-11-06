/* unit-psoc6-boot.c
 *
 * Unit tests for the PSoC 6 bootloader paths in hal/psoc6.c: dual-bank
 * swap against a model of the bank mapping, the bootloader fork, the
 * debug console, the microsecond timer, the SRAM vector table and the CM4
 * handoff. Built against the Infineon mtb-pdl-cat1 headers.
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
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

/* PSoC 6 dual-bank layout (cypsoc6.config with DUALBANK_SWAP=1) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x1000
#define WOLFBOOT_PARTITION_SIZE              0xE0000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x10020000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x12020000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0xFFFFFFFF

const void *mock_flash_ptr(uint32_t address);
#define PSOC6_FLASH_PTR(a) mock_flash_ptr(a)

#include "cy_device_headers.h"
#include "cy_device.h"
#include "cy_tcpwm.h"
#include "cy_scb_uart.h"
#include "hal/psoc6.h"

/* The PDL's inline SCB accessors touch registers; route them to mocks */
void mock_uart_enable(CySCB_Type *base);
bool mock_uart_tx_complete(CySCB_Type const *base);
#define Cy_SCB_UART_Enable mock_uart_enable
#define Cy_SCB_UART_IsTxComplete mock_uart_tx_complete

/* Point the register blocks the HAL touches at mocks */
static TCPWM_V1_Type mock_tcpwm;
static uint32_t mock_flash_cmd;
#undef PSOC6_TIMER_TCPWM
#define PSOC6_TIMER_TCPWM ((TCPWM_Type *)&mock_tcpwm)
#undef FLASHC_FLASH_CMD
#define FLASHC_FLASH_CMD mock_flash_cmd

#include "../../hal/psoc6.c"

#define ROW            PSOC6_FLASH_ROW_SIZE
#define BANK           PSOC6_FLASH_BANK_SIZE
#define BL_SIZE        BOOTLOADER_PARTITION_SIZE
#define MOCK_MAX_OPS   1024
#define MOCK_MAX_CALLS 8

enum { CLK_ASSIGN, CLK_SET, CLK_SET_FRAC, CLK_ENABLE };

struct clk_call {
    int op;
    uint32_t type;
    uint32_t num;
    uint32_t a;
    uint32_t b;
};

struct gpio_call {
    GPIO_PRT_Type *base;
    uint32_t pin;
    uint32_t mode;
    uint32_t out;
    en_hsiom_sel_t hsiom;
};

uint32_t mock_primask;
int mock_barriers;
int mock_wfi;
NVIC_Type mock_nvic;
SCB_Type mock_scb;
SysTick_Type mock_systick;
cy_israddress __ramVectors[PSOC6_CM0P_VECTORS];
const cy_stc_device_t *cy_device;
const cy_stc_device_t cy_deviceIpBlockCfgPSoC6_02;

static uint8_t mock_main[PSOC6_FLASH_SIZE];
static uint8_t mock_wflash[PSOC6_WFLASH_SIZE];
static int mock_dual;
static cy_en_maptype_t mock_map;
static int n_flash_ops;
static int n_wflash_writes;
static int set_mapping_calls;
static uint32_t cm4_vector;
static int cm4_started;
static int fail_writes;
static jmp_buf park;
static struct clk_call clk_calls[MOCK_MAX_CALLS];
static int n_clk_calls;
static struct gpio_call gpio_calls[MOCK_MAX_CALLS];
static int n_gpio_calls;
static cy_stc_scb_uart_config_t uart_cfg;
static cy_en_scb_uart_status_t uart_init_ret;
static int uart_init_calls;
static int uart_enable_calls;
static int tx_busy_polls;
static int tx_polls;

void NVIC_SystemReset(void)
{
}

void CY_ASSERT_HANDLER(void)
{
    ck_abort_msg("PDL assert");
}

/* The parked CM0+ loops on __WFI(); leave the loop once the CM4 started */
void mock_wfi_hook(void)
{
    if (cm4_started)
        longjmp(park, 1);
}

static uint8_t *mock_phys(uint32_t address)
{
    uint32_t off;

    if ((address >= PSOC6_WFLASH_BASE) &&
        (address < PSOC6_WFLASH_BASE + PSOC6_WFLASH_SIZE)) {
        return &mock_wflash[address - PSOC6_WFLASH_BASE];
    }
    if (!mock_dual && (address >= PSOC6_FLASH_BASE) &&
        (address < PSOC6_FLASH_BASE + PSOC6_FLASH_SIZE)) {
        return &mock_main[address - PSOC6_FLASH_BASE];
    }
    /* Dual-bank mode: mapping B swaps which bank is seen at each base */
    if (mock_dual && (address >= PSOC6_FLASH_BASE) &&
        (address < PSOC6_FLASH_BASE + BANK)) {
        off = address - PSOC6_FLASH_BASE;
        return &mock_main[off + ((mock_map == CY_FLASH_MAPPING_B) ? BANK : 0)];
    }
    if (mock_dual && (address >= PSOC6_FLASH_BANK2_BASE) &&
        (address < PSOC6_FLASH_BANK2_BASE + BANK)) {
        off = address - PSOC6_FLASH_BANK2_BASE;
        return &mock_main[off + ((mock_map == CY_FLASH_MAPPING_B) ? 0 : BANK)];
    }
    ck_abort_msg("flash access outside the model: 0x%08x", address);
    return NULL;
}

const void *mock_flash_ptr(uint32_t address)
{
    return mock_phys(address);
}

static void count_op(uint32_t addr)
{
    n_flash_ops++;
    if ((addr >= PSOC6_WFLASH_BASE) &&
        (addr < PSOC6_WFLASH_BASE + PSOC6_WFLASH_SIZE))
        n_wflash_writes++;
}

cy_en_flashdrv_status_t Cy_Flash_WriteRow(uint32_t rowAddr,
    const uint32_t* data)
{
    if (fail_writes) {
        return CY_FLASH_DRV_ERR_UNC;
    }
    count_op(rowAddr);
    memcpy(mock_phys(rowAddr), data, ROW);
    return CY_FLASH_DRV_SUCCESS;
}

cy_en_flashdrv_status_t Cy_Flash_ProgramRow(uint32_t rowAddr,
    const uint32_t* data)
{
    if (fail_writes) {
        return CY_FLASH_DRV_ERR_UNC;
    }
    count_op(rowAddr);
    memcpy(mock_phys(rowAddr), data, ROW);
    return CY_FLASH_DRV_SUCCESS;
}

cy_en_flashdrv_status_t Cy_Flash_EraseRow(uint32_t rowAddr)
{
    count_op(rowAddr);
    memset(mock_phys(rowAddr), 0, ROW);
    return CY_FLASH_DRV_SUCCESS;
}

cy_en_flashdrv_status_t Cy_Flash_EraseSubsector(uint32_t subSectorAddr)
{
    count_op(subSectorAddr);
    memset(mock_phys(subSectorAddr), 0, ROW * 8U);
    return CY_FLASH_DRV_SUCCESS;
}

cy_en_flashdrv_status_t Cy_Flash_EraseSector(uint32_t sectorAddr)
{
    count_op(sectorAddr);
    memset(mock_phys(sectorAddr), 0, ROW * 512U);
    return CY_FLASH_DRV_SUCCESS;
}

void Cy_SysLib_ClearFlashCacheAndBuffer(void)
{
}

void Cy_Flashc_SetMainBankMode(cy_en_bankmode_t mode)
{
    mock_dual = (mode == CY_FLASH_DUAL_BANK_MODE);
}

void Cy_Flashc_SetMain_Flash_Mapping(cy_en_maptype_t mapping)
{
    ck_assert_int_eq(mock_dual, 1);
    mock_map = mapping;
    set_mapping_calls++;
}

void Cy_SysEnableCM4(uint32_t vectorTableOffset)
{
    cm4_vector = vectorTableOffset;
    cm4_started = 1;
}

static void record_clk(int op, uint32_t type, uint32_t num, uint32_t a,
    uint32_t b)
{
    ck_assert_int_lt(n_clk_calls, MOCK_MAX_CALLS);
    clk_calls[n_clk_calls].op = op;
    clk_calls[n_clk_calls].type = type;
    clk_calls[n_clk_calls].num = num;
    clk_calls[n_clk_calls].a = a;
    clk_calls[n_clk_calls].b = b;
    n_clk_calls++;
}

cy_en_sysclk_status_t Cy_SysClk_PeriphAssignDivider(en_clk_dst_t ipBlock,
    cy_en_divider_types_t dividerType, uint32_t dividerNum)
{
    record_clk(CLK_ASSIGN, dividerType, dividerNum, ipBlock, 0);
    return CY_SYSCLK_SUCCESS;
}

cy_en_sysclk_status_t Cy_SysClk_PeriphSetDivider(
    cy_en_divider_types_t dividerType, uint32_t dividerNum,
    uint32_t dividerValue)
{
    record_clk(CLK_SET, dividerType, dividerNum, dividerValue, 0);
    return CY_SYSCLK_SUCCESS;
}

cy_en_sysclk_status_t Cy_SysClk_PeriphSetFracDivider(
    cy_en_divider_types_t dividerType, uint32_t dividerNum,
    uint32_t dividerIntValue, uint32_t dividerFracValue)
{
    record_clk(CLK_SET_FRAC, dividerType, dividerNum, dividerIntValue,
        dividerFracValue);
    return CY_SYSCLK_SUCCESS;
}

cy_en_sysclk_status_t Cy_SysClk_PeriphEnableDivider(
    cy_en_divider_types_t dividerType, uint32_t dividerNum)
{
    record_clk(CLK_ENABLE, dividerType, dividerNum, 0, 0);
    return CY_SYSCLK_SUCCESS;
}

void Cy_GPIO_Pin_FastInit(GPIO_PRT_Type* base, uint32_t pinNum,
    uint32_t driveMode, uint32_t outVal, en_hsiom_sel_t hsiom)
{
    ck_assert_int_lt(n_gpio_calls, MOCK_MAX_CALLS);
    gpio_calls[n_gpio_calls].base = base;
    gpio_calls[n_gpio_calls].pin = pinNum;
    gpio_calls[n_gpio_calls].mode = driveMode;
    gpio_calls[n_gpio_calls].out = outVal;
    gpio_calls[n_gpio_calls].hsiom = hsiom;
    n_gpio_calls++;
}

cy_en_scb_uart_status_t Cy_SCB_UART_Init(CySCB_Type *base,
    cy_stc_scb_uart_config_t const *config,
    cy_stc_scb_uart_context_t *context)
{
    ck_assert_ptr_eq(base, SCB0);
    ck_assert_ptr_null(context);
    uart_cfg = *config;
    uart_init_calls++;
    return uart_init_ret;
}

void mock_uart_enable(CySCB_Type *base)
{
    ck_assert_ptr_eq(base, SCB0);
    ck_assert_int_eq(uart_init_calls, 1);
    uart_enable_calls++;
}

bool mock_uart_tx_complete(CySCB_Type const *base)
{
    ck_assert_ptr_eq(base, SCB0);
    tx_polls++;
    return tx_polls > tx_busy_polls;
}

void uart_printf(const char* fmt, ...)
{
    (void)fmt;
}

cy_en_tcpwm_status_t Cy_TCPWM_Counter_Init(TCPWM_Type *base, uint32_t cntNum,
    cy_stc_tcpwm_counter_config_t const *config)
{
    ck_assert_ptr_eq(base, PSOC6_TIMER_TCPWM);
    ck_assert_uint_eq(cntNum, PSOC6_TIMER_CNT);
    ck_assert_uint_eq(config->period, 0xFFFFFFFFUL);
    ck_assert_uint_eq(config->countDirection, CY_TCPWM_COUNTER_COUNT_UP);
    return CY_TCPWM_SUCCESS;
}

/* hal_init() and uart_write() link these; the tests do not run them */
#define NOT_CALLED() ck_abort_msg("unexpected PDL call in a host test")
uint32_t Cy_SCB_Write(CySCB_Type *base, uint32_t data)
{ (void)base; (void)data; NOT_CALLED(); return 0; }
void Cy_PDL_Init(const cy_stc_device_t * device)
{ (void)device; NOT_CALLED(); }
void Cy_Flash_Init(void)
{ NOT_CALLED(); }
void Cy_SysClk_ClkFastSetDivider(uint8_t divider)
{ (void)divider; NOT_CALLED(); }
void Cy_SysClk_ClkPeriSetDivider(uint8_t divider)
{ (void)divider; NOT_CALLED(); }
void Cy_SysClk_ClkSlowSetDivider(uint8_t divider)
{ (void)divider; NOT_CALLED(); }
void Cy_SysLib_SetWaitStates(bool ulpMode, uint32_t clkHfMHz)
{ (void)ulpMode; (void)clkHfMHz; NOT_CALLED(); }
cy_en_sysclk_status_t Cy_SysClk_ClkHfSetSource(uint32_t clkHf,
    cy_en_clkhf_in_sources_t source)
{ (void)clkHf; (void)source; NOT_CALLED(); return 0; }
cy_en_sysclk_status_t Cy_SysClk_ClkHfSetDivider(uint32_t clkHf,
    cy_en_clkhf_dividers_t divider)
{ (void)clkHf; (void)divider; NOT_CALLED(); return 0; }
cy_en_sysclk_status_t Cy_SysClk_ClkPathSetSource(uint32_t clkPath,
    cy_en_clkpath_in_sources_t source)
{ (void)clkPath; (void)source; NOT_CALLED(); return 0; }
cy_en_sysclk_status_t Cy_SysClk_PllManualConfigure(uint32_t clkPath,
    const cy_stc_pll_manual_config_t *config)
{ (void)clkPath; (void)config; NOT_CALLED(); return 0; }
cy_en_sysclk_status_t Cy_SysClk_PllEnable(uint32_t clkPath,
    uint32_t timeoutus)
{ (void)clkPath; (void)timeoutus; NOT_CALLED(); return 0; }

static void put_bootloader(uint32_t phys, uint8_t seed)
{
    uint32_t i;

    for (i = 0; i < BL_SIZE; i++)
        mock_main[phys + i] = (uint8_t)(seed + (i >> 2));
}

static void put_bank_record(uint32_t slot, uint32_t mapping, uint32_t seq)
{
    active_bank rec;

    rec.magic = BANKMODE_MAGIC;
    rec.mapping = mapping;
    rec.seq = seq;
    rec.check = ~(rec.magic ^ rec.mapping ^ rec.seq);
    memcpy(&mock_wflash[slot * ROW], &rec, sizeof(rec));
}

static active_bank get_bank_record(uint32_t slot)
{
    active_bank rec;

    memcpy(&rec, &mock_wflash[slot * ROW], sizeof(rec));
    return rec;
}

static void setup(void)
{
    memset(mock_main, 0, sizeof(mock_main));
    memset(mock_wflash, 0, sizeof(mock_wflash));
    memset(&mock_tcpwm, 0, sizeof(mock_tcpwm));
    memset(&mock_scb, 0, sizeof(mock_scb));
    memset(__ramVectors, 0, sizeof(__ramVectors));
    mock_dual = 0;
    mock_map = CY_FLASH_MAPPING_A;
    n_flash_ops = 0;
    n_wflash_writes = 0;
    set_mapping_calls = 0;
    cm4_vector = 0;
    cm4_started = 0;
    mock_wfi = 0;
    mock_flash_cmd = 0;
    fail_writes = 0;
    memset(clk_calls, 0, sizeof(clk_calls));
    n_clk_calls = 0;
    memset(gpio_calls, 0, sizeof(gpio_calls));
    n_gpio_calls = 0;
    memset(&uart_cfg, 0, sizeof(uart_cfg));
    uart_init_ret = CY_SCB_UART_SUCCESS;
    uart_init_calls = 0;
    uart_enable_calls = 0;
    tx_busy_polls = 0;
    tx_polls = 0;
}

static void check_clk(int i, int op, uint32_t type, uint32_t num, uint32_t a,
    uint32_t b)
{
    ck_assert_int_lt(i, n_clk_calls);
    ck_assert_int_eq(clk_calls[i].op, op);
    ck_assert_uint_eq(clk_calls[i].type, type);
    ck_assert_uint_eq(clk_calls[i].num, num);
    ck_assert_uint_eq(clk_calls[i].a, a);
    ck_assert_uint_eq(clk_calls[i].b, b);
}

static void check_gpio(int i, GPIO_PRT_Type *base, uint32_t pin,
    uint32_t mode, en_hsiom_sel_t hsiom)
{
    ck_assert_int_lt(i, n_gpio_calls);
    ck_assert_ptr_eq(gpio_calls[i].base, base);
    ck_assert_uint_eq(gpio_calls[i].pin, pin);
    ck_assert_uint_eq(gpio_calls[i].mode, mode);
    ck_assert_uint_eq(gpio_calls[i].out, 1);
    ck_assert_int_eq(gpio_calls[i].hsiom, hsiom);
}

START_TEST(test_first_boot_forks_and_records_bank_a)
{
    put_bootloader(0, 0x10);

    ck_assert_int_eq(bankmode_init(), 0);
    ck_assert_mem_eq(&mock_main[BANK], &mock_main[0], BL_SIZE);
    ck_assert_int_eq(mock_dual, 1);
    ck_assert_int_eq(mock_map, CY_FLASH_MAPPING_A);
    ck_assert_uint_eq(get_bank_record(0).magic, BANKMODE_MAGIC);
    ck_assert_uint_eq(get_bank_record(0).mapping, CY_FLASH_MAPPING_A);
    ck_assert_uint_eq(get_bank_record(0).seq, 1);
}
END_TEST

START_TEST(test_boot_restores_bank_b_without_rewrite)
{
    put_bootloader(0, 0x10);
    put_bootloader(BANK, 0x10);
    put_bank_record(0, CY_FLASH_MAPPING_A, 7);
    put_bank_record(1, CY_FLASH_MAPPING_B, 8);

    ck_assert_int_eq(bankmode_init(), 0);
    ck_assert_int_eq(mock_map, CY_FLASH_MAPPING_B);
    ck_assert_int_eq(n_flash_ops, 0);
}
END_TEST

START_TEST(test_fork_refreshes_stale_copy)
{
    put_bootloader(0, 0x10);
    put_bootloader(BANK, 0x55);
    fork_bootloader();
    ck_assert_mem_eq(&mock_main[BANK], &mock_main[0], BL_SIZE);
    /* Already identical: no flash operations */
    n_flash_ops = 0;
    fork_bootloader();
    ck_assert_int_eq(n_flash_ops, 0);
}
END_TEST

START_TEST(test_swap_toggles_and_persists)
{
    put_bootloader(0, 0x10);
    put_bootloader(BANK, 0x10);
    put_bank_record(0, CY_FLASH_MAPPING_A, 1);
    ck_assert_int_eq(bankmode_init(), 0);

    hal_flash_dualbank_swap();
    ck_assert_int_eq(mock_map, CY_FLASH_MAPPING_B);
    /* The new record goes to the other slot; the old one stays intact */
    ck_assert_uint_eq(get_bank_record(1).mapping, CY_FLASH_MAPPING_B);
    ck_assert_uint_eq(get_bank_record(1).seq, 2);
    ck_assert_uint_eq(get_bank_record(0).mapping, CY_FLASH_MAPPING_A);

    hal_flash_dualbank_swap();
    ck_assert_int_eq(mock_map, CY_FLASH_MAPPING_A);
    ck_assert_uint_eq(get_bank_record(0).mapping, CY_FLASH_MAPPING_A);
    ck_assert_uint_eq(get_bank_record(0).seq, 3);
}
END_TEST

START_TEST(test_swap_without_record_selects_bank_a)
{
    mock_dual = 1;
    hal_flash_dualbank_swap();
    ck_assert_int_eq(mock_map, CY_FLASH_MAPPING_A);
    ck_assert_uint_eq(get_bank_record(0).magic, BANKMODE_MAGIC);
    ck_assert_uint_eq(get_bank_record(0).mapping, CY_FLASH_MAPPING_A);
}
END_TEST

START_TEST(test_torn_record_keeps_previous_mapping)
{
    active_bank rec;

    put_bank_record(0, CY_FLASH_MAPPING_B, 5);
    mock_dual = 1;
    /* Power lost after the spare slot was erased, before it was written */
    fail_writes = 1;
    hal_flash_dualbank_swap();
    ck_assert_int_eq(psoc6_bank_record_read(&rec), 0);
    ck_assert_uint_eq(rec.mapping, CY_FLASH_MAPPING_B);
    /* A record with a bad check word is ignored too */
    put_bank_record(1, CY_FLASH_MAPPING_A, 6);
    mock_wflash[ROW + 8U] ^= 1U;
    ck_assert_int_eq(psoc6_bank_record_read(&rec), 0);
    ck_assert_uint_eq(rec.mapping, CY_FLASH_MAPPING_B);
}
END_TEST

START_TEST(test_record_sequence_wraps)
{
    active_bank rec;

    put_bank_record(0, CY_FLASH_MAPPING_A, 0xFFFFFFFFU);
    put_bank_record(1, CY_FLASH_MAPPING_B, 0U);
    ck_assert_int_eq(psoc6_bank_record_read(&rec), 1);
    ck_assert_uint_eq(rec.mapping, CY_FLASH_MAPPING_B);
}
END_TEST

START_TEST(test_update_alias_writes_other_bank)
{
    uint8_t data[ROW];

    memset(data, 0xC3, sizeof(data));
    mock_dual = 1;
    mock_map = CY_FLASH_MAPPING_A;
    ck_assert_int_eq(hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS, data,
        ROW), 0);
    ck_assert_mem_eq(&mock_main[BANK + 0x20000U], data, ROW);

    mock_map = CY_FLASH_MAPPING_B;
    ck_assert_int_eq(hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS, data,
        ROW), 0);
    ck_assert_mem_eq(&mock_main[0x20000U], data, ROW);
    /* The alias ends with the bank */
    ck_assert_int_eq(hal_flash_write(PSOC6_FLASH_BANK2_BASE + BANK - 4U, data,
        8), -1);
}
END_TEST

START_TEST(test_uart_init_routes_pins_and_divider)
{
    uart_init();

    ck_assert_int_eq(n_gpio_calls, 2);
    check_gpio(0, GPIO_PRT0, 3, CY_GPIO_DM_STRONG_IN_OFF, P0_3_SCB0_UART_TX);
    check_gpio(1, GPIO_PRT0, 2, CY_GPIO_DM_HIGHZ, P0_2_SCB0_UART_RX);
    /* 100 MHz / (16 * 115200) = 54.25: 16.5-bit divider 53 + 8/32 */
    ck_assert_int_eq(n_clk_calls, 3);
    check_clk(0, CLK_ASSIGN, CY_SYSCLK_DIV_16_5_BIT, 0, PCLK_SCB0_CLOCK, 0);
    check_clk(1, CLK_SET_FRAC, CY_SYSCLK_DIV_16_5_BIT, 0, 53, 8);
    check_clk(2, CLK_ENABLE, CY_SYSCLK_DIV_16_5_BIT, 0, 0, 0);
    ck_assert_int_eq(uart_init_calls, 1);
    ck_assert_uint_eq(uart_cfg.uartMode, CY_SCB_UART_STANDARD);
    ck_assert_uint_eq(uart_cfg.oversample, 16);
    ck_assert_uint_eq(uart_cfg.dataWidth, 8);
    ck_assert_uint_eq(uart_cfg.parity, CY_SCB_UART_PARITY_NONE);
    ck_assert_uint_eq(uart_cfg.stopBits, CY_SCB_UART_STOP_BITS_1);
    ck_assert_int_eq(uart_enable_calls, 1);
}
END_TEST

START_TEST(test_uart_init_failure_leaves_scb_disabled)
{
    uart_init_ret = CY_SCB_UART_BAD_PARAM;
    uart_init();
    ck_assert_int_eq(uart_init_calls, 1);
    ck_assert_int_eq(uart_enable_calls, 0);
}
END_TEST

START_TEST(test_prepare_boot_drains_uart)
{
    tx_busy_polls = 3;
    hal_prepare_boot();
    ck_assert_int_eq(tx_polls, 4);
}
END_TEST

START_TEST(test_timer_counts_and_extends_wrap)
{
    mock_tcpwm.CNT[PSOC6_TIMER_CNT].COUNTER = 1000U;
    ck_assert_uint_eq(hal_get_timer_us(), 1000U);
    /* 100 MHz / (99 + 1) = 1 MHz on the TCPWM0 counter 0 clock */
    ck_assert_int_eq(n_clk_calls, 3);
    check_clk(0, CLK_ASSIGN, CY_SYSCLK_DIV_16_BIT, 0, PCLK_TCPWM0_CLOCKS0, 0);
    check_clk(1, CLK_SET, CY_SYSCLK_DIV_16_BIT, 0, 99, 0);
    check_clk(2, CLK_ENABLE, CY_SYSCLK_DIV_16_BIT, 0, 0, 0);
    mock_tcpwm.CNT[PSOC6_TIMER_CNT].COUNTER = 0xFFFFFF00U;
    ck_assert_uint_eq(hal_get_timer_us(), 0xFFFFFF00U);
    mock_tcpwm.CNT[PSOC6_TIMER_CNT].COUNTER = 0x10U;
    ck_assert_uint_eq(hal_get_timer_us(), 0x100000010ULL);
    mock_tcpwm.CNT[PSOC6_TIMER_CNT].COUNTER = 0x20U;
    ck_assert_uint_eq(hal_get_timer_us(), 0x100000020ULL);
}
END_TEST

START_TEST(test_ram_vectors_keep_rom_nmi)
{
    /* Table entries are cy_israddress wide: 4 bytes on the target */
    cy_israddress iv[PSOC6_CM0P_SYS_VECTORS];
    uint32_t i;

    for (i = 0; i < PSOC6_CM0P_SYS_VECTORS; i++)
        iv[i] = (cy_israddress)(uintptr_t)(0x10000100U + i * 4U);
    iv[2] = (cy_israddress)(uintptr_t)0x0000000DU;
    memcpy(mock_main, iv, sizeof(iv));

    psoc6_ram_vectors();
    for (i = 0; i < PSOC6_CM0P_SYS_VECTORS; i++)
        ck_assert_ptr_eq(__ramVectors[i], iv[i]);
    for (; i < PSOC6_CM0P_VECTORS; i++)
        ck_assert_ptr_eq(__ramVectors[i], iv[PSOC6_HARDFAULT_VECTOR]);
    ck_assert_uint_eq(mock_scb.VTOR, (uint32_t)(uintptr_t)__ramVectors);
}
END_TEST

START_TEST(test_cm4_handoff_parks_cm0)
{
    const uint32_t *app =
        (const uint32_t *)(uintptr_t)(WOLFBOOT_PARTITION_BOOT_ADDRESS + 0x400U);

    if (setjmp(park) == 0) {
        hal_boot_remote_core(app);
        ck_abort_msg("hal_boot_remote_core returned");
    }
    ck_assert_int_eq(cm4_started, 1);
    ck_assert_uint_eq(cm4_vector, WOLFBOOT_PARTITION_BOOT_ADDRESS + 0x400U);
    ck_assert_int_ge(mock_wfi, 1);
}
END_TEST

Suite *psoc6_boot_suite(void)
{
    Suite *s = suite_create("psoc6-boot");
    TCase *tc = tcase_create("boot");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_first_boot_forks_and_records_bank_a);
    tcase_add_test(tc, test_boot_restores_bank_b_without_rewrite);
    tcase_add_test(tc, test_fork_refreshes_stale_copy);
    tcase_add_test(tc, test_swap_toggles_and_persists);
    tcase_add_test(tc, test_swap_without_record_selects_bank_a);
    tcase_add_test(tc, test_torn_record_keeps_previous_mapping);
    tcase_add_test(tc, test_record_sequence_wraps);
    tcase_add_test(tc, test_update_alias_writes_other_bank);
    tcase_add_test(tc, test_uart_init_routes_pins_and_divider);
    tcase_add_test(tc, test_uart_init_failure_leaves_scb_disabled);
    tcase_add_test(tc, test_prepare_boot_drains_uart);
    tcase_add_test(tc, test_timer_counts_and_extends_wrap);
    tcase_add_test(tc, test_ram_vectors_keep_rom_nmi);
    tcase_add_test(tc, test_cm4_handoff_parks_cm0);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = psoc6_boot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
