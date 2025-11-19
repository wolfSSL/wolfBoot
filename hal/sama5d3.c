/* atsama5d3.c
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

#include <string.h>
#include <target.h>
#include "image.h"
#include "sama5d3.h"

#ifndef ARCH_ARM
#   error "wolfBoot atsama5d3 HAL: wrong architecture selected. Please compile with ARCH=ARM."
#endif

void sleep_us(uint32_t usec);

/* Manual division operation */
static int division(uint32_t dividend,
        uint32_t divisor,
        uint32_t *quotient,
        uint32_t *remainder)
{
    uint32_t shift;
    uint32_t divisor_shift;
    uint32_t factor = 0;
    unsigned char end_flag = 0;

    if (!divisor)
        return 0xffffffff;

    if (dividend < divisor) {
        *quotient = 0;
        *remainder = dividend;
        return 0;
    }

    while (dividend >= divisor) {
        for (shift = 0, divisor_shift = divisor;
                dividend >= divisor_shift;
                divisor_shift <<= 1, shift++) {
            if (dividend - divisor_shift < divisor_shift) {
                factor += 1 << shift;
                dividend -= divisor_shift;
                end_flag = 1;
                break;
            }
        }

        if (end_flag)
            continue;

        factor += 1 << (shift - 1);
        dividend -= divisor_shift >> 1;
    }

    if (quotient)
        *quotient = factor;

    if (remainder)
        *remainder = dividend;

    return 0;
}

static uint32_t div_u(uint32_t dividend, uint32_t divisor)
{
    uint32_t quotient = 0;
    uint32_t remainder = 0;
    int ret;

    ret = division(dividend, divisor, &quotient, &remainder);
    if (ret)
        return 0xffffffff;

    return quotient;
}

static uint32_t mod(uint32_t dividend, uint32_t divisor)
{
    uint32_t quotient = 0;
    uint32_t remainder = 0;
    int ret;

    ret = division(dividend, divisor, &quotient, &remainder);
    if (ret)
        return 0xffffffff;

    return remainder;
}

/* RAM configuration: 2 x MT47H64M16 on SAMA5D3-Xplained
 * 8 Mwords x 8 Banks x 16 bits x 2, total 2 Gbit
 */
static const struct dram ddram ={
    .timing = { /* Hardcoded for MT47H64M16, */
        .tras = 6,
        .trcd = 2,
        .twr = 2,
        .trc = 8,
        .trp = 2,
        .trrd = 2,
        .twtr = 2,
        .tmrd = 2,
        .trfc = 17,
        .txsnr = 19,
        .txsrd = 200,
        .txp = 2,
        .txard = 8,
        .txards = 8,
        .trpa = 2,
        .trtp = 2,
        .tfaw = 6,
    }
};


void master_clock_set(uint32_t prescaler)
{
    uint32_t mck = PMC_MCKR & (PMC_MDIV_MASK | PMC_CSS_MASK);
    uint32_t diff = mck ^ prescaler;

    if (diff & PMC_ALTPRES_MASK) {
        /* Clear ALT_PRES field and extra PRES bit */
        mck &= ~((1 << 13 | PMC_ALTPRES_MASK));
        mck |= (prescaler & (PMC_ALTPRES_MASK));
        PMC_MCKR = mck;
        while ((PMC_SR & PMC_SR_MCKRDY) == 0)
            ;

    }
    if (diff & PMC_MDIV_MASK) {
        mck &= ~PMC_MDIV_MASK;
        mck |= (prescaler & PMC_MDIV_MASK);
        PMC_MCKR = mck;
        while ((PMC_SR & PMC_SR_MCKRDY) == 0)
            ;
    }
    if (diff & PMC_PLLADIV_MASK) {
        mck &= ~PMC_PLLADIV_MASK;
        mck |= (prescaler & PMC_PLLADIV_MASK);
        PMC_MCKR = mck;
        while ((PMC_SR & PMC_SR_MCKRDY) == 0)
            ;
    }
    if (diff & PMC_H32MXDIV_MASK) {
        mck &= ~PMC_H32MXDIV_MASK;
        mck |= (prescaler & PMC_H32MXDIV_MASK);
        PMC_MCKR = mck;
        while ((PMC_SR & PMC_SR_MCKRDY) == 0)
            ;
    }
    if (diff & PMC_CSS_MASK) {
        mck &= ~PMC_CSS_MASK;
        mck |= (prescaler & PMC_CSS_MASK);
        PMC_MCKR = mck;
        while ((PMC_SR & PMC_SR_MCKRDY) == 0)
            ;
    }
}

static void pll_init(void)
{
    /* Disable PLLA */
    PMC_PLLA &= PLLA_CKGR_SRCA;
    asm volatile("dmb");
    /* Configure PLLA */
    PMC_PLLA = PLLA_CONFIG;
    /* Wait for the PLLA to lock */

    while (!(PMC_SR & PMC_SR_LOCKA))
        ;
    /* Set the current charge pump */
    PMC_PLLICPR = PLLICPR_CONFIG;

    /* Set main clock */
    master_clock_set(PRESCALER_MAIN_CLOCK);

    /* Set PLLA clock */
    master_clock_set(PRESCALER_PLLA_CLOCK);
}

/* GMAC PINS: PB8, PB11, PB16, PB18 */
/* EMAC PINS: PC7, PC8 */
#define GMAC_PINS ( (1 << 8) | (1 << 11) | (1 << 16) | (1 << 18) )
#define EMAC_PINS ( (1 << 7) | (1 << 8) )
#define GPIO_GMAC GPIOB
#define GPIO_EMAC GPIOC

static void mac_init(void)
{
    PMC_CLOCK_EN(GPIOB_PMCID);
    PMC_CLOCK_EN(GPIOC_PMCID);

    GPIO_PPUDR(GPIO_GMAC) = GMAC_PINS;
    GPIO_PPDDR(GPIO_GMAC) = GMAC_PINS;
    GPIO_PER(GPIO_GMAC) = GMAC_PINS;
    GPIO_OER(GPIO_GMAC) = GMAC_PINS;
    GPIO_CODR(GPIO_GMAC) = GMAC_PINS;

    GPIO_PPUDR(GPIO_EMAC) = EMAC_PINS;
    GPIO_PPDDR(GPIO_EMAC) = EMAC_PINS;
    GPIO_PER(GPIO_EMAC) = EMAC_PINS;
    GPIO_OER(GPIO_EMAC) = EMAC_PINS;
    GPIO_CODR(GPIO_EMAC) = EMAC_PINS;
}


static void ddr_init(void)
{
    uint32_t rtr, md, cr, tpr0, tpr1, tpr2;
    uint32_t col, row, cas, bank;
    uint32_t cal;
    uint32_t ba_offset = 0;
    volatile uint32_t *dram_base = (volatile uint32_t *)DRAM_BASE;

    /* Step 1: Calculate register values
     *
     */
    md = MPDDRC_MD_DDR2_SDRAM | MPDDRC_MD_DBW_32BIT;
    col = MPDDRC_NC_10;  /* 10/9 column address */
    row = MPDDRC_NR_13;  /* 13-bit row address */
    cas = 3 << MPDDRC_CAS_SHIFT; /* CAS latency 3 */
    bank = 1 << MPDDRC_NB_SHIFT; /* NB_BANKS = 8 */
    cr = col | row | bank | cas | MPDDRC_CR_DECOD_INTERLEAVED | MPDDRC_UNAL
        | MPDDRC_NDQS_DISABLED;
    ba_offset = 12; /* Based on col = MPDDRC_NC_10, DBW 32 bit, interleaved */

    /* Set timing parameters using hardcoded values */
    rtr = 0x40F;
    tpr0 = (ddram.timing.tras << MPDDRC_TRAS_SHIFT) |
        (ddram.timing.trcd << MPDDRC_TRCD_SHIFT) |
        (ddram.timing.twr << MPDDRC_TWR_SHIFT) |
        (ddram.timing.trc << MPDDRC_TRC_SHIFT) |
        (ddram.timing.trp << MPDDRC_TRP_SHIFT) |
        (ddram.timing.trrd << MPDDRC_TRRD_SHIFT) |
        (ddram.timing.twtr << MPDDRC_TWTR_SHIFT) |
        (ddram.timing.tmrd << MPDDRC_TMRD_SHIFT);


    tpr1 = (ddram.timing.trfc << MPDDRC_TRFC_SHIFT) |
        (ddram.timing.txsnr << MPDDRC_TXSNR_SHIFT) |
        (ddram.timing.txsrd << MPDDRC_TXSRD_SHIFT) |
        (ddram.timing.txp << MPDDRC_TXP_SHIFT);

    tpr2 = (ddram.timing.txard << MPDDRC_TXARD_SHIFT) |
        (ddram.timing.txards << MPDDRC_TXARDS_SHIFT) |
        (ddram.timing.trpa << MPDDRC_TRPA_SHIFT) |
        (ddram.timing.trtp << MPDDRC_TRTP_SHIFT) |
        (ddram.timing.tfaw << MPDDRC_TFAW_SHIFT);

    /* Step 2: Enable the DDR2 SDRAM controller
     *
     */
    /* Turn on the DDRAM controller peripheral clock */
    PMC_CLOCK_EN(MPDDRC_PMCID);

    /* Enable DDR in system clock */
    PMC_SCER = MPDDRC_SCERID;

    sleep_us(10); /* 10 us */

    /* Step 3: Calibration
     *
     */
    cal = MPDDRC_IO_CALIBR;
    cal &= ~(MPDDRC_IOCALIBR_RDIV_MASK);
    cal |= MPDDRC_IOCALIBR_RDIV_DDR2_RZQ_50; /* 50 ohm */
    cal &= ~(MPDDRC_IOCALIBR_TZQIO_MASK);
    cal |= (100 << MPDDRC_IOCALIBR_TZQIO_SHIFT); /* 100 cycles at 133MHz is 0.75 us, 100 cycles at 166MHz is 0.6 us */

    MPDDRC_IO_CALIBR = cal;

    /* Data path configuration */
    MPDDRC_RD_DATA_PATH = 0x01; /* One cycle read delay */

    /* Write calibration again */
    MPDDRC_IO_CALIBR = cal;

    /* Step 4: Program the DDR2 SDRAM controller
     *
     */

    /* Program the memory device type */
    MPDDRC_MD = md;

    /* Program the features into configuration registers */
    MPDDRC_CR = cr;
    MPDDRC_TPR0 = tpr0;
    MPDDRC_TPR1 = tpr1;
    MPDDRC_TPR2 = tpr2;

    /* Send a NOP command via mode register */
    MPDDRC_MR = MPDDRC_MR_MODE_NOP;
    *dram_base = 0;

    sleep_us(200); /* 200 us */

    /* Send a second NOP command to set CKE high */
    MPDDRC_MR = MPDDRC_MR_MODE_NOP;
    *dram_base = 0;
    sleep_us(1); /* min 200 ns */

    /* Issue precharge all command */
    MPDDRC_MR = MPDDRC_MR_MODE_PRECHARGE;
    *dram_base = 0;
    sleep_us(1); /* min 15 ns */

    /* Issue external load command to set temperature mode (EMR2) */
    MPDDRC_MR = MPDDRC_MR_MODE_EXT_LOAD;
    *(volatile uint32_t *)(DRAM_BASE + (0x2 << ba_offset)) = 0x00000000;
    sleep_us(1); /* min 15 ns */

    /* Issue external load command to set DLL to 0 (EMR3)*/
    MPDDRC_MR = MPDDRC_MR_MODE_EXT_LOAD;
    *(volatile uint32_t *)(DRAM_BASE + (0x3 << ba_offset)) = 0x00000000;
    sleep_us(1); /* min 200 cycles */

    /* Issue external load command to program D.I.C. (EMR1) */
    MPDDRC_MR = MPDDRC_MR_MODE_EXT_LOAD;
    *(volatile uint32_t *)(DRAM_BASE + (0x1 << ba_offset)) = 0x00000000;
    sleep_us(1); /* min 200 cycles */

    /* Reset DLL via Configuration Register */
    MPDDRC_CR |= MPDDRC_CR_ENABLE_DLL_RESET;

    /* Issue load command to set DLL to 1 */
    MPDDRC_MR = MPDDRC_MR_MODE_LOAD;
    *dram_base = 0;
    sleep_us(1); /* min 15 ns */

    /* Issue a precharge command */
    MPDDRC_MR = MPDDRC_MR_MODE_PRECHARGE;
    *dram_base = 0;
    sleep_us(1); /* min 400 ns */

    /* Issue two auto-refresh cycles */
    MPDDRC_MR = MPDDRC_MR_MODE_AUTO_REFRESH;
    *dram_base = 0;
    sleep_us(1); /* min 400 ns */

    MPDDRC_MR = MPDDRC_MR_MODE_AUTO_REFRESH;
    *dram_base = 0;
    sleep_us(1); /* min 400 ns */

    /* Disable DLL reset */
    MPDDRC_CR &= ~MPDDRC_CR_ENABLE_DLL_RESET;

    /* Issue a mode register LOAD command */
    MPDDRC_MR = MPDDRC_MR_MODE_LOAD;
    *dram_base = 0;
    sleep_us(1); /* min 15 ns */


    /* Trigger OCD default calibration  */
    MPDDRC_CR |= MPDDRC_CR_OCD_DEFAULT;
    sleep_us(1); /* min 15 ns */

    /* Issue a mode register LOAD command (EMR1) */
    MPDDRC_MR = MPDDRC_MR_MODE_EXT_LOAD;
    *(volatile uint32_t *)(DRAM_BASE + (0x1 << ba_offset)) = 0x00000000;
    sleep_us(1); /* min 15 ns */

    /* Exit OCD default calibration */
    MPDDRC_CR &= ~MPDDRC_CR_OCD_DEFAULT;
    sleep_us(1); /* min 15 ns */

    /* Issue a mode register LOAD command (EMR1) */
    MPDDRC_MR = MPDDRC_MR_MODE_EXT_LOAD;
    *(volatile uint32_t *)(DRAM_BASE + (0x1 << ba_offset)) = 0x00000000;
    sleep_us(1); /* min 15 ns */

    /* Switch mode to NORMAL */
    MPDDRC_MR = MPDDRC_MR_MODE_NORMAL;
    *dram_base = 0;
    sleep_us(1); /* min 15 ns */

    /* Perform a write access to the DDR2-SDRAM */
    *(dram_base) = 0xA5A5A5D1;

    /* finally, set the refresh rate */
    MPDDRC_RTR = rtr;

    /* DDR is now ready to use. Wait for the end of calibration */
    sleep_us(10);
}

/* Static variables to hold nand info */
static uint8_t nand_manif_id;
static uint8_t nand_dev_id;
static char nand_onfi_id[4];

struct nand_flash {
    uint16_t revision;
    uint16_t features;
    uint16_t ext_page_len;
    uint16_t parameter_page;

    uint32_t page_size;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t pages_per_block;
    uint32_t pages_per_device;
    uint32_t total_size;

    uint16_t bad_block_pos;
    uint16_t ecc_bytes;
    uint16_t eccpos[MAX_ECC_BYTES];
    uint16_t eccwordsize;

    uint32_t bus_width;
    uint32_t oob_size;
} nand_flash = { 0 };

static void nand_wait_ready(void)
{
    NAND_CMD = NAND_CMD_STATUS;
    while (!(NAND_DATA & 0x40));
}


static void nand_read_id(uint8_t *manif_id, uint8_t *dev_id)
{
    NAND_CMD = NAND_CMD_READID;
    NAND_ADDR = 0x00;
    *manif_id = NAND_DATA;
    *dev_id = NAND_DATA;
}

static void nand_reset(void)
{
    NAND_CMD = NAND_CMD_RESET;
    nand_wait_ready();
}


static void write_column_address(uint32_t col_address)
{
    NAND_ADDR = col_address & 0xFF;
    NAND_ADDR = (col_address >> 8) & 0xFF;
    NAND_ADDR = (col_address >> 16) & 0xFF;
}

static void write_row_address(uint32_t row_address)
{
    NAND_ADDR = row_address & 0xFF;
    NAND_ADDR = (row_address >> 8) & 0xFF;
    NAND_ADDR = (row_address >> 16) & 0xFF;
    NAND_ADDR = (row_address >> 24) & 0xFF;
}

static void nand_read_info(void)
{
    uint8_t onfi_data[ONFI_PARAMS_SIZE];
    uint32_t i;

    nand_reset();

    nand_read_id(&nand_manif_id, &nand_dev_id);
    NAND_CMD = NAND_CMD_READID;
    NAND_ADDR = 0x20;
    nand_onfi_id[0] = NAND_DATA;
    nand_onfi_id[1] = NAND_DATA;
    nand_onfi_id[2] = NAND_DATA;
    nand_onfi_id[3] = NAND_DATA;
    if (memcmp(nand_onfi_id, "ONFI", 4) != 0) {
        /* Fail: no ONFI support */
        asm("bkpt 0");
        return;
    }
    memset(&nand_flash, 0, sizeof(nand_flash));
    memset(nand_flash.eccpos, 0xFF, sizeof(nand_flash.eccpos));
    NAND_CMD = NAND_CMD_READ_ONFI;
    NAND_ADDR = 0x00;
    nand_wait_ready();
    NAND_CMD = NAND_CMD_READ1;
    for (i = 0; i < ONFI_PARAMS_SIZE; i++) {
        onfi_data[i] = NAND_DATA;
    }
    /* Store ONFI parameters in nand_flash struct */
    nand_flash.page_size = *(uint16_t *)(onfi_data + PARAMS_POS_PAGESIZE);
    nand_flash.pages_per_block = *(uint16_t *)(onfi_data + PARAMS_POS_BLOCKSIZE);
    nand_flash.block_size = nand_flash.page_size * nand_flash.pages_per_block;
    nand_flash.block_count = *(uint16_t *)(onfi_data + PARAMS_POS_NBBLOCKS);
    nand_flash.total_size = nand_flash.block_count * nand_flash.block_size;
    nand_flash.ecc_bytes = *(uint16_t *)(onfi_data + PARAMS_POS_ECC_BITS);
    nand_flash.bad_block_pos = (*(uint16_t *)(onfi_data + PARAMS_POS_FEATURES)) & 1;
    nand_flash.ext_page_len = *(uint16_t *)(onfi_data + PARAMS_POS_EXT_PARAM_PAGE_LEN);
    nand_flash.parameter_page = *(uint16_t *)(onfi_data + PARAMS_POS_PARAMETER_PAGE);
    nand_flash.pages_per_block = div_u(nand_flash.block_size, nand_flash.page_size);
    nand_flash.pages_per_device = nand_flash.pages_per_block * nand_flash.block_count;
    nand_flash.oob_size = *(uint16_t *)(onfi_data + PARAMS_POS_OOBSIZE);
    nand_flash.revision = *(uint16_t *)(onfi_data + PARAMS_POS_REVISION);
    nand_flash.features = *(uint16_t *)(onfi_data + PARAMS_POS_FEATURES);
    nand_flash.bus_width = (onfi_data[PARAMS_POS_FEATURES] & PARAMS_FEATURE_BUSWIDTH) ? 16 : 8;
    if (nand_flash.ecc_bytes <= MAX_ECC_BYTES) {
        for (int i = 0; i < nand_flash.ecc_bytes; i++) {
            nand_flash.eccpos[i] = *(uint16_t *)(onfi_data + PARAMS_POS_ECC_BITS + i * 2);
        }
    }
    if (nand_flash.page_size != NAND_FLASH_PAGE_SIZE) {
        /* Fail: unsupported page size */
        asm("bkpt 0");
    }
    if (nand_flash.oob_size != NAND_FLASH_OOB_SIZE) {
        /* Fail: unsupported oob size */
        asm("bkpt 0");
    }


}

static void set_col_addr(uint32_t col_address)
{
    uint32_t page_size = nand_flash.page_size;
    while (page_size > 0) {
        NAND_ADDR = col_address & 0xFF;
        col_address >>= 8;
        page_size >>= 8;
    }
}

static void set_row_addr(uint32_t row_address)
{
    uint32_t pages_per_device = nand_flash.pages_per_device;
    while (pages_per_device > 0) {
        NAND_ADDR = row_address & 0xFF;
        row_address >>= 8;
        pages_per_device >>= 8;
    }
}

static int nand_device_read(uint32_t row_address, uint8_t *data, int mode)
{
    uint32_t col_address = 0x00;
    uint32_t tot_len = 0;
    uint32_t i;

    if (mode == NAND_MODE_DATAPAGE) {
        tot_len = nand_flash.page_size;
    } else if (mode == NAND_MODE_INFO) {
        tot_len = nand_flash.oob_size;
        col_address = nand_flash.page_size;
    } else if (mode == NAND_MODE_DATABLOCK) {
        tot_len = nand_flash.block_size;
    } else {
        /* Fail: unknown mode */
        return -1;
    }
    NAND_CMD = NAND_CMD_READ1;

    set_col_addr(col_address);
    set_row_addr(row_address);

    NAND_CMD = NAND_CMD_READ2;
    nand_wait_ready();
    NAND_CMD = NAND_CMD_READ1;
    for (i = 0; i < tot_len; i++) {
        data[i] = NAND_DATA;
    }
    return 0;
}

static int nand_read_page(uint32_t block, uint32_t page, uint8_t *data)
{
    uint32_t row_address = block * nand_flash.pages_per_block + page;
    return nand_device_read(row_address, data, NAND_MODE_DATAPAGE);
}

static int nand_check_bad_block(uint32_t block)
{
    uint32_t row_address = block * nand_flash.pages_per_block;
    uint8_t oob[NAND_FLASH_OOB_SIZE];
    uint32_t page;
    for (page = 0; page < 2; page++) {
        nand_device_read(row_address + page, oob, NAND_MODE_INFO);
        if (oob[0] != 0xFF) {
            return -1;
        }
    }
    return 0;
}



int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uint8_t buffer_page[NAND_FLASH_PAGE_SIZE];
    uint32_t block = div_u(address, nand_flash.block_size); /* The block where the address falls in */
    uint32_t page = div_u(address, nand_flash.page_size); /* The page where the address falls in */
    uint32_t start_page_in_block = mod(page, nand_flash.pages_per_block); /* The start page within this block */
    uint32_t in_block_offset = mod(address, nand_flash.block_size); /* The offset of the address within the block */
    uint32_t remaining = nand_flash.block_size - in_block_offset; /* How many bytes remaining to read in the first block */
    int len_to_read = len;
    uint8_t *buffer = data;
    uint32_t i;
    int copy = 0;
    int ret;

    if (len < (int)nand_flash.page_size) {
        buffer = buffer_page;
        copy = 1;
        len_to_read = nand_flash.page_size;
    }

    while (len_to_read > 0) {
        uint32_t sz = len_to_read;
        uint32_t pages_to_read;
        if (sz > remaining)
            sz = remaining;

        do {
            ret = nand_check_bad_block(block);
            if (ret < 0) {
                /* Block is bad, skip it */
                block++;
            }
        } while (ret < 0);

        /* Amount of pages to be read from this block */
        pages_to_read = div_u((sz + nand_flash.page_size - 1), nand_flash.page_size);

        if (pages_to_read * nand_flash.page_size > remaining)
            pages_to_read--;

        /* Read (remaining) pages off a block */
        for (i = 0; i < pages_to_read; i++) {
            nand_read_page(block, start_page_in_block + i, buffer);
            if (sz > nand_flash.page_size)
                sz = nand_flash.page_size;
            len_to_read -= sz;
            buffer += sz;
        }
        /* The block is over, move to the next one */
        block++;
        start_page_in_block = 0;
        remaining = nand_flash.block_size;
    }
    if (copy) {
        uint32_t *dst = (uint32_t *)data;
        uint32_t *src = (uint32_t *)buffer_page;
        uint32_t tot_len = (uint32_t)len;
        for (i = 0; i < (tot_len >> 2); i++) {
            dst[i] = src[i];
        }
    }
    return len;
}

static void pit_init(void)
{
    /* Turn on clock for PIT */
    PMC_CLOCK_EN(PIT_PMCID);

    /* Set clock source to MCK/2 */
    PIT_MR = MAX_PIV | PIT_MR_EN;
}

void sleep_us(uint32_t usec)
{
    uint32_t base = PIT_PIIR;
    uint32_t delay;
    uint32_t current;

    /* Since our division function which costs much run time
     * causes the delay time error.
     * So here using shifting to implement the division.
     * to change "1000" to "1024", this cause some inaccuacy,
     * but it is acceptable.
     * ((MASTER_CLOCK / 1024) * usec) / (16 * 1024)
     */
    delay = ((MASTER_FREQ >> 10) * usec) >> 14;
    do {
        current = PIT_PIIR;
        current -= base;
    } while (current < delay);
}

/* Set up DBGU.
 * Assume baud rate is correcly set by RomBoot
 */
static void dbgu_init(void) {
    /* Set up pins */
    PMC_CLOCK_EN(GPIOB_PMCID);

    /* Disable Pull */
    GPIO_PPUDR(DBGU_GPIO) = (1U << DBGU_PIN_TX) | (1U << DBGU_PIN_RX);
    GPIO_PPDDR(DBGU_GPIO) = (1U << DBGU_PIN_TX) | (1U << DBGU_PIN_RX);

    /* Set "Peripheral A" */
    GPIO_ASR(DBGU_GPIO) = (1U << DBGU_PIN_TX) | (1U << DBGU_PIN_RX);

    /* Enable the peripheral clock for the DBGU */
    PMC_CLOCK_EN(DBGU_PMCID);

    /* Enable the transmitter and receiver */
    DBGU_CR = DBGU_CR_TXEN | DBGU_CR_RXEN;
}



int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* TODO */
    (void)address;
    (void)data;
    (void)len;

    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    /* TODO */
    (void)address;
    (void)len;
    return 0;
}

/* SAMA5D3 NAND flash does not have an enable pin */
void ext_flash_unlock(void)
{
}

void ext_flash_lock(void)
{
}

void* hal_get_dts_address(void)
{
    return (void*)&dts_addr;
}

void* hal_get_dts_update_address(void)
{
    return NULL; /* Not yet supported */
}


/* public HAL functions */
void hal_init(void)
{
    pll_init();
    pit_init();
    watchdog_disable();
    ddr_init();
    dbgu_init();
    nand_read_info();
}

void hal_prepare_boot(void)
{
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}


