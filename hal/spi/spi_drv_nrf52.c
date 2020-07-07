/* spi_drv_nrf52.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for nrf52F4.
 *
 * Pinout: see spi_drv_nrf52.h
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
#include "spi_drv.h"
#include "spi_drv_nrf52.h"

#define SPI0 (0x40003000)
#define SPI1 (0x40004000)
#define SPI2 (0x40023000)

#define SPI SPI0
#define SPI_TASKS_START   *((volatile uint32_t *)(SPI + 0x10))
#define SPI_TASKS_STOP    *((volatile uint32_t *)(SPI + 0x14))
#define SPI_EVENTS_ENDRX  *((volatile uint32_t *)(SPI + 0x110))
#define SPI_EVENTS_END    *((volatile uint32_t *)(SPI + 0x118))
#define SPI_EVENTS_ENDTX  *((volatile uint32_t *)(SPI + 0x120))
#define SPI_EV_RDY        *((volatile uint32_t *)(SPI + 0x108))
#define SPI_INTENSET      *((volatile uint32_t *)(SPI + 0x304))
#define SPI_INTENCLR      *((volatile uint32_t *)(SPI + 0x308))
#define SPI_ENABLE        *((volatile uint32_t *)(SPI + 0x500))
#define SPI_PSEL_SCK      *((volatile uint32_t *)(SPI + 0x508))
#define SPI_PSEL_MOSI     *((volatile uint32_t *)(SPI + 0x50C))
#define SPI_PSEL_MISO     *((volatile uint32_t *)(SPI + 0x510))
#define SPI_RXDATA        *((volatile uint32_t *)(SPI + 0x518))
#define SPI_TXDATA        *((volatile uint32_t *)(SPI + 0x51C))
#define SPI_FREQUENCY     *((volatile uint32_t *)(SPI + 0x524))
#define SPI_CONFIG        *((volatile uint32_t *)(SPI + 0x554))

#define K125 0x02000000  
#define K250 0x04000000  
#define K500 0x08000000  
#define M1   0x10000000  
#define M2   0x20000000  
#define M4   0x40000000  
#define M8   0x80000000  

void RAMFUNCTION spi_cs_off(int pin)
{
    GPIO_OUTSET = (1 << pin);
    while ((GPIO_OUT & (1 << pin)) == 0)
        ;
}

void RAMFUNCTION spi_cs_on(int pin)
{
    GPIO_OUTCLR = (1 << pin);
    while ((GPIO_OUT & (1 << pin)) != 0)
        ;

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
