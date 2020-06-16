/* stm32wb.c
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
#include "image.h"
#ifdef WOLFSSL_STM32_PKA
#include "stm32wbxx_hal.h"
PKA_HandleTypeDef hpka = { };
#endif

/* STM32 WB register configuration */

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")

/*** RCC ***/
#ifndef RCC_BASE
#define RCC_BASE (0x58000000)
#endif
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x0C))

#ifndef WOLFSSL_STM32_PKA
#define RCC_CR_PLLRDY                (1 << 25)
#define RCC_CR_PLLON                 (1 << 24)
#define RCC_CR_MSIRDY                (1 << 1)
#define RCC_CR_MSION                 (1 << 0)
#define RCC_CR_HSIRDY                (1 << 10)
#define RCC_CR_HSION                 (1 << 8)
#define RCC_CR_MSIRANGE_SHIFT        4
#define RCC_CR_MSIRANGE_6            (0x06 << 4)
#define RCC_CR_MSIRANGE_Msk          (0x0F << 4)
#endif /* !WOLFSSL_STM32_PKA */

#define RCC_CFGR_SW_MSI               0x0
#define RCC_CFGR_SW_PLL               0x3
#define RCC_CFGR_SW_MASK              0x3

#define RCC_CFGR_HPRE_MASK  0x0F
#define RCC_CFGR_PPRE1_MASK 0x07
#define RCC_CFGR_PPRE2_MASK 0x07
#define RCC_CFGR_HPRE_SHIFT  4
#define RCC_CFGR_PPRE1_SHIFT 8
#define RCC_CFGR_PPRE2_SHIFT 11

#define RCC_PLLCFGR_SRC_SHIFT 0
#define RCC_PLLCFGR_PLLSRC_MSI      0x1
#define RCC_PLLCFGR_PLLSRC_MASK     0x3
#define RCC_PLLCFGR_PLLM_DIV2       (0x1 << 4)
#define RCC_PLLCFGR_PLLM_MASK       (0x3 << 4)
#define RCC_PLLCFGR_PLLN_32         (32   << 8)
#define RCC_PLLCFGR_PLLN_MASK       (0x7f << 8)
#define RCC_PLLCFGR_PLLP_DIV5       (4 << 17)
#define RCC_PLLCFGR_PLLP_MASK       (0x7 << 17)
#define RCC_PLLCFGR_PLLQ_DIV4       (3 << 25)
#define RCC_PLLCFGR_PLLQ_MASK       (0x7 << 25)
#define RCC_PLLCFGR_PLLR_DIV2       (1 << 29)
#define RCC_PLLCFGR_PLLR_MASK       (0x7 << 29)
#define RCC_PLLCFGR_PLLP_EN         (1 << 16)
#define RCC_PLLCFGR_PLLQ_EN         (1 << 24)
#define RCC_PLLCFGR_PLLR_EN         (1 << 28)

#define RCC_PRESCALER_DIV_NONE 0

/*** FLASH ***/
#ifndef FLASH_BASE
#define FLASH_BASE          (0x58004000)
#endif
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14))

#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#define FLASH_PAGE_SIZE     (0x1000) /* 4KB */

/* Register values */
#define FLASH_ACR_LATENCY_MASK                (0x07)

#ifndef WOLFSSL_STM32_PKA
#define FLASH_SR_BSY                          (1 << 16)
#define FLASH_SR_CFGBSY                       (1 << 18)
#define FLASH_SR_SIZERR                       (1 << 6)
#define FLASH_SR_PGAERR                       (1 << 5)
#define FLASH_SR_WRPERR                       (1 << 4)
#define FLASH_SR_PROGERR                      (1 << 3)
#define FLASH_SR_EOP                          (1 << 0)

#define FLASH_CR_LOCK                         (1 << 31)
#define FLASH_CR_STRT                         (1 << 16)

#define FLASH_CR_PER                          (1 << 1)
#define FLASH_CR_PG                           (1 << 0)
#define FLASH_CR_FSTPG                        (1 << 18)

#endif /* !WOLFSSL_STM32_PKA */

#define FLASH_CR_PNB_SHIFT                     3
#define FLASH_CR_PNB_MASK                      0xFF

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR |= ((reg & ~FLASH_ACR_LATENCY_MASK) | waitstates);
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY)) != 0)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    FLASH_SR |= ( FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR |  FLASH_SR_PROGERR);
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

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    uint32_t pdword[2] __attribute__((aligned(16)));
    uint32_t reg;

    flash_clear_errors();
    reg = FLASH_CR & (~FLASH_CR_FSTPG);
    FLASH_CR = reg | FLASH_CR_PG;

    while (i < len) {
        flash_clear_errors();
        if ((len - i > 3) && ((((address + i) & 0x07) == 0)  && ((((uint32_t)data) + i) & 0x07) == 0)) {
            uint32_t idx = i >> 2;
            src = (uint32_t *)data;
            dst = (uint32_t *)(address);
            pdword[0] = src[idx];
            pdword[1] = src[idx + 1];
            flash_wait_complete();
            dst[idx] = pdword[0];
            dst[idx + 1] = pdword[1];
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


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;
    if (len == 0)
        return -1;
    address -= FLASHMEM_ADDRESS_SPACE;
    end_address = address + len - 1;
    flash_wait_complete();
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        flash_clear_errors();
        reg = FLASH_CR & ~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_FSTPG | FLASH_CR_PG);
        FLASH_CR = reg | ((p >> 12) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        DMB();
        flash_wait_complete();
        FLASH_CR &= ~(FLASH_CR_PER);
    }
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;
    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_MSION;
    DMB();
    while ((RCC_CFGR & RCC_CR_MSIRDY) == 0) {};
    /* Select MSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_SW_MASK);
    DMB();
    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

static void clock_pll_on(void)
{
    uint32_t reg32;
    uint32_t cpu_freq, pllm, plln, pllp,pllq, pllr;
    uint32_t hpre, ppre1, ppre2;
    uint32_t flash_waitstates;

    /* Select clock parameters (CPU Speed = 64MHz) */
    cpu_freq = 64000000;
    flash_waitstates = 4;
    flash_set_waitstates(flash_waitstates);

    /* Configure + enable internal high-speed oscillator. */
    RCC_CR = (RCC_CR & (~RCC_CR_MSIRANGE_Msk)) | RCC_CR_MSIRANGE_6;
    RCC_CR |= RCC_CR_MSION;
    DMB();
    while ((RCC_CR & RCC_CR_MSIRDY) == 0)
        ;
    /* Select MSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_SW_MASK);
    RCC_CFGR = (reg32 | RCC_CFGR_SW_MSI);
    DMB();
    /*
     * Set prescalers
     */
    hpre = RCC_PRESCALER_DIV_NONE;
    ppre1 = RCC_PRESCALER_DIV_NONE;
    ppre2 = RCC_PRESCALER_DIV_NONE;
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_HPRE_MASK << RCC_CFGR_HPRE_SHIFT);
    RCC_CFGR = (hpre & RCC_CFGR_HPRE_MASK) << RCC_CFGR_HPRE_SHIFT;
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_PPRE1_MASK << RCC_CFGR_PPRE1_SHIFT);
    RCC_CFGR = (reg32 | (ppre1 << RCC_CFGR_PPRE1_SHIFT));
    DMB();
    reg32 &= ~(RCC_CFGR_PPRE2_MASK << RCC_CFGR_PPRE2_SHIFT);
    RCC_CFGR = (reg32 | (ppre2 << RCC_CFGR_PPRE2_SHIFT));
    DMB();
    /* Set PLLCFGR parameter */
    RCC_PLLCFGR = RCC_PLLCFGR_PLLM_DIV2 | RCC_PLLCFGR_PLLN_32 |
        RCC_PLLCFGR_PLLP_DIV5 | RCC_PLLCFGR_PLLQ_DIV4 |
        RCC_PLLCFGR_PLLR_DIV2 | RCC_PLLCFGR_PLLP_EN |
        RCC_PLLCFGR_PLLQ_EN | RCC_PLLCFGR_PLLR_EN |
        RCC_PLLCFGR_PLLSRC_MSI;

    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0)
        ;

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_SW_MASK);
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();
    /* Wait for PLL clock to be selected (via SWS, bits 3:2) */
    while (((RCC_CFGR >> 2) & RCC_CFGR_SW_MASK) != RCC_CFGR_SW_PLL)
        ;
}

void hal_init(void)
{
    clock_pll_on();
#ifdef WOLFSSL_STM32_PKA
    __HAL_RCC_PKA_CLK_ENABLE();
    hpka.Instance = PKA;
    HAL_PKA_Init(&hpka);
#endif
}

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_release();
#endif
    clock_pll_off();
}

#ifdef WOLFSSL_STM32_PKA


void HAL_PKA_MspInit(PKA_HandleTypeDef* hpka)
{
  if(hpka->Instance==PKA)
  {
    /* Peripheral clock enable */
    __HAL_RCC_PKA_CLK_ENABLE();
  }
}

/* This value is unused, the function is never called
 * as long as the timeout is 0xFFFFFFFF.
 * It is defined here only to avoid a compiler error
 * for a missing symbol in hal_pka_driver.
 */
uint32_t HAL_GetTick(void)
{
    return 0;
}

#endif
