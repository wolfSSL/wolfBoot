/* spi_drv_nrf52.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for nrf52F4.
 *
 * Pinout: see spi_drv_nrf52.h
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

#ifdef TARGET_nrf52

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)

#include "hal/nrf52.h"

void RAMFUNCTION spi_cs_off(uint32_t base, int pin)
{
    GPIO_OUTSET = (1 << pin);
    while ((GPIO_OUT & (1 << pin)) == 0)
        ;
    (void)base;
}

void RAMFUNCTION spi_cs_on(uint32_t base, int pin)
{
    GPIO_OUTCLR = (1 << pin);
    while ((GPIO_OUT & (1 << pin)) != 0)
        ;
    (void)base;
}

uint8_t RAMFUNCTION spi_read(void)
{
    volatile uint32_t reg = SPI_EV_RDY;
    while (!reg)
        reg = SPI_EV_RDY;
    reg = SPI_RXDATA;
    SPI_EV_RDY = 0;
    return reg;
}

void RAMFUNCTION spi_write(const char byte)
{
    uint32_t reg;
    SPI_EV_RDY = 0;
    SPI_TXDATA = (uint32_t)byte;
    reg = SPI_EV_RDY;
    while (!reg)
        reg = SPI_EV_RDY;
}


void spi_init(int polarity, int phase)
{
    static int initialized = 0;
    if (!initialized) {
        initialized++;
        GPIO_PIN_CNF[SPI_CS_PIN]   = GPIO_CNF_OUT;
        GPIO_PIN_CNF[SPI_SCLK_PIN] = GPIO_CNF_OUT;
        GPIO_PIN_CNF[SPI_MOSI_PIN] = GPIO_CNF_OUT;
        GPIO_PIN_CNF[SPI_MISO_PIN] = GPIO_CNF_IN;
        //GPIO_DIRSET = ((1 << SPI_CS_PIN) | (1 << SPI_SCLK_PIN) | (1 << SPI_MOSI_PIN));
        GPIO_OUTSET = (1 << SPI_CS_PIN);
        GPIO_OUTCLR = (1 << SPI_MOSI_PIN) | (1 << SPI_SCLK_PIN);

        SPI_PSEL_MISO = SPI_MISO_PIN;
        SPI_PSEL_MOSI = SPI_MOSI_PIN;
        SPI_PSEL_SCK = SPI_SCLK_PIN;

        SPI_FREQUENCY = M1;
        SPI_CONFIG = 0; /* mode 0,0 default */
        SPI_ENABLE = 1;
    }
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
