/* spi_drv_nrf5340.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for nrf52F4.
 *
 * Pinout: see spi_drv_nrf5340.h
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
#include "spi_flash.h"
#include "string.h"
#include "printf.h"

#ifdef TARGET_nrf5340

#if defined(QSPI_FLASH) || defined(SPI_FLASH) || defined(WOLFBOOT_TPM)

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
void spi_cs_off(uint32_t base, int pin)
{
    GPIO_OUTSET(base) = (1 << pin);
    while ((GPIO_OUT(base) & (1 << pin)) == 0)
        ;
}

void spi_cs_on(uint32_t base, int pin)
{
    GPIO_OUTCLR(base) = (1 << pin);
    while ((GPIO_OUT(base) & (1 << pin)) != 0)
        ;
}

uint8_t spi_read(void)
{
    volatile uint32_t reg = SPI_EV_RDY(SPI_PORT);
    while (!reg)
        reg = SPI_EV_RDY(SPI_PORT);
    reg = SPI_RXDATA(SPI_PORT);
    SPI_EV_RDY(SPI_PORT) = 0;
    return reg;
}

void spi_write(const char byte)
{
    uint32_t reg;
    SPI_EV_RDY(SPI_PORT) = 0;
    SPI_TXDATA(SPI_PORT) = (uint32_t)byte;
    reg = SPI_EV_RDY(SPI_PORT);
    while (!reg)
        reg = SPI_EV_RDY(SPI_PORT);
}
#endif

#ifdef QSPI_FLASH

void qspi_wait_ready(void)
{
    int timeout = 1000000;
    while (QSPI_EVENTS_READY == 0 && --timeout > 0) {
        NOP();
    }
    if (timeout == 0) {
    #ifdef DEBUG_QSPI
        wolfBoot_printf("QSPI Wait timeout!\n");
    #endif
    }
}

int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode)
{
    uint32_t cintData[2] = {0, 0};

    QSPI_EVENTS_READY = 0; /* clear events */

    if (addrSz == 0) { /* command only operation */
        if (dataSz > sizeof(cintData))
            dataSz = sizeof(cintData);
        if (fmode == QSPI_MODE_WRITE) {
            memcpy(cintData, data, dataSz);
            if (dataSz >= 4)
                QSPI_CINSTRDAT1 = cintData[1];
            if (dataSz > 0)
                QSPI_CINSTRDAT0 = cintData[0];
        }
        QSPI_CINSTRCONF = (
            QSPI_CINSTRCONF_OPCODE(cmd) |
            QSPI_CINSTRCONF_LENGTH(1 + dataSz) |
            QSPI_CINSTRCONF_LIO2 |
            QSPI_CINSTRCONF_LIO3   /* IO3 high (not reset) */
        );
    }
    else if (fmode == QSPI_MODE_WRITE && dataSz == 0) { /* erase */
        QSPI_ERASE_PTR = addr;
        QSPI_ERASE_LEN = SPI_FLASH_SECTOR_SIZE;

        QSPI_TASKS_ERASESTART = 1;
    }
    else if (fmode == QSPI_MODE_WRITE) { /* write */
        QSPI_WRITE_DST = addr;
        QSPI_WRITE_SRC = (uint32_t)data;
        QSPI_WRITE_CNT = dataSz;
        QSPI_TASKS_WRITESTART = 1;
    }
    else { /* read */
        QSPI_READ_DST = (uint32_t)data;
        QSPI_READ_SRC = addr;
        QSPI_READ_CNT = dataSz;
        QSPI_TASKS_READSTART = 1;
    }

    /* wait for generated ready event */
    qspi_wait_ready();

    /* command only read */
    if (addrSz == 0 && fmode == QSPI_MODE_READ) {
        cintData[1] = QSPI_CINSTRDAT1;
        cintData[0] = QSPI_CINSTRDAT0;
        memcpy(data, cintData, dataSz);
    }

    return 0;
}
#endif /* QSPI_FLASH */

static int spi_initialized = 0;
void spi_init(int polarity, int phase)
{
    uint32_t reg;

    if (spi_initialized) {
        return;
    }
    spi_initialized++;

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
    GPIO_PIN_CNF(SPI_CS_PIO_BASE, SPI_CS_FLASH) = GPIO_CNF_OUT;
    GPIO_PIN_CNF(SPI_CS_PIO_BASE, SPI_SCLK_PIN) = GPIO_CNF_OUT;
    GPIO_PIN_CNF(SPI_CS_PIO_BASE, SPI_MOSI_PIN) = GPIO_CNF_OUT;
    GPIO_PIN_CNF(SPI_CS_PIO_BASE, SPI_MISO_PIN) = GPIO_CNF_IN;
    GPIO_OUTSET(SPI_CS_PIO_BASE) = (1 << SPI_CS_FLASH);
    GPIO_OUTCLR(SPI_CS_PIO_BASE) = (1 << SPI_MOSI_PIN) | (1 << SPI_SCLK_PIN);

    SPI_PSEL_MISO(SPI_PORT) = SPI_MISO_PIN;
    SPI_PSEL_MOSI(SPI_PORT) = SPI_MOSI_PIN;
    SPI_PSEL_SCK(SPI_PORT) = SPI_SCLK_PIN;

    SPI_FREQUENCY(SPI_PORT) = SPI_FREQ_M1;
    SPI_CONFIG(SPI_PORT) = 0; /* mode 0,0 default */
    SPI_ENABLE(SPI_PORT) = 1;
    (void)reg;
#endif /* SPI_FLASH || WOLFBOOT_TPM */

#ifdef QSPI_FLASH
    /* Enable QSPI Clock */
    CLOCK_HFCLK192MSRC = 0; /* internal osc */
    CLOCK_HFCLK192MCTRL = QSPI_CLK_DIV;
    CLOCK_HFCLK192MSTART = 1;
    while (CLOCK_HFCLK192MSTARTED == 0);

    /* Configure QSPI Pins */
    QSPI_PSEL_SCK = PSEL_PORT(QSPI_CLK_PORT) | QSPI_CLK_PIN;
    QSPI_PSEL_CSN = PSEL_PORT(QSPI_CS_PORT)  | QSPI_CS_PIN;
    QSPI_PSEL_IO0 = PSEL_PORT(QSPI_IO0_PORT) | QSPI_IO0_PIN;
    QSPI_PSEL_IO1 = PSEL_PORT(QSPI_IO1_PORT) | QSPI_IO1_PIN;
    QSPI_PSEL_IO2 = PSEL_PORT(QSPI_IO2_PORT) | QSPI_IO2_PIN;
    QSPI_PSEL_IO3 = PSEL_PORT(QSPI_IO3_PORT) | QSPI_IO3_PIN;

    /* Configure all pins for GPIO input */
    GPIO_PIN_CNF(QSPI_CLK_PORT, QSPI_CLK_PIN) = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
    GPIO_PIN_CNF(QSPI_CS_PORT,  QSPI_CS_PIN)  = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
    GPIO_PIN_CNF(QSPI_IO0_PORT, QSPI_IO0_PIN) = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
    GPIO_PIN_CNF(QSPI_IO1_PORT, QSPI_IO1_PIN) = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
    GPIO_PIN_CNF(QSPI_IO2_PORT, QSPI_IO2_PIN) = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
    GPIO_PIN_CNF(QSPI_IO3_PORT, QSPI_IO3_PIN) = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
#if defined(QSPI_PWR_CTRL_PORT) && defined(QSPI_PWR_CTRL_PIN)
    GPIO_PIN_CNF(QSPI_PWR_CTRL_PORT, QSPI_PWR_CTRL_PIN) = (GPIO_CNF_IN_DIS | GPIO_CNF_HIGH_DRIVE);
    GPIO_OUTCLR(QSPI_PWR_CTRL_PORT) = (1 << QSPI_PWR_CTRL_PIN); /* active low */
#endif

    reg = QSPI_IFCONFIG0;
    reg &= ~(QSPI_IFCONFIG0_READOC_MASK | QSPI_IFCONFIG0_WRITEOC_MASK);
#if QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI
    reg |= QSPI_IFCONFIG0_READOC_READ4O | QSPI_IFCONFIG0_WRITEOC_PP4O;
#elif QSPI_DATA_MODE == QSPI_DATA_MODE_DSPI
    reg |= QSPI_IFCONFIG0_READOC_READ2O | QSPI_IFCONFIG0_WRITEOC_PP2O;
#else
    reg |= QSPI_IFCONFIG0_READOC_FASTREAD | QSPI_IFCONFIG0_WRITEOC_PP;
#endif
#if QSPI_ADDR_SZ == 4
    reg |= QSPI_IFCONFIG0_ADDRMODE_32BIT;
#else
    reg &= ~QSPI_IFCONFIG0_ADDRMODE_32BIT;
#endif
#if SPI_FLASH_PAGE_SIZE == 512
    reg |= QSPI_IFCONFIG0_PPSIZE_512;
#else
    reg &= ~QSPI_IFCONFIG0_PPSIZE_512;
#endif
    QSPI_IFCONFIG0 = reg;

#if 1 /* errata 121 */
    reg = QSPI_IFCONFIG0;
    #if QSPI_CLK_FREQ_DIV == 0 /* DIV1 */
    reg |= (1 << 16) | (1<<17);
    #else
    reg &= ~(1 << 17);
    reg |=  (1 << 16);
    #endif
    QSPI_IFCONFIG0 = reg;
    QSPI_IFTIMING = QSPI_IFTIMING_RXDELAY(6);
#endif /* errata 121 */

    reg = QSPI_IFCONFIG1;
    reg &= ~QSPI_IFCONFIG1_SCKDELAY_MASK;
    reg |= QSPI_IFCONFIG1_SCKDELAY(5);
    /* SCK = 96MHz / (SCKFREQ + 1) */
    reg &= ~QSPI_IFCONFIG1_SCKFREQ_MASK;
    reg |= QSPI_IFCONFIG1_SCKFREQ(QSPI_CLK_FREQ_DIV);
    if (polarity == 0 && phase == 0)
        reg &= ~QSPI_IFCONFIG1_SPIMODE3;
    else
        reg |= QSPI_IFCONFIG1_SPIMODE3;
    QSPI_IFCONFIG1 = reg;

    QSPI_ENABLE = 1;

    /* make sure interrupts are disabled */
    QSPI_INTENCLR = 1; /* write "1" to disable READY interrupt */

#ifdef DEBUG_QSPI
    /* Display QSPI config */
    reg = QSPI_IFCONFIG0;
    wolfBoot_printf(
        "QSPI Freq=%dMHz (Div Clk=%d/Sck=%d), Addr=%d-bits, PageSz=%d\n",
        QSPI_CLOCK_MHZ/1000000,
        (QSPI_CLK_DIV == 3) ? 4 : QSPI_CLK_DIV+1,
        QSPI_CLK_FREQ_DIV+1,
        (reg & QSPI_IFCONFIG0_ADDRMODE_32BIT) ? 32 : 24,
        (reg & QSPI_IFCONFIG0_PPSIZE_512)     ? 512 : 256);
#endif

    /* Activate QSPI */
#ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI Activate\n");
#endif
    QSPI_EVENTS_READY = 0; /* clear events */
    QSPI_TASKS_ACTIVATE = 1;
    qspi_wait_ready();
#endif /* QSPI_FLASH */
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
    if (spi_initialized) {
        spi_initialized--;

        /* Disable QSPI Clock to save power */
        QSPI_ENABLE = 0;
        CLOCK_HFCLK192MSTOP = 1;
    #if defined(QSPI_PWR_CTRL_PORT) && defined(QSPI_PWR_CTRL_PIN)
        GPIO_OUTSET(QSPI_PWR_CTRL_PORT) = (1 << QSPI_PWR_CTRL_PIN);
    #endif
    }
}

#ifdef WOLFBOOT_TPM
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    uint32_t i;
    spi_cs_on(SPI_CS_PIO_BASE, cs);
    for (i = 0; i < sz; i++) {
        spi_write((const char)tx[i]);
        rx[i] = spi_read();
    }
    if (!(flags & SPI_XFER_FLAG_CONTINUE)) {
        spi_cs_off(SPI_CS_PIO_BASE, cs);
    }
    return 0;
}
#endif /* WOLFBOOT_TPM */

#endif /* QSPI_FLASH || SPI_FLASH || WOLFBOOT_TPM */
#endif /* TARGET_ */
