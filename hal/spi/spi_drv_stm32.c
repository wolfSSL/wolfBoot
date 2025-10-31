/* spi_drv_stm32.c
 *
 * Driver for the SPI back-end of the SPI_FLASH module.
 *
 * Example implementation for stm32F4, using SPI1.
 *
 * Pinout: see spi_drv_stm32.h
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
#include <stddef.h>
#include "spi_drv.h"

#ifdef WOLFBOOT_STM32_SPIDRV

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM) || defined(QSPI_FLASH) || \
    defined(OCTOSPI_FLASH)

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
    /* Delay after an RCC peripheral clock enabling */
    reg = RCC_GPIO_CLOCK_ER;

    /* Set Mode and Alternate Function */
    reg = GPIO_MODE(base) & ~(0x03UL << (pin * 2));
    GPIO_MODE(base) = reg | (mode << (pin * 2));
    if (mode < 2) {
        if (pin < 8)
            GPIO_AFL(base) &= ~(0xfUL << (pin * 4));
        else
            GPIO_AFH(base) &= ~(0xfUL << ((pin - 8) * 4));
    }
    else if (mode == 2) {
        /* alternate mode */
        if (pin < 8) {
            reg = GPIO_AFL(base) & ~(0xfUL << (pin * 4));
            GPIO_AFL(base) = reg | (af << (pin * 4));
        }
        else {
            reg = GPIO_AFH(base) & ~(0xfUL << ((pin - 8) * 4));
            GPIO_AFH(base) = reg | (af << ((pin - 8) * 4));
        }
    }

    /* configure for pull 0=float, 1=pull up, 2=pull down */
    reg = GPIO_PUPD(base) & ~(0x03UL << (pin * 2));
    GPIO_PUPD(base) = reg | (pull << (pin * 2));

    /* configure output speed 0=low, 1=med, 2=high, 3=very high */
    reg = GPIO_OSPD(base) & ~(0x03UL << (pin * 2));
    GPIO_OSPD(base) |= (speed << (pin * 2));

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    /* TODO: Consider setting GPIO_SECCFGR(base) */
#endif
}

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
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
#endif /* SPI_FLASH || WOLFBOOT_TPM */


static void RAMFUNCTION stm_pins_setup(void)
{
#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
    #ifdef TARGET_stm32l0
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
#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)
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
#if defined (SPI_FLASH) || defined (WOLFBOOT_TPM)
    stm_gpio_config(SPI_CLOCK_PIO_BASE, SPI_CLOCK_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(SPI_MOSI_PIO_BASE, SPI_MOSI_PIN, GPIO_MODE_INPUT, 0, 0, 0);
    stm_gpio_config(SPI_MISO_PIO_BASE, SPI_MISO_PIN, GPIO_MODE_INPUT, 0, 0, 0);
#endif
#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)
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
#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)
    #ifndef TARGET_stm32u5
        AHB3_CLOCK_RST |= RCC_AHB3ENR_QSPIEN;
        AHB3_CLOCK_RST &= ~RCC_AHB3ENR_QSPIEN;
    #else
        AHB2_CLOCK_RST |= RCC_AHB2ENR_QSPIEN;
        AHB2_CLOCK_RST &= ~RCC_AHB2ENR_QSPIEN;
    #endif
#endif
#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
    APB2_CLOCK_RST |= SPI1_APB2_CLOCK_ER_VAL;
    APB2_CLOCK_RST &= ~SPI1_APB2_CLOCK_ER_VAL;
#endif
}

#ifdef OCTOSPI_FLASH
int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode)
{
    uint32_t adsz = 0, absz = 0;

    if (addrSz > 0) {
        adsz = addrSz-1;
    }
    if (altSz > 0) {
        absz = altSz-1;
    }

    /* Enable the QSPI peripheral */
    OCTOSPI_CR &= ~(OCTOSPI_CR_EN | OCTOSPI_CR_FMODE_MASK);
    OCTOSPI_CR |= OCTOSPI_CR_EN | OCTOSPI_CR_FMODE(fmode);

    if (dataSz > 0) {
        OCTOSPI_DLR = dataSz-1;
    }

    /* Configure QSPI: CCR register with all communications parameters */
    /* mode 1=1SPI, 2=2SPI, 3=4SPI, 4=8SPI */
    OCTOSPI_CCR = (
        OCTOSPI_CCR_IMODE(1) |         /* Instruction Mode - always single SPI */
        OCTOSPI_CCR_ADMODE(addrMode) | /* Address Mode */
        OCTOSPI_CCR_ADSIZE(adsz) |     /* Address Size */
        OCTOSPI_CCR_ABMODE(altMode) |  /* Alternate byte mode */
        OCTOSPI_CCR_ABSIZE(absz ) |    /* Alternate byte size */
        OCTOSPI_CCR_DMODE(dataMode)    /* Data Mode */
    );
    OCTOSPI_TCR = OCTOSPI_TCR_DCYC(dummySz); /* Dummy Cycles (between instruction and read) */
    OCTOSPI_IR = cmd;

    /* Set optional alternate bytes */
    if (altSz > 0) {
        OCTOSPI_ABR = alt;
    }

    /* Set command address 4 or 3 byte */
    OCTOSPI_AR = addr;

    /* Fill data 32-bits at a time */
    while (dataSz >= 4U) {
        if (fmode == 0) {
            while ((OCTOSPI_SR & OCTOSPI_SR_FTF) == 0);
            OCTOSPI_DR32 = *(uint32_t*)data;
        }
        else {
            while ((OCTOSPI_SR & (OCTOSPI_SR_FTF | OCTOSPI_SR_TCF)) == 0);
            *(uint32_t*)data = OCTOSPI_DR32;
        }
        dataSz -= 4;
        data += 4;
    }

    /* Fill remainder bytes */
    while (dataSz > 0U) {
        if (fmode == 0) {
            while ((OCTOSPI_SR & OCTOSPI_SR_FTF) == 0);
            OCTOSPI_DR = *data;
        }
        else {
            while ((OCTOSPI_SR & (OCTOSPI_SR_FTF | OCTOSPI_SR_TCF)) == 0);
            *data = OCTOSPI_DR;
        }
        dataSz--;
        data++;
    }

    /* wait for transfer complete */
    while ((OCTOSPI_SR & OCTOSPI_SR_TCF) == 0);
    OCTOSPI_FCR |= OCTOSPI_SR_TCF; /* clear transfer complete */

    /* Disable QSPI */
    OCTOSPI_CR &= ~OCTOSPI_CR_EN;

    return 0;
}
#elif defined(QSPI_FLASH)
int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode)
{
    uint32_t adsz = 0, absz = 0;

    if (addrSz > 0) {
        adsz = addrSz-1;
    }
    if (altSz > 0) {
        absz = altSz-1;
    }

    /* Enable the QSPI peripheral */
    QUADSPI_CR |= QUADSPI_CR_EN;

    if (dataSz > 0) {
        QUADSPI_DLR = dataSz-1;
    }

    /* Configure QSPI: CCR register with all communications parameters */
    /* mode 1=1SPI, 2=2SPI, 3=4SPI */
    QUADSPI_CCR = (
        QUADSPI_CCR_FMODE(fmode) |     /* Functional Mode */
        QUADSPI_CCR_IMODE(1) |         /* Instruction Mode - always single SPI */
        QUADSPI_CCR_ADMODE(addrMode) | /* Address Mode */
        QUADSPI_CCR_ADSIZE(adsz) |     /* Address Size */
        QUADSPI_CCR_ABMODE(altMode) |  /* Alternate byte mode */
        QUADSPI_CCR_ABSIZE(absz ) |    /* Alternate byte size */
        QUADSPI_CCR_DMODE(dataMode) |  /* Data Mode */
        QUADSPI_CCR_DCYC(dummySz) |    /* Dummy Cycles (between instruction and read) */
        cmd                            /* Instruction / Command byte */
    );

    /* Set optional alternate bytes */
    if (altSz > 0) {
        QUADSPI_ABR= alt;
    }

    /* Set command address 4 or 3 byte */
    QUADSPI_AR = addr;

    /* Fill data 32-bits at a time */
    while (dataSz >= 4U) {
        if (fmode == 0) {
            while ((QUADSPI_SR & QUADSPI_SR_FTF) == 0);
            QUADSPI_DR32 = *(uint32_t*)data;
        }
        else {
            while ((QUADSPI_SR & (QUADSPI_SR_FTF | QUADSPI_SR_TCF)) == 0);
            *(uint32_t*)data = QUADSPI_DR32;
        }
        dataSz -= 4;
        data += 4;
    }

    /* Fill remainder bytes */
    while (dataSz > 0U) {
        if (fmode == 0) {
            while ((QUADSPI_SR & QUADSPI_SR_FTF) == 0);
            QUADSPI_DR = *data;
        }
        else {
            while ((QUADSPI_SR & (QUADSPI_SR_FTF | QUADSPI_SR_TCF)) == 0);
            *data = QUADSPI_DR;
        }
        dataSz--;
        data++;
    }

    /* wait for transfer complete */
    while ((QUADSPI_SR & QUADSPI_SR_TCF) == 0);
    QUADSPI_FCR |= QUADSPI_SR_TCF; /* clear transfer complete */

    /* Disable QSPI */
    QUADSPI_CR &= ~QUADSPI_CR_EN;

    return 0;
}
#endif /* QSPI_FLASH */

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
uint8_t RAMFUNCTION spi_read(void)
{
    while (!(SPI1_SR & SPI_SR_RX_NOTEMPTY));
#ifdef SPI1_RXDR
    return SPI1_RXDR;
#else
    return SPI1_DR;
#endif
}

void RAMFUNCTION spi_write(const char byte)
{
    while (!(SPI1_SR & SPI_SR_TX_EMPTY));

#ifdef SPI1_TXDR
    SPI1_TXDR = (uint8_t)byte;
#else
    SPI1_DR = (uint8_t)byte;
#endif
}
#endif /* SPI_FLASH || WOLFBOOT_TPM */

static int initialized = 0;
void RAMFUNCTION spi_init(int polarity, int phase)
{
    if (!initialized) {
        initialized++;

/* Setup clocks */
#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)
    #ifdef TARGET_stm32u5
        /* Clock configuration for QSPI defaults to SYSCLK
         * (RM0456 section 11.8.47)
         */
    #else
        /* Select QUADSPI clock source */
        RCC_D1CCIPR &= ~RCC_D1CCIPR_QSPISEL_MASK;
        RCC_D1CCIPR |= RCC_D1CCIPR_QSPISEL(QSPI_CLOCK_SEL);
        AHB3_CLOCK_EN |= RCC_AHB3ENR_QSPIEN;
    #endif
#endif

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
        APB2_CLOCK_ER |= SPI1_APB2_CLOCK_ER_VAL;
        #ifdef TARGET_stm32h5
        RCC_CCIPR3 &= ~ (RCC_CCIPR3_SPI1SEL_MASK << RCC_CCIPR3_SPI1SEL_SHIFT);
        RCC_CCIPR3 |= (0 << RCC_CCIPR3_SPI1SEL_SHIFT); /* PLL1_Q */
        #endif
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

#ifdef OCTOSPI_FLASH
        /* STM32 OCTOSPI Peripheral */
        /* Configure OCTOSPI FIFO Threshold (4 bytes) */
        OCTOSPI_CR &= ~OCTOSPI_CR_FTHRES_MASK;
        OCTOSPI_CR |= OCTOSPI_CR_FTHRES(4);

        /* Wait till BUSY flag cleared */
        while (OCTOSPI_SR & OCTOSPI_SR_BUSY) {};

        /* Configure OCTOSPI Clock Prescaler (64/X), Flash ID 2 (IO4-7)
         * Sample Shift=None */
        OCTOSPI_DCR2 &= ~OCTOSPI_DCR2_PRESCALER_MASK;
        OCTOSPI_DCR2 |= OCTOSPI_DCR2_PRESCALER((QSPI_CLOCK_BASE/QSPI_CLOCK_MHZ));
        OCTOSPI_CR &= ~OCTOSPI_CR_FSEL;
    #if QSPI_FLASH_BANK == 2
        OCTOSPI_CR |= OCTOSPI_CR_FSEL;
    #endif
        OCTOSPI_TCR &= ~OCTOSPI_TCR_SSHIFT;

        /* Configure OCTOSPI Flash Size (16MB), CS High Time (1 clock) and
         * Clock Mode (0) */
        OCTOSPI_DCR1 &= ~(OCTOSPI_DCR1_DEVSIZE_MASK | OCTOSPI_DCR1_CSHT_MASK |
            OCTOSPI_DCR1_CKMODE_3);
        OCTOSPI_DCR1 |= (OCTOSPI_DCR1_DEVSIZE(QSPI_FLASH_SIZE) |
            OCTOSPI_DCR1_CSHT(0) | OCTOSPI_DCR1_CKMODE_0);

#elif defined(QSPI_FLASH)
        /* STM32 QSPI Peripheral */
        /* Configure QSPI FIFO Threshold (4 bytes) */
        QUADSPI_CR &= ~QUADSPI_CR_FTHRES_MASK;
        QUADSPI_CR |= QUADSPI_CR_FTHRES(4);

        /* Wait till BUSY flag cleared */
        while (QUADSPI_SR & QUADSPI_SR_BUSY) {};

        /* Configure QSPI Clock Prescaler (64/X), Flash ID 0, Dual Flash=0,
         * Sample Shift=None */
        QUADSPI_CR &= ~(QUADSPI_CR_PRESCALER_MASK | QUADSPI_CR_FSEL |
            QUADSPI_CR_DFM | QUADSPI_CR_SSHIFT);
        QUADSPI_CR |= (QUADSPI_CR_PRESCALER((QSPI_CLOCK_BASE/QSPI_CLOCK_MHZ)));
    #if QSPI_FLASH_BANK == 2
        QUADSPI_CR |= QUADSPI_CR_FSEL;
    #endif

        /* Configure QSPI Flash Size (16MB), CS High Time (1 clock) and
         * Clock Mode (0) */
        QUADSPI_DCR &= ~(QUADSPI_DCR_FSIZE_MASK | QUADSPI_DCR_CSHT_MASK |
            QUADSPI_DCR_CKMODE_3);
        QUADSPI_DCR |= (QUADSPI_DCR_FSIZE(QSPI_FLASH_SIZE) |
            QUADSPI_DCR_CSHT(0) | QUADSPI_DCR_CKMODE_0);
#endif
#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
        /* Configure SPI1 for master mode */
        SPI1_CR1 &= ~SPI_CR1_SPI_EN;
    #if defined(TARGET_stm32h5)
        /* Clear any faults in the status register */
        SPI1_IFCR = (SPI_IFCR_SUSPC | SPI_IFCR_MODFC | SPI_IFCR_TIFREC |
                     SPI_IFCR_OVRC | SPI_IFCR_UDRC);

        /* baud rate 2 (hclk/8), data size (8-bits), CRC Size (8-bits),
         * FIFO threshold level (1-data) */
        SPI1_CFG1 = (
            ((2 & SPI_CFG1_BAUDRATE_MASK) << SPI_CFG1_BAUDRATE_SHIFT) |
            ((7 & SPI_CFG1_CRCSIZE_MASK) << SPI_CFG1_CRCSIZE_SHIFT) |
            ((0 & SPI_CFG1_FTHLV_MASK) << SPI_CFG1_FTHLV_SHIFT) |
            ((7 & SPI_CFG1_DSIZE_MASK) << SPI_CFG1_DSIZE_SHIFT));
        SPI1_CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSOE |
            (polarity << SPI_CFG2_CLOCK_POL_SHIFT) |
            (phase << SPI_CFG2_CLOCK_PHASE_SHIFT);
    #else
        #ifndef TARGET_stm32l0 /* use existing/default baud for L0 */
        /* Baud rate 5 (hclk/6), data size 8 bits */
        SPI1_CR1 |= ((5 & SPI_CR1_BAUDRATE_MASK) << SPI_CR1_BAUDRATE_SHIFT);
        #endif
        SPI1_CR1 &= ~((1 << SPI_CR1_CLOCK_POL_SHIFT) | (1 << SPI_CR1_CLOCK_PHASE_SHIFT));
        SPI1_CR1 |= SPI_CR1_MASTER |
            (polarity << SPI_CR1_CLOCK_POL_SHIFT) |
            (phase << SPI_CR1_CLOCK_PHASE_SHIFT);
        SPI1_CR2 |= SPI_CR2_SSOE;
    #endif

        SPI1_CR1 |= SPI_CR1_SPI_EN; /* Enable SPI */

    #ifdef SPI_CR1_CSTART
        SPI1_CR1 |= SPI_CR1_CSTART; /* use continuous start mode */
    #endif
#endif /* SPI_FLASH || WOLFBOOOT_TPM */
    }
}

void RAMFUNCTION spi_release(void)
{
    if (initialized > 0) {
        initialized--;
    }
    if (initialized == 0) {
        spi_reset();
    #if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)
        #if defined(TARGET_stm32h5)
        SPI1_CFG2 &= ~SPI_CFG2_SSOE;
        #else
        SPI1_CR2 &= ~SPI_CR2_SSOE;
        #endif
        SPI1_CR1 = 0;
    #endif
        stm_pins_release();
    }
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

#endif /* SPI_FLASH || WOLFBOOT_TPM || QSPI_FLASH || OCTOSPI_FLASH */
#endif /* WOLFBOOT_STM32_SPIDRV */
