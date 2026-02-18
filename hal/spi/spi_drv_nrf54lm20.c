/* spi_drv_nrf54lm20.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Pinout: see spi_drv_nrf54lm20.h
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
#include <stdint.h>
#include "spi_drv.h"

#ifdef TARGET_nrf54lm20

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)

#include "hal/nrf54lm20.h"
#include "hal/spi/spi_drv_nrf54lm20.h"

static uint8_t spi_tx_byte;
static volatile uint8_t spi_rx_byte;
static volatile uint8_t spi_rx_ready;

static inline void spim_clear_events(void)
{
    SPI_EVENTS_STARTED = 0;
    SPI_EVENTS_STOPPED = 0;
    SPI_EVENTS_END = 0;
    SPI_EVENTS_DMA_RX_END = 0;
    SPI_EVENTS_DMA_RX_READY = 0;
    SPI_EVENTS_DMA_RX_BUSERROR = 0;
    SPI_EVENTS_DMA_TX_END = 0;
    SPI_EVENTS_DMA_TX_READY = 0;
    SPI_EVENTS_DMA_TX_BUSERROR = 0;
}

void RAMFUNCTION spi_cs_off(uint32_t base, int pin)
{
    uint32_t mask = (1U << pin);
    GPIO_OUTSET(base) = mask;
}

void RAMFUNCTION spi_cs_on(uint32_t base, int pin)
{
    uint32_t mask = (1U << pin);
    GPIO_OUTCLR(base) = mask;
}

uint8_t RAMFUNCTION spi_read(void)
{
    while (!spi_rx_ready)
        ;
    spi_rx_ready = 0;
    return spi_rx_byte;
}

void RAMFUNCTION spi_write(const char byte)
{
    spi_tx_byte = (uint8_t)byte;
    spi_rx_ready = 0;

    spim_clear_events();

    SPI_DMA_RX_PTR = (uint32_t)&spi_rx_byte;
    SPI_DMA_RX_MAXCNT = 1;
    SPI_DMA_RX_LIST = 0;

    SPI_DMA_TX_PTR = (uint32_t)&spi_tx_byte;
    SPI_DMA_TX_MAXCNT = 1;
    SPI_DMA_TX_LIST = 0;

    SPI_TASKS_START = SPIM_TASKS_START_TASKS_START_Trigger;
    while (SPI_EVENTS_END == 0)
        ;
    SPI_TASKS_STOP = SPIM_TASKS_STOP_TASKS_STOP_Trigger;
    while (SPI_EVENTS_STOPPED == 0)
        ;
    SPI_EVENTS_STOPPED = 0;
    spi_rx_ready = 1;
}


void spi_init(int polarity, int phase)
{
    static int initialized = 0;
    if (!initialized) {
        initialized++;
        GPIO_PIN_CNF(SPI_CS_PORT, SPI_CS_PIN) =
            (GPIO_CNF_OUT | GPIO_CNF_HIGH_DRIVE_0);
        GPIO_PIN_CNF(SPI_SCK_PORT, SPI_SCK_PIN) =
            (GPIO_CNF_OUT | GPIO_CNF_HIGH_DRIVE_0);
        GPIO_PIN_CNF(SPI_MOSI_PORT, SPI_MOSI_PIN) =
            (GPIO_CNF_OUT | GPIO_CNF_HIGH_DRIVE_0);
        GPIO_PIN_CNF(SPI_MISO_PORT, SPI_MISO_PIN) =
            (GPIO_CNF_IN | GPIO_CNF_PULL_UP);

        GPIO_OUTSET(SPI_CS_PORT) = (1 << SPI_CS_PIN);
        GPIO_OUTCLR(SPI_SCK_PORT) = (1 << SPI_SCK_PIN);
        GPIO_OUTCLR(SPI_MOSI_PORT) = (1 << SPI_MOSI_PIN);

        SPI_ENABLE_REG = SPIM_ENABLE_ENABLE_Disabled;
        SPI_PSEL_MISO = (PSEL_PORT(SPI_MISO_PORT) | SPI_MISO_PIN);
        SPI_PSEL_MOSI = (PSEL_PORT(SPI_MOSI_PORT) | SPI_MOSI_PIN);
        SPI_PSEL_SCK  = (PSEL_PORT(SPI_SCK_PORT)  | SPI_SCK_PIN);
        SPI_PSEL_CSN  = 0xFFFFFFFFUL; /* manual CS */

        SPI_PRESCALER_REG = SPI_PRESCALER_DIV;

        uint32_t cfg = (SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos);
        if (phase)
            cfg |= (SPIM_CONFIG_CPHA_Trailing << SPIM_CONFIG_CPHA_Pos);
        if (polarity)
            cfg |= (SPIM_CONFIG_CPOL_ActiveLow << SPIM_CONFIG_CPOL_Pos);
        SPI_CONFIG_REG = cfg;

        SPI_IFTIMING_RXDELAY = 0;
        SPI_IFTIMING_CSNDUR = 2;
        SPI_DMA_RX_LIST = 0;
        SPI_DMA_TX_LIST = 0;

        SPI_ENABLE_REG = SPIM_ENABLE_ENABLE_Enabled;
    }
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{

}

#ifdef WOLFBOOT_TPM
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    uint32_t i;
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

#endif /* SPI_FLASH || WOLFBOOT_TPM */
#endif /* TARGET_ */
