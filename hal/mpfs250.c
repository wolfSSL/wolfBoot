/* mpfs250.c
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

/* Microchip PolarFire SoC MPFS250T HAL for wolfBoot */
/* Supports:
 *   RISC-V 64-bit architecture
 *   External flash operations
 *   UART communication
 *   System initialization
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <target.h>

#include "mpfs250.h"
#include "image.h"
#ifndef ARCH_RISCV64
#   error "wolfBoot mpfs250 HAL: wrong architecture selected. Please compile with ARCH=RISCV64."
#endif

#include "printf.h"
#include "loader.h"
#include "disk.h"

/* Placeholder functions - to be implemented */
void hal_init(void)
{

}

int hal_dts_fixup(void* dts_addr)
{
    /* TODO: Consider FDT fixups:
     * ethernet0: local-mac-address {0x00, 0x04, 0xA3, SERIAL2, SERIAL1, SERIAL0} */
    (void)dts_addr;
    return 0;
}
void hal_prepare_boot(void)
{

}

void RAMFUNCTION hal_flash_unlock(void)
{

}

void RAMFUNCTION hal_flash_lock(void)
{

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

#ifdef EXT_FLASH
/* External flash support */
void ext_flash_lock(void)
{
    /* TODO: Lock external flash */
}

void ext_flash_unlock(void)
{
    /* TODO: Unlock external flash */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* TODO: Write to external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    /* TODO: Read from external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    /* TODO: Erase external flash sectors */
    (void)address;
    (void)len;
    return 0;
}
#endif /* EXT_FLASH */

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif

int mmc_set_timeout(uint32_t timeout_us)
{
    uint32_t reg, i, tcfclk, tcfclk_mhz, tcfclk_khz, timeout_val, dtcv;

    /* read capabilities to determine timeout clock frequency and unit (MHz or kHz) */
    reg = EMMC_SD_SRS16;
    tcfclk_khz = (reg * EMMC_SD_SRS16_TCF_MASK);

    if (((reg & EMMC_SD_SRS16_TCU) == 0) && (timeout_us < 1000)) {
        /* invalid timeout_us value */
        return -1;
    }
    if (tcfclk_khz == 0) {
        /* reported timeout clock frequency is 0 */
        return -1;
    }

    if ((reg & EMMC_SD_SRS16_TCU) != 0) {
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

    /* set the data timeout counter value */
    reg = EMMC_SD_SRS11;
    reg &= ~EMMC_SD_SRS11_DTCV_MASK;
    reg |= (timeout_val << EMMC_SD_SRS11_DTCV_SHIFT) & EMMC_SD_SRS11_DTCV_MASK;
    EMMC_SD_SRS11 = reg;

    return 0;
}

#define DEFAULT_DELAY 0x00FFFFFF
void mmc_delay(uint32_t delay)
{
    while (delay--) {
        asm volatile("nop");
    }
}

/* voltage values:
 *  0 = off
 *  EMMC_SD_SRS10_BVS_1_8V
 *  EMMC_SD_SRS10_BVS_3_0V
 *  EMMC_SD_SRS10_BVS_3_3V
 */
int mmc_set_power(uint32_t voltage)
{
    uint32_t reg;

    /* disable bus power */
    reg = EMMC_SD_SRS10;
    reg &= ~EMMC_SD_SRS10_BP;
    EMMC_SD_SRS10 = reg;

    if (voltage != 0) {
        /* read voltage capabilities */
        uint32_t cap2 = EMMC_SD_SRS16;

        /* select voltage (if capable) */
        reg &= ~EMMC_SD_SRS10_BVS_MASK;
        if (voltage == EMMC_SD_SRS10_BVS_1_8V && (cap2 & EMMC_SD_SRS16_VS18)) {
            reg |= EMMC_SD_SRS10_BP | EMMC_SD_SRS10_BVS_1_8V;
        }
        else if (voltage == EMMC_SD_SRS10_BVS_3_0V && (cap2 & EMMC_SD_SRS16_VS30)) {
            reg |= EMMC_SD_SRS10_BVS_3_0V;
        }
        else if (voltage == EMMC_SD_SRS10_BVS_3_3V && (cap2 & EMMC_SD_SRS16_VS33)) {
            reg |= EMMC_SD_SRS10_BVS_3_3V;
        }
        else {
            /* voltage not supported */
            return -1;
        }
        EMMC_SD_SRS10 = reg;
        mmc_delay(DEFAULT_DELAY); /* delay after bus power is applied */
    }
    return 0;
}

/* returns actual frequency in kHz */
uint32_t mmc_set_clock(uint32_t clock_khz)
{
    uint32_t reg, base_clk_khz, i, mclk, freq_khz;

    /* disable clock */
    EMMC_SD_SRS11 &= ~EMMC_SD_SRS11_SDCE;

    /* get base clock */
    reg = EMMC_SD_SRS16;
    base_clk_khz = (reg & EMMC_SD_SRS16_BCSDCLK_MASK) >> EMMC_SD_SRS16_BCSDCLK_SHIFT;
    if (base_clk_khz == 0) {
        /* error getting base clock */
        return 0;
    }

    /* select clock frequency */
    reg = EMMC_SD_SRS11;
    reg &= ~(EMMC_SD_SRS11_SDCFSL_MASK | EMMC_SD_SRS11_SDCFSH_MASK);
    /* calculate divider */
    for (i=1; i<2046; i++) {
        if (((base_clk_khz / i) < clock_khz) ||
            (((base_clk_khz / i) == clock_khz) && (base_clk_khz % i) == 0)) {
            break;
        }
    }
    mclk = ((i / 2) << EMMC_SD_SRS11_SDCFSL_SHIFT);
    reg |= (mclk & EMMC_SD_SRS11_SDCFSL_MASK) | ((mclk & 0x30000) >> 10);
    reg |= EMMC_SD_SRS11_ICE; /* clock enable */
    reg &= ~EMMC_SD_SRS11_CGS; /* select clock */
    EMMC_SD_SRS11 = reg;
    freq_khz = base_clk_khz / i;

    /* wait for clock to stabilize */
    while ((EMMC_SD_SRS11 & EMMC_SD_SRS11_ICE) == 0);

    /* enable clock */
    EMMC_SD_SRS11 |= EMMC_SD_SRS11_SDCE;

    mmc_delay(DEFAULT_DELAY); /* delay after clock changed */

    return freq_khz;
}

/* eMMC/SD Response Type  */
typedef enum {
    EMMC_SD_RESP_NONE,
    EMMC_SD_RESP_R1,
    EMMC_SD_RESP_R1B,
    EMMC_SD_RESP_R2,
    EMMC_SD_RESP_R3,
    EMMC_SD_RESP_R4,
    EMMC_SD_RESP_R5,
    EMMC_SD_RESP_R5B,
    EMMC_SD_RESP_R6,
    EMMC_SD_RESP_R7,
    EMMC_SD_RESP_R1A
} EMMC_SD_Resp_t;

static uint32_t mmc_get_response_type(uint8_t resp_type)
{
    uint32_t cmd_reg;
    switch (resp_type) {
        case EMMC_SD_RESP_R2:
            cmd_reg = (EMMC_SD_SRS03_RESP_136 | EMMC_SD_SRS03_CRCCE);
            break;
        case EMMC_SD_RESP_R3:
        case EMMC_SD_RESP_R4:
            cmd_reg = EMMC_SD_SRS03_RESP_48;
            break;
        case EMMC_SD_RESP_R1:
        case EMMC_SD_RESP_R5:
        case EMMC_SD_RESP_R6:
        case EMMC_SD_RESP_R7:
            cmd_reg = (EMMC_SD_SRS03_RESP_48 | EMMC_SD_SRS03_CRCCE | EMMC_SD_SRS03_CICE);
            break;
        case EMMC_SD_RESP_R1B:
        case EMMC_SD_RESP_R5B:
            cmd_reg = (EMMC_SD_SRS03_RESP_48B | EMMC_SD_SRS03_CRCCE | EMMC_SD_SRS03_CICE);
            break;
        case EMMC_SD_RESP_NONE:
        default:
            cmd_reg = EMMC_SD_SRS03_RESP_NONE;
            break;
    }
    return cmd_reg;
}

int mmc_send_cmd(uint32_t cmd_index, uint32_t cmd_arg, uint8_t resp_type)
{
    uint32_t cmd_reg;
    uint32_t cmd_type = EMMC_SD_SRS03_CMD_NORMAL; /* TODO: Add support for suspend and resume */

#ifdef DEBUG_MMC
    wolfBoot_printf("mmc_send_cmd: cmd_index: %d, cmd_arg: %08X, resp_type: %d\n",
        cmd_index, cmd_arg, resp_type);
#endif

    /* wait for command line to be idle - TODO: Add timeout */
    while ((EMMC_SD_SRS09 & EMMC_SD_SRS09_CICMD) != 0);

    /* clear all status interrupts (except current limit, card interrupt/removal/insert) */
    EMMC_SD_SRS12 = ~(EMMC_SD_SRS12_ECL |
                      EMMC_SD_SRS12_CINT |
                      EMMC_SD_SRS12_CR |
                      EMMC_SD_SRS12_CIN);

    /* set command argument and command transfer registers */
    EMMC_SD_SRS02 = cmd_arg;
    cmd_reg =
        ((cmd_index & EMMC_SD_SRS03_CIDX_MASK) << EMMC_SD_SRS03_CIDX_SHIFT) |
        ((cmd_type & EMMC_SD_SRS03_CT_MASK) << EMMC_SD_SRS03_CT_SHIFT) |
        mmc_get_response_type(resp_type);

    EMMC_SD_SRS03 = cmd_reg;

    if (resp_type != EMMC_SD_RESP_NONE) {
        /* wait for command complete or error - TODO: Add timeout  */
        while ((EMMC_SD_SRS12 & (EMMC_SD_SRS12_CC | EMMC_SD_SRS12_ERR_STAT)) == 0);
    }

    /* clear all status interrupts (except current limit, card interrupt/removal/insert) */
    EMMC_SD_SRS12 = ~(EMMC_SD_SRS12_ECL |
                      EMMC_SD_SRS12_CINT |
                      EMMC_SD_SRS12_CR |
                      EMMC_SD_SRS12_CIN);

    return 0;
}

/* Set power and send initialization commands */
/* voltage: 0=off or EMMC_SD_SRS10_BVS_ */
int mmc_power_init_seq(uint32_t voltage)
{
    /* Set power to specified voltage */
    int status = mmc_set_power(voltage);
    if (status == 0) {
        /* send CMD0 (go idle) to reset card */
        status = mmc_send_cmd(MMC_CMD0_GO_IDLE, 0, EMMC_SD_RESP_NONE);

        if (status == 0) {
            mmc_delay(DEFAULT_DELAY);

            /* send the operating conditions command */
            status = mmc_send_cmd(SD_CMD8_SEND_IF_COND, IF_COND_27V_33V,
                EMMC_SD_RESP_R7);
        }
    }
    return status;
}

int mmc_card_init(uint32_t acmd41_arg, uint32_t *ocr_reg)
{
    int status = mmc_send_cmd(SD_CMD55_APP_CMD, 0, EMMC_SD_RESP_R1);
    if (status == 0) {
        status = mmc_send_cmd(SD_ACMD41_SD_SEND_OP, acmd41_arg,
            EMMC_SD_RESP_R3);
    }
    if (status == 0) {
        *ocr_reg = EMMC_SD_SRS04;
    }
    return status;
}

int mmc_init(void)
{
    int status = 0;
    uint32_t reg, cap;
    uint32_t ctrl_volts = EMMC_SD_SRS10_BVS_3_3V; /* default to 3.3v */
    uint32_t card_volts;
    int is_xpc, is_si8r;

    /* Reset the MMC controller */
    SYSREG_SOFT_RESET_CR &= ~SYSREG_SOFT_RESET_CR_MMC;
    /* Reset the host controller */
    EMMC_SD_HRS00 |= EMMC_SD_HRS00_SWR;
    /* Bit will clear when reset is done */
    while ((EMMC_SD_HRS00 & EMMC_SD_HRS00_SWR) != 0);

    /* Set debounce period to ~15ms (at 200MHz) */
    EMMC_SD_HRS01 = (EMMC_SD_DEBOUNCE_TIME & EMMC_SD_HRS01_DP_MASK);

    /* Select SDCard Mode */
    reg = EMMC_SD_HRS06;
    reg &= ~EMMC_SD_HRS06_EMM_MASK;
    reg |= EMMC_SD_HRS06_MODE_SD;
    EMMC_SD_HRS06 = reg;

    /* Clear error/interrupt status */
    EMMC_SD_SRS12 = (EMMC_SD_SRS12_NORM_STAT | EMMC_SD_SRS12_ERR_STAT);

    /* Check and enable 64-bit DMA support */
    reg = EMMC_SD_SRS15;
    cap = EMMC_SD_SRS16;
    if (cap & EMMC_SD_SRS16_A64S) {
        reg |= EMMC_SD_SRS15_A64;
        reg |= EMMC_SD_SRS15_HV4E;
        EMMC_SD_SRS15 = reg;
    }
    /* Set all status enables */
    EMMC_SD_SRS13 = 0xFFFFFFFF;
    /* Clear all signal enables */
    EMMC_SD_SRS14 = 0;
    /* Set initial timeout to 500ms */
    status = mmc_set_timeout(EMMC_SD_DATA_TIMEOUT_US);
    if (status != 0) {
        return status;
    }
    /* Turn off host controller power */
    (void)mmc_set_power(0);

    /* check if card inserted and stable */
    reg = EMMC_SD_SRS09;
    if ((reg & EMMC_SD_SRS09_CSS) == 0) {
        /* card not inserted or not stable */
        return -1;
    }
    /* NOTE: if using eMMC mode skip this check */
    if ((reg & EMMC_SD_SRS09_CI) == 0) {
        /* card not inserted */
        return -1;
    }

    /* Start in 1-bit bus mode */
    reg = EMMC_SD_SRS10;
    reg &= ~EMMC_SD_SRS10_DTW;
    reg &= ~EMMC_SD_SRS10_EDTW;
    EMMC_SD_SRS10 = reg;

    /* Setup 400khz starting clock */
    mmc_set_clock(EMMC_SD_CLK_400KHZ);

    /* Set power to 3.3v and send init commands */
    status = mmc_power_init_seq(ctrl_volts);
    if (status == 0) {
        uint32_t max_ma_3_3v, max_ma_1_8v;
        /* determine host controller capabilities */
        reg = EMMC_SD_SRS18;
        max_ma_3_3v = (reg & EMMC_SD_SRS18_MC33_MASK) >> EMMC_SD_SRS18_MC33_SHIFT;
        max_ma_1_8v = (reg & EMMC_SD_SRS18_MC18_MASK) >> EMMC_SD_SRS18_MC18_SHIFT;
        /* does it support eXtended Power Control (XPC)? */
        is_xpc = (max_ma_1_8v >= 150) && (max_ma_3_3v >= 150) ? 1 : 0;
        /* does it support UHS-I (Ultra High Speed Interface) v1.8 signaling? */
        is_si8r =((EMMC_SD_SRS16 & EMMC_SD_SRS16_VS18) && /* 1.8v supported */
                  (EMMC_SD_SRS17 & (EMMC_SD_SRS17_DDR50 | /* DDR50, SDR104, SDR50 or supported */
                          EMMC_SD_SRS17_SDR104 |
                          EMMC_SD_SRS17_SDR50))) ? 1: 0;
    }
    if (status == 0) {
        reg = 0;
        status = mmc_card_init(0, &reg);
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
                ctrl_volts = EMMC_SD_SRS10_BVS_3_0V;
            }
            else if (reg & SDCARD_REG_OCR_2_9_3_0) { /* 2.9v - 3.0v */
                card_volts = SDCARD_REG_OCR_2_9_3_0;
                ctrl_volts = EMMC_SD_SRS10_BVS_3_0V;
            }
            else { /* default to v3.3 */
                card_volts = SDCARD_REG_OCR_3_3_3_4;
            }
            /* if needed change operating volage and re-init */
            if (ctrl_volts != EMMC_SD_SRS10_BVS_3_3V) {
                status = mmc_power_init_seq(ctrl_volts);
            }
        }
    }
    if (status == 0) {
        /* configure operating conditions */
        uint32_t cmd_arg = SDCARD_ACMD41_HCS;
        cmd_arg |= card_volts;
        if (is_si8r) {
            cmd_arg |= SDCARD_REG_OCR_S18RA;
        }
        if (is_xpc) {
            cmd_arg |= SDCARD_REG_OCR_XPC;
        }

        /* retry until OCR ready */
        do {
            status = mmc_card_init(cmd_arg, &reg);
        } while (status == 0 && (reg & SDCARD_REG_OCR_READY) == 0);


    }
    return status;
}

/* TODO: Add support for reading uSD card with GPT (Global Partition Table) */
/* The partition ID's are determined using BOOT_PART_A and BOOT_PART_B. */
int disk_open(int drv)
{
    wolfBoot_printf("disk_open: drv = %d\r\n", drv);
    (void)drv;
    return mmc_init();
}
int disk_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    (void)drv;
    (void)part;
    (void)off;
    (void)sz;
    (void)buf;
    return 0;
}
int disk_write(int drv, int part, uint64_t off, uint64_t sz, const uint8_t *buf)
{
    (void)drv;
    (void)part;
    (void)off;
    (void)sz;
    (void)buf;
    return 0;
}
int disk_find_partition_by_label(int drv, const char *label)
{
    (void)drv;
    (void)label;
    return 0;
}

#ifdef DEBUG_UART

#ifndef DEBUG_UART_BASE
#define DEBUG_UART_BASE MSS_UART1_LO_BASE
#endif

/* Configure baud divisors with fractional baud rate support.
 *
 * UART baud rate divisor formula: divisor = PCLK / (baudrate * 16)
 *
 * To support fractional divisors (6-bit, 0-63), we scale up the calculation:
 *   divisor_x128 = (PCLK * 8) / baudrate  (128x scaled for rounding precision)
 *   divisor_x64  = divisor_x128 / 2       (64x scaled for 6-bit fractional)
 *   integer_div  = divisor_x64 / 64       (integer portion of divisor)
 *   frac_div     = divisor_x64 % 64       (fractional portion, 0-63)
 *
 * The fractional part is then adjusted using the x128 value for rounding.
 */
static void uart_config_clk(uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;

    /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;

    /* Extract integer and fractional parts */
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);

    /* Apply rounding correction from x128 calculation */
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

    if (div_int > (uint32_t)UINT16_MAX)
        return;

    /* Write 16-bit divisor: set DLAB, write high/low bytes, clear DLAB */
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

    /* Enable fractional divisor if integer divisor > 1 */
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    }
    else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}

void uart_init(void)
{
    /* Disable special modes: LIN, IrDA, SmartCard */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ELIN_MASK;
    MMUART_MM1(DEBUG_UART_BASE) &= ~EIRD_MASK;
    MMUART_MM2(DEBUG_UART_BASE) &= ~EERR_MASK;

    /* Disable interrupts */
    MMUART_IER(DEBUG_UART_BASE) = 0u;

    /* Reset and configure FIFOs, enable RXRDYN/TXRDYN pins */
    MMUART_FCR(DEBUG_UART_BASE) = 0u;
    MMUART_FCR(DEBUG_UART_BASE) |= CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK;
    MMUART_FCR(DEBUG_UART_BASE) |= RXRDY_TXRDYN_EN_MASK;

    /* Disable loopback (local and remote) */
    MMUART_MCR(DEBUG_UART_BASE) &= ~(LOOP_MASK | RLOOP_MASK);

    /* Set LSB-first for TX/RX */
    MMUART_MM1(DEBUG_UART_BASE) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);

    /* Disable AFM, single wire mode */
    MMUART_MM2(DEBUG_UART_BASE) &= ~(EAFM_MASK | ESWM_MASK);

    /* Disable TX time guard, RX timeout, fractional baud */
    MMUART_MM0(DEBUG_UART_BASE) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);

    /* Clear timing registers */
    MMUART_GFR(DEBUG_UART_BASE) = 0u;
    MMUART_TTG(DEBUG_UART_BASE) = 0u;
    MMUART_RTO(DEBUG_UART_BASE) = 0u;

    /* Configure baud rate (115200) */
    uart_config_clk(115200);

    /* Set line config: 8N1 */
    MMUART_LCR(DEBUG_UART_BASE) = MSS_UART_DATA_8_BITS |
                                  MSS_UART_NO_PARITY |
                                  MSS_UART_ONE_STOP_BIT;
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}
#endif /* DEBUG_UART */
