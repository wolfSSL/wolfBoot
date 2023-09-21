/* stm32u5.c
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
#include "hal/stm32u5.h"
#include "hal.h"


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR =  (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates;
}

static RAMFUNCTION void flash_wait_complete(uint8_t bank)
{
    while ((FLASH_NS_SR & (FLASH_SR_BSY | FLASH_SR_WDW)) != 0)
        ;
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    while ((FLASH_SR & (FLASH_SR_BSY | FLASH_SR_WDW)) != 0)
        ;
#endif

}

static void RAMFUNCTION flash_clear_errors(uint8_t bank)
{

    FLASH_NS_SR |= (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
                    FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR
#if !(defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
                    | FLASH_SR_OPTWERR
#endif
            );
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    FLASH_SR |= (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
                 FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR |
                 FLASH_SR_OPTWERR);
#endif

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    uint32_t qword[4];
    volatile uint32_t *sr, *cr;

    flash_clear_errors(0);
    src = (uint32_t*)data;
    dst = (uint32_t*)address;

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    if ((((FLASH_OPTR & FLASH_OPTR_DBANK) == 0) && (address <= FLASH_TOP)) || (address < FLASH_BANK2_BASE)) {
        cr = &FLASH_CR;
        sr = &FLASH_SR;
        /* Convert into secure address space */
        dst = (uint32_t *)((address & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
    }
    else if (address >= (FLASH_BANK2_BASE) && (address <= (FLASH_TOP) )) {
        cr = &FLASH_NS_CR;
        sr = &FLASH_NS_SR;
    }
    else {
        return 0; /* Address out of range */
    }
#else
    cr = &FLASH_NS_CR;
    sr = &FLASH_NS_SR;
#endif

    while (i < len) {
        qword[0] = src[i >> 2];
        qword[1] = src[(i >> 2) + 1];
        qword[2] = src[(i >> 2) + 2];
        qword[3] = src[(i >> 2) + 3];
        *cr |= FLASH_CR_PG;
        dst[i >> 2] = qword[0];
        ISB();
        dst[(i >> 2) + 1] = qword[1];
        ISB();
        dst[(i >> 2) + 2] = qword[2];
        ISB();
        dst[(i >> 2) + 3] = qword[3];
        ISB();
        flash_wait_complete(0);
        if ((*sr & FLASH_SR_EOP) != 0)
            *sr |= FLASH_SR_EOP;
        *cr &= ~FLASH_CR_PG;
        i += 16;
    }

    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete(0);
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR = FLASH_KEY1;
        DMB();
        FLASH_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
#endif
    if ((FLASH_NS_CR & FLASH_CR_LOCK) != 0) {
        FLASH_NS_KEYR = FLASH_KEY1;
        DMB();
        FLASH_NS_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_NS_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete(0);
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
#endif
     if ((FLASH_NS_CR & FLASH_CR_LOCK) == 0)
        FLASH_NS_CR |= FLASH_CR_LOCK;
}

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    flash_wait_complete(0);

    if ((FLASH_NS_CR & FLASH_CR_OPTLOCK) != 0) {
        FLASH_NS_OPTKEYR = FLASH_OPTKEY1;
        DMB();
        FLASH_NS_OPTKEYR = FLASH_OPTKEY2;
        DMB();
        while ((FLASH_NS_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_opt_lock(void)
{

    FLASH_NS_CR |= FLASH_CR_OPTSTRT;
    flash_wait_complete(0);
    FLASH_NS_CR |= FLASH_CR_OBL_LAUNCH;
    if ((FLASH_NS_CR & FLASH_CR_OPTLOCK) == 0)
        FLASH_NS_CR |= FLASH_CR_OPTLOCK;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;
    volatile uint32_t *cr;

    flash_clear_errors(0);
    if (len == 0)
        return -1;

    if (address < ARCH_FLASH_OFFSET)
        return -1;

    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t base;
        uint32_t bker = 0;
        cr = &FLASH_NS_CR;

        if ((((FLASH_OPTR & FLASH_OPTR_DBANK) == 0) && (p <= FLASH_TOP)) || (p < FLASH_BANK2_BASE)) {
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
            cr = &FLASH_CR;
#endif
            base = FLASHMEM_ADDRESS_SPACE;
        }
        else if (p >= (FLASH_BANK2_BASE) && (p <= (FLASH_TOP) )) {
            bker = FLASH_CR_BKER;
            base = FLASH_BANK2_BASE;
        }
        else {
            *cr &= ~FLASH_CR_PER ;
            return 0; /* Address out of range */
        }
        reg = *cr & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_BKER));
        reg |= ((((p - base)  >> 13) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | bker );
        *cr = reg;
        DMB();
        *cr |= FLASH_CR_STRT;
        flash_wait_complete(0);
    }
    /* If the erase operation is completed, disable the associated bits */
    *cr &= ~FLASH_CR_PER ;
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32, flash_waitstates ;

    /* Select MSI as SYSCLK source. */
    reg32 = RCC_CFGR1;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR1 = (reg32 | RCC_CFGR_SW_MSI);
    DMB();

    /* Wait for MSI clock to be selected. */
    while ((RCC_CFGR1 & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_MSI) {};

    flash_waitstates = 1;
    flash_set_waitstates(flash_waitstates);

    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_HSION;
    RCC_CR &= ~RCC_CR_PLL1ON;
    DMB();
}

/* This implementation will setup MSI 48 MHz as PLL Source Mux, PLLCLK as 
 * System Clock Source */
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t pll1n, pll1m, pll1mboost, pll1q, pll1p, pll1r, pll1fracn, pll1rge;
    uint32_t hpre, apb1pre, apb2pre, apb3pre, flash_waitstates;

    /* Reset the RCC clock configuration to the default reset state ----------*/
    /* Set MSION bit */
    RCC_CR = RCC_CR_MSISON;

    /* Reset CFGR register */
    RCC_CFGR1 = 0U;
    RCC_CFGR2 = 0U;
    RCC_CFGR3 = 0U;

    /* Reset HSEON, CSSON , HSION, PLLxON bits */
    RCC_CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLL1ON | RCC_CR_PLL2ON | RCC_CR_PLL3ON);

    /* Reset PLLCFGR register */
    RCC_PLL1CFGR = 0U;

    /* Reset HSEBYP bit */
    RCC_CR &= ~(RCC_CR_HSEBYP);

    /* Disable all interrupts */
    RCC_CIER = 0U;

    FLASH_ACR|=FLASH_ACR_PRFTEN;

    RCC_AHB3ENR |= RCC_AHB3ENR_PWREN;
    RCC_AHB1ENR |= RCC_AHB1ENR_GTZC1EN;
    RCC_AHB3ENR |= RCC_AHB3ENR_GTZC2EN;

    PWR_UCPDR |= PWR_UCPDR_DBDIS;

    PWR_SVMCR |= PWR_SVMCR_IOS2V;
    PWR_VOSR &= ~( (PWR_VOSR_VOS_1 << PWR_VOSR_VOS_SHIFT) | PWR_VOSR_BOOSTEN );
    PWR_VOSR|= ((PWR_VOSR_VOS_1<< PWR_VOSR_VOS_SHIFT) | PWR_VOSR_BOOSTEN);

    /* Wait until VOSRDY is raised */
    reg32 = PWR_VOSR;
    while ((PWR_VOSR & PWR_VOSR_VOSRDY) == 0) {};

    RCC_ICSCR1|= RCC_ICSCR1_MSIRGSEL;
    reg32 = RCC_ICSCR1;
    reg32 &= ~( (0xF << RCC_ICSCR1_MSIRANGE_SHIFT));
    reg32|= (RCC_ICSCR1_MSIRG_0 << RCC_ICSCR1_MSIRANGE_SHIFT);
    RCC_ICSCR1 = reg32;
    reg32 = RCC_ICSCR1;
    DMB();

    /* Adjusts the Multiple Speed oscillator (MSI) calibration value */
    reg32 = RCC_ICSCR2;
    reg32 &= ~((0x1F << RCC_ICSCR2_MSITRIM0_SHIFT));
    reg32 |= (RCC_ICSCR2_MSITRIM0_DEFAULT << RCC_ICSCR2_MSITRIM0_SHIFT);
    RCC_ICSCR2 = reg32;
    reg32 = RCC_ICSCR2;
    DMB();

    flash_waitstates = 1;
    flash_set_waitstates(flash_waitstates);

    /*----------------------------- HSI Configuration ------------------------*/
    /* Enable the Internal High Speed oscillator (HSI) */
    RCC_CR |= RCC_CR_HSION;
    /* Wait till HSI is ready */
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};
    /* Adjusts the Internal High Speed oscillator (HSI) calibration value */
    reg32 = RCC_ICSCR3;
    reg32 &= ~((1 << 20) | (1 << 19) | (1 << 18) | (1 << 17) | (1 << 16) );
    reg32 |= (RCC_ICSCR3_HSITRIM_DEFAULT << RCC_ICSCR3_HSITRIM_SHIFT);
    RCC_ICSCR3 = reg32;
    reg32 = RCC_ICSCR3;
    DMB();

    /*-------------------------------- PLL Configuration ---------------------*/

    /* Select clock parameters (CPU Speed = 160 MHz) */
    pll1m = 3;
    pll1mboost = RCC_PLL1CFGR_PLL1MBOOST_DIV4;
    pll1n = 10;
    pll1p = 2;
    pll1q = 2;
    pll1r = 1;
    pll1fracn = 0;
    pll1rge = RCC_PLL1VCIRANGE_1;
    hpre  = RCC_AHB_PRESCALER_DIV_NONE;
    apb1pre = RCC_APB_PRESCALER_DIV_NONE;
    apb2pre = RCC_APB_PRESCALER_DIV_NONE;
    apb3pre = RCC_APB_PRESCALER_DIV_NONE;

    /* Disable the main PLL */
    RCC_CR &= ~RCC_CR_PLL1ON;

    /* Wait till PLL is ready */
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0) {};

    /* Enable PWR CLK */
    RCC_AHB3ENR|= RCC_AHB3ENR_PWREN;

    /*Disable EPOD to configure PLL1MBOOST*/
    PWR_VOSR &= ~PWR_VOSR_BOOSTEN;

    /* Configure the main PLL clock source, multiplication and division factors */
    reg32 = RCC_PLL1CFGR ;
    reg32 &= ~((1 << 15) | (1 << 14) | (1 << 13) | (1 << 12) | (1 << 11) |
               (1 << 10) | (1 << 9)  | (1 << 8)  | (1 << 1)  | (1 << 0));
    reg32 |= RCC_PLLCKSELR_PLLSRC_MSI;
    reg32 |= ((pll1m-1) << RCC_PLL1CFGR_PLLM_SHIFT);
    reg32 |= ((pll1mboost) << RCC_PLL1CFGR_PLL1MBOOST_SHIFT);
    RCC_PLL1CFGR = reg32;

    reg32 =0;
    reg32 |= ((pll1n-1) << RCC_PLL1DIVR_PLLN_SHIFT);
    reg32 |= ((pll1p-1) << RCC_PLL1DIVR_PLLP_SHIFT);
    reg32 |= ((pll1q-1) << RCC_PLL1DIVR_PLLQ_SHIFT);
    reg32 |= ((pll1r-1) << RCC_PLL1DIVR_PLLR_SHIFT);
    RCC_PLL1DIVR = reg32;
    DMB();

    /* Disable PLL1FRACN  */
    RCC_PLL1CFGR&= ~RCC_PLL1CFGR_PLL1FRACEN;

    /* Configure PLL  PLL1FRACN */
    reg32 = RCC_PLL1FRACR ;
    reg32 &= ~((1 << 15) | (1 << 14) | (1 << 13) | (1 << 12) | (1 << 11) |
               (1 << 10) | (1 << 9)  | (1 << 8)  | (1 << 7)  | (1 << 6) |
               (1 << 5) | (1 << 4) | (1 << 3));
    reg32 |= ((pll1fracn) << RCC_PLL1FRACR_SHIFT);
    RCC_PLL1FRACR = reg32;

    /* Enable PLL1FRACN  */
    RCC_PLL1CFGR|= RCC_PLL1CFGR_PLL1FRACEN;

    /* Select PLL1 input reference frequency range: VCI */
    reg32 = RCC_PLL1CFGR ;
    reg32 &= ~((1 << 3) | (1 << 2));
    reg32 |= ((pll1rge) << RCC_PLL1CFGR_PLL1RGE_SHIFT);
    RCC_PLL1CFGR = reg32;

    /* Enable the EPOD to reach max frequency */
    PWR_VOSR |= PWR_VOSR_BOOSTEN;

    /* Disable PWR clk */
    RCC_AHB3ENR&=~RCC_AHB3ENR_PWREN;

    /* Enable PLL System Clock output */
    RCC_PLL1CFGR|=RCC_PLL1CFGR_PLL1REN;

    /* Enable the main PLL */
    RCC_CR|=RCC_CR_PLL1ON;

    /* Wait till PLL is ready */
    while ((RCC_CR & RCC_CR_PLL1RDY) == 0) {};

    /* Increasing the number of wait states because of higher CPU frequency */
    flash_waitstates = 4;
    flash_set_waitstates(flash_waitstates);

     /* Enable PWR CLK */
    RCC_AHB3ENR|= RCC_AHB3ENR_PWREN;

    /* Wait till BOOST is ready */
    while ((PWR_VOSR & PWR_VOSR_BOOSTRDY) == 0) {};

    /*Disable PWR clk */
    RCC_AHB3ENR&=~RCC_AHB3ENR_PWREN;

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR1;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR1 = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR1 & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /* HCLK Configuration */
    reg32 = RCC_CFGR2 ;
    reg32 &= ~((1 << 3) | (1 << 2) | (1 << 1) | (1 << 0));
    reg32 |= hpre;
    RCC_CFGR2 = reg32;
    DMB();

    /* PRE1 and PRE2 conf */
    reg32 = RCC_CFGR2 ;
    reg32 &= ~((1 << 6) | (1 << 5) | (1 << 4));
    reg32 |= ((apb1pre) << RCC_CFGR2_PPRE1_SHIFT) ;
    reg32 &= ~((1 << 10) | (1 << 9) | (1 << 8));
    reg32 |= ((apb2pre) << RCC_CFGR2_PPRE2_SHIFT) ;
    RCC_CFGR2 = reg32;
    DMB();

    /* PRE3 conf */
    reg32 = RCC_CFGR3;
    reg32 &= ~((1 << 6) | (1 << 5) | (1 << 4));
    reg32 |= ((apb3pre) << RCC_CFGR3_PPRE3_SHIFT) ;
    RCC_CFGR3 = reg32;
    DMB();

    /* Disable PWR clk */
    RCC_AHB3ENR&=~RCC_AHB3ENR_PWREN;
}

static void RAMFUNCTION stm32u5_reboot(void)
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
    stm32u5_reboot();
}

static void led_unsecure()
{
    uint32_t pin;

    /* Enable clock for User LED GPIOs */
    RCC_AHB2ENR1_CLOCK_ER|= GPIOH_AHB2ENR1_CLOCK_ER;

    /* Un-secure User LED GPIO pins */
    GPIOH_SECCFGR&=~(1<<LED_USR_PIN);
    GPIOH_SECCFGR&=~(1<<LED_BOOT_PIN);
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
    clock_pll_on(0);
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    hal_tz_sau_init();
    hal_gtzc_init();
#endif
}

void hal_prepare_boot(void)
{
    clock_pll_off();

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    led_unsecure();
#endif
}
