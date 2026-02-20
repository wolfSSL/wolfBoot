/* sdhci.c
 *
 * Cadence SD Host Controller Interface (SDHCI) Driver
 * Generic implementation supporting SD cards and eMMC.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#include "sdhci.h"

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "printf.h"
#include "hal.h"
#include "disk.h"

/* ============================================================================
 * Internal state
 * ============================================================================ */

static uint32_t g_sector_count;
static uint32_t g_sector_size;
static uint32_t g_bus_width = 1;
static uint32_t g_rca = 0; /* SD Card Relative Address */

/* MMC Interrupt state - volatile for interrupt handler access */
static volatile uint32_t g_mmc_irq_status = 0;
static volatile int g_mmc_irq_pending = 0;

/* Microsecond delay using hardware timer */
static void udelay(uint32_t us)
{
    uint64_t start = hal_get_timer_us();
    while ((hal_get_timer_us() - start) < us);
}

/* ============================================================================
 * Register Access Helpers
 * ============================================================================ */

/* Read 32-bit register at offset */
#define SDHCI_REG(offset) sdhci_reg_read(offset)

/* Write 32-bit register at offset */
#define SDHCI_REG_SET(offset, val) sdhci_reg_write(offset, val)

/* Read-modify-write helper */
static inline void sdhci_reg_or(uint32_t offset, uint32_t val)
{
    sdhci_reg_write(offset, sdhci_reg_read(offset) | val);
}

static inline void sdhci_reg_and(uint32_t offset, uint32_t val)
{
    sdhci_reg_write(offset, sdhci_reg_read(offset) & val);
}

/* ============================================================================
 * PHY Register Access
 * ============================================================================ */

/* Write to SD/eMMC PHY register via HRS04 */
static void sdhci_phy_write(uint8_t phy_addr, uint8_t delay_val)
{
    uint32_t phycfg;

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_phy_write: phyaddr: 0x%08x, delay_value: %d\n",
        phy_addr, delay_val);
#endif

    /* Wait for ACK to clear */
    while ((SDHCI_REG(SDHCI_HRS04) & SDHCI_HRS04_UIS_ACK) == 0);

    /* Set address and delay value */
    phycfg = ((uint32_t)phy_addr & SDHCI_HRS04_UIS_ADDR_MASK) |
             ((uint32_t)delay_val << SDHCI_HRS04_UIS_WDATA_SHIFT);
    SDHCI_REG_SET(SDHCI_HRS04, phycfg);

    /* Send write request */
    SDHCI_REG_SET(SDHCI_HRS04, phycfg | SDHCI_HRS04_UIS_WR);
    /* Wait for ACK */
    while ((SDHCI_REG(SDHCI_HRS04) & SDHCI_HRS04_UIS_ACK) == 0);

    /* Clear write request */
    SDHCI_REG_SET(SDHCI_HRS04, phycfg);
    SDHCI_REG_SET(SDHCI_HRS04, 0);
}

/* ============================================================================
 * Interrupt Handler
 * ============================================================================ */

void sdhci_irq_handler(void)
{
    uint32_t status = SDHCI_REG(SDHCI_SRS12);

    /* Check for DMA interrupt (SDMA boundary crossing) */
    if (status & SDHCI_SRS12_DMAINT) {
        /* Read the next DMA address saved by the controller */
        uint32_t addr_lo = SDHCI_REG(SDHCI_SRS22);
        uint32_t addr_hi = SDHCI_REG(SDHCI_SRS23);

        /* Clear DMA interrupt status before restarting */
        SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_DMAINT);
#if defined(__riscv)
        asm volatile("fence rw, rw" ::: "memory");
#elif defined(__aarch64__)
        asm volatile("dsb sy" ::: "memory");
#endif

        /* Write SDMA address to resume transfer.
         * Per SDHCI v4 spec: write high 32 bits first, then low 32 bits.
         * Writing the low address (SRS22 / offset 0x058) triggers the
         * DMA engine to resume. */
        SDHCI_REG_SET(SDHCI_SRS23, addr_hi);
        SDHCI_REG_SET(SDHCI_SRS22, addr_lo);

        g_mmc_irq_status |= SDHCI_IRQ_FLAG_DMAINT;
    }

    /* Check for transfer complete */
    if (status & SDHCI_SRS12_TC) {
        g_mmc_irq_status |= SDHCI_IRQ_FLAG_TC;
        SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_TC); /* Clear interrupt */
    }

    /* Check for command complete */
    if (status & SDHCI_SRS12_CC) {
        g_mmc_irq_status |= SDHCI_IRQ_FLAG_CC;
        SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_CC); /* Clear interrupt */
    }

    /* Check for data timeout error */
    if (status & SDHCI_SRS12_EDT) {
        g_mmc_irq_status |= SDHCI_IRQ_FLAG_ERROR;
        SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_EDT); /* Clear interrupt */
    }

    /* Check for any other errors */
    if (status & SDHCI_SRS12_EINT) {
        g_mmc_irq_status |= SDHCI_IRQ_FLAG_ERROR;
        /* Clear all error status bits */
        SDHCI_REG_SET(SDHCI_SRS12, (status & SDHCI_SRS12_ERR_STAT));
    }

    /* Signal that interrupt was handled */
    g_mmc_irq_pending = 1;

#ifdef DEBUG_SDHCI_IRQ
    wolfBoot_printf("sdhci_irq_handler: status=0x%08X, flags=0x%02X\n",
        status, g_mmc_irq_status);
#endif
}

/* Enable SDHCI interrupts for SDMA transfer */
static void sdhci_enable_sdma_interrupts(void)
{
    /* Enable signal interrupts for: DMA, Transfer Complete, Command Complete,
     * Data Timeout Error */
    uint32_t sig_enable = SDHCI_SRS14_DMAINT_IE |
                          SDHCI_SRS14_TC_IE |
                          SDHCI_SRS14_CC_IE |
                          SDHCI_SRS14_EDT_IE;
    sdhci_reg_or(SDHCI_SRS14, sig_enable);

    /* Clear any pending interrupt state */
    g_mmc_irq_status = 0;
    g_mmc_irq_pending = 0;
}

/* Disable SDHCI signal interrupts (status enables remain for polling) */
static void sdhci_disable_sdma_interrupts(void)
{
    uint32_t reg = SDHCI_REG(SDHCI_SRS14);
    reg &= ~(SDHCI_SRS14_DMAINT_IE |
             SDHCI_SRS14_TC_IE |
             SDHCI_SRS14_CC_IE |
             SDHCI_SRS14_EDT_IE);
    SDHCI_REG_SET(SDHCI_SRS14, reg);
}

/* Wait for SDHCI interrupt with timeout.
 * Supports both hardware interrupt and polling modes:
 * - Interrupt mode: g_mmc_irq_pending set by sdhci_irq_handler() via platform ISR
 * - Polling mode: directly reads SRS12 status register and calls handler */
static int sdhci_wait_irq(uint32_t expected_flags, uint32_t timeout)
{
    while (timeout-- > 0) {
        /* Poll SRS12 directly for platforms without interrupt routing.
         * In interrupt mode this is redundant (bits already cleared by ISR). */
        uint32_t status = SDHCI_REG(SDHCI_SRS12);
        if (status & (SDHCI_SRS12_TC | SDHCI_SRS12_CC | SDHCI_SRS12_DMAINT |
                      SDHCI_SRS12_EDT | SDHCI_SRS12_EINT)) {
            sdhci_irq_handler();
        }

        if (g_mmc_irq_pending) {
            g_mmc_irq_pending = 0;

            /* Check for error */
            if (g_mmc_irq_status & SDHCI_IRQ_FLAG_ERROR) {
                return -1;
            }

            /* Check for expected flags */
            if (g_mmc_irq_status & expected_flags) {
                return 0;
            }
        }
        asm volatile("nop");
    }
    return -1; /* Timeout */
}

/* ============================================================================
 * Timeout Configuration
 * ============================================================================ */

static int sdhci_set_timeout(uint32_t timeout_us)
{
    uint32_t reg, i, tcfclk, tcfclk_mhz, tcfclk_khz, timeout_val, dtcv;

    /* read capabilities to determine timeout clock frequency and unit (MHz or kHz) */
    reg = SDHCI_REG(SDHCI_SRS16);
    tcfclk_khz = (reg & SDHCI_SRS16_TCF_MASK) >> SDHCI_SRS16_TCF_SHIFT;
    /* Default timeout clock frequency should be 50MHz */

    if (((reg & SDHCI_SRS16_TCU) == 0) && (timeout_us < 1000)) {
        /* invalid timeout_us value */
        return -1;
    }
    if (tcfclk_khz == 0) {
        /* reported timeout clock frequency is 0 */
        return -1;
    }

    if ((reg & SDHCI_SRS16_TCU) != 0) {
        tcfclk_khz *= 1000; /* MHz to kHz */
    }
    tcfclk_mhz = tcfclk_khz / 1000;
    if (tcfclk_mhz == 0) {
        tcfclk = tcfclk_khz;
        timeout_val = timeout_us / 1000;
    }
    else {
        tcfclk = tcfclk_mhz;
        timeout_val = timeout_us;
    }

    /* calculate the data timeout counter value */
    dtcv = 8192; /* 2*13 */
    for (i=0; i<15; i++) {
        if (timeout_val < (dtcv / tcfclk)) {
            break;
        }
        dtcv *= 2;
    }
    dtcv = i;

    /* set the data timeout counter value */
    reg = SDHCI_REG(SDHCI_SRS11);
    reg &= ~SDHCI_SRS11_DTCV_MASK;
    reg |= (dtcv << SDHCI_SRS11_DTCV_SHIFT) & SDHCI_SRS11_DTCV_MASK;
    SDHCI_REG_SET(SDHCI_SRS11, reg);

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_set_timeout: timeout_val %d (%d)\n", timeout_val, dtcv);
#endif

    return 0;
}

/* ============================================================================
 * Power Control
 * ============================================================================ */

/* voltage values:
 *  0 = off
 *  SDHCI_SRS10_BVS_1_8V
 *  SDHCI_SRS10_BVS_3_0V
 *  SDHCI_SRS10_BVS_3_3V
 */
static int sdhci_set_power(uint32_t voltage)
{
    uint32_t reg;

    /* disable bus power */
    reg = SDHCI_REG(SDHCI_SRS10);
    reg &= ~SDHCI_SRS10_BP;
    SDHCI_REG_SET(SDHCI_SRS10, reg);

    if (voltage != 0) {
        /* read voltage capabilities */
        uint32_t cap2 = SDHCI_REG(SDHCI_SRS16);

        /* select voltage (if capable) */
        reg &= ~SDHCI_SRS10_BVS_MASK;
        if (voltage == SDHCI_SRS10_BVS_1_8V && (cap2 & SDHCI_SRS16_VS18)) {
            reg |= SDHCI_SRS10_BP | SDHCI_SRS10_BVS_1_8V;
        }
        else if (voltage == SDHCI_SRS10_BVS_3_0V && (cap2 & SDHCI_SRS16_VS30)) {
            reg |= SDHCI_SRS10_BP | SDHCI_SRS10_BVS_3_0V;
        }
        else if (voltage == SDHCI_SRS10_BVS_3_3V && (cap2 & SDHCI_SRS16_VS33)) {
            reg |= SDHCI_SRS10_BP | SDHCI_SRS10_BVS_3_3V;
        }
        else {
            /* voltage not supported */
            return -1;
        }
        /* should be - 0xf06 */
        SDHCI_REG_SET(SDHCI_SRS10, reg);
    }
    return 0;
}

/* ============================================================================
 * Clock Control
 * ============================================================================ */

/* returns actual frequency in kHz */
static uint32_t sdhci_set_clock(uint32_t clock_khz)
{
    static uint32_t last_clock_khz = 0;
    uint32_t reg, base_clk_khz, i, mclk, freq_khz;

    if (last_clock_khz != 0 && last_clock_khz == clock_khz) {
        /* clock already set */
        return 0;
    }

    /* disable clock */
    sdhci_reg_and(SDHCI_SRS11, ~SDHCI_SRS11_SDCE);

    /* get base clock */
    reg = SDHCI_REG(SDHCI_SRS16);
    base_clk_khz = (reg & SDHCI_SRS16_BCSDCLK_MASK) >> SDHCI_SRS16_BCSDCLK_SHIFT;
    if (base_clk_khz == 0) {
        /* error getting base clock */
        return -1;
    }
    base_clk_khz *= 1000; /* convert MHz to kHz */

    /* calculate divider */
    for (i=1; i<2046; i++) {
        if (((base_clk_khz / i) < clock_khz) ||
            (((base_clk_khz / i) == clock_khz) && (base_clk_khz % i) == 0)) {
            break;
        }
    }
    mclk = (i / 2);

    /* select clock frequency */
    reg = SDHCI_REG(SDHCI_SRS11);
    reg &= ~(SDHCI_SRS11_SDCFSL_MASK | SDHCI_SRS11_SDCFSH_MASK);
    reg |= (((mclk & 0x0FF) << SDHCI_SRS11_SDCFSL_SHIFT) & SDHCI_SRS11_SDCFSL_MASK);  /* lower 8 bits */
    reg |= (((mclk & 0x300) << SDHCI_SRS11_SDCFSH_SHIFT) & SDHCI_SRS11_SDCFSH_SHIFT); /* upper 2 bits */
    reg |= SDHCI_SRS11_ICE; /* clock enable */
    reg &= ~SDHCI_SRS11_CGS; /* select clock */
    SDHCI_REG_SET(SDHCI_SRS11, reg);
    freq_khz = base_clk_khz / i;

    /* wait for clock to stabilize */
    while ((SDHCI_REG(SDHCI_SRS11) & SDHCI_SRS11_ICS) == 0);

    /* enable clock */
    sdhci_reg_or(SDHCI_SRS11, SDHCI_SRS11_SDCE);
    last_clock_khz = clock_khz;

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_set_clock: requested khz: %d, actual khz: %d\n",
        clock_khz, freq_khz);
#endif

    return freq_khz;
}

/* ============================================================================
 * Command Response Helpers
 * ============================================================================ */

static uint32_t sdhci_get_response_type(uint8_t resp_type)
{
    uint32_t cmd_reg;
    switch (resp_type) {
        case SDHCI_RESP_R2:
            cmd_reg = (SDHCI_SRS03_RESP_136 | SDHCI_SRS03_CRCCE);
            break;
        case SDHCI_RESP_R3:
        case SDHCI_RESP_R4:
            cmd_reg = SDHCI_SRS03_RESP_48;
            break;
        case SDHCI_RESP_R1:
        case SDHCI_RESP_R5:
        case SDHCI_RESP_R6:
        case SDHCI_RESP_R7:
            cmd_reg = (SDHCI_SRS03_RESP_48 | SDHCI_SRS03_CRCCE | SDHCI_SRS03_CICE);
            break;
        case SDHCI_RESP_R1B:
        case SDHCI_RESP_R5B:
            cmd_reg = (SDHCI_SRS03_RESP_48B | SDHCI_SRS03_CRCCE | SDHCI_SRS03_CICE);
            break;
        case SDHCI_RESP_NONE:
        default:
            cmd_reg = SDHCI_SRS03_RESP_NONE;
            break;
    }
    return cmd_reg;
}

/* ============================================================================
 * Command Sending
 * ============================================================================ */

#define DEVICE_BUSY 1

static int sdhci_send_cmd_internal(uint32_t cmd_type,
    uint32_t cmd_index, uint32_t cmd_arg, uint8_t resp_type)
{
    int status = 0;
    uint32_t cmd_reg;
    uint32_t timeout = 0x000FFFFF;

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_send_cmd: cmd_index: %d, cmd_arg: %08X, resp_type: %d\n",
        cmd_index, cmd_arg, resp_type);
#endif

    /* wait for command line to be idle */
    while ((SDHCI_REG(SDHCI_SRS09) & SDHCI_SRS09_CICMD) != 0);

    /* set command argument and command transfer registers */
    SDHCI_REG_SET(SDHCI_SRS02, cmd_arg);
    cmd_reg =
        ((cmd_index << SDHCI_SRS03_CIDX_SHIFT) & SDHCI_SRS03_CIDX_MASK) |
        ((cmd_type << SDHCI_SRS03_CT_SHIFT) & SDHCI_SRS03_CT_MASK) |
        sdhci_get_response_type(resp_type);

    SDHCI_REG_SET(SDHCI_SRS03, cmd_reg);

    /* wait for command complete or error */
    while ((SDHCI_REG(SDHCI_SRS12) & (SDHCI_SRS12_CC | SDHCI_SRS12_TC |
        SDHCI_SRS12_EINT)) == 0 && --timeout > 0);

    if (timeout == 0) {
        wolfBoot_printf("sdhci_send_cmd: timeout waiting for command complete\n");
        status = -1; /* error */
    }
    else if (SDHCI_REG(SDHCI_SRS12) & SDHCI_SRS12_EINT) {
        wolfBoot_printf("sdhci_send_cmd: error SRS12: 0x%08X\n", SDHCI_REG(SDHCI_SRS12));
        status = -1; /* error */
    }

    SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_CC); /* clear command complete */
    while ((SDHCI_REG(SDHCI_SRS09) & SDHCI_SRS09_CICMD) != 0);

    if (status == 0) {
        /* check for device busy */
        if (resp_type == SDHCI_RESP_R1 || resp_type == SDHCI_RESP_R1B) {
            uint32_t resp = SDHCI_REG(SDHCI_SRS04);
            #define CARD_STATUS_READY_FOR_DATA (1U << 8)
            if ((resp & CARD_STATUS_READY_FOR_DATA) == 0) {
                status = DEVICE_BUSY; /* card is busy */
            }
        }
    }

    return status;
}

int sdhci_cmd(uint32_t cmd_index, uint32_t cmd_arg, uint8_t resp_type)
{
    /* send command */
    int status = sdhci_send_cmd_internal(SDHCI_SRS03_CMD_NORMAL, cmd_index,
        cmd_arg, resp_type);

    /* clear all status interrupts
     * (except current limit, card interrupt/removal/insert) */
    SDHCI_REG_SET(SDHCI_SRS12, ~(SDHCI_SRS12_ECL |
                      SDHCI_SRS12_CINT |
                      SDHCI_SRS12_CR |
                      SDHCI_SRS12_CIN));

    return status;
}

/* TODO: Add timeout */
static int sdhci_wait_busy(int check_dat0)
{
    uint32_t status;
    if (check_dat0) {
        /* wait for DATA0 not busy */
        while ((SDHCI_REG(SDHCI_SRS09) & SDHCI_SRS09_DAT0_LVL) == 0);
    }
    /* wait for CMD13 */
    while ((status = sdhci_cmd(MMC_CMD13_SEND_STATUS,
        (g_rca << SD_RCA_SHIFT), SDHCI_RESP_R1)) == DEVICE_BUSY);
    return status;
}

/* Reset data and command lines to recover from errors */
static inline void sdhci_reset_lines(void)
{
    sdhci_reg_or(SDHCI_SRS11, SDHCI_SRS11_RESET_DAT_CMD);
    while (SDHCI_REG(SDHCI_SRS11) & SDHCI_SRS11_RESET_DAT_CMD);
}

/* ============================================================================
 * Response Parsing Helper
 * ============================================================================ */

/* Helper to get bits from the response registers (shared by eMMC and SD) */
static uint32_t sdhci_get_response_bits(int from, int count)
{
    uint32_t mask, ret;
    int off, shft;

    from -= 8;
    mask = ((count < 32) ? (1U << (uint32_t)count) : 0) - 1;
    off = from / 32;
    shft = from & 31;

    /* Read the response registers (SRS04-SRS07) */
    volatile uint32_t resp[4];
    resp[0] = SDHCI_REG(SDHCI_SRS04);
    resp[1] = SDHCI_REG(SDHCI_SRS05);
    resp[2] = SDHCI_REG(SDHCI_SRS06);
    resp[3] = SDHCI_REG(SDHCI_SRS07);

    ret = resp[off] >> shft;
    if ((from + shft) > 32) {
        ret |= resp[off + 1] << ((32 - shft) % 32);
    }
    return ret & mask;
}

/* ============================================================================
 * SD Card Specific Functions
 * ============================================================================ */

#ifdef DISK_SDCARD

/* Set power and send SD card initialization commands */
/* voltage: 0=off or SDHCI_SRS10_BVS_[X_X]V */
static int sdcard_power_init_seq(uint32_t voltage)
{
    /* Set power to specified voltage */
    int status = sdhci_set_power(voltage);
    if (status == 0) {
        /* send CMD0 (go idle) to reset card */
        status = sdhci_cmd(MMC_CMD0_GO_IDLE, 0, SDHCI_RESP_NONE);
    }
    if (status == 0) {
        /* send the operating conditions command */
        status = sdhci_cmd(SD_CMD8_SEND_IF_COND, SD_IF_COND_27V_33V,
            SDHCI_RESP_R7);
    }
    return status;
}

/* SD card operating conditions initialization using ACMD41 */
static int sdcard_card_init(uint32_t acmd41_arg, uint32_t *ocr_reg)
{
    int status = sdhci_cmd(SD_CMD55_APP_CMD, 0, SDHCI_RESP_R1);
    if (status == 0) {
        status = sdhci_cmd(SD_ACMD41_SEND_OP_COND, acmd41_arg,
            SDHCI_RESP_R3);
        if (status == 0) {
            *ocr_reg = SDHCI_REG(SDHCI_SRS04);
        #ifdef DEBUG_SDHCI
            wolfBoot_printf("ocr_reg: 0x%08X\n", *ocr_reg);
        #endif
        }
    }
    return status;
}

/* Forward declarations for SD card functions */
static int sdcard_set_bus_width(uint32_t bus_width);
static int sdcard_set_function(uint32_t function_number, uint32_t group_number);

/* Full SD card initialization sequence
 * Returns 0 on success */
static int sdcard_card_full_init(void)
{
    int status = 0;
    uint32_t reg;
    uint32_t ctrl_volts, card_volts;
    uint32_t irq_restore;
    int xpc, si8r;

    /* Set power to 3.3v and send init commands */
    ctrl_volts = SDHCI_SRS10_BVS_3_3V; /* default to 3.3v */
    status = sdcard_power_init_seq(ctrl_volts);
    if (status == 0) {
        uint32_t max_ma_3_3v, max_ma_1_8v;
        /* determine host controller capabilities */
        reg = SDHCI_REG(SDHCI_SRS18);
        max_ma_3_3v = ((reg & SDHCI_SRS18_MC33_MASK) >> SDHCI_SRS18_MC33_SHIFT) * 4;
        max_ma_1_8v = ((reg & SDHCI_SRS18_MC18_MASK) >> SDHCI_SRS18_MC18_SHIFT) * 4;
        /* does controller support eXtended Power Control (XPC)? */
        xpc = (max_ma_1_8v >= 150) && (max_ma_3_3v >= 150) ? 1 : 0;
        /* does controller support UHS-I (Ultra High Speed Interface) v1.8 signaling? */
        si8r = ((SDHCI_REG(SDHCI_SRS16) & SDHCI_SRS16_VS18) && /* 1.8v supported */
                (SDHCI_REG(SDHCI_SRS17) & (SDHCI_SRS17_DDR50 | /* DDR50, SDR104 or SDR50 supported */
                                  SDHCI_SRS17_SDR104 |
                                  SDHCI_SRS17_SDR50))) ? 1 : 0;
    #ifdef DEBUG_SDHCI
        wolfBoot_printf("sdcard_init: xpc:%d, si8r:%d, max_ma (3.3v:%d 1.8v:%d)\n",
            xpc, si8r, max_ma_3_3v, max_ma_1_8v);
    #endif
    }

    if (status == 0) {
        reg = 0;
        /* get operating conditions */
        status = sdcard_card_init(0, &reg);
        if (status == 0) {
            /* pick host and card operating voltages */
            if (reg & SDCARD_REG_OCR_3_3_3_4) { /* 3.3v - 3.4v */
                card_volts = SDCARD_REG_OCR_3_3_3_4;
            }
            else if (reg & SDCARD_REG_OCR_3_2_3_3) { /* 3.2v - 3.3v */
                card_volts = SDCARD_REG_OCR_3_2_3_3;
            }
            else if (reg & SDCARD_REG_OCR_3_1_3_2) { /* 3.1v - 3.2v */
                card_volts = SDCARD_REG_OCR_3_1_3_2;
            }
            else if (reg & SDCARD_REG_OCR_3_0_3_1) { /* 3.0v - 3.1v */
                card_volts = SDCARD_REG_OCR_3_0_3_1;
                ctrl_volts = SDHCI_SRS10_BVS_3_0V;
            }
            else if (reg & SDCARD_REG_OCR_2_9_3_0) { /* 2.9v - 3.0v */
                card_volts = SDCARD_REG_OCR_2_9_3_0;
                ctrl_volts = SDHCI_SRS10_BVS_3_0V;
            }
            else { /* default to v3.3 */
                card_volts = SDCARD_REG_OCR_3_3_3_4;
            }
            /* if needed change operating voltage and re-init */
            if (ctrl_volts != SDHCI_SRS10_BVS_3_3V) {
            #ifdef DEBUG_SDHCI
                wolfBoot_printf("sdcard_init: changing operating voltage to 3.0v\n");
            #endif
                status = sdcard_power_init_seq(ctrl_volts);
            }
        }
    }

    if (status == 0) {
        /* configure operating conditions */
        uint32_t cmd_arg = SDCARD_ACMD41_HCS;
        cmd_arg |= card_volts;
        if (si8r) {
            cmd_arg |= SDCARD_REG_OCR_S18RA;
        }
        if (xpc) {
            cmd_arg |= SDCARD_REG_OCR_XPC;
        }
    #ifdef DEBUG_SDHCI
        wolfBoot_printf("sdcard_init: sending OCR arg: 0x%08X\n", cmd_arg);
    #endif

        /* retry until OCR ready */
        do {
            status = sdcard_card_init(cmd_arg, &reg);
        } while (status == 0 && (reg & SDCARD_REG_OCR_READY) == 0);
    }

    if (status == 0) {
        /* Get card identification */
        status = sdhci_cmd(MMC_CMD2_ALL_SEND_CID, 0, SDHCI_RESP_R2);
    }

    if (status == 0) {
        /* Set relative address - SD card assigns its own RCA */
        status = sdhci_cmd(MMC_CMD3_SET_REL_ADDR, 0, SDHCI_RESP_R6);
    }

    if (status == 0) {
        g_rca = ((SDHCI_REG(SDHCI_SRS04) >> SD_RCA_SHIFT) & 0xFFFF);
    #ifdef DEBUG_SDHCI
        wolfBoot_printf("sdcard_init: rca: %d\n", g_rca);
    #endif
    }

    if (status == 0) {
        /* read CSD register from device */
        status = sdhci_cmd(MMC_CMD9_SEND_CSD, g_rca << SD_RCA_SHIFT,
            SDHCI_RESP_R2);
    }

    if (status == 0) {
        /* Get sector size and count */
        uint32_t csd_struct;
        uint32_t bl_len, c_size, c_size_mult;
        bl_len = sdhci_get_response_bits(22, 4);
        g_sector_size = (1U << bl_len);

        csd_struct = sdhci_get_response_bits(126, 2);
        switch (csd_struct) {
            case 0:
                c_size = sdhci_get_response_bits(62, 12);
                c_size_mult = sdhci_get_response_bits(47, 3);
                g_sector_count = (c_size + 1) << (c_size_mult + 2);
                break;
            case 1:
                c_size = sdhci_get_response_bits(48, 22);
                g_sector_count = (c_size + 1) << 10;
                break;
            default:
                /* invalid CSD structure */
                status = -1;
                break;
        }
    #ifdef DEBUG_SDHCI
        wolfBoot_printf("sdcard_init: csd_version: %d, sector: size %d count %d\n",
            csd_struct, g_sector_size, g_sector_count);
    #endif
    }

    if (status == 0) {
        /* select card */
        status = sdhci_cmd(MMC_CMD7_SELECT_CARD, g_rca << SD_RCA_SHIFT,
            SDHCI_RESP_R1B);
        if (status == DEVICE_BUSY) {
            status = sdhci_wait_busy(1);
        }
    }

    irq_restore = SDHCI_REG(SDHCI_SRS13);
    if (status == 0) {
        /* disable card insert interrupt while changing bus width to avoid false triggers */
        SDHCI_REG_SET(SDHCI_SRS13, (irq_restore & ~SDHCI_SRS13_CINT_SE));

        status = sdcard_set_bus_width(4);
    }

    if (status == 0) {
        /* Get SCR registers - 8 bytes */
        uint32_t scr_reg[SCR_REG_DATA_SIZE/sizeof(uint32_t)];
        status = sdhci_read(SD_ACMD51_SEND_SCR, 0, scr_reg,
            sizeof(scr_reg));
    }

    if (status == 0) {
        /* set UHS mode to SDR25 and driver strength to Type B */
        uint32_t card_access_mode = SDCARD_SWITCH_ACCESS_MODE_SDR25;
        status = sdcard_set_function(card_access_mode, 1);
        if (status == 0) {
            /* set driver strength */
            reg = SDHCI_REG(SDHCI_SRS15);
            reg &= ~SDHCI_SRS15_DSS_MASK;
            reg |= SDHCI_SRS15_DSS_TYPE_B; /* default */
            SDHCI_REG_SET(SDHCI_SRS15, reg);

            /* enable high speed */
            sdhci_reg_or(SDHCI_SRS10, SDHCI_SRS10_HSE);

            /* set UHS mode */
            reg = SDHCI_REG(SDHCI_SRS15);
            reg &= ~SDHCI_SRS15_UMS_MASK;
            reg |= SDHCI_SRS15_UMS_SDR25;
            SDHCI_REG_SET(SDHCI_SRS15, reg);
        }
    }

    if (status == 0) {
        sdhci_set_clock(SDHCI_CLK_50MHZ);
    }

    SDHCI_REG_SET(SDHCI_SRS13, irq_restore); /* re-enable interrupt */

    return status;
}

/* Set SD card bus width using ACMD6 */
static int sdcard_set_bus_width(uint32_t bus_width)
{
    int status;

    if (bus_width == g_bus_width) {
        /* nothing to do */
        return 0;
    }

    /* set bus width */
    status = sdhci_cmd(SD_CMD55_APP_CMD, g_rca << SD_RCA_SHIFT,
        SDHCI_RESP_R1);
    if (status == 0) {
        uint32_t cmd_arg = (bus_width == 4) ? 2 : 0;
        status = sdhci_cmd(SD_ACMD6_SET_BUS_WIDTH, cmd_arg, SDHCI_RESP_R1);
        if (status == 0) {
            /* change host bus width */
            if (bus_width == 4) {
                sdhci_reg_or(SDHCI_SRS10, SDHCI_SRS10_DTW);
            }
            else {
                sdhci_reg_and(SDHCI_SRS10, ~SDHCI_SRS10_DTW);
            }
        }
    }
    return status;
}

/* Check or set SD card switch function/group
 * Returns 0 if supported */
static int sdcard_send_switch_function(uint32_t mode, uint32_t function_number,
    uint32_t group_number)
{
    int status;
    uint32_t timeout = 4;
    uint32_t cmd_arg;
    uint32_t func_status[64/sizeof(uint32_t)]; /* fixed 512 bits */
    uint8_t* p_func_status = (uint8_t*)func_status;

    if (group_number > 6 || function_number > 15) {
        return -1; /* Invalid group or function number */
    }

    cmd_arg = (function_number << ((group_number - 1) * 4));
    do {
        /* first run check to see if function is supported */
        status = sdhci_read(SD_CMD6_SWITCH_FUNC,
            (mode | cmd_arg),
            func_status, sizeof(func_status));
        if (status == 0) {
            /* check if busy */
            /* data structure version 368:375
             * (0=supported only, 1=supported and busy) */
            if (p_func_status[17] == 1) {
                /* busy status: group 1 272:287 */
                if ((p_func_status[29 -
                        ((group_number-1)*2)] & (1 << function_number))) {
                    continue; /* busy */
                }
            }

            /* supported: group 1 415:400 */
            if ((p_func_status[13 -
                    ((group_number-1)*2)] & (1 << function_number))) {
                status = 0; /* supported */
            }
            else {
                status = -1; /* not supported */
            }
            break;
        }
    } while (status == 0 && --timeout > 0); /* retry until function not busy */
    return status;
}

/* Set SD card function (check first, then switch) */
static int sdcard_set_function(uint32_t function_number, uint32_t group_number)
{
    /* send check first */
    int status = sdcard_send_switch_function(SDCARD_SWITCH_FUNC_MODE_CHECK,
        function_number, group_number);
    if (status == 0) {
        /* send switch function */
        status = sdcard_send_switch_function(SDCARD_SWITCH_FUNC_MODE_SWITCH,
            function_number, group_number);
    }
    return status;
}

#endif /* DISK_SDCARD */

/* ============================================================================
 * eMMC Specific Functions
 * ============================================================================ */

#ifdef DISK_EMMC

/* Send CMD1 (SEND_OP_COND) for eMMC and wait for device ready
 * Returns 0 on success, negative on error */
static int emmc_send_op_cond(uint32_t ocr_arg, uint32_t *ocr_reg)
{
    int status;
    uint32_t timeout = 1000; /* retry count */
    uint32_t response;

    /* First CMD1 to query supported voltages */
    status = sdhci_cmd(MMC_CMD1_SEND_OP_COND, 0, SDHCI_RESP_R3);
    if (status != 0) {
        wolfBoot_printf("eMMC: CMD1 query failed\n");
        return status;
    }

    /* Get OCR from response */
    response = SDHCI_REG(SDHCI_SRS04);
#ifdef DEBUG_SDHCI
    wolfBoot_printf("eMMC: Initial OCR: 0x%08X\n", response);
#endif

    /* Loop sending CMD1 with operating conditions until device is ready */
    do {
        status = sdhci_cmd(MMC_CMD1_SEND_OP_COND, ocr_arg, SDHCI_RESP_R3);
        if (status != 0) {
            wolfBoot_printf("eMMC: CMD1 failed\n");
            return status;
        }

        response = SDHCI_REG(SDHCI_SRS04);

        /* Check if device is ready (busy bit cleared = ready) */
        if (response & MMC_OCR_BUSY_BIT) {
            /* Device is ready */
            if (ocr_reg != NULL) {
                *ocr_reg = response;
            }
#ifdef DEBUG_SDHCI
            wolfBoot_printf("eMMC: Device ready, OCR: 0x%08X\n", response);
#endif
            return 0;
        }

        /* Small delay between retries */
        udelay(10);

    } while (--timeout > 0);

    wolfBoot_printf("eMMC: Timeout waiting for device ready\n");
    return -1;
}

/* Set eMMC bus width using CMD6 SWITCH command
 * width: MMC_EXT_CSD_WIDTH_1BIT, MMC_EXT_CSD_WIDTH_4BIT, or MMC_EXT_CSD_WIDTH_8BIT
 * Returns 0 on success */
static int emmc_set_bus_width(uint32_t width)
{
    int status;
    uint32_t cmd_arg;

    /* Build CMD6 argument: Access=Write Byte, Index=183 (BUS_WIDTH), Value=width */
    cmd_arg = MMC_DW_CSD | (width << 8);

#ifdef DEBUG_SDHCI
    wolfBoot_printf("eMMC: Setting bus width to %d (CMD6 arg: 0x%08X)\n",
        (width == MMC_EXT_CSD_WIDTH_4BIT) ? 4 :
        (width == MMC_EXT_CSD_WIDTH_8BIT) ? 8 : 1, cmd_arg);
#endif

    /* Send CMD6 SWITCH */
    status = sdhci_cmd(MMC_CMD6_SWITCH, cmd_arg, SDHCI_RESP_R1B);
    if (status == DEVICE_BUSY) {
        status = sdhci_wait_busy(1);
    }

    if (status != 0) {
        wolfBoot_printf("eMMC: Set bus width failed\n");
        return status;
    }

    /* Update host controller bus width */
    if (width == MMC_EXT_CSD_WIDTH_4BIT || width == MMC_EXT_CSD_WIDTH_4BIT_DDR) {
        sdhci_reg_or(SDHCI_SRS10, SDHCI_SRS10_DTW);
        sdhci_reg_and(SDHCI_SRS10, ~SDHCI_SRS10_EDTW);
        g_bus_width = 4;
    }
    else if (width == MMC_EXT_CSD_WIDTH_8BIT || width == MMC_EXT_CSD_WIDTH_8BIT_DDR) {
        sdhci_reg_or(SDHCI_SRS10, SDHCI_SRS10_EDTW);
        g_bus_width = 8;
    }
    else {
        uint32_t reg = SDHCI_REG(SDHCI_SRS10);
        reg &= ~(SDHCI_SRS10_DTW | SDHCI_SRS10_EDTW);
        SDHCI_REG_SET(SDHCI_SRS10, reg);
        g_bus_width = 1;
    }

    return 0;
}

/* Full eMMC card initialization sequence
 * Returns 0 on success */
static int emmc_card_full_init(void)
{
    int status;
    uint32_t ocr_reg;

    /* Set power to 3.3v */
    status = sdhci_set_power(SDHCI_SRS10_BVS_3_3V);
    if (status != 0) {
        wolfBoot_printf("eMMC: Failed to set power\n");
        return status;
    }

    /* Send CMD0 (GO_IDLE) to reset eMMC */
    status = sdhci_cmd(MMC_CMD0_GO_IDLE, 0, SDHCI_RESP_NONE);
    if (status != 0) {
        wolfBoot_printf("eMMC: CMD0 failed\n");
        return status;
    }

    /* Small delay after reset */
    udelay(100);

    /* Send CMD1 with operating conditions (3.3V, sector mode) */
    status = emmc_send_op_cond(MMC_DEVICE_3_3V_VOLT_SET, &ocr_reg);
    if (status != 0) {
        return status;
    }

    /* CMD2 - Get CID (card identification) */
    status = sdhci_cmd(MMC_CMD2_ALL_SEND_CID, 0, SDHCI_RESP_R2);
    if (status != 0) {
        wolfBoot_printf("eMMC: CMD2 failed\n");
        return status;
    }

    /* CMD3 - Set relative address (host assigns RCA for eMMC) */
    g_rca = MMC_EMMC_RCA_DEFAULT;
    status = sdhci_cmd(MMC_CMD3_SET_REL_ADDR, g_rca << SD_RCA_SHIFT,
        SDHCI_RESP_R1);
    if (status != 0) {
        wolfBoot_printf("eMMC: CMD3 failed\n");
        return status;
    }

#ifdef DEBUG_SDHCI
    wolfBoot_printf("eMMC: RCA set to %d\n", g_rca);
#endif

    /* CMD9 - Get CSD (card-specific data) */
    status = sdhci_cmd(MMC_CMD9_SEND_CSD, g_rca << SD_RCA_SHIFT,
        SDHCI_RESP_R2);
    if (status != 0) {
        wolfBoot_printf("eMMC: CMD9 failed\n");
        return status;
    }

    /* Parse CSD to get sector size/count */
    {
        uint32_t csd_struct, c_size;
        uint32_t bl_len = sdhci_get_response_bits(22, 4);
        g_sector_size = (1U << bl_len);

        csd_struct = sdhci_get_response_bits(126, 2);
        /* eMMC typically uses CSD version 3 (EXT_CSD) with fixed 512-byte sectors */
        if (csd_struct >= 2) {
            /* For eMMC, actual capacity is in EXT_CSD, use default for now */
            g_sector_size = 512;
            g_sector_count = 0; /* Will be read from EXT_CSD if needed */
        }
        else {
            /* Legacy CSD parsing */
            c_size = sdhci_get_response_bits(48, 22);
            g_sector_count = (c_size + 1) << 10;
        }

#ifdef DEBUG_SDHCI
        wolfBoot_printf("eMMC: CSD version %d, sector size %d\n",
            csd_struct, g_sector_size);
#endif
    }

    /* CMD7 - Select card */
    status = sdhci_cmd(MMC_CMD7_SELECT_CARD, g_rca << SD_RCA_SHIFT,
        SDHCI_RESP_R1B);
    if (status == DEVICE_BUSY) {
        status = sdhci_wait_busy(1);
    }
    if (status != 0) {
        wolfBoot_printf("eMMC: CMD7 failed\n");
        return status;
    }

    /* Set bus width to 4-bit */
    status = emmc_set_bus_width(MMC_EXT_CSD_WIDTH_4BIT);
    if (status != 0) {
        wolfBoot_printf("eMMC: Set bus width failed, continuing with 1-bit\n");
        /* Non-fatal, continue with 1-bit */
    }

    /* Set clock to 25MHz for legacy mode */
    sdhci_set_clock(SDHCI_CLK_25MHZ);

    /* Enable high speed if desired (optional for legacy mode) */
    sdhci_reg_or(SDHCI_SRS10, SDHCI_SRS10_HSE);

    return 0;
}

#endif /* DISK_EMMC */

/* ============================================================================
 * Data Transfer
 * ============================================================================ */

/* Transfer direction for sdhci_transfer() */
#define SDHCI_DIR_READ  1
#define SDHCI_DIR_WRITE 0

/* Unified internal transfer function for read and write operations
 * dir: SDHCI_DIR_READ or SDHCI_DIR_WRITE
 * cmd_index: command to send (e.g., MMC_CMD17_READ_SINGLE, MMC_CMD25_WRITE_MULTIPLE)
 * block_addr: block address for the transfer
 * buf: data buffer (source for write, destination for read)
 * sz: transfer size in bytes
 * Returns 0 on success, negative on error */
static int sdhci_transfer(int dir, uint32_t cmd_index, uint32_t block_addr,
    uint32_t* buf, uint32_t sz)
{
    int status;
    uint32_t block_count, reg, cmd_reg, bcr_reg;
    int is_multi_block;

    /* Determine if multi-block operation */
    is_multi_block = (dir == SDHCI_DIR_READ) ?
        (cmd_index == MMC_CMD18_READ_MULTIPLE) :
        (cmd_index == MMC_CMD25_WRITE_MULTIPLE);

    /* Get block count (round up) */
    block_count = (sz + (SDHCI_BLOCK_SIZE - 1)) / SDHCI_BLOCK_SIZE;

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_%s: cmd:%d, addr:0x%08X, buf:%p, sz:%d (%d blocks)\n",
        (dir == SDHCI_DIR_READ) ? "read" : "write",
        cmd_index, block_addr, buf, sz, block_count);
#endif

    /* Wait for idle */
    status = sdhci_wait_busy(0);
    if (status != 0) {
    #ifdef DEBUG_SDHCI
        wolfBoot_printf("sdhci_transfer: wait busy error\n");
    #endif
        return status;
    }

    /* Reset data and command lines */
    sdhci_reset_lines();

    /* Wait for command and data line busy to clear */
    while ((SDHCI_REG(SDHCI_SRS09) & (SDHCI_SRS09_CICMD | SDHCI_SRS09_CIDAT)) != 0);

    /* Setup default transfer block count and block size */
    bcr_reg = (block_count << SDHCI_SRS01_BCCT_SHIFT) | sz;

    /* Build command register:
     * - CIDX: Command index
     * - DPS: Data present
     * - DTDS: Data transfer direction (1=read, 0=write)
     * - BCE, RECE, RID: Block count enable, response error check, response interrupt disable
     * - RESP_48, CRCCE, CICE: 48-bit response, CRC check, index check */
    cmd_reg = ((cmd_index << SDHCI_SRS03_CIDX_SHIFT) |
        SDHCI_SRS03_DPS |
        SDHCI_SRS03_BCE | SDHCI_SRS03_RECE | SDHCI_SRS03_RID |
        SDHCI_SRS03_RESP_48 | SDHCI_SRS03_CRCCE | SDHCI_SRS03_CICE);

    /* Set direction bit for read */
    if (dir == SDHCI_DIR_READ) {
        cmd_reg |= SDHCI_SRS03_DTDS;
    }

    /* Handle special case for SD_ACMD51_SEND_SCR (read only) */
    if (cmd_index == SD_ACMD51_SEND_SCR) {
        status = sdhci_cmd(SD_CMD16, sz, SDHCI_RESP_R1);
        if (status == 0) {
            status = sdhci_cmd(SD_CMD55_APP_CMD, (g_rca << SD_RCA_SHIFT),
                SDHCI_RESP_R1);
        }
        status = 0; /* ignore error */
    }
    else if (is_multi_block) {
        cmd_reg |= SDHCI_SRS03_MSBS; /* enable multi-block select */

    #ifndef SDHCI_SDMA_DISABLED
        if (sz >= SDHCI_DMA_THRESHOLD) { /* use DMA for large transfers */
            cmd_reg |= SDHCI_SRS03_DMAE; /* enable DMA */

            bcr_reg = (block_count << SDHCI_SRS01_BCCT_SHIFT) |
                SDHCI_DMA_BUFF_BOUNDARY | SDHCI_BLOCK_SIZE;

            /* SDMA mode with Host Version 4 enable.
             * HV4E is required for SDMA to use the 64-bit address registers
             * (SRS22/SRS23) instead of the legacy 32-bit register (SRS00).
             * A64 is cleared in SRS15 to use 32-bit DMA addressing. */
            sdhci_reg_or(SDHCI_SRS10, SDHCI_SRS10_DMA_SDMA);
            sdhci_reg_or(SDHCI_SRS15, SDHCI_SRS15_HV4E);
            sdhci_reg_and(SDHCI_SRS15, ~SDHCI_SRS15_A64);
            /* Set SDMA address */
            SDHCI_REG_SET(SDHCI_SRS22, (uint32_t)(uintptr_t)buf);
            SDHCI_REG_SET(SDHCI_SRS23, (uint32_t)(((uint64_t)(uintptr_t)buf) >> 32));

            /* Enable SDMA interrupts */
            sdhci_enable_sdma_interrupts();
        }
        else
    #endif /* !SDHCI_SDMA_DISABLED */
        {
            bcr_reg = (block_count << SDHCI_SRS01_BCCT_SHIFT) |
                SDHCI_BLOCK_SIZE;
        }
    }

    /* Execute command */
    SDHCI_REG_SET(SDHCI_SRS01, bcr_reg);
    SDHCI_REG_SET(SDHCI_SRS02, block_addr);
    SDHCI_REG_SET(SDHCI_SRS03, cmd_reg);

    /* Handle data transfer */
    if (cmd_reg & SDHCI_SRS03_DMAE) {
        /* DMA mode with interrupt support */
        while (1) {
            status = sdhci_wait_irq(SDHCI_IRQ_FLAG_TC, 0x00FFFFFF);
            if (status != 0) {
                wolfBoot_printf("sdhci_transfer: SDMA timeout/error\n");
                status = -1;
                break;
            }
            if (g_mmc_irq_status & SDHCI_IRQ_FLAG_TC) {
                g_mmc_irq_status &= ~SDHCI_IRQ_FLAG_TC;
                break;
            }
        }
        sdhci_disable_sdma_interrupts();
    }
    else {
        /* Blocking mode - buffer ready flag differs for read vs write */
        uint32_t buf_ready_flag = (dir == SDHCI_DIR_READ) ?
            SDHCI_SRS12_BRR : SDHCI_SRS12_BWR;

        while (sz > 0) {
            /* Wait for buffer ready (or error) */
            while (((reg = SDHCI_REG(SDHCI_SRS12)) &
                (buf_ready_flag | SDHCI_SRS12_EINT)) == 0);

            if (reg & buf_ready_flag) {
                uint32_t i, xfer_sz = (sz > SDHCI_BLOCK_SIZE) ?
                    SDHCI_BLOCK_SIZE : sz;
                for (i = 0; i < xfer_sz; i += 4) {
                    if (dir == SDHCI_DIR_READ) {
                        *buf = SDHCI_REG(SDHCI_SRS08);
                    } else {
                        SDHCI_REG_SET(SDHCI_SRS08, *buf);
                    }
                    buf++;
                }
                sz -= xfer_sz;

            #ifdef DISK_EMMC /* workaround for eMMC only */
                /* For multi-block READ: clear BRR by writing 1 to it (W1C),
                 * then the outer loop waits for BRR to be set again when the
                 * next block's data is available from the card.
                 * For WRITE: BWR auto-clears when buffer full, don't touch. */
                if (sz > 0 && dir == SDHCI_DIR_READ) {
                    SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_BRR);
                }
            #endif
            }
            if (reg & SDHCI_SRS12_EINT) {
                break; /* error */
            }
        }

        /* For write: wait for transfer complete before checking status */
        if (dir == SDHCI_DIR_WRITE) {
            while (((reg = SDHCI_REG(SDHCI_SRS12)) &
                (SDHCI_SRS12_TC | SDHCI_SRS12_EINT)) == 0);
        }
    }

    /* Check for errors */
    reg = SDHCI_REG(SDHCI_SRS12);
    if ((reg & SDHCI_SRS12_ERR_STAT) == 0) {
        /* If multi-block, send CMD12 to stop transfer */
        if (is_multi_block) {
        #ifdef DISK_EMMC
            uint32_t stop_arg = 0;
        #else
            uint32_t stop_arg = (g_rca << SD_RCA_SHIFT);
        #endif

            SDHCI_REG_SET(SDHCI_SRS12, SDHCI_SRS12_TC); /* Clear transfer complete */

            /* Send stop multi-block transfer */
            status = sdhci_send_cmd_internal(SDHCI_SRS03_CMD_ABORT,
                MMC_CMD12_STOP_TRANS, stop_arg, SDHCI_RESP_R1B);
            /* Card may be busy programming data after CMD12 */
            if (status == DEVICE_BUSY) {
                status = sdhci_wait_busy(1);
            }
            if (status != 0) {
                wolfBoot_printf("sdhci_transfer: CMD12 error\n");
            }
        }
        if (status == 0) {
            status = sdhci_wait_busy(0);
        }
    }
    else {
        wolfBoot_printf("sdhci_transfer: error SRS12: 0x%08X\n", reg);
        status = -1;
    }

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_%s: status: %d\n",
        (dir == SDHCI_DIR_READ) ? "read" : "write", status);
#endif

    /* Clear status interrupts (except current limit, card interrupt/removal/insert) */
    sdhci_reset_lines();
    SDHCI_REG_SET(SDHCI_SRS12, ~(SDHCI_SRS12_ECL | SDHCI_SRS12_CINT |
                      SDHCI_SRS12_CR | SDHCI_SRS12_CIN));

    return status;
}

/* Public API: Read from MMC/SD card */
int sdhci_read(uint32_t cmd_index, uint32_t block_addr, uint32_t* dst, uint32_t sz)
{
    return sdhci_transfer(SDHCI_DIR_READ, cmd_index, block_addr, dst, sz);
}

/* Public API: Write to MMC/SD card */
int sdhci_write(uint32_t cmd_index, uint32_t block_addr, const uint32_t* src, uint32_t sz)
{
    return sdhci_transfer(SDHCI_DIR_WRITE, cmd_index, block_addr, (uint32_t*)src, sz);
}

/* ============================================================================
 * Controller Initialization
 * ============================================================================ */

int sdhci_init(void)
{
    int status = 0;
    uint32_t reg, cap;

    /* Call platform-specific initialization (clocks, resets, pin mux) */
    sdhci_platform_init();

    /* Reset the host controller */
    sdhci_reg_or(SDHCI_HRS00, SDHCI_HRS00_SWR);
    /* Bit will clear when reset is done */
    while ((SDHCI_REG(SDHCI_HRS00) & SDHCI_HRS00_SWR) != 0);

    /* Set debounce period to ~15ms (platform-specific value may be different) */
    SDHCI_REG_SET(SDHCI_HRS01, (0x300000UL << SDHCI_HRS01_DP_SHIFT) &
                      SDHCI_HRS01_DP_MASK);

    /* Select card mode */
    reg = SDHCI_REG(SDHCI_HRS06);
    reg &= ~SDHCI_HRS06_EMM_MASK;
#ifdef DISK_EMMC
    reg |= SDHCI_HRS06_MODE_LEGACY;  /* eMMC Legacy mode */
    wolfBoot_printf("SDHCI: eMMC mode\n");
#else
    reg |= SDHCI_HRS06_MODE_SD;      /* SD card mode */
    wolfBoot_printf("SDHCI: SDCard mode\n");
#endif
    SDHCI_REG_SET(SDHCI_HRS06, reg);

    /* Let platform set bus mode (SD vs eMMC) */
#ifdef DISK_EMMC
    sdhci_platform_set_bus_mode(1);
#else
    sdhci_platform_set_bus_mode(0);
#endif

    /* Clear error/interrupt status */
    SDHCI_REG_SET(SDHCI_SRS12, (SDHCI_SRS12_NORM_STAT | SDHCI_SRS12_ERR_STAT));

    /* Check and enable 64-bit DMA support */
    reg = SDHCI_REG(SDHCI_SRS15);
    cap = SDHCI_REG(SDHCI_SRS16);
    if (cap & SDHCI_SRS16_A64S) {
        reg |= SDHCI_SRS15_A64;
        reg |= SDHCI_SRS15_HV4E;
        SDHCI_REG_SET(SDHCI_SRS15, reg);
    }
    /* Set all status enables - 0xbff40ff */
    SDHCI_REG_SET(SDHCI_SRS13, (
        SDHCI_SRS13_ETUNE_SE | SDHCI_SRS13_EADMA_SE | SDHCI_SRS13_EAC_SE |
        SDHCI_SRS13_ECL_SE | SDHCI_SRS13_EDEB_SE |
        SDHCI_SRS13_EDCRC_SE | SDHCI_SRS13_EDT_SE |
        SDHCI_SRS13_ECI_SE | SDHCI_SRS13_ECEB_SE | SDHCI_SRS13_ECCRC_SE |
        SDHCI_SRS13_ECT_SE | SDHCI_SRS13_RTUNE_SE |
        SDHCI_SRS13_INT_ONC | SDHCI_SRS13_INT_ONB | SDHCI_SRS13_INT_ONA |
        SDHCI_SRS13_CR_SE | SDHCI_SRS13_CIN_SE |
        SDHCI_SRS13_BRR_SE | SDHCI_SRS13_BWR_SE | SDHCI_SRS13_DMAINT_SE |
        SDHCI_SRS13_BGE_SE | SDHCI_SRS13_TC_SE | SDHCI_SRS13_CC_SE |
        SDHCI_SRS13_ERSP_SE | SDHCI_SRS13_CQINT_SE
    ));
    /* Clear all signal enables (will be enabled per-transfer for SDMA) */
    SDHCI_REG_SET(SDHCI_SRS14, 0);

    /* Initialize platform interrupts (PLIC/NVIC/GIC/etc.) */
    sdhci_platform_irq_init();

    /* Set initial timeout to 500ms */
    status = sdhci_set_timeout(SDHCI_INIT_TIMEOUT_US);
    if (status != 0) {
        return status;
    }
    /* Turn off host controller power */
    (void)sdhci_set_power(0);

    /* check if card inserted and stable */
    reg = SDHCI_REG(SDHCI_SRS09);
    if ((reg & SDHCI_SRS09_CSS) == 0) {
        /* card not inserted or not stable */
        return -1;
    }
#ifdef DISK_SDCARD
    /* For SD card: check if card is inserted
     * (eMMC is soldered on board, skip this check) */
    if ((reg & SDHCI_SRS09_CI) == 0) {
        /* card not inserted */
        return -1;
    }
#endif

    /* Start in 1-bit bus mode */
    reg = SDHCI_REG(SDHCI_SRS10);
    reg &= ~(SDHCI_SRS10_EDTW | SDHCI_SRS10_DTW);
    SDHCI_REG_SET(SDHCI_SRS10, reg);

    /* Setup 400khz starting clock */
    sdhci_set_clock(SDHCI_CLK_400KHZ);

#ifdef DISK_EMMC
    /* Run full eMMC card initialization */
    status = emmc_card_full_init();
    if (status != 0) {
        wolfBoot_printf("eMMC: Card initialization failed\n");
        return status;
    }

#else /* DISK_SDCARD */
    /* Run full SD card initialization */
    status = sdcard_card_full_init();
    if (status != 0) {
        wolfBoot_printf("SD Card: Card initialization failed\n");
        return status;
    }

#endif /* DISK_EMMC */

    /* Common finalization for both eMMC and SD */
    if (status == 0) {
        /* Set data timeout to 3000ms */
        status = sdhci_set_timeout(SDHCI_DATA_TIMEOUT_US);
    }

    wolfBoot_printf("sdhci_init: %s status: %d\n",
    #ifdef DISK_EMMC
        "eMMC"
    #else
        "SD"
    #endif
        , status
    );

    return status;
}

/* ============================================================================
 * disk.h Interface
 * ============================================================================ */

/* returns number of bytes read on success or negative on error */
/* start may not be block aligned and count may not be block multiple */
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    int status = 0;
    uint32_t read_sz, block_addr;
    uint32_t tmp_block[SDHCI_BLOCK_SIZE/sizeof(uint32_t)];
    uint32_t start_offset = (start % SDHCI_BLOCK_SIZE);
    (void)drv; /* only one drive supported */

#ifdef DEBUG_SDHCI
    wolfBoot_printf("disk_read: drv:%d, start:%llu, count:%d, dst:%p\n",
        drv, start, count, buf);
#endif

    while (count > 0) {
        block_addr = (start / SDHCI_BLOCK_SIZE);
        read_sz = count;
        if (read_sz > SDHCI_BLOCK_SIZE) {
            read_sz = SDHCI_BLOCK_SIZE;
        }
        if (read_sz < SDHCI_BLOCK_SIZE || /* last partial */
            start_offset != 0 ||            /* start not block aligned */
            ((uintptr_t)buf % 4) != 0)      /* buf not 4-byte aligned */
        {
            /* block read to temporary buffer */
            status = sdhci_read(MMC_CMD17_READ_SINGLE, block_addr,
                tmp_block, SDHCI_BLOCK_SIZE);
            if (status == 0) {
                uint8_t* tmp_buf = (uint8_t*)tmp_block;
                memcpy(buf, tmp_buf + start_offset, read_sz);
                start_offset = 0;
            }
        }
        else {
            /* direct full block(s) read */
            uint32_t blocks = (count / SDHCI_BLOCK_SIZE);
            read_sz = (blocks * SDHCI_BLOCK_SIZE);
            status = sdhci_read(blocks > 1 ?
                                MMC_CMD18_READ_MULTIPLE :
                                MMC_CMD17_READ_SINGLE,
                block_addr, (uint32_t*)buf, read_sz);
        }
        if (status != 0) {
            break;
        }

        start += read_sz;
        buf += read_sz;
        count -= read_sz;
    }
    return status;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    int status = 0;
    uint32_t write_sz, block_addr;
    uint32_t tmp_block[SDHCI_BLOCK_SIZE/sizeof(uint32_t)];
    uint32_t start_offset = (start % SDHCI_BLOCK_SIZE);
    (void)drv; /* only one drive supported */

#ifdef DEBUG_SDHCI
    wolfBoot_printf("disk_write: drv:%d, start:%llu, count:%d, src:%p\n",
        drv, start, count, buf);
#endif

    while (count > 0) {
        block_addr = (start / SDHCI_BLOCK_SIZE);
        write_sz = count;
        if (write_sz > SDHCI_BLOCK_SIZE) {
            write_sz = SDHCI_BLOCK_SIZE;
        }
        if (write_sz < SDHCI_BLOCK_SIZE || /* partial block */
            start_offset != 0 ||             /* start not block aligned */
            ((uintptr_t)buf % 4) != 0)       /* buf not 4-byte aligned */
        {
            /* read-modify-write for partial block */
            status = sdhci_read(MMC_CMD17_READ_SINGLE, block_addr,
                tmp_block, SDHCI_BLOCK_SIZE);
            if (status == 0) {
                uint8_t* tmp_buf = (uint8_t*)tmp_block;
                memcpy(tmp_buf + start_offset, buf, write_sz);
                status = sdhci_write(MMC_CMD24_WRITE_SINGLE, block_addr,
                    tmp_block, SDHCI_BLOCK_SIZE);
                start_offset = 0;
            }
        }
        else {
            /* direct full block(s) write */
            uint32_t blocks = (count / SDHCI_BLOCK_SIZE);
            write_sz = (blocks * SDHCI_BLOCK_SIZE);
            status = sdhci_write(blocks > 1 ?
                                MMC_CMD25_WRITE_MULTIPLE :
                                MMC_CMD24_WRITE_SINGLE,
                block_addr, (const uint32_t*)buf, write_sz);
        }
        if (status != 0) {
            break;
        }

        start += write_sz;
        buf += write_sz;
        count -= write_sz;
    }
    return status;
}

#ifdef DISK_TEST

/* disk_test: Test read/write functionality
 * Tests sizes: 128, 512, 1024, 512KB (524288), 1MB (1048576) bytes
 * Returns 0 on success, negative on failure */
static int disk_test(int drv)
{
    int status = 0;
    int test_num = 0;
    uint32_t i;
    static const uint32_t test_sizes[] = {
        128,            /* partial block */
        512,            /* single block */
        1024,           /* two blocks */
        512 * 1024,     /* 512KB - DMA threshold */
        1024 * 1024     /* 1MB */
    };
    /* Use platform-defined buffer address */
    uint32_t* tmp_buf32 = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
    uint8_t* tmp_buf = (uint8_t*)tmp_buf32;

    wolfBoot_printf("disk_test: Starting tests at block %d (buf @ %p)\n",
        DISK_TEST_BLOCK_ADDR, tmp_buf);

    for (test_num = 0; test_num < (int)(sizeof(test_sizes)/sizeof(test_sizes[0])); test_num++) {
        uint32_t test_sz = test_sizes[test_num];
        uint64_t test_addr = (uint64_t)DISK_TEST_BLOCK_ADDR * SDHCI_BLOCK_SIZE;
        uint32_t blocks_needed = (test_sz + SDHCI_BLOCK_SIZE - 1) / SDHCI_BLOCK_SIZE;

        wolfBoot_printf("  Test %d: size=%u bytes (%u blocks)... ",
            test_num + 1, test_sz, blocks_needed);

        /* Fill with test pattern */
        for (i = 0; i < test_sz / sizeof(uint32_t); i++) {
            tmp_buf32[i] = (test_num << 24) | i;
        }
        /* Handle remaining bytes for non-word-aligned sizes */
        for (i = (test_sz / sizeof(uint32_t)) * sizeof(uint32_t); i < test_sz; i++) {
            tmp_buf[i] = (uint8_t)((test_num << 4) | (i & 0x0F));
        }

        /* Write */
        status = disk_write(drv, test_addr, test_sz, tmp_buf);
        if (status != 0) {
            wolfBoot_printf("FAIL (write error %d)\n", status);
            continue;
        }

        /* Clear buffer */
        memset(tmp_buf, 0, test_sz);

        /* Read back */
        status = disk_read(drv, test_addr, test_sz, tmp_buf);
        if (status != 0) {
            wolfBoot_printf("FAIL (read error %d)\n", status);
            continue;
        }

        /* Verify pattern */
        for (i = 0; i < test_sz / sizeof(uint32_t); i++) {
            uint32_t expected = (test_num << 24) | i;
            if (tmp_buf32[i] != expected) {
                wolfBoot_printf("FAIL (verify @ word %u: got 0x%08X, expected 0x%08X)\n",
                    i, tmp_buf32[i], expected);
                status = -1;
                break;
            }
        }
        /* Verify remaining bytes for non-word-aligned sizes */
        if (status == 0) {
            for (i = (test_sz / sizeof(uint32_t)) * sizeof(uint32_t); i < test_sz; i++) {
                uint8_t expected = (uint8_t)((test_num << 4) | (i & 0x0F));
                if (tmp_buf[i] != expected) {
                    wolfBoot_printf("FAIL (verify @ byte %u: got 0x%02X, expected 0x%02X)\n",
                        i, tmp_buf[i], expected);
                    status = -1;
                    break;
                }
            }
        }

        if (status == 0) {
            wolfBoot_printf("PASS\n");
        }
    }

    wolfBoot_printf("disk_test: Complete\n");
    return status;
}
#endif /* DISK_TEST */

int disk_init(int drv)
{
    int r = sdhci_init();
    if (r != 0) {
        wolfBoot_printf("Failed to initialize SDHCI\n");
    }
    (void)drv;
#ifdef DISK_TEST
    disk_test(drv);
#endif
    return r;
}

void disk_close(int drv)
{
    (void)drv;
}

#endif /* DISK_SDCARD || DISK_EMMC */
