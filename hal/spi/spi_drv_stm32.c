/* spi_drv_stm32.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for stm32F4, using SPI1.
 *
 * Pinout: see spi_drv_stm32.h
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
#include "spi_drv_stm32.h"

void RAMFUNCTION spi_cs_off(int pin)
{
    SPI_PIO_CS_BSRR |= (1 << pin);
    while(!(SPI_PIO_CS_ODR & (1 << pin)))
        ;
}

void RAMFUNCTION spi_cs_on(int pin)
{
    SPI_PIO_CS_BSRR |= (1 << (pin + 16));
    while(SPI_PIO_CS_ODR & (1 << pin))
        ;
}


static void spi_flash_pin_setup(void)
{
    uint32_t reg;
    RCC_GPIO_CLOCK_ER |= SPI_PIO_CS_CEN;
    reg = SPI_PIO_CS_MODE & ~ (0x03 << (SPI_CS_FLASH * 2));
    SPI_PIO_CS_MODE = reg | (1 << (SPI_CS_FLASH * 2));
    reg = SPI_PIO_CS_PUPD & ~(0x03 <<  (SPI_CS_FLASH * 2));
    SPI_PIO_CS_PUPD = reg | (0x01 << (SPI_CS_FLASH * 2));
    reg = SPI_PIO_CS_OSPD & ~(0x03 << (SPI_CS_FLASH * 2));
    SPI_PIO_CS_OSPD |= (0x03 << (SPI_CS_FLASH * 2));
    spi_cs_off(SPI_CS_FLASH);
}

static void spi_tpm2_pin_setup(void)
{
#ifdef WOLFBOOT_TPM
    uint32_t reg;
    RCC_GPIO_CLOCK_ER |= SPI_PIO_CS_CEN;
    reg = SPI_PIO_CS_MODE & ~ (0x03 << (SPI_CS_TPM * 2));
    SPI_PIO_CS_MODE = reg | (1 << (SPI_CS_TPM * 2));
    reg = SPI_PIO_CS_PUPD & ~(0x03 <<  (SPI_CS_TPM * 2));
    SPI_PIO_CS_PUPD = reg | (0x01 << (SPI_CS_TPM * 2));
    reg = SPI_PIO_CS_OSPD & ~(0x03 << (SPI_CS_TPM * 2));
    SPI_PIO_CS_OSPD |= (0x03 << (SPI_CS_TPM * 2));
    spi_cs_off(SPI_CS_TPM);
#endif
}

static void spi1_pins_setup(void)
{
    uint32_t reg;
    RCC_GPIO_CLOCK_ER |= SPI_PIO_CEN;
    /* Set mode = AF */
    reg = SPI_PIO_MODE & ~ (0x03 << (SPI1_CLOCK_PIN * 2));
    SPI_PIO_MODE = reg | (2 << (SPI1_CLOCK_PIN * 2));
    reg = SPI_PIO_MODE & ~ (0x03 << (SPI1_MOSI_PIN * 2));
    SPI_PIO_MODE = reg | (2 << (SPI1_MOSI_PIN * 2));
    reg = SPI_PIO_MODE & ~ (0x03 << (SPI1_MISO_PIN * 2));
    SPI_PIO_MODE = reg | (2 << (SPI1_MISO_PIN * 2));

    /* Alternate function: use low pins (5,6,7) */
    reg = SPI_PIO_AFL & ~(0xf << ((SPI1_CLOCK_PIN) * 4));
    SPI_PIO_AFL = reg | (SPI1_PIN_AF << ((SPI1_CLOCK_PIN) * 4));
    reg = SPI_PIO_AFL & ~(0xf << ((SPI1_MOSI_PIN) * 4));
    SPI_PIO_AFL = reg | (SPI1_PIN_AF << ((SPI1_MOSI_PIN) * 4));
    reg = SPI_PIO_AFL & ~(0xf << ((SPI1_MISO_PIN) * 4));
    SPI_PIO_AFL = reg | (SPI1_PIN_AF << ((SPI1_MISO_PIN) * 4));

#ifdef PLATFORM_stm32l0
    reg = SPI_PIO_PUPD & ~(0x03 <<  (SPI1_CLOCK_PIN * 2));
    SPI_PIO_PUPD = reg | (0x02 << (SPI1_CLOCK_PIN * 2));
    reg = SPI_PIO_PUPD & ~(0x03 <<  (SPI1_MOSI_PIN * 2));
    SPI_PIO_PUPD = reg | (0x02 << (SPI1_MOSI_PIN * 2));
    reg = SPI_PIO_PUPD & ~(0x03 <<  (SPI1_MISO_PIN * 2));
    SPI_PIO_PUPD = reg | (0x02 << (SPI1_MISO_PIN * 2));

    reg = SPI_PIO_OSPD & ~(0x03 << (SPI1_CLOCK_PIN * 2));
    SPI_PIO_OSPD |= (0x03 << (SPI1_CLOCK_PIN * 2));
    reg = SPI_PIO_OSPD & ~(0x03 << (SPI1_MOSI_PIN * 2));
    SPI_PIO_OSPD |= (0x03 << (SPI1_MOSI_PIN * 2));
    reg = SPI_PIO_OSPD & ~(0x03 << (SPI1_MISO_PIN * 2));
    SPI_PIO_OSPD |= (0x03 << (SPI1_MISO_PIN * 2));
#endif
}

static void spi_pins_release(void)
{
    uint32_t reg;
    /* Set mode = 0 */
    SPI_PIO_MODE &= ~ (0x03 << (SPI1_CLOCK_PIN * 2));
    SPI_PIO_MODE &= ~ (0x03 << (SPI1_MOSI_PIN * 2));
    SPI_PIO_MODE &= ~ (0x03 << (SPI1_MISO_PIN * 2));

    /* Alternate function clear */
    SPI_PIO_AFL &= ~(0xf << ((SPI1_CLOCK_PIN) * 4));
    SPI_PIO_AFL &= ~(0xf << ((SPI1_MOSI_PIN) * 4));
    SPI_PIO_AFL &= ~(0xf << ((SPI1_MISO_PIN) * 4));

    /* Floating */
    SPI_PIO_PUPD &= ~ (0x03 << (SPI1_CLOCK_PIN * 2));
    SPI_PIO_PUPD &= ~ (0x03 << (SPI1_MOSI_PIN * 2));
    SPI_PIO_PUPD &= ~ (0x03 << (SPI1_MISO_PIN * 2));

    /* Release CS */
    SPI_PIO_CS_MODE &= ~ (0x03 << (SPI_CS_FLASH * 2));
    SPI_PIO_CS_PUPD &= ~ (0x03 <<  (SPI_CS_FLASH * 2));

}

static void spi1_reset(void)
{
    APB2_CLOCK_RST |= SPI1_APB2_CLOCK_ER_VAL;
    APB2_CLOCK_RST &= ~SPI1_APB2_CLOCK_ER_VAL;
}

uint8_t RAMFUNCTION spi_read(void)
{
    volatile uint32_t reg;
    do {
        reg = SPI1_SR;
    } while(!(reg & SPI_SR_RX_NOTEMPTY));
    return (uint8_t)SPI1_DR;
}

void RAMFUNCTION spi_write(const char byte)
{
    int i;
    volatile uint32_t reg;
    do {
        reg = SPI1_SR;
    } while ((reg & SPI_SR_TX_EMPTY) == 0);
    SPI1_DR = byte;
    do {
        reg = SPI1_SR;
    } while ((reg & SPI_SR_TX_EMPTY) == 0);
}


void spi_init(int polarity, int phase)
{
    static int initialized = 0;
    if (!initialized) {
        initialized++;
        spi1_pins_setup();
        spi_flash_pin_setup();
        spi_tpm2_pin_setup();
        APB2_CLOCK_ER |= SPI1_APB2_CLOCK_ER_VAL;
        spi1_reset();
#ifdef PLATFORM_stm32l0
        SPI1_CR1 = SPI_CR1_MASTER | (polarity << 1) | (phase << 0);
#else
        SPI1_CR1 = SPI_CR1_MASTER | (5 << 3) | (polarity << 1) | (phase << 0);
#endif
        SPI1_CR2 |= SPI_CR2_SSOE;
        SPI1_CR1 |= SPI_CR1_SPI_EN;
    }
}

void RAMFUNCTION spi_release(void)
{
    spi1_reset();
	SPI1_CR2 &= ~SPI_CR2_SSOE;
	SPI1_CR1 = 0;
    spi_pins_release();
}
