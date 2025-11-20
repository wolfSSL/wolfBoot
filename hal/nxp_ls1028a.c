/* nxp_ls1028a.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"

#ifdef TARGET_nxp_ls1028a

#ifndef ARCH_AARCH64
#   error "wolfBoot ls1028a HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

#include "nxp_ls1028a.h"

/* HAL options */
#define ENABLE_DDR

#define BAUD_RATE 115200
#define UART_SEL 0 /* select UART 0 or 1 */


void hal_flash_init(void);
void switch_el3_to_el2(void);
extern void mmu_enable(void);

#ifdef DEBUG_UART
void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 400000000
     * +0.5 to round up
     */
    uint32_t div = (((SYS_CLK / 2.0) / (16 * BAUD_RATE)) + 0.5);

    while (!(UART_LSR(UART_SEL) & UART_LSR_TEMT));

    /* set ier, fcr, mcr */
    UART_IER(UART_SEL) = 0;
    UART_FCR(UART_SEL) = (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN);

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    UART_LCR(UART_SEL) = (UART_LCR_DLAB | UART_LCR_WLS);
    /* set divisor */
    UART_DLB(UART_SEL) = (div & 0xff);
    UART_DMB(UART_SEL) = ((div>>8) & 0xff);
    /* disable rate access (DLAB=0) */
    UART_LCR(UART_SEL) = (UART_LCR_WLS);
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while (!(UART_LSR(UART_SEL) & UART_LSR_THRE));
            UART_THR(UART_SEL) = '\r';
        }
        while (!(UART_LSR(UART_SEL) & UART_LSR_THRE));
        UART_THR(UART_SEL) = c;
    }
}
#endif /* DEBUG_UART */



/* SPI interface */

/* Clear and halt the SPI controller*/
static void spi_close(unsigned int sel)
{
    /* Halt, flush  and set as master */
    SPI_MCR(sel) = SPI_MCR_MASTER_HALT;
}

/*Clear the SPI controller and setup as a running master*/
static void spi_open(unsigned int sel)
{
    spi_close(sel);
    /* Setup CTAR0 */
    SPI_CTAR0(sel) = SPI_CTAR_8_00MODE_8DIV;
    /* Enable as master */
    SPI_MCR(sel) = SPI_MCR_MASTER_RUNNING;
}

static int spi_can_rx(unsigned int sel)
{
    return !!(SPI_SR(sel) & SPI_SR_RXCTR);
}

static unsigned char spi_pop_rx(unsigned int sel)
{
    return SPI_POPR(sel) & 0xFF;
}

static void spi_flush_rx(unsigned int sel)
{
    unsigned char rx_data;
    while (spi_can_rx(sel)) {
        rx_data = spi_pop_rx(sel);
    }
    (void) rx_data;
}

static int spi_can_tx(unsigned int sel)
{
    return !!(SPI_SR(sel) & SPI_SR_TFFF);
}

static void spi_push_tx(unsigned int sel, unsigned int pcs, unsigned char data,
        int last)
{
    SPI_PUSHR(sel) = (last ? SPI_PUSHR_LAST : SPI_PUSHR_CONT)
            | SPI_PUSHR_PCS(pcs) | data;
}

/* Perform a SPI transaction.  Set cont!=0 to not let CS go low after this*/
static void spi_transaction(unsigned int sel, unsigned int pcs,
        const unsigned char *out, unsigned char *in, unsigned int size,
        int cont)
{
    unsigned int tx_count = size;
    unsigned int rx_count = size;
    int last = 0;
    unsigned char rx_data = 0;
    unsigned char tx_data = 0;

    /* XXX No parameter validation */

    /* Flush RX FIFO */
    spi_flush_rx(sel);

    /* Nothing to do? */
    if (!size)
        return;

    while (rx_count > 0) {
        /* Try to push TX data */
        while ((tx_count > 0) && ((rx_count - tx_count) < SPI_FIFO_DEPTH)
                && spi_can_tx(sel)) {
            if (out) {
                tx_data = *out;
                out++;
            }
            tx_count--;
            last = (!cont && !tx_count);
            spi_push_tx(sel, pcs, tx_data, last);
        }

        /* Try to pop RX data */
        while ((rx_count > 0) && spi_can_rx(sel)) {
            rx_data = spi_pop_rx(sel);
            if (in) {
                *in = rx_data;
                in++;
            }
            rx_count--;
        }
    }
}

/*#define TPM_TEST*/
#ifdef TPM_TEST
void read_tpm_id(void)
{
    /* Read 4 bytes from offset D40F00.  Assumes 0 wait state on TPM */
    unsigned char out[8] = { 0x83, 0xD4, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00 };
    unsigned char in[8] = { 0 };
    unsigned char in2[8] = { 0 };
    unsigned int counter;

    /* LS1028A SPI to the MikroBus uses SPI3 (sel is 2) and CS 0 */
    #ifndef SPI_SEL_TPM
    #define SPI_SEL_TPM 2
    #endif

    #ifndef SPI_CS_TPM
    #define SPI_CS_TPM 0
    #endif

    spi_open(SPI_SEL_TPM);

    /* Single transaction */
    spi_transaction(SPI_SEL_TPM, SPI_CS_TPM, out, in, sizeof(out), 0);

    /* Use individual transactions with cont set */
    for (counter = 0; counter < (sizeof(out) - 1); counter++) {
        spi_transaction(SPI_SEL_TPM, SPI_CS_TPM, &out[counter], &in2[counter], 1,
                1);
    }
    spi_transaction(SPI_SEL_TPM, SPI_CS_TPM, &out[counter], &in2[counter], 1, 0);

    spi_close(SPI_SEL_TPM);

    (void) in;
    (void) in2;
}
#endif

void nxp_ls1028a_spi_init(unsigned int sel)
{
    /* TODO: Expose more configuration options */
    spi_open(sel);
}

int nxp_ls1028a_spi_xfer(unsigned int sel, unsigned int cs,
        const unsigned char *out, unsigned char *in,
        unsigned int size, int cont)
{
    /* TODO Make spi_transaction actually return errors */
    spi_transaction(sel, cs, out, in, size, cont);
    return 0;
}

void nxp_ls1028a_spi_deinit(unsigned int sel)
{
    spi_close(sel);
}


void hal_delay_us(uint32_t us) {
    uint64_t delay = (uint64_t)SYS_CLK * us / 1000000;
    volatile uint32_t i = 0;
    for (i = 0; i < delay; i++) {
        asm volatile("nop");
    }
}

/* Fixed addresses */
static const void* kernel_addr  = (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
static const void* update_addr  = (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;

void* hal_get_primary_address(void)
{
    return (void*)kernel_addr;
}

void* hal_get_update_address(void)
{
  return (void*)update_addr;
}

void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
}

void* hal_get_dts_update_address(void)
{
  return (void*)NULL;
}

void erratum_err050568(void)
{
	/* Use IP bus only if systembus PLL is 300MHz (Dont use 300MHz) */
}

/* Application on Serial NOR Flash device 18.6.3 */
void xspi_init(void)
{
    /* Configure module control register */
    XSPI_MCR0 = XSPI_MCR0_CFG;
    XSPI_MCR1 = XSPI_MCR1_CFG;
    XSPI_MCR2 = XSPI_MCR2_CFG;

    /* Clear RX/TX fifos */
    XSPI_IPRXFCR = XSPI_IPRXFCR_CFG; /* Note: RX/TX Water Mark can be set here default is 64bit */
    XSPI_IPTXFCR = XSPI_IPTXFCR_CFG; /* Increase size to reduce transfer requests */

    /* Configure AHB bus control register (AHBCR) and AHB RX Buffer control register (AHBRXBUFxCR0) */
    XSPI_AHBCR = XSPI_AHBCR_CFG;

    XSPI_AHBRXBUFnCR0(0) = XSPI_AHBRXBUF0CR_CFG;
    XSPI_AHBRXBUFnCR0(1) = XSPI_AHBRXBUF1CR_CFG;
    XSPI_AHBRXBUFnCR0(2) = XSPI_AHBRXBUF2CR_CFG;
    XSPI_AHBRXBUFnCR0(3) = XSPI_AHBRXBUF3CR_CFG;
    XSPI_AHBRXBUFnCR0(4) = XSPI_AHBRXBUF4CR_CFG;
    XSPI_AHBRXBUFnCR0(5) = XSPI_AHBRXBUF5CR_CFG;
    XSPI_AHBRXBUFnCR0(6) = XSPI_AHBRXBUF6CR_CFG;
    XSPI_AHBRXBUFnCR0(7) = XSPI_AHBRXBUF7CR_CFG;

    /* Configure Flash control registers (FLSHxCR0,FLSHxCR1,FLSHxCR2) */
    XSPI_FLSHA1CR0 = XSPI_FLSHA1CR0_CFG;
    XSPI_FLSHA2CR0 = XSPI_FLSHA2CR0_CFG;
    XSPI_FLSHB1CR0 = XSPI_FLSHB1CR0_CFG;
    XSPI_FLSHB2CR0 = XSPI_FLSHB2CR0_CFG;

    XSPI_FLSHA1CR1 = XSPI_FLSHA1CR1_CFG;
    XSPI_FLSHA2CR1 = XSPI_FLSHA2CR1_CFG;
    XSPI_FLSHB1CR1 = XSPI_FLSHB1CR1_CFG;
    XSPI_FLSHB2CR1 = XSPI_FLSHB2CR1_CFG;
    XSPI_FLSHA1CR2 = XSPI_FLSHA1CR2_CFG;
    XSPI_FLSHA2CR2 = XSPI_FLSHA2CR2_CFG;
    XSPI_FLSHB1CR2 = XSPI_FLSHB1CR2_CFG;
    XSPI_FLSHB2CR2 = XSPI_FLSHB2CR2_CFG;

    /* Configure DLL control register (DLLxCR) according to sample clock source selection */
    XSPI_DLLACR = XSPI_DLLACR_CFG;
    XSPI_DLLBCR = XSPI_DLLBCR_CFG;
}

void xspi_lut_lock(void)
{
    XSPI_LUTKEY = LUT_KEY;
    XSPI_LUT_LOCK()
}

void xspi_lut_unlock(void)
{
    XSPI_LUTKEY = LUT_KEY;
    XSPI_LUT_UNLOCK()
}

void hal_flash_init(void)
{
    xspi_lut_unlock();

    xspi_init();

    /* Fast Read */
    XSPI_LUT(LUT_INDEX_READ) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_READ3B);
    XSPI_LUT(1) = XSPI_LUT_SEQ(STOP, LUT_PAD_SINGLE, 0x0, READ_SDR, LUT_PAD_SINGLE, 0x04);
    XSPI_LUT(2) = 0x0;
    XSPI_LUT(3) = 0x0;

    /* Write Enable */
    XSPI_LUT(LUT_INDEX_WRITE_EN) = XSPI_LUT_SEQ(STOP, LUT_PAD_SINGLE, 0x0, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_WE);
    XSPI_LUT(5) = 0x0;
    XSPI_LUT(6) = 0x0;
    XSPI_LUT(7) = 0x0;

    /* Erase */
    XSPI_LUT(LUT_INDEX_SE) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_SE);
    XSPI_LUT(9) = 0x0;
    XSPI_LUT(10) = 0x0;
    XSPI_LUT(11) = 0x0;

    /* Subsector 4k Erase */
    XSPI_LUT(LUT_INDEX_SSE4K) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_SE_4K);
    XSPI_LUT(13) = 0x0;
    XSPI_LUT(14) = 0x0;
    XSPI_LUT(15) = 0x0;

    /* Page Program */
    XSPI_LUT(LUT_INDEX_PP) = XSPI_LUT_SEQ(RADDR_SDR, LUT_PAD_SINGLE, LUT_OP_ADDR3B, CMD_SDR, LUT_PAD(1), LUT_OP_PP);
    XSPI_LUT(17) = XSPI_LUT_SEQ(STOP, LUT_PAD_SINGLE, 0x0, WRITE_SDR, LUT_PAD_SINGLE, 0x1);
    XSPI_LUT(18) = 0x0;
    XSPI_LUT(19) = 0x0;

    /* Read Flag Status Regsiter */
    XSPI_LUT(LUT_INDEX_RDSR) = XSPI_LUT_SEQ(READ_SDR, LUT_PAD_SINGLE, LUT_OP_1BYTE, CMD_SDR, LUT_PAD_SINGLE, LUT_OP_RDSR);
    XSPI_LUT(21) = 0x0;
    XSPI_LUT(22) = 0x0;
    XSPI_LUT(23) = 0x0;

    xspi_lut_lock();
}

/* Called from boot_aarch64_start.S */
void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    uint64_t counter = 0;
    DDR_DDRCDR_1 = DDR_DDRCDR_1_VAL;
    DDR_SDRAM_CLK_CNTL = DDR_SDRAM_CLK_CNTL_VAL;

    /* Setup DDR CS (chip select) bounds */
    DDR_CS_BNDS(0)   = DDR_CS0_BNDS_VAL;
    DDR_CS_BNDS(1)   = DDR_CS1_BNDS_VAL;
    DDR_CS_BNDS(2)   = DDR_CS2_BNDS_VAL;
    DDR_CS_BNDS(3)   = DDR_CS3_BNDS_VAL;

    /* DDR SDRAM timing configuration */
    DDR_TIMING_CFG_0 = DDR_TIMING_CFG_0_VAL;
    DDR_TIMING_CFG_1 = DDR_TIMING_CFG_1_VAL;
    DDR_TIMING_CFG_2 = DDR_TIMING_CFG_2_VAL;
    DDR_TIMING_CFG_3 = DDR_TIMING_CFG_3_VAL;
    DDR_TIMING_CFG_4 = DDR_TIMING_CFG_4_VAL;
    DDR_TIMING_CFG_5 = DDR_TIMING_CFG_5_VAL;
    DDR_TIMING_CFG_6 = DDR_TIMING_CFG_6_VAL;
    DDR_TIMING_CFG_7 = DDR_TIMING_CFG_7_VAL;
    DDR_TIMING_CFG_8 = DDR_TIMING_CFG_8_VAL;

    DDR_ZQ_CNTL = DDR_ZQ_CNTL_VAL;
    DDR_DQ_MAP_0 = DDR_DQ_MAP_0_VAL;
    DDR_DQ_MAP_1 = DDR_DQ_MAP_1_VAL;
    DDR_DQ_MAP_2 = DDR_DQ_MAP_2_VAL;
    DDR_DQ_MAP_3 = DDR_DQ_MAP_3_VAL;

    /* DDR SDRAM mode configuration */
    DDR_SDRAM_CFG_3  = DDR_SDRAM_CFG_3_VAL;

    DDR_SDRAM_MODE   = DDR_SDRAM_MODE_VAL;
    DDR_SDRAM_MODE_2 = DDR_SDRAM_MODE_2_VAL;
    DDR_SDRAM_MODE_3 = DDR_SDRAM_MODE_3_VAL;
    DDR_SDRAM_MODE_4 = DDR_SDRAM_MODE_4_VAL;
    DDR_SDRAM_MODE_5 = DDR_SDRAM_MODE_5_VAL;
    DDR_SDRAM_MODE_6 = DDR_SDRAM_MODE_6_VAL;
    DDR_SDRAM_MODE_7 = DDR_SDRAM_MODE_7_VAL;
    DDR_SDRAM_MODE_8 = DDR_SDRAM_MODE_8_VAL;
    DDR_SDRAM_MODE_9 =  DDR_SDRAM_MODE_9_VAL;
    DDR_SDRAM_MODE_10 = DDR_SDRAM_MODE_10_VAL;
    DDR_SDRAM_MODE_11 = DDR_SDRAM_MODE_11_VAL;
    DDR_SDRAM_MODE_12 = DDR_SDRAM_MODE_12_VAL;
    DDR_SDRAM_MODE_13 = DDR_SDRAM_MODE_13_VAL;
    DDR_SDRAM_MODE_14 = DDR_SDRAM_MODE_14_VAL;
    DDR_SDRAM_MODE_15 = DDR_SDRAM_MODE_15_VAL;
    DDR_SDRAM_MODE_16 = DDR_SDRAM_MODE_16_VAL;
    DDR_SDRAM_MD_CNTL = DDR_SDRAM_MD_CNTL_VAL;

    /* DDR Configuration */
    DDR_SDRAM_INTERVAL = DDR_SDRAM_INTERVAL_VAL;
    DDR_DATA_INIT = DDR_DATA_INIT_VAL;

    DDR_WRLVL_CNTL = DDR_WRLVL_CNTL_VAL;
    DDR_WRLVL_CNTL_2 = DDR_WRLVL_CNTL_2_VAL;
    DDR_WRLVL_CNTL_3 = DDR_WRLVL_CNTL_3_VAL;

    DDR_SR_CNTR = 0;
    DDR_SDRAM_RCW_1 = DDR_SDRAM_RCW_1_VAL;
    DDR_SDRAM_RCW_2 = DDR_SDRAM_RCW_2_VAL;
    DDR_SDRAM_RCW_3 = DDR_SDRAM_RCW_3_VAL;
    DDR_SDRAM_RCW_4 = DDR_SDRAM_RCW_4_VAL;
    DDR_SDRAM_RCW_5 = DDR_SDRAM_RCW_5_VAL;
    DDR_SDRAM_RCW_6 = DDR_SDRAM_RCW_6_VAL;
    DDR_DDRCDR_2 = DDR_DDRCDR_2_VAL;
    DDR_SDRAM_CFG_2 = DDR_SDRAM_CFG_2_VAL;
    DDR_INIT_ADDR = 0;
    DDR_INIT_EXT_ADDR = 0;
    DDR_ERR_DISABLE = 0;
    DDR_ERR_INT_EN = DDR_ERR_INT_EN_VAL;

    DDR_DDRDSR_1 = DDR_DDRDSR_1_VAL;
    DDR_DDRDSR_2 = DDR_DDRDSR_2_VAL;
    DDR_ERR_SBE = DDR_ERR_SBE_VAL;

    DDR_CS_CONFIG(0) = DDR_CS0_CONFIG_VAL;
    DDR_CS_CONFIG(1) = DDR_CS1_CONFIG_VAL;
    DDR_CS_CONFIG(2) = DDR_CS2_CONFIG_VAL;
    DDR_CS_CONFIG(3) = DDR_CS3_CONFIG_VAL;

    /* Set values, but do not enable the DDR yet */
    DDR_SDRAM_CFG = (DDR_SDRAM_CFG_VAL & ~DDR_SDRAM_CFG_MEM_EN);

    hal_delay_us(500);
    asm volatile("isb");

    /* Enable controller */
    DDR_SDRAM_CFG &= ~(DDR_SDRAM_CFG_BI);
    DDR_SDRAM_CFG |= DDR_SDRAM_CFG_MEM_EN;
    asm volatile("isb");

    /* Wait for data initialization is complete */
    while ((DDR_SDRAM_CFG_2 & DDR_SDRAM_CFG2_D_INIT)) {
        counter++;
    }

    (void)counter;

#endif
}

void xspi_writereg(uint32_t* addr, uint32_t val)
{
    *(volatile uint32_t *)(addr) = val;
}

void xspi_write_en(uint32_t addr)
{
    XSPI_IPTXFCR = XSPI_IPRCFCR_FLUSH;
    XSPI_IPCR0 = addr;
    XSPI_IPCR1 =  XSPI_ISEQID(LUT_INDEX_WRITE_EN) | 1;
    XSPI_IPCMD_START();

    while(!(XSPI_INTR & XSPI_IPCMDDONE));

    XSPI_INTR |= XSPI_IPCMDDONE;
}

void xspi_read_sr(uint8_t* rxbuf, uint32_t addr, uint32_t len)
{
    uint32_t data = 0;

    /* Read IP CR regsiter */
    uint32_t rxfcr = XSPI_IPRXFCR;

    /* Flush RX fifo */
    rxfcr = rxfcr | XSPI_IPRCFCR_FLUSH;
    XSPI_IPTXFCR = rxfcr;

    /* Trigger read SR command */
    XSPI_IPCR0 = addr;
    XSPI_IPCR1 = XSPI_ISEQID(LUT_INDEX_RDSR) | len;
    XSPI_IPCMD_START();

    while(!(XSPI_INTR & XSPI_IPCMDDONE));

    XSPI_INTR |= XSPI_IPCMDDONE;

    data = XSPI_RFD(0);
    memcpy(rxbuf, &data, len);

    XSPI_IPRXFCR = XSPI_IPRCFCR_FLUSH;
    XSPI_INTR = XSPI_IPRXWA;
    XSPI_INTR = XSPI_IPCMDDONE;
}

void xspi_sw_reset(void)
{
    XSPI_SWRESET();
    while (XSPI_MCR0 & XSPI_SW_RESET);
}

void xspi_flash_write(uintptr_t address, const uint8_t *data, uint32_t len)
{
    uint32_t size = 0;
    uint32_t tx_data = 0;
    uint32_t loop_cnt = 0;
    uint32_t remaining, rem_size = 0;
    uint32_t i = 0, j = 0;

    while (len) {
        size = len > XSPI_IP_BUF_SIZE ? XSPI_IP_BUF_SIZE : len;

        XSPI_IPCR0 = address;
        loop_cnt = size / XSPI_IP_WM_SIZE;

        /* Fill TX fifos */
        for (i = 0; i < loop_cnt; i++) {
            /* Wait for TX fifo ready */
            while (!(XSPI_INTR & XSPI_INTR_IPTXWE_MASK));

            for(j = 0; j < XSPI_IP_WM_SIZE; j+=4) {
                memcpy(&tx_data, data++, 4);
                xspi_writereg((uint32_t*)XSPI_TFD_BASE + j, tx_data);
            }

            /* Reset fifo */
            XSPI_INTR = XSPI_INTR_IPTXWE_MASK;
        }

        remaining = size % XSPI_IP_WM_SIZE;

        /* Write remaining data for non aligned data */
        if (remaining) {
            /* Wait for fifo Empty */
            while(!(XSPI_INTR & XSPI_INTR_IPTXWE_MASK));

            for(j = 0; j < remaining; j+=4) {
                tx_data = 0;
                rem_size = (remaining < 4) ? remaining : 4;
                memcpy(&tx_data, data++, rem_size);
                xspi_writereg((uint32_t*)XSPI_TFD_BASE + j, tx_data);
            }

            /* Reset fifo */
            XSPI_INTR = XSPI_INTR_IPTXWE_MASK;
        }

        XSPI_IPCR1 = XSPI_ISEQID(LUT_INDEX_PP) | size;
        XSPI_IPCMD_START();

        /* Wait command done */
        while (!(XSPI_INTR & XSPI_IPCMDDONE))

        /* Flush fifo, set done flag */
        XSPI_IPTXFCR = XSPI_IPRCFCR_FLUSH;
        XSPI_INTR = XSPI_IPCMDDONE;

        len -= size;
        address += size;
    }
}

void xspi_flash_sec_erase(uintptr_t address)
{
    XSPI_IPCR0 = address;
    XSPI_IPCR1 = XSPI_ISEQID(LUT_INDEX_SE) | FLASH_ERASE_SIZE;
    XSPI_IPCMD_START();

    while(!(XSPI_INTR & XSPI_IPCMDDONE));

    XSPI_INTR &= ~XSPI_IPCMDDONE;
}


void hal_flash_unlock(void)
{
}
void hal_flash_lock(void)
{
}

int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    xspi_write_en(address);
    xspi_flash_write(address, data, len);

    return len;
}

int hal_flash_erase(uintptr_t address, int len)
{
    uint32_t num_sectors = 0;
    uint32_t i = 0;
    uint8_t status[4] = {0, 0, 0, 0};

    num_sectors = len / FLASH_ERASE_SIZE;
    num_sectors += (len % FLASH_ERASE_SIZE) ? 1 : 0;

    for (i = 0; i < num_sectors; i++) {
        xspi_write_en(address + i * FLASH_ERASE_SIZE);
        xspi_flash_sec_erase(address + i * FLASH_ERASE_SIZE);

        while (!(status[0] & FLASH_READY_MSK))  {
            xspi_read_sr(status, 0, 1);
        }
    }

    xspi_sw_reset();

    return len;
}

#ifdef EXT_FLASH
void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    xspi_write_en(address);
    xspi_flash_write(address, data, len);

    return len;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    address = (address & MASK_32BIT);
    memcpy(data, (void*)address, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    uint32_t num_sectors = 0;
    uint32_t i = 0;
    uint8_t status[4] = {0, 0, 0, 0};

    num_sectors = len / FLASH_ERASE_SIZE;
    num_sectors += (len % FLASH_ERASE_SIZE) ? 1 : 0;

    for (i = 0; i < num_sectors; i++) {
        xspi_write_en(address + i * FLASH_ERASE_SIZE);
        xspi_flash_sec_erase(address + i * FLASH_ERASE_SIZE);

        while (!(status[0] & FLASH_READY_MSK))  {
            xspi_read_sr(status, 0, 1);
        }
    }

    xspi_sw_reset();

    return len;
}
#endif /* EXT_FLASH */


void hal_prepare_boot(void)
{
    #if 0
        /* TODO: EL2 */
        switch_el3_to_el2();
    #endif
}

#ifdef TEST_HW_DDR
static int test_hw_ddr(void)
{
    int status = 0;
    uint64_t counter = 0;

    DDR_MTPn(0) = 0xffffffff;
    DDR_MTPn(1) = 0x00000001;
    DDR_MTPn(2) = 0x00000002;
    DDR_MTPn(3) = 0x00000003;
    DDR_MTPn(4) = 0x00000004;
    DDR_MTPn(5) = 0x00000005;
    DDR_MTPn(6) = 0xcccccccc;
    DDR_MTPn(7) = 0xbbbbbbbb;
    DDR_MTPn(8) = 0xaaaaaaaa;
    DDR_MTPn(9) = 0xffffffff;

    DDR_MTCR = DDR_MEM_TEST_EN;

    while (DDR_MTCR & DDR_MEM_TEST_EN)
        counter++;

    if (DDR_ERR_SBE & 0xffff || DDR_ERR_DETECT) {
        status = -1;
        wolfBoot_printf("DDR ECC error\n");
    }
    if (DDR_MTCR & DDR_MEM_TEST_FAIL) {
        status = -1;
        wolfBoot_printf("DDR self-test failed\n");
    } else {
        status = 0;
        wolfBoot_printf("DDR self-test passed\n");
    }

    return status;
}
#endif /* TEST_HW_DDR */

#ifdef TEST_DDR
static int test_ddr(void)
{
    int ret = 0;
    int i;
    uint32_t *ptr = (uint32_t*)(DDR_ADDRESS + TEST_DDR_OFFSET);
    uint32_t tmp[TEST_DDR_SIZE/4];

    memset(tmp, 0, sizeof(tmp));

    /* test write to DDR */
    for (i = 0; i < TEST_DDR_SIZE/4; i++) {
        ptr[i] = (uint32_t)i;
    }

    /* test read from DDR */
    for (i = 0; i < TEST_DDR_SIZE/4; i++) {
        tmp[i] = ptr[i];
    }

    /* compare results */
    for (i = 0; i < TEST_DDR_SIZE/4; i++) {
        if (tmp[i] != (uint32_t)i) {
            ret = -1;
            break;
        }
    }

    return ret;
}
#endif /* TEST_DDR */

#ifdef TEST_EXT_FLASH
#define TEST_ADDRESS 0x20012000

static int test_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE];

    /* Erase sector */
    ret = ext_flash_erase(TEST_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return -i;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* TEST_EXT_FLASH */

/* Function to set MMU MAIR memory attributes base on index */
void set_memory_attribute(uint32_t attr_idx, uint64_t mair_value)
{
    uint64_t mair = 0;

    asm volatile("mrs %0, mair_el3" : "=r"(mair));
    mair &= ~(0xffUL << (attr_idx * 8));
    mair |= (mair_value << (attr_idx * 8));
    asm volatile("msr mair_el3, %0" : : "r"(mair));
}

void hal_init_tzpc(void)
{
    TZDECPROT0_SET = 0xff; //0x86;
    TZDECPROT1_SET = 0xff; //0x00;
    TZPCR0SIZE = 0x00; //0x200;

    /* Enable TZASC to allow secure read/write access to the DDR */
    /* Really, we are allowing the full Region 0 to be R/W in secure world */
    TZASC_ACTION = TZASC_ACTION_ENABLE_DECERR;
    TZASC_REGION_ATTRIBUTES_0 = TZASC_REGION_ATTRIBUTES_ALLOW_SECRW;
    TZASC_GATE_KEEPER = TZASC_GATE_KEEPER_REQUEST_OPEN;
}

void hal_init(void)
{
    volatile uint32_t counter=0xFFFFul; /* used for delay */
#ifdef DEBUG_UART
    uart_init();
    wolfBoot_printf("wolfBoot Init\n");
#endif

    hal_init_tzpc();

    hal_flash_init();
    wolfBoot_printf("Flash init done\n");

#ifdef TEST_EXT_FLASH
    test_flash();
#endif

#ifdef TPM_TEST
    read_tpm_id();
    wolfBoot_printf("TPM test done\n");

#endif

    hal_ddr_init();
    wolfBoot_printf("DDR init done\n");
#ifdef TEST_HW_DDR
    test_hw_ddr();
#endif

    while (counter--);
    wolfBoot_printf("Delay is done\n");

#if TEST_DDR
    if (test_ddr() == -1) {
        wolfBoot_printf("DDR R/W test failed\n");
    }
    else {
        wolfBoot_printf("DDR R/W test passed\n");
    }
#endif

#if 0
    /* TODO: MMU enable? */
    mmu_enable();
    wolfBoot_printf("MMU init done\n");
#endif
}

#endif /* TARGET_nxp_ls1028a */
