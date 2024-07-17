/* spi_drv_renesas_rx.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for Renesas RX65N.
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include <stdint.h>
#include "spi_drv.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "printf.h"

#include "hal/renesas-rx.h"
#include "spi_drv_renesas_rx.h"

#if defined(SPI_FLASH) || defined(QSPI_FLASH)

#ifdef SPI_FLASH
static int rx_spi_init_done = 0;
#endif
#ifdef QSPI_FLASH
static int rx_qspi_init_done = 0;
static uint16_t rx_qspi_cmd_def;
#endif

/* RSPI1: P27/RSPCKB-A, P26/MOSIB-A, P30/MISOB-A, P31/SSLB0-A */
/* QSPI:  PD2/QIO2-B, PD3/QIO3-B, PD4/QSSL-B, PD5/QSPCLK-B, PD6/QIO0-B, PD7/QIO1-B */
void spi_init(int polarity, int phase)
{
#ifdef SPI_FLASH
    /* Release RSPI1 module stop (clear bit) */
    PROTECT_OFF();
    /* SYS_MSTPCRB: bit 17=RSPI0, 16=RSPI1, SYS_MSTPCRC: bit 22=RSPI2 */
#if FLASH_RSPI_PORT == 0
    SYS_MSTPCRB &= ~(1 << 17);
#elif FLASH_RSPI_PORT == 1
    SYS_MSTPCRB &= ~(1 << 16);
#elif FLASH_RSPI_PORT == 2
    SYS_MSTPCRC &= ~(1 << 22);
#endif
    PROTECT_ON();

    /* Configure P26-27 and P30-31 for alt mode */
    PORT_PMR(0x2) |= ((1 << 6) | (1 << 7));
    PORT_PMR(0x3) |= (1 << 0);
    PORT_PDR(0x3) &= ~(1 << 0); /* input */
    #ifdef FLASH_SPI_USE_HW_CS
    PORT_PMR(0x3) |= (1 << 1);
    #else
    PORT_PDR(0x3) |= (1 << 1); /* output */
    #endif

    /* Disable MPC Write Protect for PFS */
    MPC_PWPR &= ~MPC_PWPR_B0WI;
    MPC_PWPR |=  MPC_PWPR_PFSWE;

    /* Pin Function Select */
    MPC_PFS(0x76) = 0xD; /* P26/MOSIB-A */
    MPC_PFS(0x77) = 0xD; /* P27/RSPCKB-A */
    MPC_PFS(0x78) = 0xD; /* P30/MISOB-A */
    #ifdef FLASH_SPI_USE_HW_CS
    MPC_PFS(0x79) = 0xD; /* P31/SSLB0-A */
    #endif

    /* Enable MPC Write Protect for PFS */
    MPC_PWPR &= ~(MPC_PWPR_PFSWE | MPC_PWPR_B0WI);
    MPC_PWPR |=   MPC_PWPR_PFSWE;

    /* Configure RSPI */
    RSPI_SPPCR(FLASH_RSPI_PORT) = (RSPI_SPPCR_MOIFV | RSPI_SPPCR_MOIDE); /* enable idle fixing */
    RSPI_SPSCR(FLASH_RSPI_PORT) = RSPI_SPSCR_SPSLN(0); /* seq len 1 */
    RSPI_SPBR(FLASH_RSPI_PORT)  = 5; /* 5Mbps */
    RSPI_SPDCR(FLASH_RSPI_PORT) = (RSPI_SPDCR_SPFC(0) | RSPI_SPDCR_SPBYT); /* frames=1, SPDR=byte */
    RSPI_SPCKD(FLASH_RSPI_PORT) = RSPI_SPCKD_SCKDL(0); /* 1 clock delay (SSL assert and first clock cycle) */
    RSPI_SSLND(FLASH_RSPI_PORT) = RSPI_SSLND_SLNDL(0); /* 1 clock delay (last clock cycle and SSL negation) */
    RSPI_SPND(FLASH_RSPI_PORT)  = RSPI_SPND_SPNDL(0); /* Next-Access Delay: 1RSPCK+2PCLK */
    RSPI_SPCR2(FLASH_RSPI_PORT) = 0; /* no parity */
    RSPI_SPCMD(FLASH_RSPI_PORT, 0) = (
        RSPI_SPCMD_BRDV(1) | /* div/1 */
        RSPI_SPCMD_SSLA(0) | /* slave select 0 */
        RSPI_SPCMD_SSLKP |   /* keep signal level between transfers */
        RSPI_SPCMD_SPB(7) |  /* 8-bit data */
        RSPI_SPCMD_SPNDEN |  /* enable Next-Access Delay */
        RSPI_SPCMD_SCKDEN    /* enable RSPCK Delay */
    );
    if (polarity)
        RSPI_SPCMD(FLASH_RSPI_PORT, 0) |= RSPI_SPCMD_CPOL;
    if (phase)
        RSPI_SPCMD(FLASH_RSPI_PORT, 0) |= RSPI_SPCMD_CPHA;

    /* Master SPI operation (4-wire method) */
    RSPI_SPCR(FLASH_RSPI_PORT) = RSPI_SPCR_MSTR;

    rx_spi_init_done++;
#endif /* SPI_FLASH */

#ifdef QSPI_FLASH
    /* Release QSPI module stop (clear bit) */
    PROTECT_OFF();
    /* SYS_MSTPCRC: bit 23=QSPI */
    SYS_MSTPCRC &= ~(1 << 23);
    PROTECT_ON();

    /* Configure PD2-PD7 for alt mode */
    PORT_PMR(0xD) |= ((1 << 2) | (1 << 3) | (1 << 4) |
                      (1 << 5) | (1 << 6) | (1 << 7));

    /* Disable MPC Write Protect for PFS */
    MPC_PWPR &= ~MPC_PWPR_B0WI;
    MPC_PWPR |=  MPC_PWPR_PFSWE;

    /* Pin Function Select */
    MPC_PFS(0x6A) = 0x1B; /* PD2/QIO2-B */
    MPC_PFS(0x6B) = 0x1B; /* PD3/QIO3-B */
    MPC_PFS(0x6C) = 0x1B; /* PD4/QSSL-B */
    MPC_PFS(0x6D) = 0x1B; /* PD5/QSPCLK-B */
    MPC_PFS(0x6E) = 0x1B; /* PD6/QIO0-B */
    MPC_PFS(0x6F) = 0x1B; /* PD7/QIO1-B */

    /* Enable MPC Write Protect for PFS */
    MPC_PWPR &= ~(MPC_PWPR_PFSWE | MPC_PWPR_B0WI);
    MPC_PWPR |=   MPC_PWPR_PFSWE;

    /* Configure QSPI */
    QSPI_SPCR = QSPI_SPCR_MSTR; /* Master mode */
    QSPI_SSLP &= ~QSPI_SSLP_SSLP; /* SS Active low */
    QSPI_SPPCR = (QSPI_SPPCR_MOIFV | QSPI_SPPCR_MOIDE); /* enable idle fixing */
    QSPI_SPBR  = 1; /* 30Mhz */
    QSPI_SPCKD = QSPI_SPCKD_SCKDL(0); /* 1 clock delay (SSL assert and first clock cycle) */
    QSPI_SSLND = QSPI_SSLND_SLNDL(0); /* 1 clock delay (last clock cycle and SSL negation) */
    QSPI_SPND  = QSPI_SPND_SPNDL(0); /* Next-Access Delay: 1RSPCK+2PCLK */
    QSPI_SPDCR = 0; /* no dummy TX */

    /* Setup default QSPI commands */
    rx_qspi_cmd_def = (
        QSPI_SPCMD_SPIMOD(0) | /* Single SPI */
        QSPI_SPCMD_SPB(0) |    /* use byte */
        QSPI_SPCMD_BRDV(0) |   /* div/1 (no div) */
        QSPI_SPCMD_SSLKP |     /* keep signal level between transfers */
        QSPI_SPCMD_SPNDEN |    /* enable Next-Access Delay */
        QSPI_SPCMD_SLNDEN |    /* enable negation Delay */
        QSPI_SPCMD_SCKDEN      /* enable RSPCK Delay */
    );
    if (polarity)
        rx_qspi_cmd_def |= QSPI_SPCMD_CPOL;
    if (phase)
        rx_qspi_cmd_def |= QSPI_SPCMD_CPHA;
    QSPI_SPCMD(0) = rx_qspi_cmd_def;
    QSPI_SPCMD(1) = rx_qspi_cmd_def;
    QSPI_SPCMD(2) = rx_qspi_cmd_def;
    QSPI_SPCMD(3) = rx_qspi_cmd_def;

    rx_qspi_init_done++;
#endif /* QSPI_FLASH */
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
#ifdef SPI_FLASH
    RSPI_SPCR(FLASH_RSPI_PORT) &= ~RSPI_SPCR_SPE; /* Disable SPI master */
#endif
#ifdef QSPI_FLASH
    QSPI_SPCR &= ~QSPI_SPCR_SPE; /* Disable QSPI master */
#endif
}
#endif /* SPI_FLASH || QSPI_FLASH */


#ifdef SPI_FLASH
void spi_cs_on(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
#ifdef FLASH_SPI_USE_HW_CS
    /* Enable SPI Master */
    RSPI_SPCR(FLASH_RSPI_PORT) |= RSPI_SPCR_SPE;
    RSPI_SPCMD(FLASH_RSPI_PORT, 0) |= RSPI_SPCMD_SSLKP;
#else
    PORT_PODR(0x3) &= ~(1 << 1); /* drive low */
#endif
}
void spi_cs_off(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
#ifdef FLASH_SPI_USE_HW_CS
    RSPI_SPCMD(FLASH_RSPI_PORT, 0) &= ~RSPI_SPCMD_SSLKP;
    RSPI_SPCR(FLASH_RSPI_PORT) &= ~RSPI_SPCR_SPE;
#else
    PORT_PODR(0x3) |= (1 << 1); /* drive high */
#endif
}

void spi_write(const char byte)
{
    while ((RSPI_SPSR(FLASH_RSPI_PORT) & RSPI_SPSR_SPTEF) == 0);
    RSPI_SPSR8(FLASH_RSPI_PORT) = byte;
}
uint8_t spi_read(void)
{
    while ((RSPI_SPSR(FLASH_RSPI_PORT) & RSPI_SPSR_SPRF) == 0);
    return RSPI_SPSR8(FLASH_RSPI_PORT);
}

#ifdef WOLFBOOT_TPM
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    uint32_t i;
    if (!rx_spi_init_done) {
        wolfBoot_printf("SPI init not yet called\n");
        return -1;
    }

    spi_cs_on(SPI_CS_TPM_PIO_BASE, cs);
    for (i = 0; i < sz; i++) {
        spi_write((const char)tx[i]);
        rx[i] = spi_read();
    }
    if (!(flags & SPI_XFER_FLAG_CONTINUE)) {
        spi_cs_off(SPI_CS_TPM_PIO_BASE, cs);
    }
    return 0;
}
#endif /* WOLFBOOT_TPM */

#endif /* SPI_FLASH */


#ifdef QSPI_FLASH

static uint32_t fifoLvl = 0;
static void qspi_cmd(const uint8_t* cmd, uint32_t cmdSz)
{
    uint8_t tmp;
    while (cmdSz > 0) {
        while ((QSPI_SPSR & QSPI_SPSR_SPTEF) == 0);
        if (cmd != NULL)
            QSPI_SPDR8 = *cmd++;
        else
            QSPI_SPDR8 = 0xFF;
        QSPI_SPSR &= ~QSPI_SPSR_SPTEF;
        cmdSz--;
        fifoLvl++;
    }
}

/* dataSz in bytes */
static int qspi_data(const uint32_t* txData, uint32_t* rxData, uint32_t dataSz)
{
    volatile uint32_t tmp;
    uint32_t i;
    uint8_t *pTx, *pRx;

    /* flush anything in the RX FIFO */
    while (fifoLvl > 0) {
        while ((QSPI_SPSR & QSPI_SPSR_SPRFF) == 0);
        tmp = QSPI_SPDR8;
        QSPI_SPSR &= ~QSPI_SPSR_SPRFF;
        fifoLvl--;
    }

    /* Do full FIFO (32 bytes) TX/RX - word */
    while (dataSz >= (QSPI_FIFO_SIZE/2)) {
        /* Transfer bytes - fill 16 bytes */
        while ((QSPI_SPSR & QSPI_SPSR_SPTEF) == 0);
        for (i=0; i<(QSPI_FIFO_SIZE/2); i+=4) {
            tmp = 0xFFFFFFFF;
            if (txData) {
                tmp = *txData++;
            #ifndef BIG_ENDIAN_ORDER
                tmp = __builtin_bswap32(tmp);
            #endif
            }
            QSPI_SPDR32 = tmp;
            dataSz -= 4;
            fifoLvl += 4;
        }
        QSPI_SPSR &= ~QSPI_SPSR_SPTEF;

        /* Recieve bytes - (previous 16 bytes) */
        while ((QSPI_SPSR & QSPI_SPSR_SPRFF) == 0);
        while (fifoLvl > (QSPI_FIFO_SIZE/2)) {
            tmp = QSPI_SPDR32;
            if (rxData) {
            #ifndef BIG_ENDIAN_ORDER
                tmp = __builtin_bswap32(tmp);
            #endif
                *rxData++ = tmp;
            }
            fifoLvl -= 4;
        }
        QSPI_SPSR &= ~QSPI_SPSR_SPRFF;
    }

    /* Remainder < FIFO TX/RX - byte */
    pTx = (uint8_t*)txData;
    pRx = (uint8_t*)rxData;

    /* Transmit Data */
    while (dataSz > 0) {
        while ((QSPI_SPSR & QSPI_SPSR_SPTEF) == 0);
        if (pTx)
            QSPI_SPDR8 = *pTx++;
        else
            QSPI_SPDR8 = 0xFF;
        QSPI_SPSR &= ~QSPI_SPSR_SPTEF;
        dataSz--;
        fifoLvl++;
    }

    /* wait for transfer to finish */
    while ((QSPI_SPSR & QSPI_SPSR_SPSSLF) == 0);

    /* Recieve bytes */
    while (fifoLvl > 0) {
        while ((QSPI_SPSR & QSPI_SPSR_SPRFF) == 0);
        if (pRx)
            *pRx++ = QSPI_SPDR8;
        else
            tmp = QSPI_SPDR8;
        fifoLvl--;
        QSPI_SPSR &= ~QSPI_SPSR_SPRFF;
    }

    return 0;
}

/* fmode = read (1) / write (0) */
int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode)
{
    int ret;
    uint8_t seq = 0;
    volatile uint32_t reg;
    uint32_t timeout = 10000;

    if (!rx_qspi_init_done) {
        wolfBoot_printf("QSPI init not yet called\n");
        return -1;
    }

    /* Clear flags - write 0 to bit to clear */
    QSPI_SPSR &= ~(QSPI_SPSR_SPTEF | QSPI_SPSR_SPRFF | QSPI_SPSR_SPSSLF);

    /* Reset buffers */
    QSPI_SPBFCR |= (QSPI_SPBFCR_RXRST | QSPI_SPBFCR_TXRST);
    reg = QSPI_SPBFCR; /* SPBFCR requires dummy read after write */
    /* Set FIFO Trigger Level - must be set when SPCR.SPE=0 */
    //QSPI_SPBFCR = QSPI_SPBFCR_RXTRG(5) | QSPI_SPBFCR_TXTRG(3); /* RX Trig=16 bytes, TX Trig=16 bytes */
    QSPI_SPBFCR = QSPI_SPBFCR_RXTRG(0) | QSPI_SPBFCR_TXTRG(6); /* RX Trig=1 byte, TX Trig=0 bytes */
    reg = QSPI_SPBFCR; /* SPBFCR requires dummy read after write */

    /* Command / Instruction - Write (command always SPI mode) */
    QSPI_SPBMUL(seq) = 1; /* Set Data length */
    QSPI_SPCMD(seq) = (rx_qspi_cmd_def | QSPI_SPCMD_SPIMOD(0));
    seq++;

    /* Address Write */
    if (addrSz > 0 && addrMode != QSPI_DATA_MODE_NONE) {
        QSPI_SPBMUL(seq) = addrSz;
        QSPI_SPCMD(seq) = (rx_qspi_cmd_def | QSPI_SPCMD_SPIMOD(addrMode-1));
        seq++;
    }

    /* Alternate bytes */
    if (altSz > 0 && altMode != QSPI_DATA_MODE_NONE) {
        QSPI_SPBMUL(seq) = altSz;
        QSPI_SPCMD(seq) = (rx_qspi_cmd_def | QSPI_SPCMD_SPIMOD(altMode-1));
        seq++;
    }

    /* Data */
    if (dataSz > 0 && dataMode != QSPI_DATA_MODE_NONE) {
        QSPI_SPBMUL(seq) = dataSz;
        QSPI_SPCMD(seq) = (rx_qspi_cmd_def | QSPI_SPCMD_SPIMOD(dataMode-1));
        if (fmode == QSPI_MODE_READ)
            QSPI_SPCMD(seq) |= QSPI_SPCMD_SPREAD;
        seq++;
    }

    /* End CS (set high) on last transaction */
    QSPI_SPCMD(seq-1) &= ~QSPI_SPCMD_SSLKP;

    /* Set number of sequences */
    QSPI_SPSCR = QSPI_SPSCR_SPSC(seq-1);

    /* Enable the QSPI peripheral */
    QSPI_SPCR |= QSPI_SPCR_SPE;

    /* Transfer Data for sequences */
    qspi_cmd(&cmd, 1);
    if (addrMode != QSPI_DATA_MODE_NONE) {
        qspi_cmd((const uint8_t*)&addr, addrSz);
    }
    if (altMode != QSPI_DATA_MODE_NONE) {
        qspi_cmd((const uint8_t*)&alt, altSz);
    }
    if (dummySz > 0) {
        qspi_cmd(NULL, dummySz/8);
    }
    if (fmode == QSPI_MODE_READ)
        qspi_data(NULL, (uint32_t*)data, dataSz);
    else
        qspi_data((const uint32_t*)data, NULL, dataSz);

    /* wait for slave select to de-assert */
    while ((QSPI_SPSR & QSPI_SPSR_SPSSLF) == 0 && --timeout > 0) {
        hal_delay_us(1);
    }

    /* check for timeout (-1) or success */
    ret = (timeout == 0) ? -1 : 0;

    /* Clear flags - write 0 to bit to clear */
    QSPI_SPSR &= ~(QSPI_SPSR_SPTEF | QSPI_SPSR_SPRFF | QSPI_SPSR_SPSSLF);

    /* Disable QSPI */
    QSPI_SPCR &= ~QSPI_SPCR_SPE;

    return ret;
}
#endif /* QSPI_FLASH */
