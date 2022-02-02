/* stm32l4.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
#include "stm32l4xx_hal.h"

/* Assembly helpers */
#define DMB() asm volatile ("dmb")

/*** RCC ***/
#define RCC_PRESCALER_DIV_NONE 0

static uint32_t Address = 0, PAGEError = 0;
static FLASH_EraseInitTypeDef EraseInitStruct;


static uint32_t RAMFUNCTION GetPage(uint32_t Addr)
{
    uint32_t page = 0;

    if (Addr < (FLASH_BASE + FLASH_BANK_SIZE))
        page = (Addr - FLASH_BASE) / FLASH_PAGE_SIZE;
    else
        page = (Addr - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_PAGE_SIZE;

    return page;
}


static uint32_t RAMFUNCTION GetBank(uint32_t Addr)
{
    uint32_t bank = 0;

    if (READ_BIT(SYSCFG->MEMRMP, SYSCFG_MEMRMP_FB_MODE) == 0) {
        if (Addr < (FLASH_BASE + FLASH_BANK_SIZE)) {
            bank = FLASH_BANK_1;
        } else {
            bank = FLASH_BANK_2;
        }
    } else {
        if (Addr < (FLASH_BASE + FLASH_BANK_SIZE)) {
            bank = FLASH_BANK_2;
        } else {
            bank = FLASH_BANK_1;
        }
    }

    return bank;
}

static void RAMFUNCTION flash_clear_errors(void)
{
     __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
}

void RAMFUNCTION hal_flash_unlock(void)
{
     HAL_FLASH_Unlock();
}

void RAMFUNCTION hal_flash_lock(void)
{
    HAL_FLASH_Lock();
}

int RAMFUNCTION hal_flash_erase(uint32_t address,int len)
{
    uint32_t FirstPage = 0, LastPage = 0, NbOfPages = 0, BankNumber = 0;
    uint32_t PAGEError = 0;
    int ret;
    flash_clear_errors();

    if (len == 0) {
        return -1;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

    FirstPage = GetPage(address);
    LastPage = GetPage(address + len - 1);
    NbOfPages = LastPage - FirstPage + 1;
    BankNumber = GetBank(address);
    EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.Banks       = BankNumber;
    EraseInitStruct.Page        = FirstPage;
    EraseInitStruct.NbPages     = NbOfPages;
    ret = HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError);

    if (ret != HAL_OK)
        return -1;
    return 0;
}


static void RAMFUNCTION flash_set_waitstates(int waitstates)
{
    FLASH->ACR |= (waitstates | FLASH_ACR_DCEN | FLASH_ACR_ICEN);
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *dst, *src;
    uint32_t pdword[2] __attribute__((aligned(16)));
    uint32_t reg;
    int ret = -1;

    flash_clear_errors();
    reg = FLASH->CR & (~FLASH_CR_FSTPG);
    FLASH->CR = reg | FLASH_CR_PG;

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
    if ((FLASH->SR &FLASH_SR_PROGERR)!= FLASH_SR_PROGERR) {
        ret=0;
    }
    if ((FLASH->SR & FLASH_SR_EOP) == FLASH_SR_EOP) {
        FLASH->SR |= FLASH_SR_EOP;
    }
    FLASH->CR &= ~FLASH_CR_PG;

    return ret;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Enable internal multi-speed oscillator. */
    RCC->CR |= RCC_CR_HSION;
    DMB();
    while ((RCC->CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */
    reg32 = RCC->CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC->CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Turn off PLL */
    RCC->CR &= ~RCC_CR_PLLON;
    DMB();
}

static void clockconfig(int powersave)
{
    uint32_t reg32;

    uint32_t hpre,ppre1,ppre2,flash_waitstates;

    /* Enable Power controller */
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;

    /* Select clock parameters  */
    /*cpu_freq=16000000;*/
    hpre= RCC_PRESCALER_DIV_NONE;
    ppre1= RCC_PRESCALER_DIV_NONE;
    ppre2=RCC_PRESCALER_DIV_NONE;
    flash_waitstates = 3;
    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC->CR |=RCC_CR_HSION;
    DMB();
    while ((RCC->CR & RCC_CR_HSIRDY)==0);

    /* select HSI as SYSCLK source*/
    reg32 = RCC->CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC->CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Set prescalers for AHB, ADC, ABP1, ABP2 */
    reg32 = RCC->CFGR;
    reg32 &= ~(0xF0);
    RCC->CFGR = (reg32 | (hpre << 4));
    DMB();
    reg32 = RCC->CFGR;
    reg32 &= ~(0x700);
    RCC->CFGR = (reg32 | (ppre1 << 8));
    DMB();
    reg32 = RCC->CFGR;
    reg32 &= ~(0x07 << 11);
    RCC->CFGR = (reg32 | (ppre2 << 11));
    DMB();

    /* Disable internal high-speed oscillator. */
    RCC->CR &= ~RCC_CR_HSION;

}

void hal_init(void)
{
    clockconfig(0);
}

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_release();
#endif

    clock_pll_off();
}

/* This value is unused, the function is never called
 * as long as the timeout is 0xFFFFFFFF.
 * It is defined here only to avoid a compiler error
 * for a missing symbol in hal_flash_driver.
 */
uint32_t HAL_GetTick(void)
{
    return 0;
}
