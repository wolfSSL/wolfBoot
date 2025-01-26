/* stm32f1.c
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
#include <string.h>

#include "image.h"

#ifndef ARCH_FLASH_OFFSET
#define ARCH_FLASH_OFFSET 0x08000000U
#endif

/* STM32 F103 register Assembly helpers */
#define DMB() asm volatile ("dmb")

/*** RCC ***/
#define RCC_BASE    (0x40021000U)
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00U))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x04U))

#define RCC_CR_PLLRDY               (1 << 25)
#define RCC_CR_PLLON                (1 << 24)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 1)
#define RCC_CR_HSION                (1 << 0)

#define RCC_CFGR_SW_MASK            0x0003
#define RCC_CFGR_SW_HSI             0x0
#define RCC_CFGR_SW_HSE             0x1
#define RCC_CFGR_SW_PLL             0x2

#define RCC_CFGR_SWS_MASK           0x000C
#define RCC_CFGR_SWS_HSI            (0 << 2)
#define RCC_CFGR_SWS_HSE            (1 << 2)
#define RCC_CFGR_SWS_PLL            (2 << 2)

#define RCC_CFGR_HPRE_MASK          0x00F0
#define RCC_CFGR_HPRE_DIV_NONE      (0 << 4)
#define RCC_CFGR_HPRE_DIV_2         (8 << 4)
#define RCC_CFGR_HPRE_DIV_4         (9 << 4)

#define RCC_CFGR_PPRE1_MASK         0x0700
#define RCC_CFGR_PPRE1_DIV_NONE     (0 << 8)
#define RCC_CFGR_PPRE1_DIV_2        (4 << 8)
#define RCC_CFGR_PPRE1_DIV_4        (5 << 8)

#define RCC_CFGR_PPRE2_MASK         0x3800
#define RCC_CFGR_PPRE2_DIV_NONE     (0 << 11)
#define RCC_CFGR_PPRE2_DIV_2        (4 << 11)
#define RCC_CFGR_PPRE2_DIV_4        (5 << 11)

#define PLL_FULL_MASK (0x003F0000)
#define RCC_CFGR_PLLSRC             (1 << 22)
#define RCC_CFGR_PLLMUL_MUL_9       (7 << 18)

/*** FLASH ***/
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#define RCC_APB1ENR_PWREN   (1 << 28)

#define FLASH_BASE          (0x40022000U)
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00U))
#define FLASH_KEYR          (*(volatile uint32_t *)(FLASH_BASE + 0x04U))
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x0CU))
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x10U))
#define FLASH_AR            (*(volatile uint32_t *)(FLASH_BASE + 0x14U))

#define FLASH_MAX_SZ        (128*1024) /* only low- and medium-density devices supported */
#define FLASH_PAGE_SZ       (1024)

/* Register values */
#define FLASH_ACR_ENABLE_PRFT                 (1 << 4)

#define FLASH_SR_BSY                          (1 << 0)

#define FLASH_CR_LOCK                         (1 << 7)
#define FLASH_CR_STRT                         (1 << 6)
#define FLASH_CR_PER                          (1 << 1)
#define FLASH_CR_PG                           (1 << 0)

#define FLASH_KEY1                            (0x45670123U)
#define FLASH_KEY2                            (0xCDEF89ABU)

static void RAMFUNCTION flash_set_waitstates(int waitstates)
{
    FLASH_ACR |=  waitstates | FLASH_ACR_ENABLE_PRFT;
}

static int RAMFUNCTION valid_flash_area(uint32_t address, int len)
{
    if (len <= 0 || len > FLASH_MAX_SZ)
        return -1;
    if (address < ARCH_FLASH_OFFSET || address >= (ARCH_FLASH_OFFSET + FLASH_MAX_SZ))
        return -1;
    if ((address + len) > (ARCH_FLASH_OFFSET + FLASH_MAX_SZ))
        return -1;

    return 0;
}

static inline void RAMFUNCTION flash_wait_complete(void)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
}

static int RAMFUNCTION flash_erase_page(uint32_t address)
{
    const uint32_t end = address + FLASH_PAGE_SZ;

    flash_wait_complete();

    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = address;
    FLASH_CR |= FLASH_CR_STRT;
    flash_wait_complete();
    FLASH_CR &= ~FLASH_CR_PER;

    /* verify erase */
    while (address < end) {
        if (*(uint32_t*)address != (uint32_t)~0)
            return -1;

        address += 4;
    }

    return 0;
}

static int RAMFUNCTION flash_w16(volatile uint16_t *dst, const uint16_t value)
{
    /* do the write */
    *dst = value;
    DMB();
    flash_wait_complete();

    /* verify */
    return (*dst == value) ? 0 : -1;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint16_t tmp;
    const uint8_t *src = data;
    const uint8_t * const end = src + len;
    /* make sure dst is aligned to 16 bits */
    volatile uint16_t *dst = (volatile uint16_t*)(address & ~1);

    if (valid_flash_area(address, len) != 0)
        return -1;

    flash_wait_complete();
    FLASH_CR |= FLASH_CR_PG;
    /* writes are 16 bits. Check for unaligned initial write */
    if (address & 1) {
        /* read-modify-write */
        tmp = (*dst & 0x00FF) | (data[0] << 8);
        if (flash_w16(dst, tmp) != 0) {
            FLASH_CR &= ~FLASH_CR_PG;
            return -1;
        }
        /* advance dst 2 bytes, src 1 byte */
        dst++;
        src++;
    }
    /* main write loop */
    while (src < end) {
        /* check for unaligned last write */
        if (src + 1 == end) {
            /* read-modify-write */
            tmp = (*dst & 0xFF00) | *src;
            if (flash_w16(dst, tmp) != 0) {
                FLASH_CR &= ~FLASH_CR_PG;
                return -1;
            }
            break;
        }
        /* all systems go for a regular write */
        tmp = *(const uint16_t*)src;
        if (flash_w16(dst, tmp) != 0) {
            FLASH_CR &= ~FLASH_CR_PG;
            return -1;
        }
        /* advance 2 bytes */
        src += 2;
        dst++;
    }
    FLASH_CR &= ~FLASH_CR_PG;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    FLASH_KEYR = FLASH_KEY1;
    FLASH_KEYR = FLASH_KEY2;
}

void RAMFUNCTION hal_flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    const uint32_t end_address = address + len;

    if (valid_flash_area(address, len) != 0)
        return -1;
    if (len % FLASH_PAGE_SZ)
        return -1;

    while (address < end_address) {
        if (flash_erase_page(address))
            return -1;

        address += FLASH_PAGE_SZ;
    }

    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_SW_MASK);
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

static void clock_pll_on(int powersave)
{
    uint32_t reg32;

    /* Enable Power controller */
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;

    /* 2 wait states, if 48 MHz < SYSCLK <= 72 MHz */
    flash_set_waitstates(2);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_SW_MASK);
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Enable external high-speed oscillator 8MHz. */
    RCC_CR |= RCC_CR_HSEON;
    DMB();
    while ((RCC_CR & RCC_CR_HSERDY) == 0) {};

    /*
     * Set prescalers for AHB, ABP1, ABP2.
     */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_HPRE_MASK);
    RCC_CFGR = reg32 | RCC_CFGR_HPRE_DIV_NONE;
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_PPRE1_MASK);
    RCC_CFGR = reg32 | RCC_CFGR_PPRE1_DIV_2;
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_PPRE2_MASK);
    RCC_CFGR = reg32 | RCC_CFGR_PPRE1_DIV_NONE;
    DMB();

    /* Set PLL config */
    reg32 = RCC_CFGR;
    reg32 &= ~(PLL_FULL_MASK);
    /* PLL clock: 8 MHz (HSE) * 9 = 72 MHz */
    RCC_CFGR = reg32 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL_MUL_9;
    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~(RCC_CFGR_SW_MASK);
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {};

    /* Disable internal high-speed oscillator. */
    RCC_CR &= ~RCC_CR_HSION;
}

void hal_init(void)
{
    clock_pll_on(0);
}

void hal_prepare_boot(void)
{
    clock_pll_off();
}
