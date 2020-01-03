/* stm32g0.c
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

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot STM32G0 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

/* STM32 G0 register configuration */

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")

/*** RCC ***/

#define RCC_BASE (0x40021000)
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  //RM0444 - 5.4.1
#define RCC_PLLCFGR         (*(volatile uint32_t *)(RCC_BASE + 0x0C))  //RM0444 - 5.4.4
#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x08))  //RM0444 - 5.4.3
#define APB1_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x3C))
#define APB2_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x40))


#define RCC_CR_PLLRDY               (1 << 25)
#define RCC_CR_PLLON                (1 << 24)
#define RCC_CR_HSIRDY               (1 << 10)
#define RCC_CR_HSION                (1 << 8)

#define RCC_CFGR_SW_HSISYS          0x0
#define RCC_CFGR_SW_PLL             0x2
#define RCC_PLLCFGR_PLLR_EN       (1 << 28) //RM0444 - 5.4.3

#define RCC_PLLCFGR_PLLSRC_HSI16  2


/*** APB PRESCALER ***/
#define RCC_PRESCALER_DIV_NONE 0

/*** FLASH ***/
#define PWR_APB1_CLOCK_ER_VAL       (1 << 28)
#define SYSCFG_APB2_CLOCK_ER_VAL    (1 << 0) //RM0444 - 5.4.15 - RCC_APBENR2 - SYSCFGEN

#define FLASH_BASE          (0x40022000)  /*FLASH_R_BASE = 0x40000000UL + 0x00020000UL + 0x00002000UL */
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00)) //RM0444 - 3.7.1 - FLASH_ACR
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08)) //RM0444 - 3.7.2 - FLASH_KEYR
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10)) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14)) //RM0444 - 3.7.5 - FLASH_CR

#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#define FLASH_PAGE_SIZE     (0x800) /* 2KB */

/* Register values */
#define FLASH_SR_BSY1                         (1 << 16) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_SIZERR                       (1 << 6)  //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_PGAERR                       (1 << 5) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_WRPERR                       (1 << 4) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_PROGERR                      (1 << 3)
#define FLASH_SR_EOP                          (1 << 0) //RM0444 - 3.7.4 - FLASH_SR

#define FLASH_CR_LOCK                         (1 << 31) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_STRT                         (1 << 16) //RM0444 - 3.7.5 - FLASH_CR

#define FLASH_CR_PER                          (1 << 1) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_PG                           (1 << 0) //RM0444 - 3.7.5 - FLASH_CR

#define FLASH_CR_PNB_SHIFT                     3     //RM0444 - 3.7.5 - FLASH_CR - PNB bits 8:3
#define FLASH_CR_PNB_MASK                      0x3f   //RM0444 - 3.7.5 - FLASH_CR  - PNB bits 8:3 - 6 bits

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & 0x03) != waitstates)
        FLASH_ACR =  (reg & ~0x03) | waitstates ;
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & FLASH_SR_BSY1) == FLASH_SR_BSY1)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    FLASH_SR |= ( FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR |  FLASH_SR_PROGERR);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    flash_clear_errors();
    FLASH_CR |= FLASH_CR_PG;

    while (i < len) {
        flash_clear_errors();
        if ((len - i > 3) && ((((address + i) & 0x07) == 0)  && ((((uint32_t)data) + i) & 0x07) == 0)) {
            src = (uint32_t *)data;
            dst = (uint32_t *)(address + FLASHMEM_ADDRESS_SPACE);
            flash_wait_complete();
            dst[i >> 2] = src[i >> 2];
            dst[(i >> 2) + 1] = src[(i >> 2) + 1];
            flash_wait_complete();
            i+=8;
        } else {
            uint32_t val[2];
            uint8_t *vbytes = (uint8_t *)(val);
            int off = (address + i) - (((address + i) >> 3) << 3);
            uint32_t base_addr = address & (~0x07); /* aligned to 64 bit */
            int u32_idx = (i >> 2);
            dst = (uint32_t *)(base_addr);
            val[0] = dst[u32_idx];
            val[1] = dst[u32_idx + 1];
            while ((off < 8) && (i < len))
                vbytes[off++] = data[i++];
            dst[u32_idx] = val[0];
            dst[u32_idx + 1] = val[1];
            flash_wait_complete();
        }
    }
    if ((FLASH_SR & FLASH_SR_EOP) == FLASH_SR_EOP)
        FLASH_SR |= FLASH_SR_EOP;
    FLASH_CR &= ~FLASH_CR_PG;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEY = FLASH_KEY1;
        DMB();
        FLASH_KEY = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
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
        uint32_t reg = FLASH_CR & (~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT));
        FLASH_CR = reg | ((p >> 11) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        flash_wait_complete();
        FLASH_CR &= ~FLASH_CR_PER;
    }
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

   /* Select HSISYS as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();
    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

/*This implementation will setup HSI RC 16 MHz as PLL Source Mux, PLLCLK as System Clock Source*/
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t cpu_freq, plln, pllm, pllq, pllp, pllr, hpre, ppre, flash_waitstates;

    /* Enable Power controller */
    APB1_CLOCK_ER |= PWR_APB1_CLOCK_ER_VAL;

    /* Select clock parameters (CPU Speed = 64MHz) */
    cpu_freq = 64000000;
    pllm = 4;
    plln = 80;
    pllp = 10;
    pllq = 5;
    pllr = 5;
    hpre  = RCC_PRESCALER_DIV_NONE;
    ppre = RCC_PRESCALER_DIV_NONE;
    flash_waitstates = 2;

    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSISYS as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();

    /* Disable PLL */
    RCC_CR &= ~RCC_CR_PLLON;

    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     */
    reg32 = RCC_CFGR;
    reg32 &= ~(0xF0); //don't change bits [0-3] that were previously set
    RCC_CFGR = (reg32 | (hpre << 8)); //RM0444 - 5.4.3 - RCC_CFGR
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x1C00); //don't change bits [0-14]
    RCC_CFGR = (reg32 | (ppre << 12));  //RM0444 - 5.4.3 - RCC_CFGR
    DMB();

    /* Set PLL config */
    reg32 = RCC_PLLCFGR;
    reg32 |= RCC_PLLCFGR_PLLSRC_HSI16;
    reg32 |= ((pllm - 1) << 4);
    reg32 |= plln << 8;
    reg32 |= ((pllp - 1) << 17);
//    reg32 |= ((pllq - 1) << 25); /* ? - Not in in RM0464 for STM32G0x0 */
    reg32 |= ((pllr - 1) << 29);
    RCC_PLLCFGR = reg32;

    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_PLLCFGR |= RCC_PLLCFGR_PLLR_EN;
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /* SYSCFG, COMP and VREFBUF clock enable */
    APB2_CLOCK_ER |= SYSCFG_APB2_CLOCK_ER_VAL;
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
    clock_pll_off();
}

