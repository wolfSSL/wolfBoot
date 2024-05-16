/* stm32_tz.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifdef PLATFORM_stm32l5
    #include "hal/stm32l5.h"
#endif

#ifdef PLATFORM_stm32u5
    #include "hal/stm32u5.h"
#endif

#ifdef PLATFORM_stm32h5
    #include "hal/stm32h5.h"
#endif

#include "hal/stm32_tz.h"

#include "image.h"
#include "hal.h"
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && (!defined(FLAGS_HOME) || !defined(DISABLE_BACKUP))


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

static int is_range_nonsecure(uint32_t address, int len)
{
#ifndef DUALBANK_SWAP
    /* The non secure area begins at the BOOT partition */
    uint32_t min = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t max = FLASH_TOP + 1;
    uint32_t end;
    if (len < 0)
        return 0;
    end = (uint32_t)(address + len);
    if ((address >= min) && (end <= max))
        return 1;
    return 0;
#else
    /* In this case, the secure area is in the lower side of both banks. */
    uint32_t boot_offset = WOLFBOOT_PARTITION_BOOT_ADDRESS - ARCH_FLASH_OFFSET;
    uint32_t min1 = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t max1 = FLASH_BANK2_BASE + 1;
    uint32_t min2 = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    uint32_t max2 = FLASH_TOP + 1;
    uint32_t end;
    if (len < 0)
        return 0;
    end = (uint32_t)(address + len);
    if (((address >= min1) && (end <= max1)) ||
            ((address >= min2) && (end <= max2)) )
        return 1;
    return 0;
#endif
}


void hal_tz_claim_nonsecure_area(uint32_t address, int len)
{
    int page_n, reg_idx;
    uint32_t reg;
    uint32_t end = address + len;
    uint32_t bank = 0;
    int pos;

    if (!is_range_nonsecure(address, len))
        return;
    while (address < end) {
        if (address < FLASH_BANK2_BASE) {
            page_n = (address - ARCH_FLASH_OFFSET) / FLASH_PAGE_SIZE;
            bank = 1;
        } else {
            page_n = (address - FLASH_BANK2_BASE) / FLASH_PAGE_SIZE;
            bank = 2;
        }
        reg_idx = page_n / 32;
        pos = page_n % 32;
        hal_flash_wait_complete(bank);
        hal_flash_clear_errors(bank);
        hal_flash_nonsecure_unlock();
        if (bank == 1)
            FLASH_SECBB1[reg_idx] |= ( 1 << pos);
        else
            FLASH_SECBB2[reg_idx] |= ( 1 << pos);
        ISB();
        hal_flash_wait_complete(bank);
        hal_flash_nonsecure_lock();
        /* Erase claimed non-secure page, in secure mode */
#ifndef PLATFORM_stm32h5
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | FLASH_CR_BKER | FLASH_CR_PG | FLASH_CR_MER1 | FLASH_CR_MER2));
        FLASH_CR = reg | ((page_n << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER);
#else
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_SER | FLASH_CR_BER | FLASH_CR_PG | FLASH_CR_MER));
        FLASH_CR = reg | ((page_n << FLASH_CR_PNB_SHIFT) | FLASH_CR_SER);
#endif

        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        ISB();
        hal_flash_wait_complete(bank);
        address += FLASH_PAGE_SIZE;
    }
#ifndef PLATFORM_stm32h5
    FLASH_CR &= ~FLASH_CR_PER ;
#else
    FLASH_CR &= ~FLASH_CR_SER ;
#endif
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



#ifdef PLATFORM_stm32h5
#define GTZC1_BASE             (0x50032400)
#define GTZC1_TZSC             (*(volatile uint32_t *)(GTZC1_BASE + 0x00))
#define GTZC1_TZIC             (*(volatile uint32_t *)(GTZC1_BASE + 0x0400))
#define GTZC1_MPCBB1_S         ((volatile uint32_t *)(GTZC1_BASE + 0x0800 + 0x100))
#define GTZC1_MPCBB2_S         ((volatile uint32_t *)(GTZC1_BASE + 0x0C00 + 0x100))
#define GTZC1_MPCBB3_S         ((volatile uint32_t *)(GTZC1_BASE + 0x1000 + 0x100))

#define SET_GTZC1_MPCBBx_S_VCTR(bank,n,val) \
    (*((volatile uint32_t *)(GTZC1_MPCBB##bank##_S) + n ))= val

void hal_gtzc_init(void)
{
    int i;
    /* One bit in the bitmask: 512B */

    /* Configure SRAM1 as secure (Low 256 KB) */
    for (i = 0; i < 16; i++) {
        SET_GTZC1_MPCBBx_S_VCTR(1, i, 0xFFFFFFFF);
    }

    /* Configure SRAM2 as secure (64 KB) */
    for (i = 0; i < 4; i++) {
        SET_GTZC1_MPCBBx_S_VCTR(2, i, 0xFFFFFFFF);
    }

    /* Configure SRAM3 as non-secure (320 KB) */
    for (i = 0; i < 20; i++) {
        SET_GTZC1_MPCBBx_S_VCTR(3, i, 0x0);
    }
}

#else

#define GTZC_MPCBB1_S_BASE        (0x50032C00)
#define GTZC_MPCBB1_S_VCTR_BASE   (GTZC_MPCBB1_S_BASE + 0x100)

#define GTZC_MPCBB2_S_BASE        (0x50033000)
#define GTZC_MPCBB2_S_VCTR_BASE   (GTZC_MPCBB2_S_BASE + 0x100)

#define SET_GTZC_MPCBBx_S_VCTR(bank,n,val) \
    (*((volatile uint32_t *)(GTZC_MPCBB##bank##_S_VCTR_BASE ) + n ))= val

void hal_gtzc_init(void)
{
   int i;
   /* One bit in the bitmask: 256B */

   /* Configure lower half of total RAM as secure
    * 0x3000 0000 : 0x3001 FFFF - 128KB
    */
   for (i = 0; i < 16; i++) {
       SET_GTZC_MPCBBx_S_VCTR(1, i, 0xFFFFFFFF);
   }

   /* Configure high portion of SRAM1 as non-secure
    * 0x2002 0000 : 0x2002 FFFF - 64 KB
    */
   for (i = 16; i < 24; i++) {
       SET_GTZC_MPCBBx_S_VCTR(1, i, 0x0);
   }

   /* Configure SRAM2 as non-secure
    * 0x2003 0000 : 0x2003 FFFF - 64 KB
    */
   for (i = 0; i < 8; i++) {
       SET_GTZC_MPCBBx_S_VCTR(2, i, 0x0);
   }
}
#endif

#ifdef PLATFORM_stm32h5

void hal_tz_sau_init(void)
{
    uint32_t page_n = 0;
    /* SAU is set up before staging. Set up all areas as secure. */
    /* Non-secure callable: NSC functions area */
    sau_init_region(0, 0x0C038000, 0x0C040000, 1);

    /* Non-Secure: application flash area (first bank) */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS, FLASH_BANK2_BASE - 1, 0);

    /* Non-Secure: application flash area (second bank) */
    sau_init_region(2, WOLFBOOT_PARTITION_UPDATE_ADDRESS, FLASH_TOP -1, 0);

    /* Secure RAM regions in SRAM1/SRAM2 */
    sau_init_region(3, 0x30000000, 0x3004FFFF, 1);

    /* Non-secure RAM region in SRAM3 */
    sau_init_region(4, 0x20050000, 0x2008FFFF, 0);

    /* Non-secure: internal peripherals */
    sau_init_region(5, 0x40000000, 0x4FFFFFFF, 0);

    /* Set as non-secure: OTP + RO area */
    sau_init_region(6, 0x08FFF000, 0x08FFFFFF, 0);

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}

#else
void hal_tz_sau_init(void)
{
    /* Non-secure callable: NSC functions area */
    sau_init_region(0, 0x0C038000, 0x0C040000, 1);

    /* Non-secure: application flash area */
    sau_init_region(1, 0x08040000, 0x0807FFFF, 0);

    /* Non-secure RAM region in SRAM1/SRAM2 */
    sau_init_region(2, 0x20020000, 0x2003FFFF, 0);

    /* Non-secure: internal peripherals */
    sau_init_region(3, 0x40000000, 0x4FFFFFFF, 0);

    /* Enable SAU */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable securefault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;

}
#endif

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
#else /* U5 and H5 */
    RCC_CR |= RCC_CR_HSI48ON;
    while ((RCC_CR & RCC_CR_HSI48RDY) == 0)
        ;
#endif
}

void hal_trng_init(void)
{
    uint32_t reg_val;
    hsi48_on();
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

