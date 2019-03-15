/* spi_drv.h
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for stm32F4, using SPI1.
 *
 * Pinout: see spi_drv_stm32f4.h
 *
 * Copyright (C) 2018 wolfSSL Inc.
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
#include "spi_drv_stm32f4.h"

void spi_cs_off(void)
{
    int i;
    GPIOE_BSRR |= (1 << SPI_FLASH_PIN);
    while(!(GPIOE_ODR & (1 << SPI_FLASH_PIN)))
        ;
    for(i = 0; i < 168000; i++)
        ;
}

void spi_cs_on(void)
{
    GPIOE_BSRR |= (1 << (SPI_FLASH_PIN + 16));
    while(GPIOE_ODR & (1 << SPI_FLASH_PIN))
        ;
}


static void spi_flash_pin_setup(void)
{
    uint32_t reg;
    AHB1_CLOCK_ER |= GPIOE_AHB1_CLOCK_ER;
    reg = GPIOE_MODE & ~ (0x03 << (SPI_FLASH_PIN * 2));
    GPIOE_MODE = reg | (1 << (SPI_FLASH_PIN * 2));

    reg = GPIOE_PUPD & ~(0x03 <<  (SPI_FLASH_PIN * 2));
    GPIOE_PUPD = reg | (0x01 << (SPI_FLASH_PIN * 2));

    reg = GPIOE_OSPD & ~(0x03 << (SPI_FLASH_PIN * 2));
    GPIOE_OSPD |= (0x03 << (SPI_FLASH_PIN * 2));

}

static void spi1_pins_setup(void)
{
    uint32_t reg;
    AHB1_CLOCK_ER |= GPIOB_AHB1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOB_MODE & ~ (0x03 << (SPI1_CLOCK_PIN * 2));
    GPIOB_MODE = reg | (2 << (SPI1_CLOCK_PIN * 2));
    reg = GPIOB_MODE & ~ (0x03 << (SPI1_MOSI_PIN * 2));
    GPIOB_MODE = reg | (2 << (SPI1_MOSI_PIN * 2));
    reg = GPIOB_MODE & ~ (0x03 << (SPI1_MISO_PIN * 2));
    GPIOB_MODE = reg | (2 << (SPI1_MISO_PIN * 2));

    /* Alternate function: use low pins (5,6,7) */
    reg = GPIOB_AFL & ~(0xf << ((SPI1_CLOCK_PIN) * 4));
    GPIOB_AFL = reg | (SPI1_PIN_AF << ((SPI1_CLOCK_PIN) * 4));
    reg = GPIOB_AFL & ~(0xf << ((SPI1_MOSI_PIN) * 4));
    GPIOB_AFL = reg | (SPI1_PIN_AF << ((SPI1_MOSI_PIN) * 4));
    reg = GPIOB_AFL & ~(0xf << ((SPI1_MISO_PIN) * 4));
    GPIOB_AFL = reg | (SPI1_PIN_AF << ((SPI1_MISO_PIN) * 4));
}

static void spi_pins_release(void)
{
    uint32_t reg;
    /* Set mode = 0 */
    GPIOB_MODE &= ~ (0x03 << (SPI1_CLOCK_PIN * 2));
    GPIOB_MODE &= ~ (0x03 << (SPI1_MOSI_PIN * 2));
    GPIOB_MODE &= ~ (0x03 << (SPI1_MISO_PIN * 2));

    /* Alternate function clear */
    GPIOB_AFL &= ~(0xf << ((SPI1_CLOCK_PIN) * 4));
    GPIOB_AFL &= ~(0xf << ((SPI1_MOSI_PIN) * 4));
    GPIOB_AFL &= ~(0xf << ((SPI1_MISO_PIN) * 4));
    
    /* Floating */
    GPIOB_PUPD &= ~ (0x03 << (SPI1_CLOCK_PIN * 2));
    GPIOB_PUPD &= ~ (0x03 << (SPI1_MOSI_PIN * 2));
    GPIOB_PUPD &= ~ (0x03 << (SPI1_MISO_PIN * 2));
    
    /* Release CS */
    GPIOE_MODE &= ~ (0x03 << (SPI_FLASH_PIN * 2));
    GPIOE_PUPD &= ~ (0x03 <<  (SPI_FLASH_PIN * 2));

    /* Disable GPIOB+GPIOE clock */
    AHB1_CLOCK_ER &= ~(GPIOB_AHB1_CLOCK_ER | GPIOE_AHB1_CLOCK_ER);
}

static void spi1_reset(void)
{
    APB2_CLOCK_RST |= SPI1_APB2_CLOCK_ER_VAL;
    APB2_CLOCK_RST &= ~SPI1_APB2_CLOCK_ER_VAL;
}

uint8_t spi_read(void)
{
    volatile uint32_t reg;
    do {
        reg = SPI1_SR;
    } while(!(reg & SPI_SR_RX_NOTEMPTY));
    return (uint8_t)SPI1_DR;
}

void spi_write(const char byte)
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
    spi1_pins_setup();
    spi_flash_pin_setup();
    APB2_CLOCK_ER |= SPI1_APB2_CLOCK_ER_VAL;
    spi1_reset();
	SPI1_CR1 = SPI_CR1_MASTER | (5 << 3) | (polarity << 1) | (phase << 0);
	SPI1_CR2 |= SPI_CR2_SSOE;
    SPI1_CR1 |= SPI_CR1_SPI_EN;
}

void spi_release(void)
{
    spi1_reset();
	SPI1_CR2 &= ~SPI_CR2_SSOE;
	SPI1_CR1 = 0;
    spi_pins_release();
}

