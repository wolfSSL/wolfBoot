/* stm32_tz.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include <string.h>

#ifdef PLATFORM_stm32l5
    #include "hal/stm32l5.h"
#endif

#ifdef PLATFORM_stm32u5
    #include "hal/stm32u5.h"
#endif

#include "image.h"
#include "hal.h"
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && (!defined(FLAGS_HOME) || !defined(DISABLE_BACKUP))

#define SCB_SHCSR     (*(volatile uint32_t *)(0xE000ED24))
#define SCB_SHCSR_SECUREFAULT_EN            (1<<19)

static void RAMFUNCTION hal_flash_nonsecure_unlock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_NS_CR & FLASH_CR_LOCK) != 0) {
        FLASH_NS_KEYR = FLASH_KEY1;
        DMB();
        FLASH_NS_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_NS_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

static void RAMFUNCTION hal_flash_nonsecure_lock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_NS_CR & FLASH_CR_LOCK) == 0)
        FLASH_NS_CR |= FLASH_CR_LOCK;
}

void hal_tz_claim_nonsecure_area(uint32_t address, int len)
{
    int page_n, reg_idx;
    uint32_t reg;
    uint32_t end = address + len;


    if (address < FLASH_BANK2_BASE)
        return;
    if (end > (FLASH_TOP + 1))
        return;

    hal_flash_wait_complete(0);
    hal_flash_clear_errors(0);
    while (address < end) {
        page_n = (address - FLASH_BANK2_BASE) / FLASH_PAGE_SIZE;
        reg_idx = page_n / 32;
        int pos;
        pos = page_n % 32;
        hal_flash_nonsecure_unlock();
        FLASH_SECBB2[reg_idx] |= ( 1 << pos);
        ISB();
        hal_flash_wait_complete(0);
        hal_flash_nonsecure_lock();
        /* Erase claimed non-secure page, in secure mode */
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | FLASH_CR_BKER | FLASH_CR_PG | FLASH_CR_MER1 | FLASH_CR_MER2));
        FLASH_CR = reg | ((page_n << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | FLASH_CR_BKER);
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        ISB();
        hal_flash_wait_complete(0);
        address += FLASH_PAGE_SIZE;
    }
    FLASH_CR &= ~FLASH_CR_PER ;
}
#else
#define claim_nonsecure_area(...) do{}while(0)
#endif

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
void hal_tz_release_nonsecure_area(void)
{
    int i;
    for (i = 0; i < FLASH_SECBB_NREGS; i++)
        FLASH_SECBB2[i] = 0;
}
#else
#define release_nonsecure_area(...) do{}while(0)
#endif

#define GTZC_MPCBB1_S_BASE        (0x50032C00)
#define GTZC_MPCBB1_S_VCTR_BASE   (GTZC_MPCBB1_S_BASE + 0x100)

#define GTZC_MPCBB2_S_BASE        (0x50033000)
#define GTZC_MPCBB2_S_VCTR_BASE   (GTZC_MPCBB2_S_BASE + 0x100)

#define SET_GTZC_MPCBBx_S_VCTR(bank,n,val) \
    (*((volatile uint32_t *)(GTZC_MPCBB##bank##_S_VCTR_BASE ) + n ))= val

void hal_gtzc_init(void)
{
   int i;
  /* Configure lower half of SRAM1 as secure */
   for (i = 0; i < 12; i++) {
       SET_GTZC_MPCBBx_S_VCTR(1, i, 0xFFFFFFFF);
   }
   /* Configure upper half of SRAM1 as non-secure */
   for (i = 12; i < 24; i++) {
       SET_GTZC_MPCBBx_S_VCTR(1, i, 0x0);
   }

  /* Configure SRAM2 as secure */
   for (i = 0; i < 8; i++) {
       SET_GTZC_MPCBBx_S_VCTR(2, i, 0xFFFFFFFF);
   }
}

/* SAU registers, used to define memory mapped regions */
#define SAU_CTRL   (*(volatile uint32_t *)(0xE000EDD0))
#define SAU_RNR (*(volatile uint32_t *)(0xE000EDD8)) /** SAU_RNR - region number register **/
#define SAU_RBAR (*(volatile uint32_t *)(0xE000EDDC)) /** SAU_RBAR - region base address register **/
#define SAU_RLAR (*(volatile uint32_t *)(0xE000EDE0)) /** SAU_RLAR - region limit address register **/

#define SAU_REGION_MASK 0x000000FF
#define SAU_ADDR_MASK 0xFFFFFFE0 /* LS 5 bit are reserved or used for flags */

/* Flag for the SAU region limit register */
#define SAU_REG_ENABLE (1 << 0) /* Indicates that the region is enabled. */
#define SAU_REG_SECURE (1 << 1) /* When on, the region is S or NSC */

#define SAU_INIT_CTRL_ENABLE (1 << 0)
#define SAU_INIT_CTRL_ALLNS  (1 << 1)

static void sau_init_region(uint32_t region, uint32_t start_addr,
        uint32_t end_addr, int secure)
{
    uint32_t secure_flag = 0;
    if (secure)
        secure_flag = SAU_REG_SECURE;
    SAU_RNR = region & SAU_REGION_MASK;
    SAU_RBAR = start_addr & SAU_ADDR_MASK;
    SAU_RLAR = (end_addr & SAU_ADDR_MASK)
        | secure_flag | SAU_REG_ENABLE;
}


void hal_tz_sau_init(void)
{
    /* Non-secure callable: NSC functions area */
    sau_init_region(0, 0x0C020000, 0x0C040000, 1);

    /* Non-secure: application flash area */
    sau_init_region(1, 0x08040000, 0x0804FFFF, 0);

    /* Non-secure RAM region in SRAM1 */
    sau_init_region(2, 0x20018000, 0x2002FFFF, 0);

    /* Non-secure: internal peripherals */
    sau_init_region(3, 0x40000000, 0x4FFFFFFF, 0);

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;

}

#ifdef WOLFCRYPT_SECURE_MODE

#define TRNG_BASE 0x520C0800
#define TRNG_CR *((volatile uint32_t *)(TRNG_BASE + 0x00))
#define TRNG_SR *((volatile uint32_t *)(TRNG_BASE + 0x04))
#define TRNG_DR *((volatile uint32_t *)(TRNG_BASE + 0x08))

#define TRNG_SR_DRDY (1 << 0)
#define TRNG_CR_RNGEN (1 << 2)
#define TRNG_CR_CONFIG3_SHIFT (8)
#define TRNG_CR_CONFIG2_SHIFT (13)
#define TRNG_CR_CLKDIV_SHIFT (16)
#define TRNG_CR_CONFIG1_SHIFT (20)
#define TRNG_CR_CONDRST (1 << 30)


static void hsi48_on(void)
{

#ifdef PLATFORM_stm32l5
    RCC_CRRCR |= RCC_CRRCR_HSI48ON;
    while ((RCC_CRRCR & RCC_CRRCR_HSI48RDY) == 0)
        ;
#endif
#ifdef PLATFORM_stm32u5
    RCC_CR |= RCC_CR_HSI48ON;
    while ((RCC_CR & RCC_CR_HSI48RDY) == 0)
        ;
#endif
}


void hal_trng_init(void)
{
    uint32_t reg_val;
    hsi48_on();
#ifdef PLATFORM_stm32u5
    #define RCC_AHB2_CLOCK_ER RCC_AHB2ENR1_CLOCK_ER
#endif
    RCC_AHB2_CLOCK_ER |= TRNG_AHB2_CLOCK_ER;

    reg_val = TRNG_CR;
    reg_val &= ~(0x1F << TRNG_CR_CONFIG1_SHIFT);
    reg_val &= ~(0x7 << TRNG_CR_CLKDIV_SHIFT);
    reg_val &= ~(0x3 << TRNG_CR_CONFIG2_SHIFT);
    reg_val &= ~(0x7 << TRNG_CR_CONFIG3_SHIFT);

    reg_val |= 0x0F << TRNG_CR_CONFIG1_SHIFT;
    reg_val |= 0x0D << TRNG_CR_CONFIG3_SHIFT;
#ifdef PLATFORM_stm32u5 /* RM0456 40.6.2 */
    reg_val |= 0x06 << TRNG_CR_CLKDIV_SHIFT;
#endif
    TRNG_CR = TRNG_CR_CONDRST | reg_val;
    while ((TRNG_CR & TRNG_CR_CONDRST) == 0)
        ;
    TRNG_CR = reg_val | TRNG_CR_RNGEN;
    while ((TRNG_SR & TRNG_SR_DRDY) == 0)
        ;
}

/* Never used (RNG keeps running when in secure-mode) */
void hal_trng_fini(void)
{
    TRNG_CR &= (~TRNG_CR_RNGEN);
}

int hal_trng_get_entropy(unsigned char *out, unsigned len)
{
    unsigned i;
    uint32_t rand_seed = 0;
    for (i = 0; i < len; i += 4)
    {
        while ((TRNG_SR & TRNG_SR_DRDY) == 0)
            ;
        rand_seed = TRNG_DR;
        if ((len - i) < 4)
            memcpy(out + i, &rand_seed, len - i);
        else
            memcpy(out + i, &rand_seed, 4);
    }
    return 0;
}

#endif

