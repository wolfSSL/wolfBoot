/* stm32l5.c
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
#include <string.h>

#include "hal.h"
#include "hal/stm32l5.h"


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR =  (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates ;
}

void RAMFUNCTION hal_flash_wait_complete(uint8_t bank)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    while ((FLASH_NS_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
#endif

}

void RAMFUNCTION hal_flash_clear_errors(uint8_t bank)
{

    FLASH_SR |= ( FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
            FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR
#if !(defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
            |
            FLASH_SR_OPTWERR
#endif
            ) ;
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    FLASH_NS_SR |= ( FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
            FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR |
            FLASH_SR_OPTWERR);
#endif

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    uint32_t dword[2];
    volatile uint32_t *sr, *cr;

    cr = &FLASH_CR;
    sr = &FLASH_SR;

    hal_flash_clear_errors(0);
    src = (uint32_t *)data;
    dst = (uint32_t *)address;

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    if (address >= FLASH_BANK2_BASE)
        hal_tz_claim_nonsecure_area(address, len);
    /* Convert into secure address space */
    dst = (uint32_t *)((address & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
#endif

    while (i < len) {
        dword[0] = src[i >> 2];
        dword[1] = src[(i >> 2) + 1];
        *cr |= FLASH_CR_PG;
        dst[i >> 2] = dword[0];
        ISB();
        dst[(i >> 2) + 1] = dword[1];
        hal_flash_wait_complete(0);
        if ((*sr & FLASH_SR_EOP) != 0)
            *sr |= FLASH_SR_EOP;
        *cr &= ~FLASH_CR_PG;
        i+=8;
    }

    hal_tz_release_nonsecure_area();
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR = FLASH_KEY1;
        DMB();
        FLASH_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_CR & FLASH_CR_OPTLOCK) != 0) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        DMB();
        FLASH_OPTKEYR = FLASH_OPTKEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }

}

void RAMFUNCTION hal_flash_opt_lock(void)
{
    FLASH_CR |= FLASH_CR_OPTSTRT;
    hal_flash_wait_complete(0);
    FLASH_CR |= FLASH_CR_OBL_LAUNCH;
    if ((FLASH_CR & FLASH_CR_OPTLOCK) == 0)
        FLASH_CR |= FLASH_CR_OPTLOCK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    hal_flash_clear_errors(0);
    if (len == 0)
        return -1;

    if (address < ARCH_FLASH_OFFSET)
        return -1;

    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t base;
        uint32_t bker = 0;
        if ((((FLASH_OPTR & FLASH_OPTR_DBANK) == 0) && (p <= FLASH_TOP)) ||
                (p < FLASH_BANK2_BASE)) {
            base = FLASHMEM_ADDRESS_SPACE;
        }
        else if(p >= (FLASH_BANK2_BASE) && (p <= (FLASH_TOP) ))
        {
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
            /* When in secure mode, skip erasing non-secure pages: will be erased upon claim */
            return 0;
#endif
            bker = FLASH_CR_BKER;
            base = FLASH_BANK2_BASE;
        } else {
            FLASH_CR &= ~FLASH_CR_PER ;
            return 0; /* Address out of range */
        }
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_BKER));
        reg |= ((((p - base)  >> 11) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | bker );
        FLASH_CR = reg;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        hal_flash_wait_complete(0);
    }
    /* If the erase operation is completed, disable the associated bits */
    FLASH_CR &= ~FLASH_CR_PER ;
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

     /* Select MSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_MSI);
    DMB();

    /* Wait for MSI clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_MSI) {};

    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

/*This implementation will setup MSI 48 MHz as PLL Source Mux, PLLCLK as System Clock Source*/

static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t plln, pllm, pllq, pllp, pllr, hpre, apb1pre, apb2pre , flash_waitstates;

    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    PWR_CR3 |= PWR_CR3_UCPD_DBDIS;

    PWR_CR1 &= ~((1 << 10) | (1 << 9));
    PWR_CR1 |= (PWR_CR1_VOS_0 << PWR_CR1_VOS_SHIFT);
    /* Delay after setting the voltage scaling */
    reg32 = PWR_CR1;
    while ((PWR_SR2 & PWR_SR2_VOSF)) {};

    while ((RCC_CR & RCC_CR_MSIRDY) == 0) {};
    flash_waitstates = 2;
    flash_set_waitstates(flash_waitstates);

    RCC_CR |= RCC_CR_MSIRGSEL;

    reg32 = RCC_CR;
    reg32 &= ~((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
    reg32 |= (RCC_CR_MSIRANGE_11 << RCC_CR_MSIRANGE_SHIFT);
    RCC_CR = reg32;
    reg32 = RCC_CR;
    DMB();

    /* Select clock parameters (CPU Speed = 110 MHz) */
    pllm = 12;
    plln = 55;
    pllp = 7;
    pllq = RCC_PLLCFGR_QR_DIV_2;
    pllr = RCC_PLLCFGR_QR_DIV_2;
    hpre  = RCC_AHB_PRESCALER_DIV_NONE;
    apb1pre = RCC_APB_PRESCALER_DIV_NONE;
    apb2pre = RCC_APB_PRESCALER_DIV_NONE;
    flash_waitstates = 5;

    RCC_CR &= ~RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) != 0) {};

    /*PLL Clock source selection*/
    reg32 = RCC_PLLCFGR ;
    reg32 |= RCC_PLLCKSELR_PLLSRC_MSI;
    reg32 |= ((pllm-1) << RCC_PLLCFGR_PLLM_SHIFT);
    reg32 |= ((plln) << RCC_PLLCFGR_PLLN_SHIFT);
    reg32 |= ((pllp) << RCC_PLLCFGR_PLLP_SHIFT);
    reg32 |= ((pllq) << RCC_PLLCFGR_PLLQ_SHIFT);
    reg32 |= ((pllr) << RCC_PLLCFGR_PLLR_SHIFT);
    RCC_PLLCFGR = reg32;
    DMB();

    RCC_CR |= RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    RCC_PLLCFGR |= RCC_PLLCFGR_PLLREN;

    flash_set_waitstates(flash_waitstates);

    /*step down HPRE before going to >80MHz*/
    reg32 = RCC_CFGR ;
    reg32 &= ~((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
    reg32 |= ((RCC_AHB_PRESCALER_DIV_2) << RCC_CFGR_HPRE_SHIFT) ;
    RCC_CFGR = reg32;
    DMB();

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /*step-up HPRE to go > 80MHz*/
    reg32 = RCC_CFGR ;
    reg32 &= ~((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
    reg32 |= ((hpre) << RCC_CFGR_HPRE_SHIFT) ;
    RCC_CFGR = reg32;
    DMB();

    /*PRE1 and PRE2 conf*/
    reg32 = RCC_CFGR ;
    reg32 &= ~((1 << 10) | (1 << 9) | (1 << 8));
    reg32 |= ((apb1pre) << RCC_CFGR_PPRE1_SHIFT) ;
    reg32 &= ~((1 << 13) | (1 << 12) | (1 << 11));
    reg32 |= ((apb2pre) << RCC_CFGR_PPRE2_SHIFT) ;
    RCC_CFGR = reg32;
    DMB();
}




#define OPTR_SWAP_BANK (1 << 20)

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

static void RAMFUNCTION stm32l5_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;

}

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    uint32_t cur_opts;
    hal_flash_unlock();
    hal_flash_opt_unlock();
    cur_opts = (FLASH_OPTR & FLASH_OPTR_SWAP_BANK) >> 20;
    if (cur_opts)
        FLASH_OPTR &= (~FLASH_OPTR_SWAP_BANK);
    else
        FLASH_OPTR |= FLASH_OPTR_SWAP_BANK;
    hal_flash_opt_lock();
    hal_flash_lock();
    stm32l5_reboot();
}

static void led_unsecure()
{
    uint32_t pin;

    /*Enable clock for User LED GPIOs */
    RCC_AHB2_CLOCK_ER|= GPIOD_AHB2_CLOCK_ER;
    RCC_AHB2_CLOCK_ER|= GPIOG_AHB2_CLOCK_ER;
    PWR_CR2 |= PWR_CR2_IOSV;

    /*Un-secure User LED GPIO pins */
    GPIOD_SECCFGR&=~(1<<LED_USR_PIN);
    GPIOG_SECCFGR&=~(1<<LED_BOOT_PIN);

}

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
static uint8_t bootloader_copy_mem[BOOTLOADER_SIZE];
static void RAMFUNCTION fork_bootloader(void)
{
    uint8_t *data = (uint8_t *) FLASHMEM_ADDRESS_SPACE;
    uint32_t dst  = FLASH_BANK2_BASE;
    uint32_t r = 0, w = 0;
    int i;

    /* Read the wolfBoot image in RAM */
    memcpy(bootloader_copy_mem, data, BOOTLOADER_SIZE);

    /* Mass-erase */
    hal_flash_unlock();
    hal_flash_erase(dst, BOOTLOADER_SIZE);
    hal_flash_write(dst, bootloader_copy_mem, BOOTLOADER_SIZE);
    hal_flash_lock();
}
#endif

void hal_init(void)
{

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
    if ((FLASH_OPTR & (FLASH_OPTR_SWAP_BANK | FLASH_OPTR_DBANK)) == FLASH_OPTR_DBANK)
        fork_bootloader();
#endif

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    hal_tz_sau_init();
    hal_gtzc_init();
#endif
    clock_pll_on(0);

}

void hal_prepare_boot(void)
{
    clock_pll_off();
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    led_unsecure();
#endif
}

