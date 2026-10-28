/* unit-psoc-e8-flash.c
 *
 * Unit tests for hal/psoc_e8.c: the QSPI path through src/qspi_flash.c and
 * qspi_transfer(), the RRAM controller path, address routing and the cycle
 * counter time base, with the register blocks replaced by host models.
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

#include "unit-mmio.h"

/* KIT_PSE84_AI layout (config/examples/psoc_e8_ai.config) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x40000
#define WOLFBOOT_PARTITION_SIZE              0x100000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x18140000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x18240000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x18340000

#define __WOLFBOOT
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
#define PSOC_E8_PERI_TIMEOUT 64U
#define PSOC_E8_RRAM_TIMEOUT 64U

/* src/qspi_flash.c as arch.mk builds it for this target */
#define QSPI_FLASH
#define QSPI_FLASH_RAM_CODE
#define QSPI_ADDR_SZ 4
#define QSPI_NO_4BYTE_MODE
#define QSPI_DATA_MODE QSPI_DATA_MODE_SPI
#define PAGE_PROG_CMD 0x12
#define SEC_ERASE_CMD 0xDC
#define FLASH_DEVICE_SIZE 0x4000000
#define SPI_FLASH_SECTOR_SIZE 0x40000
#define QSPI_FLASH_READY_TRIES 100

#include "hal/psoc_e8.h"

/* ---- SMIF model: a log of command entries and data bytes, and scripted
 * status register reads. ---------------------------------------------- */

#define MOCK_LOG 256
static uint32_t cmd_log[MOCK_LOG];
static unsigned int cmd_n;
static uint8_t data_log[MOCK_LOG];
static unsigned int data_n;
static uint32_t cmd_sink;
static uint32_t data_sink;
static uint8_t sr_script[16];
static unsigned int sr_n;
static unsigned int sr_pos;
static uint32_t mock_dev_ctl[SMIF_DEVICES];
static uint32_t mock_fifo_status;
static uint32_t mock_rx_status = 1;
static uint32_t mock_smif_status;
static uint32_t mock_cache_maint;
static uint32_t mock_cache_status;
static uint32_t mock_slow_ca;
static uint32_t mock_fast_ca;
static uint32_t mock_icache;

static uint32_t *cmd_slot(void)
{
    if (cmd_n < MOCK_LOG)
        return &cmd_log[cmd_n++];
    return &cmd_sink;
}

static uint32_t *data_slot(void)
{
    data_sink = 0;
    return &data_sink;
}

static uint32_t rx_read(void)
{
    if (sr_pos < sr_n)
        return sr_script[sr_pos++];
    return 0;
}

#undef SMIF_TX_CMD_STATUS
#undef SMIF_TX_CMD_WR
#undef SMIF_TX_DATA_STATUS
#undef SMIF_TX_DATA_WR1
#undef SMIF_RX_DATA_STATUS
#undef SMIF_RX_DATA_RD1
#undef SMIF_STATUS
#undef SMIF_DEVICE_CTL
#undef SMIF_CACHE_MAINT_ALL
#undef SMIF_CACHE_MAINT_STATUS
#undef SMIF_SLOW_CA_CMD
#undef SMIF_FAST_CA_CMD
#undef ICACHE0_CMD
#define SMIF_TX_CMD_STATUS      mock_fifo_status
#define SMIF_TX_CMD_WR          (*cmd_slot())
#define SMIF_TX_DATA_STATUS     mock_fifo_status
#define SMIF_TX_DATA_WR1        (*data_slot())
#define SMIF_RX_DATA_STATUS     mock_rx_status
#define SMIF_RX_DATA_RD1        rx_read()
#define SMIF_STATUS             mock_smif_status
#define SMIF_DEVICE_CTL(n)      mock_dev_ctl[(n)]
#define SMIF_CACHE_MAINT_ALL    mock_cache_maint
#define SMIF_CACHE_MAINT_STATUS mock_cache_status
#define SMIF_SLOW_CA_CMD        mock_slow_ca
#define SMIF_FAST_CA_CMD        mock_fast_ca
#define ICACHE0_CMD             mock_icache

/* ---- RRAM model: memory is mapped at the Secure system-bus alias, and each
 * STARTOP consumes the next scripted VERR value. ----------------------- */

static uint32_t mock_nvm_prog;
static uint32_t mock_nvm_addr;
static uint32_t mock_pc_mask;
static uint32_t mock_ipc_acquire;
static uint32_t mock_ipc_release;
static uint32_t verr_script[8];
static unsigned int verr_n;
static unsigned int verr_pos;
static uint32_t last_verr;
static uint32_t mock_ecc_status;
static unsigned int startops;

static uint32_t nvm_status(void)
{
    if ((mock_nvm_prog & RRAMC_NVM_PROG_STARTOP) != 0U) {
        mock_nvm_prog &= ~RRAMC_NVM_PROG_STARTOP;
        startops++;
        last_verr = (verr_pos < verr_n) ? verr_script[verr_pos++] : 0U;
    }
    return (last_verr << RRAMC_NVM_STATUS_VERR_Pos) | mock_ecc_status;
}

#undef RRAMC_NVM_STATUS
#undef RRAMC_NVM_PROG
#undef RRAMC_NVM_ADDR
#undef RRAMC_PC_MASK
#undef IPC0_RRAM_ACQUIRE
#undef IPC0_RRAM_RELEASE
#define RRAMC_NVM_STATUS        nvm_status()
#define RRAMC_NVM_PROG          mock_nvm_prog
#define RRAMC_NVM_ADDR          mock_nvm_addr
#define RRAMC_PC_MASK           mock_pc_mask
#define IPC0_RRAM_ACQUIRE       mock_ipc_acquire
#define IPC0_RRAM_RELEASE       mock_ipc_release

/* ---- Cycle counter and the rest ---------------------------------------- */

static uint32_t mock_demcr;
static uint32_t mock_dwt_ctrl;
static uint32_t cyc_script[4];
static unsigned int cyc_n;
static unsigned int cyc_pos;
static uint32_t cyc_free;
static uint64_t cyc_total;

/* Scripted values first, then a free-running counter. */
static uint32_t cyccnt(void)
{
    if (cyc_pos < cyc_n)
        return cyc_script[cyc_pos++];
    cyc_free += 1000U;
    cyc_total += 1000U;
    return cyc_free;
}

static uint32_t mock_sl_ctl[8];
static uint32_t mock_sl_ctl2[8];

/* One divider and clock destination; the divider reports enabled from the
 * first read after it was written. */
static uint32_t mock_div_ctl;
static uint32_t mock_clock_ctl;
static uint32_t mock_div_cmd;
static unsigned int div_ctl_accesses;
static int div_never_enables;

static uint32_t *div_ctl(void)
{
    if ((div_ctl_accesses++ > 0U) && !div_never_enables)
        mock_div_ctl |= PERI_PCLK_DIV_16_5_EN;
    return &mock_div_ctl;
}

#undef DEMCR
#undef DWT_CTRL
#undef DWT_CYCCNT
#undef PERI_GR_SL_CTL
#undef PERI_GR_SL_CTL2
#define DEMCR                   mock_demcr
#define DWT_CTRL                mock_dwt_ctrl
#define DWT_CYCCNT              cyccnt()
#define PERI_GR_SL_CTL(g)       mock_sl_ctl[(g)]
#define PERI_GR_SL_CTL2(g)      mock_sl_ctl2[(g)]
#undef PERI_PCLK_DIV_16_5_CTL
#undef PERI_PCLK_CLOCK_CTL
#undef PERI_PCLK_DIV_CMD
#define PERI_PCLK_DIV_16_5_CTL(g, d) (*div_ctl())
#define PERI_PCLK_CLOCK_CTL(g, i)    mock_clock_ctl
#define PERI_PCLK_DIV_CMD(g)         mock_div_cmd

#include "../../src/qspi_flash.c"
#include "../../hal/psoc_e8.c"

static uint8_t *rram;

#define SS1 SMIF_CMD_SS(1)

static void reset_mocks(void)
{
    memset(cmd_log, 0, sizeof(cmd_log));
    memset(data_log, 0, sizeof(data_log));
    cmd_n = data_n = 0;
    sr_n = sr_pos = 0;
    memset(mock_dev_ctl, 0, sizeof(mock_dev_ctl));
    mock_dev_ctl[1] = SMIF_DEVICE_CTL_ENABLED;
    verr_n = verr_pos = 0;
    last_verr = 0;
    mock_ecc_status = 0;
    startops = 0;
    mock_nvm_prog = 0;
    mock_pc_mask = 0;
    mock_ipc_acquire = IPC_ACQUIRE_SUCCESS;
    cyc_n = cyc_pos = 0;
    qspi_failed = 0;
}

static void script_sr(const uint8_t *sr, unsigned int n)
{
    memcpy(sr_script, sr, n);
    sr_n = n;
    sr_pos = 0;
}

/* Index of the first command entry carrying opcode op as its first byte. */
static int find_cmd(uint8_t op)
{
    unsigned int i;

    for (i = 0; i < cmd_n; i++) {
        if ((cmd_log[i] & (0x7UL << 24)) == SMIF_CMD_MODE_TX &&
            (cmd_log[i] & 0xFFU) == op &&
            (i == 0 || (cmd_log[i - 1] & SMIF_CMD_LAST) != 0 ||
             (cmd_log[i - 1] & (0x7UL << 24)) != SMIF_CMD_MODE_TX))
            return (int)i;
    }
    return -1;
}

START_TEST(test_is_rram_aliases)
{
    uint32_t off;

    ck_assert_int_eq(is_rram(0x02040010U, &off), 1);
    ck_assert_uint_eq(off, 0x40010);
    ck_assert_int_eq(is_rram(0x12040010U, &off), 1);
    ck_assert_int_eq(is_rram(0x22040010U, &off), 1);
    ck_assert_int_eq(is_rram(0x3207FFFFU, &off), 1);
    ck_assert_uint_eq(off, 0x7FFFF);
    ck_assert_int_eq(is_rram(0x32080000U, &off), 0);
    ck_assert_int_eq(is_rram(0x18140000U, &off), 0);
    ck_assert_int_eq(is_rram(0x08140000U, &off), 0);
    ck_assert_int_eq(is_rram(0x60140000U, &off), 0);
    ck_assert_int_eq(is_rram(0x70140000U, &off), 0);
}
END_TEST

START_TEST(test_rram_write_spans_blocks)
{
    uint8_t buf[40];
    unsigned int i;

    reset_mocks();
    memset(rram + 0x40000, 0xAA, 0x40);
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)i;
    ck_assert_int_eq(hal_flash_write(0x32040005U, buf, sizeof(buf)), 0);
    ck_assert_int_eq(memcmp(rram + 0x40005, buf, sizeof(buf)), 0);
    /* The rest of the touched blocks is preserved. */
    ck_assert_uint_eq(rram[0x40004], 0xAA);
    ck_assert_uint_eq(rram[0x40005 + sizeof(buf)], 0xAA);
    /* 0x40005..0x4002C spans three 16-byte blocks. */
    ck_assert_uint_eq(startops, 3);
    ck_assert_uint_eq(mock_ipc_release, 0);
    ck_assert_uint_eq(mock_pc_mask & RRAMC_PC_MASK_PC_LOCK_Msk, 0);
}
END_TEST

START_TEST(test_rram_erase_fills_ff)
{
    unsigned int i;

    reset_mocks();
    memset(rram + 0x41000, 0x00, 0x40);
    ck_assert_int_eq(hal_flash_erase(0x32041000U, 0x20), 0);
    for (i = 0; i < 0x20; i++)
        ck_assert_uint_eq(rram[0x41000 + i], 0xFF);
    ck_assert_uint_eq(rram[0x41020], 0x00);
}
END_TEST

START_TEST(test_rram_write_ecc_block)
{
    uint8_t b = 0x5A;
    unsigned int i;

    /* Only the block-level uncorrectable flag rebuilds the block. */
    reset_mocks();
    memset(rram + 0x43000, 0xAA, 0x10);
    mock_ecc_status = 1UL << 10; /* sticky ECC4READ from an earlier read */
    ck_assert_int_eq(hal_flash_write(0x32043003U, &b, 1), 0);
    ck_assert_uint_eq(rram[0x43000], 0xAA);
    ck_assert_uint_eq(rram[0x4300F], 0xAA);
    ck_assert_uint_eq(rram[0x43003], 0x5A);

    reset_mocks();
    memset(rram + 0x43000, 0xAA, 0x10);
    mock_ecc_status = RRAMC_NVM_STATUS_ECC4MRD;
    ck_assert_int_eq(hal_flash_write(0x32043003U, &b, 1), 0);
    for (i = 0; i < 0x10; i++)
        ck_assert_uint_eq(rram[0x43000 + i], (i == 3) ? 0x5A : 0x00);
}
END_TEST

START_TEST(test_rram_verify_retry)
{
    uint8_t b = 0x5A;
    static const uint32_t retry_ok[] = { 3, 0 };
    static const uint32_t last_try_2[] = { 3, 3, 2 };
    static const uint32_t never[] = { 3, 3, 3 };
    static const uint32_t fatal[] = { 5 };

    reset_mocks();
    memcpy(verr_script, retry_ok, sizeof(retry_ok));
    verr_n = 2;
    ck_assert_int_eq(hal_flash_write(0x32042000U, &b, 1), 0);
    ck_assert_uint_eq(startops, 2);

    reset_mocks();
    memcpy(verr_script, last_try_2, sizeof(last_try_2));
    verr_n = 3;
    ck_assert_int_eq(hal_flash_write(0x32042000U, &b, 1), 0);
    ck_assert_uint_eq(startops, 3);

    reset_mocks();
    memcpy(verr_script, never, sizeof(never));
    verr_n = 3;
    ck_assert_int_eq(hal_flash_write(0x32042000U, &b, 1), -1);

    reset_mocks();
    memcpy(verr_script, fatal, sizeof(fatal));
    verr_n = 1;
    ck_assert_int_eq(hal_flash_write(0x32042000U, &b, 1), -1);
    ck_assert_uint_eq(startops, 1);
}
END_TEST

START_TEST(test_rram_lock_busy)
{
    uint8_t b = 0x5A;

    reset_mocks();
    mock_ipc_acquire = 0;
    ck_assert_int_eq(hal_flash_write(0x32042000U, &b, 1), -1);
    ck_assert_uint_eq(startops, 0);
}
END_TEST

START_TEST(test_rram_bounds)
{
    uint8_t b[2] = { 1, 2 };

    reset_mocks();
    ck_assert_int_eq(hal_flash_write(0x3207FFFFU, b, 2), -1);
}
END_TEST

START_TEST(test_qspi_program)
{
    static const uint8_t sr[] = { 0x02, 0x00 };  /* WEL, then ready */
    uint8_t buf[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    int i;

    reset_mocks();
    script_sr(sr, sizeof(sr));
    ck_assert_int_eq(hal_flash_write(0x18140010U, buf, sizeof(buf)), 0);
    ck_assert_int_ge(find_cmd(0x06), 0);
    i = find_cmd(PAGE_PROG_CMD);
    ck_assert_int_ge(i, 0);
    /* 4-byte offset, MSB first, then a 4-byte data phase. */
    ck_assert_uint_eq(cmd_log[i], SS1 | SMIF_CMD_MODE_TX | 0x12U);
    ck_assert_uint_eq(cmd_log[i + 1], SS1 | SMIF_CMD_MODE_TX | 0x00U);
    ck_assert_uint_eq(cmd_log[i + 2], SS1 | SMIF_CMD_MODE_TX | 0x14U);
    ck_assert_uint_eq(cmd_log[i + 3], SS1 | SMIF_CMD_MODE_TX | 0x00U);
    ck_assert_uint_eq(cmd_log[i + 4], SS1 | SMIF_CMD_MODE_TX | 0x10U);
    ck_assert_uint_eq(cmd_log[i + 5],
            SMIF_CMD_MODE_TX_COUNT | SMIF_CMD_LAST | 3U);
    /* The data went out of the staging buffer. */
    ck_assert_int_eq(memcmp(qspi_page, buf, sizeof(buf)), 0);
}
END_TEST

START_TEST(test_qspi_erase)
{
    static const uint8_t sr[] = { 0x02, 0x01, 0x01, 0x00 };
    int i;

    reset_mocks();
    script_sr(sr, sizeof(sr));
    ck_assert_int_eq(hal_flash_erase(0x18240000U, 0x40000), 0);
    i = find_cmd(SEC_ERASE_CMD);
    ck_assert_int_ge(i, 0);
    ck_assert_uint_eq(cmd_log[i + 2], SS1 | SMIF_CMD_MODE_TX | 0x24U);
    ck_assert_uint_eq(cmd_log[i + 4],
            SS1 | SMIF_CMD_MODE_TX | SMIF_CMD_LAST | 0x00U);
    /* Unaligned erases are refused. */
    ck_assert_int_eq(hal_flash_erase(0x18241000U, 0x40000), -1);
    ck_assert_int_eq(hal_flash_erase(0x18240000U, 0x1000), -1);
}
END_TEST

START_TEST(test_qspi_failure_reported)
{
    /* WEL, then a program failure, then ready: the HAL must still fail. */
    static const uint8_t sr[] = { 0x02, NOR_SR_P_ERR, 0x00 };
    uint8_t b = 0x5A;

    reset_mocks();
    script_sr(sr, sizeof(sr));
    ck_assert_int_eq(hal_flash_write(0x18140000U, &b, 1), -1);
    ck_assert_int_ge(find_cmd(NOR_CMD_CLPEF), 0);
}
END_TEST

START_TEST(test_qspi_transfer_args)
{
    uint8_t buf[4];

    reset_mocks();
    ck_assert_int_eq(qspi_transfer(QSPI_MODE_READ, 0x05, 0, 0, 0, 0xF0, 1, 1,
            0, buf, 1, 1), -1);
    ck_assert_int_eq(qspi_transfer(QSPI_MODE_READ, 0x0B, 0, 4, 1, 0, 0, 0,
            8, buf, 1, 1), -1);
    ck_assert_int_eq(qspi_transfer(QSPI_MODE_WRITE, 0x12, 0, 4, 1, 0, 0, 0,
            0, buf, SPI_FLASH_PAGE_SIZE + 1U, 1), -1);
    ck_assert_int_eq(qspi_transfer(QSPI_MODE_READ, 0x05, 0, 0, 0, 0, 0, 0,
            0, NULL, 1, 1), -1);
    ck_assert_uint_eq(cmd_n, 0);
    /* No XIP device configured. */
    memset(mock_dev_ctl, 0, sizeof(mock_dev_ctl));
    ck_assert_int_eq(qspi_transfer(QSPI_MODE_WRITE, 0x06, 0, 0, 0, 0, 0, 0,
            0, NULL, 0, 0), -1);
}
END_TEST

static int pclk(uint32_t pclk_hz, uint32_t target_hz, uint32_t *int_part,
        uint32_t *frac)
{
    int ret;

    mock_div_ctl = 0;
    mock_clock_ctl = 0;
    mock_div_cmd = 0;
    div_ctl_accesses = 0;
    ret = psoc_e8_pclk_setup(1, 1, 3, pclk_hz, target_hz);
    *int_part = (mock_div_ctl >> PERI_PCLK_DIV_16_5_INT_Pos) & 0xFFFFU;
    *frac = (mock_div_ctl >> PERI_PCLK_DIV_16_5_FRAC_Pos) & 0x1FU;
    return ret;
}

START_TEST(test_pclk_divider)
{
    uint32_t i;
    uint32_t f;

    div_never_enables = 0;
    /* 100 MHz to 115200 x 16: 54.25 -> INT 53, FRAC 8 */
    ck_assert_int_eq(pclk(100000000U, 1843200U, &i, &f), 0);
    ck_assert_uint_eq(i, 53);
    ck_assert_uint_eq(f, 8);
    ck_assert_uint_eq(mock_clock_ctl,
            (PERI_PCLK_DIV_TYPE_16_5 << PERI_PCLK_CLOCK_CTL_TYPE_Pos) | 3U);
    ck_assert_uint_eq(mock_div_cmd, PERI_PCLK_DIV_CMD_ENABLE |
            PERI_PCLK_DIV_CMD_PA_NONE |
            (PERI_PCLK_DIV_TYPE_16_5 << PERI_PCLK_DIV_CMD_TYPE_Pos) | 3U);
    /* A fraction above 16/32. */
    ck_assert_int_eq(pclk(100000000U, 1826484U, &i, &f), 0);
    ck_assert_uint_eq(i, 53);
    ck_assert_uint_eq(f, 24);
    /* Exact integer division. */
    ck_assert_int_eq(pclk(100000000U, 25000000U, &i, &f), 0);
    ck_assert_uint_eq(i, 3);
    ck_assert_uint_eq(f, 0);
    /* Faster than the source clamps to divide by one. */
    ck_assert_int_eq(pclk(100000000U, 200000000U, &i, &f), 0);
    ck_assert_uint_eq(i, 0);
    ck_assert_uint_eq(f, 0);
    /* Just inside and beyond the 16-bit integer range. */
    ck_assert_int_eq(pclk(0x10000U, 1U, &i, &f), 0);
    ck_assert_uint_eq(i, 0xFFFF);
    ck_assert_uint_eq(f, 0);
    ck_assert_int_eq(pclk(100000000U, 1U, &i, &f), 0);
    ck_assert_uint_eq(i, 0xFFFF);
    ck_assert_uint_eq(f, 0);
    ck_assert_int_eq(pclk(0x20001U, 2U, &i, &f), 0);
    ck_assert_uint_eq(i, 0xFFFF);
    ck_assert_uint_eq(f, 0);
    /* No target, and a divider that never starts. */
    mock_div_ctl = 0x1234;
    ck_assert_int_eq(psoc_e8_pclk_setup(1, 1, 3, 100000000U, 0), -1);
    ck_assert_uint_eq(mock_div_ctl, 0x1234);
    div_never_enables = 1;
    ck_assert_int_eq(pclk(100000000U, 1843200U, &i, &f), -1);
    div_never_enables = 0;
}
END_TEST

START_TEST(test_rram_write_skips_smif_cache)
{
    uint8_t b = 0x5A;

    reset_mocks();
    mock_cache_maint = 0;
    mock_slow_ca = 0;
    mock_fast_ca = 0;
    ck_assert_int_eq(hal_flash_write(0x32042000U, &b, 1), 0);
    ck_assert_uint_eq(mock_cache_maint | mock_slow_ca | mock_fast_ca, 0);
}
END_TEST

START_TEST(test_delay_long)
{
    reset_mocks();
    cyc_total = 0;
    /* 30 s is more cycles than one 32-bit count holds. */
    psoc_e8_delay_us(30000000U);
    ck_assert(cyc_total >= 30000000ULL * (PSOC_E8_CPU_HZ / 1000000UL));
}
END_TEST

START_TEST(test_timer_wrap)
{
    uint64_t a;
    uint64_t b;

    reset_mocks();
    cyc_script[0] = 0xFFFFF000U;
    cyc_script[1] = 0x00001000U;
    cyc_n = 2;
    a = hal_get_timer_us();
    b = hal_get_timer_us();
    ck_assert(b > a);
    ck_assert_uint_eq(b - a, (0x2000U) / (PSOC_E8_CPU_HZ / 1000000UL));
}
END_TEST

static Suite *psoc_e8_flash_suite(void)
{
    Suite *s = suite_create("psoc_e8_flash");
    TCase *tc = tcase_create("flash");

    tcase_add_test(tc, test_is_rram_aliases);
    tcase_add_test(tc, test_rram_write_spans_blocks);
    tcase_add_test(tc, test_rram_erase_fills_ff);
    tcase_add_test(tc, test_rram_write_ecc_block);
    tcase_add_test(tc, test_rram_verify_retry);
    tcase_add_test(tc, test_rram_lock_busy);
    tcase_add_test(tc, test_rram_bounds);
    tcase_add_test(tc, test_qspi_program);
    tcase_add_test(tc, test_qspi_erase);
    tcase_add_test(tc, test_qspi_failure_reported);
    tcase_add_test(tc, test_qspi_transfer_args);
    tcase_add_test(tc, test_timer_wrap);
    tcase_add_test(tc, test_pclk_divider);
    tcase_add_test(tc, test_rram_write_skips_smif_cache);
    tcase_add_test(tc, test_delay_long);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s;
    SRunner *sr;

    rram = unit_mmio_map(PSOC_E8_RRAM_SBUS_S, PSOC_E8_RRAM_SIZE);
    if (rram == NULL) {
        fprintf(stderr, "cannot map RRAM model at 0x%08lx\n",
                (unsigned long)PSOC_E8_RRAM_SBUS_S);
        return 1;
    }
    s = psoc_e8_flash_suite();
    sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
