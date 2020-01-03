/* stm32l0.c
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
#include <image.h>
/* STM32 L0 register configuration */

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")

/*** RCC ***/

#define RCC_BASE (0x40021000)
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x0C))
#define RCC_CR_PLLRDY                (1 << 25)
#define RCC_CR_PLLON                 (1 << 24)
#define RCC_CR_MSIRDY                 (1 << 9)
#define RCC_CR_MSION                  (1 << 8)
#define RCC_CR_HSI16RDY               (1 << 2)
#define RCC_CR_HSI16ON                (1 << 0)
#define RCC_CFGR_SW_MSI             0x0
#define RCC_CFGR_SW_HSI16           0x1
#define RCC_CFGR_SW_PLL             0x3
#define RCC_CFGR_PLLDIV2            (0x01 << 22)
#define RCC_CFGR_PLLMUL4            (0x01 << 18)
#define RCC_PRESCALER_DIV_NONE 0

/*** FLASH ***/
#define APB1_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x38))
#define PWR_APB1_CLOCK_ER_VAL   (1 << 28)
#define FLASH_BASE          (0x40022000)
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_PECR          (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_PEKEY         (*(volatile uint32_t *)(FLASH_BASE + 0x0c))
#define FLASH_PRGKEY        (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x18))
#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#define FLASH_PAGE_SIZE     (128)

/* Register values */
#define FLASH_ACR_ENABLE_PRFT                 (1 << 1)
#define FLASH_SR_BSY                          (1 << 0)
#define FLASH_SR_SIZERR                       (1 << 10)
#define FLASH_SR_PGAERR                       (1 << 9)
#define FLASH_SR_WRPERR                       (1 << 8)
#define FLASH_SR_EOP                          (1 << 0)
#define FLASH_PEKEY1                          (0x89ABCDEF)
#define FLASH_PEKEY2                          (0x02030405)
#define FLASH_PRGKEY1                         (0x8C9DAEBF)
#define FLASH_PRGKEY2                         (0x13141516)
#define FLASH_PECR_PELOCK                     (1 << 0)
#define FLASH_PECR_PRGLOCK                    (1 << 1)
#define FLASH_PECR_PROG                       (1 << 3)
#define FLASH_PECR_ERASE                      (1 << 9)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    if (waitstates && ((FLASH_ACR & 1) == 0))
        FLASH_ACR |=  1;
    if (!waitstates && ((FLASH_ACR & 1) == 1))
        FLASH_ACR &= 1;
    while ((FLASH_ACR & 1) != waitstates)
        ;
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
}

static void RAMFUNCTION clear_errors(void)
{
    FLASH_SR |= ( FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR |  FLASH_SR_EOP );
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    clear_errors();

    while (i < len) {
        if ((len - i > 3) && ((((address + i) & 0x03) == 0)  && ((((uint32_t)data) + i) & 0x03) == 0)) {
            src = (uint32_t *)data;
            dst = (uint32_t *)(address + FLASHMEM_ADDRESS_SPACE);
            flash_wait_complete();
            dst[i >> 2] = src[i >> 2];
            flash_wait_complete();
            i+=4;
        } else {
            uint32_t val;
            uint8_t *vbytes = (uint8_t *)(&val);
            int off = (address + i) - (((address + i) >> 2) << 2);
            dst = (uint32_t *)(address + FLASHMEM_ADDRESS_SPACE - off);
            val = dst[i >> 2];
            vbytes[off] = data[i];
            flash_wait_complete();
            dst[i >> 2] = val;
            flash_wait_complete();
            i++;
        }
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_PECR & FLASH_PECR_PELOCK) != 0) {
        FLASH_PEKEY = FLASH_PEKEY1;
        DMB();
        FLASH_PEKEY = FLASH_PEKEY2;
        DMB();
        while ((FLASH_PECR & FLASH_PECR_PELOCK) != 0)
            ;
    }
    if ((FLASH_PECR & FLASH_PECR_PRGLOCK) != 0) {
        FLASH_PRGKEY = FLASH_PRGKEY1;
        DMB();
        FLASH_PRGKEY = FLASH_PRGKEY2;
        DMB();
        while ((FLASH_PECR & FLASH_PECR_PRGLOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_PECR & FLASH_PECR_PRGLOCK) == 0)
        FLASH_PECR |= FLASH_PECR_PRGLOCK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int start = -1, end = -1;
    uint32_t end_address;
    uint32_t p;
    if (len == 0)
        return -1;
    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        FLASH_PECR |= FLASH_PECR_PROG | FLASH_PECR_ERASE;
        *(volatile uint32_t *)(p + FLASHMEM_ADDRESS_SPACE)  = 0xFFFFFFFF;
        FLASH_PECR &= ~(FLASH_PECR_PROG | FLASH_PECR_ERASE);
        flash_wait_complete();
    }
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;
    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_MSION;
    DMB();
    while ((RCC_CR & RCC_CR_MSIRDY) == 0) {};
    /* Select MSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    DMB();
    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t cpu_freq, hsi_freq, hpre, ppre1, ppre2, flash_waitstates;

    /* Enable Power controller */
    APB1_CLOCK_ER |= PWR_APB1_CLOCK_ER_VAL;

    /* Select clock parameters (CPU Speed = 32MHz) */
    cpu_freq = 32000000;
    hsi_freq = 16000000;
    hpre  = RCC_PRESCALER_DIV_NONE;
    ppre1 = RCC_PRESCALER_DIV_NONE;
    ppre2 = RCC_PRESCALER_DIV_NONE;
    flash_waitstates = 1;

    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSI16ON;
    DMB();
    while ((RCC_CR & RCC_CR_HSI16RDY) == 0) {};

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI16);
    DMB();

    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     */
    reg32 = RCC_CFGR;
    reg32 &= ~(0xF << 4);
    RCC_CFGR = (reg32 | (hpre << 4));
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x07 << 8);
    RCC_CFGR = (reg32 | (ppre1 << 8));
    DMB();
    reg32 &= ~(0x07 << 11);
    RCC_CFGR = (reg32 | (ppre2 << 11));
    DMB();
    reg32 &= ~(0x0F << 18);
    RCC_CFGR = (reg32 | RCC_CFGR_PLLMUL4);
    DMB();
    reg32 &= ~(0x03 << 22);
    RCC_CFGR = (reg32 | RCC_CFGR_PLLDIV2);
    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while (((RCC_CFGR >> 2) & 0x03) != RCC_CFGR_SW_PLL)
        ;
}

void hal_init(void)
{
    clock_pll_on(0);
}

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_release();
#endif
    hal_flash_lock();
    if ((FLASH_PECR & FLASH_PECR_PELOCK) == 0)
        FLASH_PECR |= FLASH_PECR_PELOCK;
    clock_pll_off();
}

