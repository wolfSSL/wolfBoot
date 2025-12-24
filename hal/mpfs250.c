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

#include "target.h"

#include "mpfs250.h"
#include "image.h"
#ifndef ARCH_RISCV64
#   error "wolfBoot mpfs250 HAL: wrong architecture selected. Please compile with ARCH=RISCV64."
#endif

#include "printf.h"
#include "loader.h"
#include "disk.h"
#include "gpt.h"

void hal_init(void)
{
    wolfBoot_printf("wolfBoot Version: %s (%s %s)\n",
        LIBWOLFBOOT_VERSION_STRING,__DATE__, __TIME__);

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

#if defined(MMU) && !defined(WOLFBOOT_NO_PARTITIONS)
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif


static uint32_t g_sector_count;
static uint32_t g_sector_size;
static uint32_t g_bus_width = 1;
static uint32_t g_rca = 0; /* SD Card Relative Address */

#ifndef DEFAULT_DELAY
#define DEFAULT_DELAY 0xFFFF
#endif

static int mmc_set_timeout(uint32_t timeout_us)
{
    uint32_t reg, i, tcfclk, tcfclk_mhz, tcfclk_khz, timeout_val, dtcv;

    /* read capabilities to determine timeout clock frequency and unit (MHz or kHz) */
    reg = EMMC_SD_SRS16;
    tcfclk_khz = (reg & EMMC_SD_SRS16_TCF_MASK) >> EMMC_SD_SRS16_TCF_SHIFT;
    /* Default timeout clock frequency should be 50MHz */

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
    dtcv = i;

    /* set the data timeout counter value */
    reg = EMMC_SD_SRS11;
    reg &= ~EMMC_SD_SRS11_DTCV_MASK;
    reg |= (dtcv << EMMC_SD_SRS11_DTCV_SHIFT) & EMMC_SD_SRS11_DTCV_MASK;
    EMMC_SD_SRS11 = reg;

#ifdef DEBUG_MMC
    wolfBoot_printf("mmc_set_timeout: timeout_val %d (%d)\n", timeout_val, dtcv);
#endif

    return 0;
}

static void mmc_delay(uint32_t delay)
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
static int mmc_set_power(uint32_t voltage)
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
            reg |= EMMC_SD_SRS10_BP | EMMC_SD_SRS10_BVS_3_0V;
        }
        else if (voltage == EMMC_SD_SRS10_BVS_3_3V && (cap2 & EMMC_SD_SRS16_VS33)) {
            reg |= EMMC_SD_SRS10_BP | EMMC_SD_SRS10_BVS_3_3V;
        }
        else {
            /* voltage not supported */
            return -1;
        }
        /* should be - 0xf06 */
        EMMC_SD_SRS10 = reg;
        mmc_delay(DEFAULT_DELAY); /* delay after bus power is applied */
    }
    return 0;
}

/* returns actual frequency in kHz */
static uint32_t mmc_set_clock(uint32_t clock_khz)
{
    static uint32_t last_clock_khz = 0;
    uint32_t reg, base_clk_khz, i, mclk, freq_khz;

    if (last_clock_khz != 0 && last_clock_khz == clock_khz) {
        /* clock already set */
        return 0;
    }

    /* disable clock */
    EMMC_SD_SRS11 &= ~EMMC_SD_SRS11_SDCE;

    /* get base clock */
    reg = EMMC_SD_SRS16;
    base_clk_khz = (reg & EMMC_SD_SRS16_BCSDCLK_MASK) >> EMMC_SD_SRS16_BCSDCLK_SHIFT;
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
    reg = EMMC_SD_SRS11;
    reg &= ~(EMMC_SD_SRS11_SDCFSL_MASK | EMMC_SD_SRS11_SDCFSH_MASK);
    reg |= (((mclk & 0x0FF) << EMMC_SD_SRS11_SDCFSL_SHIFT) & EMMC_SD_SRS11_SDCFSL_MASK);  /* lower 8 bits */
    reg |= (((mclk & 0x300) << EMMC_SD_SRS11_SDCFSH_SHIFT) & EMMC_SD_SRS11_SDCFSH_SHIFT); /* upper 2 bits */
    reg |= EMMC_SD_SRS11_ICE; /* clock enable */
    reg &= ~EMMC_SD_SRS11_CGS; /* select clock */
    EMMC_SD_SRS11 = reg;
    freq_khz = base_clk_khz / i;

    /* wait for clock to stabilize */
    while ((EMMC_SD_SRS11 & EMMC_SD_SRS11_ICS) == 0);

    /* enable clock */
    EMMC_SD_SRS11 |= EMMC_SD_SRS11_SDCE;
    last_clock_khz = clock_khz;

#ifdef DEBUG_MMC
    wolfBoot_printf("mmc_set_clock: requested khz: %d, actual khz: %d\n",
        clock_khz, freq_khz);
#endif

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

#define DEVICE_BUSY 1
int mmc_send_cmd(uint32_t cmd_index, uint32_t cmd_arg, uint8_t resp_type)
{
    int status = 0;
    uint32_t cmd_reg;
    uint32_t cmd_type = EMMC_SD_SRS03_CMD_NORMAL;

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
        ((cmd_index << EMMC_SD_SRS03_CIDX_SHIFT) & EMMC_SD_SRS03_CIDX_MASK) |
        ((cmd_type << EMMC_SD_SRS03_CT_SHIFT) & EMMC_SD_SRS03_CT_MASK) |
        mmc_get_response_type(resp_type);

    EMMC_SD_SRS03 = cmd_reg;

    /* wait for command complete or error - TODO: Add timeout  */
    while ((EMMC_SD_SRS12 & (EMMC_SD_SRS12_CC | EMMC_SD_SRS12_EINT)) == 0);

    /* check for device busy */
    if (resp_type == EMMC_SD_RESP_R1 || resp_type == EMMC_SD_RESP_R1B) {
        uint32_t resp = EMMC_SD_SRS04;
        #define CARD_STATUS_READY_FOR_DATA (1U << 8)
        if ((resp & CARD_STATUS_READY_FOR_DATA) == 0) {
            status = DEVICE_BUSY; /* card is busy */
        }
    }

    /* clear all status interrupts (except current limit, card interrupt/removal/insert) */
    EMMC_SD_SRS12 = ~(EMMC_SD_SRS12_ECL |
                      EMMC_SD_SRS12_CINT |
                      EMMC_SD_SRS12_CR |
                      EMMC_SD_SRS12_CIN);

    return status;
}

/* TODO: Add timeout */
static int mmc_wait_busy(int check_dat0)
{
    uint32_t status;
    if (check_dat0) {
        /* wait for DATA0 not busy */
        while ((EMMC_SD_SRS09 & EMMC_SD_SRS09_DAT0_LVL) == 0);
    }
    /* wait for CMD13 */
    while ((status = mmc_send_cmd(MMC_CMD13_SEND_STATUS,
        (g_rca << SD_RCA_SHIFT), EMMC_SD_RESP_R1)) == DEVICE_BUSY);
    return status;
}

/* Set power and send initialization commands */
/* voltage: 0=off or EMMC_SD_SRS10_BVS_[X_X]V */
int mmc_power_init_seq(uint32_t voltage)
{
    /* Set power to specified voltage */
    int status = mmc_set_power(voltage);
    if (status == 0) {
        /* send CMD0 (go idle) to reset card */
        status = mmc_send_cmd(MMC_CMD0_GO_IDLE, 0, EMMC_SD_RESP_NONE);
    }
    if (status == 0) {
        mmc_delay(DEFAULT_DELAY);

        /* send the operating conditions command */
        status = mmc_send_cmd(SD_CMD8_SEND_IF_COND, IF_COND_27V_33V,
            EMMC_SD_RESP_R7);
    }
    return status;
}

int mmc_card_init(uint32_t acmd41_arg, uint32_t *ocr_reg)
{
    int status = mmc_send_cmd(SD_CMD55_APP_CMD, 0, EMMC_SD_RESP_R1);
    if (status == 0) {
        status = mmc_send_cmd(SD_ACMD41_SEND_OP_COND, acmd41_arg,
            EMMC_SD_RESP_R3);
        if (status == 0) {
            *ocr_reg = EMMC_SD_SRS04;
        #ifdef DEBUG_MMC
            wolfBoot_printf("ocr_reg: 0x%08X\n", *ocr_reg);
        #endif
        }
    }
    return status;
}

/* MMC_CMD17_READ_SINGLE, MMC_CMD18_READ_MULTIPLE */
int mmc_read(uint32_t cmd_index, uint32_t block_addr, uint32_t* dst,
    uint32_t sz)
{
    int status;
    uint32_t block_count;
    uint32_t reg, cmd_reg;

    /* wait for idle */
    status = mmc_wait_busy(0);

    /* reset data and command lines */
    EMMC_SD_SRS11 |= EMMC_SD_SRS11_RESET_DAT_CMD;
    mmc_delay(0xFF);

    /* wait for command and data line busy to clear */
    while ((EMMC_SD_SRS09 & (EMMC_SD_SRS09_CICMD | EMMC_SD_SRS09_CIDAT)) != 0);

    /* get block count (round up) */
    block_count = (sz + (EMMC_SD_BLOCK_SIZE - 1)) / EMMC_SD_BLOCK_SIZE;
    /* set transfer block count */
    EMMC_SD_SRS01 = (block_count << EMMC_SD_SRS01_BCCT_SHIFT) | sz;

    cmd_reg = ((cmd_index << EMMC_SD_SRS03_CIDX_SHIFT) |
        EMMC_SD_SRS03_DPS | EMMC_SD_SRS03_DTDS |
        EMMC_SD_SRS03_BCE | EMMC_SD_SRS03_RECE | EMMC_SD_SRS03_RID |
        EMMC_SD_SRS03_RESP_48 | EMMC_SD_SRS03_CRCCE | EMMC_SD_SRS03_CICE);

    if (cmd_index == SD_ACMD51_SEND_SCR) {
        status = mmc_send_cmd(SD_CMD16, sz, EMMC_SD_RESP_R1);
        if (status == 0) {
            status = mmc_send_cmd(SD_CMD55_APP_CMD, (g_rca << SD_RCA_SHIFT),
                EMMC_SD_RESP_R1);
        }
        status = 0; /* ignore error */
    }
    else if (cmd_index == MMC_CMD18_READ_MULTIPLE) {
        cmd_reg |= EMMC_SD_SRS03_MSBS; /* enable multi-block select */
        EMMC_SD_SRS01 = (block_count << EMMC_SD_SRS01_BCCT_SHIFT) |
            EMMC_SD_BLOCK_SIZE;
    }

#ifdef DEBUG_MMC
    wolfBoot_printf("mmc_read: cmd_index: %d, block_addr: %08X, dst %p, sz: %d (%d blocks)\n",
        cmd_index, block_addr, dst, sz, block_count);
#endif

    EMMC_SD_SRS02 = block_addr; /* cmd argument */
    EMMC_SD_SRS03 = cmd_reg; /* execute command */
    while (sz > 0) {
        /* wait for buffer read ready */
        while (((reg = EMMC_SD_SRS12) &
            (EMMC_SD_SRS12_BRR | EMMC_SD_SRS12_EINT)) == 0);

        /* read in buffer - read 4 bytes at a time */
        if (reg & EMMC_SD_SRS12_BRR) {
            uint32_t i, read_sz = sz;
            if (read_sz > EMMC_SD_BLOCK_SIZE) {
                read_sz = EMMC_SD_BLOCK_SIZE;
            }
            for (i=0; i<read_sz; i+=4) {
                *dst = EMMC_SD_SRS08;
                dst++;
            }
            sz -= read_sz;
        }
    }

    if (cmd_index == MMC_CMD18_READ_MULTIPLE) {
        /* send CMD12 to stop transfer - ignore response */
        (void)mmc_send_cmd(MMC_CMD12_STOP_TRANS, (g_rca << SD_RCA_SHIFT),
            EMMC_SD_RESP_R1);
    }

    /* check for any errors and wait for idle */
    reg = EMMC_SD_SRS12;
    if ((reg & EMMC_SD_SRS12_ERR_STAT) == 0) {
        mmc_delay(0xFFF);
        status = mmc_wait_busy(0);
    }
    else {
        status = -1; /* error */
    }

#ifdef DEBUG_MMC
    wolfBoot_printf("mmc_read: status: %d\n", status);
#endif

    return status;
}

int mmc_set_bus_width(uint32_t bus_width)
{
    int status;

    if (bus_width == g_bus_width) {
        /* nothing to do */
        return 0;
    }

    /* set bus width */
    status = mmc_send_cmd(SD_CMD55_APP_CMD, g_rca << SD_RCA_SHIFT,
        EMMC_SD_RESP_R1);
    if (status == 0) {
        uint32_t cmd_arg = (bus_width == 4) ? 2 : 0;
        status = mmc_send_cmd(SD_ACMD6_SET_BUS_WIDTH, cmd_arg, EMMC_SD_RESP_R1);
        if (status == 0) {
            /* change host bus width */
            if (bus_width == 4) {
                EMMC_SD_SRS10 |= EMMC_SD_SRS10_DTW;
            }
            else {
                EMMC_SD_SRS10 &= ~EMMC_SD_SRS10_DTW;
            }
        }
    }
    return status;
}

/* helper to get bits from the response registers */
static uint32_t get_srs_bits(int from, int count)
{
    volatile uint32_t *resp = ((volatile uint32_t*)(EMMC_SD_BASE + 0x210));
    uint32_t mask, ret;
    int off, shft;

    from -= 8;
    mask = ((count < 32) ? (1U << (uint32_t)count) : 0) - 1;
    off = from / 32;
    shft = from & 31;
    ret = resp[off] >> shft;
    if ((from + shft) > 32) {
        ret |= resp[off + 1] << ((32 - shft) % 32);
    }
    return ret & mask;
}

/* check or set switch function/group:
 * returns 0 if supported */
int mmc_send_switch_function(uint32_t mode, uint32_t function_number,
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
        status = mmc_read(SD_CMD6_SWITCH_FUNC,
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

int mmc_set_function(uint32_t function_number, uint32_t group_number)
{
    /* send check first */
    int status = mmc_send_switch_function(SDCARD_SWITCH_FUNC_MODE_CHECK,
        function_number, group_number);
    if (status == 0) {
        /* send switch function */
        status = mmc_send_switch_function(SDCARD_SWITCH_FUNC_MODE_SWITCH,
            function_number, group_number);
    }
    return status;
}

int mmc_init(void)
{
    int status = 0;
    uint32_t reg, cap;
    uint32_t ctrl_volts, card_volts;
    uint32_t irq_restore;
    int xpc, si8r;

    /* Reset the MMC controller */
    SYSREG_SOFT_RESET_CR &= ~SYSREG_SOFT_RESET_CR_MMC;
    /* Disable the EMMC/SD IRQ */

    /* Reset the host controller */
    EMMC_SD_HRS00 |= EMMC_SD_HRS00_SWR;
    /* Bit will clear when reset is done */
    while ((EMMC_SD_HRS00 & EMMC_SD_HRS00_SWR) != 0);

    /* Set debounce period to ~15ms (at 200MHz) */
    EMMC_SD_HRS01 = ((EMMC_SD_DEBOUNCE_TIME << EMMC_SD_HRS01_DP_SHIFT) &
                      EMMC_SD_HRS01_DP_MASK);

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
    /* Set all status enables - 0xbff40ff */
    EMMC_SD_SRS13 = (
        EMMC_SD_SRS13_ETUNE_SE | EMMC_SD_SRS13_EADMA_SE | EMMC_SD_SRS13_EAC_SE |
        EMMC_SD_SRS13_ECL_SE | EMMC_SD_SRS13_EDEB_SE |
        EMMC_SD_SRS13_EDCRC_SE | EMMC_SD_SRS13_EDT_SE |
        EMMC_SD_SRS13_ECI_SE | EMMC_SD_SRS13_ECEB_SE | EMMC_SD_SRS13_ECCRC_SE |
        EMMC_SD_SRS13_ECT_SE | EMMC_SD_SRS13_RTUNE_SE |
        EMMC_SD_SRS13_INT_ONC | EMMC_SD_SRS13_INT_ONB | EMMC_SD_SRS13_INT_ONA |
        EMMC_SD_SRS13_CR_SE | EMMC_SD_SRS13_CIN_SE |
        EMMC_SD_SRS13_BRR_SE | EMMC_SD_SRS13_BWR_SE | EMMC_SD_SRS13_DMAINT_SE |
        EMMC_SD_SRS13_BGE_SE | EMMC_SD_SRS13_TC_SE | EMMC_SD_SRS13_CC_SE |
        EMMC_SD_SRS13_ERSP_SE | EMMC_SD_SRS13_CQINT_SE
    );
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
    EMMC_SD_SRS10 &= ~(EMMC_SD_SRS10_EDTW | EMMC_SD_SRS10_DTW);

    /* Setup 400khz starting clock */
    mmc_set_clock(EMMC_SD_CLK_400KHZ);

    /* Set power to 3.3v and send init commands */
    ctrl_volts = EMMC_SD_SRS10_BVS_3_3V; /* default to 3.3v */
    status = mmc_power_init_seq(ctrl_volts);
    if (status == 0) {
        uint32_t max_ma_3_3v, max_ma_1_8v;
        /* determine host controller capabilities */
        reg = EMMC_SD_SRS18;
        max_ma_3_3v = ((reg & EMMC_SD_SRS18_MC33_MASK) >> EMMC_SD_SRS18_MC33_SHIFT) * 4;
        max_ma_1_8v = ((reg & EMMC_SD_SRS18_MC18_MASK) >> EMMC_SD_SRS18_MC18_SHIFT) * 4;
        /* does controller support eXtended Power Control (XPC)? */
        xpc = (max_ma_1_8v >= 150) && (max_ma_3_3v >= 150) ? 1 : 0;
        /* does controller support UHS-I (Ultra High Speed Interface) v1.8 signaling? */
        si8r =((EMMC_SD_SRS16 & EMMC_SD_SRS16_VS18) && /* 1.8v supported */
               (EMMC_SD_SRS17 & (EMMC_SD_SRS17_DDR50 | /* DDR50, SDR104 or SDR50 supported */
                                 EMMC_SD_SRS17_SDR104 |
                                 EMMC_SD_SRS17_SDR50))) ? 1: 0;
    #ifdef DEBUG_MMC
        wolfBoot_printf("mmc_init: xpc:%d, si8r:%d, max_ma (3.3v:%d 1.8v:%d)\n",
            xpc, si8r, max_ma_3_3v, max_ma_1_8v);
    #endif
    }
    if (status == 0) {
        reg = 0;
        /* get operating conditions */
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
            #ifdef DEBUG_MMC
                wolfBoot_printf("mmc_init: changing operating voltage to 3.0v\n");
            #endif
                status = mmc_power_init_seq(ctrl_volts);
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
    #ifdef DEBUG_MMC
        wolfBoot_printf("mmc_init: sending OCR arg: 0x%08X\n", cmd_arg);
    #endif

        /* retry until OCR ready */
        do {
            status = mmc_card_init(cmd_arg, &reg);
        } while (status == 0 && (reg & SDCARD_REG_OCR_READY) == 0);
    }
    if (status == 0) {
        /* Get card identification */
        status = mmc_send_cmd(MMC_CMD2_ALL_SEND_CID, 0, EMMC_SD_RESP_R2);
    }
    if (status == 0) {
        /* Set relative address */
        status = mmc_send_cmd(MMC_CMD3_SET_REL_ADDR, 0, EMMC_SD_RESP_R6);
    }
    if (status == 0) {
        g_rca = ((EMMC_SD_SRS04 >> SD_RCA_SHIFT) & 0xFFFF);
    #ifdef DEBUG_MMC
        wolfBoot_printf("mmc_init: rca: %d\n", g_rca);
    #endif
    }
    if (status == 0) {
        /* read CSD register from device */
        status = mmc_send_cmd(MMC_CMD9_SEND_CSD, g_rca << SD_RCA_SHIFT,
            EMMC_SD_RESP_R2);
    }
    if (status == 0) {
        /* Get sector size and count */
        uint32_t csd_struct;
        uint32_t bl_len, c_size, c_size_mult;
        bl_len = get_srs_bits(22, 4);
        g_sector_size = (1U << bl_len);

        csd_struct = get_srs_bits(126, 2);
        switch (csd_struct) {
            case 0:
                c_size = get_srs_bits(62, 12);
                c_size_mult = get_srs_bits(47, 3);
                g_sector_count = (c_size + 1) << (c_size_mult + 2);
                break;
            case 1:
                c_size = get_srs_bits(48, 22);
                g_sector_count = (c_size + 1) << 10;
                break;
            default:
                /* invalid CSD structure */
                status = -1;
                break;
        }
    #ifdef DEBUG_MMC
        wolfBoot_printf("mmc_init: csd_version: %d, sector: size %d count %d\n",
            csd_struct, g_sector_size, g_sector_count);
    #endif
    }
    if (status == 0) {
        /* select card */
        status = mmc_send_cmd(MMC_CMD7_SELECT_CARD, g_rca << SD_RCA_SHIFT,
            EMMC_SD_RESP_R1B);
        if (status == DEVICE_BUSY) {
            status = mmc_wait_busy(1);
        }
    }
    if (status == 0) {
        /* disable card insert interrupt while changing bus width to avoid false triggers */
        irq_restore = EMMC_SD_SRS13;
        EMMC_SD_SRS13 = (irq_restore & ~EMMC_SD_SRS13_CINT_SE);
        mmc_delay(DEFAULT_DELAY);

        status = mmc_set_bus_width(4);
    }
    if (status == 0) {
        /* Get SCR registers - 8 bytes */
        uint32_t scr_reg[SCR_REG_DATA_SIZE/sizeof(uint32_t)];
        status = mmc_read(SD_ACMD51_SEND_SCR, 0, scr_reg,
            sizeof(scr_reg));
    }
    if (status == 0) {
        /* set UHS mode to SDR25 and driver strength to Type B */
        uint32_t card_access_mode = SDCARD_SWITCH_ACCESS_MODE_SDR25;
        status = mmc_set_function(card_access_mode, 1);
        if (status == 0) {
            /* set driver strength */
            reg = EMMC_SD_SRS15;
            reg &= ~EMMC_SD_SRS15_DSS_MASK;
            reg |= EMMC_SD_SRS15_DSS_TYPE_B; /* default */
            EMMC_SD_SRS15 = reg;

            /* enable high speed */
            EMMC_SD_SRS10 |= EMMC_SD_SRS10_HSE;

            /* set UHS mode */
            reg = EMMC_SD_SRS15;
            reg &= ~EMMC_SD_SRS15_UMS_MASK;
            reg |= EMMC_SD_SRS15_UMS_SDR25;
            EMMC_SD_SRS15 = reg;
        }
    }
    if (status == 0) {
        mmc_set_clock(EMMC_SD_CLK_50MHZ);

        /* TODO: Phy training at SDR25 (50MHz) */

        EMMC_SD_SRS13 = irq_restore; /* re-enable interrupt */
    }
    return status;
}

/* returns number of bytes read on success or negative on error */
/* start may not be block aligned and count may not be block multiple */
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    int status = 0;
    uint32_t read_sz, block_addr;
    uint32_t tmp_block[EMMC_SD_BLOCK_SIZE/sizeof(uint32_t)];
    uint32_t start_offset = (start % EMMC_SD_BLOCK_SIZE);
    (void)drv; /* only one drive supported */

#ifdef DEBUG_MMC
    wolfBoot_printf("disk_read: drv:%d, start:%llu, count:%d, dst:%p\n",
        drv, start, count, buf);
#endif

    while (count > 0) {
        block_addr = (start / EMMC_SD_BLOCK_SIZE);
        read_sz = count;
        if (read_sz > EMMC_SD_BLOCK_SIZE) {
            read_sz = EMMC_SD_BLOCK_SIZE;
        }
        if (read_sz < EMMC_SD_BLOCK_SIZE || /* last partial */
            start_offset != 0 ||            /* start not block aligned */
            ((uintptr_t)buf % 4) != 0)      /* buf not 4-byte aligned */
        {
            /* block read to temporary buffer */
            status = mmc_read(MMC_CMD17_READ_SINGLE, block_addr,
                tmp_block, EMMC_SD_BLOCK_SIZE);
            if (status == 0) {
                uint8_t* tmp_buf = (uint8_t*)tmp_block;
                memcpy(buf, tmp_buf + start_offset, read_sz);
                start_offset = 0;
            }
        }
        else {
            /* direct full block(s) read */
            uint32_t blocks = (count / EMMC_SD_BLOCK_SIZE);
            read_sz = (blocks * EMMC_SD_BLOCK_SIZE);
            status = mmc_read(blocks > 1 ?
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
    /* not supported */
    (void)drv;
    (void)start;
    (void)count;
    (void)buf;
    return 0;
}

int disk_init(int drv)
{
    int r = mmc_init();
    if (r != 0) {
        wolfBoot_printf("Failed to initialize MMC\n");
    }
    (void)drv;
    return r;
}

void disk_close(int drv)
{
    (void)drv;
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
