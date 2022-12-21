/* spi_drv_stm32.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for stm32F4, using SPI1.
 *
 * Pinout: see spi_drv_stm32.h
 *
 * Copyright (C) 2022 wolfSSL Inc.
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
#include <stddef.h>
#include "spi_drv.h"
#include "spi_drv_stm32.h"

#if defined(SPI_FLASH) || defined(QSPI_FLASH)

void RAMFUNCTION stm_gpio_config(uint32_t base, uint32_t pin, uint32_t mode,
        uint32_t af, uint32_t pull, uint32_t speed)
{
    uint32_t reg;
    uint32_t base_num = 0;

    /* Determine GPIO clock bit */
    if      (base == GPIOA_BASE)
        base_num = 0;
    else if (base == GPIOB_BASE)
        base_num = 1;
#ifdef GPIOC_BASE
    else if (base == GPIOC_BASE)
        base_num = 2;
#endif
#ifdef GPIOD_BASE
    else if (base == GPIOD_BASE)
        base_num = 3;
#endif
#ifdef GPIOE_BASE
    else if (base == GPIOE_BASE)
        base_num = 4;
#endif
#ifdef GPIOF_BASE
    else if (base == GPIOF_BASE)
        base_num = 5;
#endif
#ifdef GPIOG_BASE
    else if (base == GPIOG_BASE)
        base_num = 6;
#endif
#ifdef GPIOH_BASE
    else if (base == GPIOH_BASE)
        base_num = 7;
#endif
#ifdef GPIOI_BASE
    else if (base == GPIOI_BASE)
        base_num = 8;
#endif
#ifdef GPIOJ_BASE
    else if (base == GPIOJ_BASE)
        base_num = 9;
#endif
#ifdef GPIOK_BASE
    else if (base == GPIOK_BASE)
        base_num = 10;
#endif

    /* Enable GPIO clock */
    RCC_GPIO_CLOCK_ER |= (1 << base_num);

    /* Set Mode and Alternate Function */
    reg = GPIO_MODE(base) & ~(0x03 << (pin * 2));
    GPIO_MODE(base) = reg | (mode << (pin * 2));
    if (mode < 2) {
        if (pin < 8)
            GPIO_AFL(base) &= ~(0xf << (pin * 4));
        else
            GPIO_AFH(base) &= ~(0xf << ((pin - 8) * 4));
    }
    else if (mode == 2) {
        /* alternate mode */
        if (pin < 8) {
            reg = GPIO_AFL(base) & ~(0xf << (pin * 4));
            GPIO_AFL(base) = reg | (af << (pin * 4));
        }
        else {
            reg = GPIO_AFH(base) & ~(0xf << ((pin - 8) * 4));
            GPIO_AFH(base) = reg | (af << ((pin - 8) * 4));
        }
    }

    /* configure for pull 0=float, 1=pull up, 2=pull down */
    reg = GPIO_PUPD(base) & ~(0x03 << (pin * 2));
    GPIO_PUPD(base) = reg | (pull << (pin * 2));

    /* configure output speed 0=low, 1=med, 2=high, 3=very high */
    reg = GPIO_OSPD(base) & ~(0x03 << (pin * 2));
    GPIO_OSPD(base) |= (speed << (pin * 2));
}

#ifdef SPI_FLASH
void RAMFUNCTION spi_cs_off(uint32_t base, int pin)
{
    GPIO_BSRR(base) |= (1 << pin);
    while (!(GPIO_ODR(base) & (1 << pin)))
        ;
}

void RAMFUNCTION spi_cs_on(uint32_t base, int pin)
{
    GPIO_BSRR(base) |= (1 << (pin + 16));
    while (GPIO_ODR(base) & (1 << pin))
        ;
}
#endif /* SPI_FLASH */


static void RAMFUNCTION stm_pins_setup(void)
{
#ifdef SPI_FLASH
    #ifdef PLATFORM_stm32l0
    stm_gpio_config(SPI_CLOCK_PIO_BASE, SPI_CLOCK_PIN, GPIO_MODE_AF,
        SPI_CLOCK_PIN_AF, 2, 3);
    stm_gpio_config(SPI_MOSI_PIO_BASE, SPI_MOSI_PIN, GPIO_MODE_AF,
        SPI_MOSI_PIN_AF, 2, 3);
    stm_gpio_config(SPI_MISO_PIO_BASE, SPI_MISO_PIN, GPIO_MODE_AF,
        SPI_MISO_PIN_AF, 2, 3);
    #else
    stm_gpio_config(SPI_CLOCK_PIO_BASE, SPI_CLOCK_PIN, GPIO_MODE_AF,
        SPI_CLOCK_PIN_AF, 0, 3);
    stm_gpio_config(SPI_MOSI_PIO_BASE, SPI_MOSI_PIN, GPIO_MODE_AF,
        SPI_MOSI_PIN_AF, 0, 0);
    stm_gpio_config(SPI_MISO_PIO_BASE, SPI_MISO_PIN, GPIO_MODE_AF,
        SPI_MISO_PIN_AF, 1, 0);
    #endif
#endif
#ifdef QSPI_FLASH
    stm_gpio_config(QSPI_CS_PIO_BASE, QSPI_CS_FLASH_PIN, GPIO_MODE_AF,
        QSPI_CS_FLASH_AF, 1, 3);
    stm_gpio_config(QSPI_CLOCK_PIO_BASE, QSPI_CLOCK_PIN, GPIO_MODE_AF,
        QSPI_CLOCK_PIN_AF, 0, 3);
    stm_gpio_config(QSPI_IO0_PIO_BASE, QSPI_IO0_PIN, GPIO_MODE_AF,
        QSPI_IO0_PIN_AF, 0, 3);
    stm_gpio_config(QSPI_IO1_PIO_BASE, QSPI_IO1_PIN, GPIO_MODE_AF,
        QSPI_IO1_PIN_AF, 0, 3);
    stm_gpio_config(QSPI_IO2_PIO_BASE, QSPI_IO2_PIN, GPIO_MODE_AF,
        QSPI_IO2_PIN_AF, 0, 3);
    stm_gpio_config(QSPI_IO3_PIO_BASE, QSPI_IO3_PIN, GPIO_MODE_AF,
        QSPI_IO3_PIN_AF, 0, 3);
#endif
}

static void stm_pins_release(void)
{
#ifdef SPI_FLASH
    stm_gpio_config(SPI_CLOCK_PIO_BASE, SPI_CLOCK_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(SPI_MOSI_PIO_BASE, SPI_MOSI_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(SPI_MISO_PIO_BASE, SPI_MISO_PIN, GPIO_MODE_INPUT, 0, 0, 0);
#endif
#ifdef QSPI_FLASH
    stm_gpio_config(QSPI_CS_PIO_BASE, QSPI_CS_FLASH_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(QSPI_CLOCK_PIO_BASE, QSPI_CLOCK_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(QSPI_IO0_PIO_BASE, QSPI_IO0_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(QSPI_IO1_PIO_BASE, QSPI_IO1_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(QSPI_IO2_PIO_BASE, QSPI_IO2_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(QSPI_IO3_PIO_BASE, QSPI_IO3_PIN, GPIO_MODE_INPUT, 0, 0, 0);
#endif
}

static void RAMFUNCTION spi_reset(void)
{
#ifdef QSPI_FLASH
    AHB3_CLOCK_RST |= RCC_AHB3ENR_QSPIEN;
    AHB3_CLOCK_RST &= ~RCC_AHB3ENR_QSPIEN;
#endif
#ifdef SPI_FLASH
    APB2_CLOCK_RST |= SPI1_APB2_CLOCK_ER_VAL;
    APB2_CLOCK_RST &= ~SPI1_APB2_CLOCK_ER_VAL;
#endif
}

#ifdef QSPI_FLASH
int qspi_transfer(
    const uint8_t cmd, uint32_t addr, uint32_t addrSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    uint32_t fmode = 0, amode = 0;
    uint32_t adsz = 0, dsz = 0;
    uint8_t* dptr = NULL;

    if (txData != NULL && txSz > 0) {
        fmode = 0; /* indirect write */
        dsz = txSz;
        dptr = (uint8_t*)txData;
    }
    else if (rxData != NULL && rxSz > 0) {
        fmode = 1; /* indirect read */
        dsz = rxSz;
        dptr = rxData;
    }
    else {
        mode = 0; /* no data */
    }

    /* Enable the QSPI peripheral */
    QUADSPI_CR |= QUADSPI_CR_EN;

    if (dsz > 0) {
        QUADSPI_DLR = dsz-1;
    }

    if (addrSz > 0) {
        amode = 1;
        adsz = addrSz-1;
    }

    /* Configure QSPI: CCR register with all communications parameters */
    /* mode 1=1SPI, 2=2SPI, 3=4SPI */
    QUADSPI_CCR = (
        QUADSPI_CCR_FMODE(fmode) |   /* Functional Mode */
        QUADSPI_CCR_IMODE(1) |       /* Instruction Mode */
        QUADSPI_CCR_ADMODE(amode) |  /* Address Mode */
        QUADSPI_CCR_ADSIZE(adsz) |   /* Address Size */
        QUADSPI_CCR_ABMODE(0) |      /* Alternate byte (none) */
        QUADSPI_CCR_DMODE(mode) |    /* Data Mode */
        QUADSPI_CCR_DCYC(dummySz) |  /* Dummy Cycles (between instruction and read) */
        cmd                          /* Instruction / Command byte */
    );

    /* Set command address 4 or 3 byte */
    QUADSPI_AR = addr;

    while (dsz > 0U) {
        if (fmode == 0) {
            while ((QUADSPI_SR & QUADSPI_SR_FTF) == 0);
            QUADSPI_DR = *dptr;
        }
        else {
            while ((QUADSPI_SR & (QUADSPI_SR_FTF | QUADSPI_SR_TCF)) == 0);
            *dptr = QUADSPI_DR;
        }
        dsz--;
        dptr++;
    }

    /* wait for transfer complete */
    while ((QUADSPI_SR & QUADSPI_SR_TCF) == 0);
    QUADSPI_FCR |= QUADSPI_SR_TCF; /* clear transfer complete */

    /* Disable QSPI */
    QUADSPI_CR &= ~QUADSPI_CR_EN;

    return 0;
}
#endif /* QSPI_FLASH */


#ifdef SPI_FLASH
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
#endif /* SPI_FLASH */

static int initialized = 0;
void RAMFUNCTION spi_init(int polarity, int phase)
{
    if (!initialized) {
        initialized++;

        /* Setup clocks */
#ifdef QSPI_FLASH
        /* Select QUADSPI clock source */
        RCC_D1CCIPR &= ~RCC_D1CCIPR_QSPISEL_MASK;
        RCC_D1CCIPR |= RCC_D1CCIPR_QSPISEL(QSPI_CLOCK_SEL);
        AHB3_CLOCK_EN |= RCC_AHB3ENR_QSPIEN;
#endif
#ifdef SPI_FLASH
        APB2_CLOCK_ER |= SPI1_APB2_CLOCK_ER_VAL;
#endif

        /* reset peripheral before setting up GPIO pins */
        spi_reset();

        /* Configure pings for SPI / QSPI */
        stm_pins_setup();

        /* Configure chip selects */
#ifdef SPI_FLASH
        stm_gpio_config(SPI_CS_PIO_BASE, SPI_CS_FLASH, GPIO_MODE_OUTPUT,
            0, 1, 3);
        spi_cs_off(SPI_CS_PIO_BASE, SPI_CS_FLASH);
#endif
#ifdef WOLFBOOT_TPM
        stm_gpio_config(SPI_CS_TPM_PIO_BASE, SPI_CS_TPM, GPIO_MODE_OUTPUT,
            0, 1, 3);
        spi_cs_off(SPI_CS_TPM_PIO_BASE, SPI_CS_TPM);
#endif

#ifdef QSPI_FLASH
        /* Configure QSPI FIFO Threshold (1 byte) */
        QUADSPI_CR &= ~QUADSPI_CR_FTHRES_MASK;
        QUADSPI_CR |= QUADSPI_CR_FTHRES(4);

        /* Wait till BUSY flag cleared */
        while (QUADSPI_SR & QUADSPI_SR_BUSY) {};

        /* Configure QSPI Clock Prescaler (64/X), Flash ID 0, Dual Flash=0,
         * Sample Shift=None */
        QUADSPI_CR &= ~(QUADSPI_CR_PRESCALER_MASK | QUADSPI_CR_FSEL |
            QUADSPI_CR_DFM | QUADSPI_CR_SSHIFT);
        QUADSPI_CR |= (QUADSPI_CR_PRESCALER((QSPI_CLOCK_BASE/QSPI_CLOCK_MHZ)));

        /* Configure QSPI Flash Size (16MB), CS High Time (1 clock) and
         * Clock Mode (0) */
        QUADSPI_DCR &= ~(QUADSPI_DCR_FSIZE_MASK | QUADSPI_DCR_CSHT_MASK |
            QUADSPI_DCR_CKMODE_3);
        QUADSPI_DCR |= (QUADSPI_DCR_FSIZE(22) | QUADSPI_DCR_CSHT(0) |
            QUADSPI_DCR_CKMODE_0);
#endif /* QSPI_FLASH */
#ifdef SPI_FLASH
        /* Configure SPI1 for master mode */
#   ifdef PLATFORM_stm32l0
        SPI1_CR1 = SPI_CR1_MASTER | (polarity << 1) | (phase << 0);
#   else
        /* baud rate 5 (hclk/6) */
        SPI1_CR1 = SPI_CR1_MASTER | (5 << 3) | (polarity << 1) | (phase << 0);
#   endif
        SPI1_CR2 |= SPI_CR2_SSOE;
        SPI1_CR1 |= SPI_CR1_SPI_EN;
#endif /* SPI_FLASH */
    }
}

void RAMFUNCTION spi_release(void)
{
    if (initialized > 0) {
        initialized--;
    }
    if (initialized == 0) {
        spi_reset();
    #ifdef SPI_FLASH
        SPI1_CR2 &= ~SPI_CR2_SSOE;
        SPI1_CR1 = 0;
    #endif
        stm_pins_release();
    }
}

#endif /* SPI_FLASH || QSPI_FLASH */
