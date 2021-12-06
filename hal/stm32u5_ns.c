/* stm32u5_ns.c
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
#include "stm32u5_partition.h"

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 U5 register configuration */
/*** RCC ***/
/*!< Memory & Instance aliases and base addresses for Non-Secure peripherals */
/*Non-Secure */
#define RCC_BASE (0x46020C00)   /* RM0456 - Table 4 */

#define FLASH_SECURE_MMAP_BASE (0x0C000000)

#define RCC_CR (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0456 - Table 108 */
#define RCC_CR_PLL3RDY              (1 << 29)
#define RCC_CR_PLL3ON               (1 << 28)
#define RCC_CR_PLL2RDY              (1 << 27)
#define RCC_CR_PLL2ON               (1 << 26)
#define RCC_CR_PLL1RDY              (1 << 25)
#define RCC_CR_PLL1ON               (1 << 24)
#define RCC_CR_CSSON                (1 << 19)
#define RCC_CR_HSEBYP               (1 << 18)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 10)
#define RCC_CR_HSION                (1 << 8)
#define RCC_CR_MSIPLLEN             (1 << 3)
#define RCC_CR_MSIRDY               (1 << 2)
#define RCC_CR_MSISON               (1 << 0)

#define RCC_CFGR1 (*(volatile uint32_t *)(RCC_BASE + 0x1C)) /* RM0456 - Table 108 */
#define RCC_CFGR1_SWS               (1 << 2)

/*** APB1&2 PRESCALER ***/
#define RCC_APB_PRESCALER_DIV_NONE  0x0  /* 0xx: HCLK not divided */

/*** AHB PRESCALER ***/
 #define RCC_AHB_PRESCALER_DIV_NONE 0x0  /* 0xxx: SYSCLK not divided */

#define RCC_CFGR_SW_MSI             0x0
#define RCC_CFGR_SW_HSI16           0x1
#define RCC_CFGR_SW_HSE             0x2
#define RCC_CFGR_SW_PLL             0x3

#define RCC_CFGR2 (*(volatile uint32_t *)(RCC_BASE + 0x20)) /* RM0456 - Table 108 */

#define RCC_CFGR2_HPRE_SHIFT        (0x00)
#define RCC_CFGR2_PPRE2_SHIFT       (0x08)
#define RCC_CFGR2_PPRE1_SHIFT       (0x04)

#define RCC_CFGR3 (*(volatile uint32_t *)(RCC_BASE + 0x24)) /* RM0456 - Table 108 */
#define RCC_CFGR3_PPRE3_SHIFT       (0x04)

#define RCC_PLL1CFGR (*(volatile uint32_t *)(RCC_BASE + 0x28)) /* RM0456 - Table 108 */
#define RCC_PLL1CFGR_PLL1REN          (1 << 18)
#define RCC_PLL1CFGR_PLL1QEN          (1 << 17)
#define RCC_PLL1CFGR_PLL1PEN          (1 << 16)
#define RCC_PLL1CFGR_PLL1FRACEN       (1 << 4)
#define RCC_PLL1CFGR_PLL1RGE_SHIFT    (2)
#define RCC_PLL1VCIRANGE_1            0x03

#define RCC_PLL1CFGR_PLLM_SHIFT       (8)
#define RCC_PLL1CFGR_PLL1MBOOST_SHIFT (12)
#define RCC_PLL1CFGR_PLL1MBOOST_DIV4  0x02

#define RCC_PLLCKSELR_PLLSRC_NONE   0x0
#define RCC_PLLCKSELR_PLLSRC_MSI    0x1
#define RCC_PLLCKSELR_PLLSRC_HSI16  0x2
#define RCC_PLLCKSELR_PLLSRC_HSE    0x3


#define RCC_PLL1DIVR (*(volatile uint32_t *)(RCC_BASE + 0x34)) /* RM0456 - Table 108 */
#define RCC_PLL1DIVR_PLLN_SHIFT     (0)
#define RCC_PLL1DIVR_PLLP_SHIFT     (9)
#define RCC_PLL1DIVR_PLLQ_SHIFT     (16)
#define RCC_PLL1DIVR_PLLR_SHIFT     (24)

#define RCC_PLL1FRACR (*(volatile uint32_t *)(RCC_BASE + 0x38)) /* RM0456 - Table 108 */
#define RCC_PLL1FRACR_SHIFT         (3)

#define RCC_CIER (*(volatile uint32_t *)(RCC_BASE + 0x50)) /* RM0456 - Table 108 */

#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x88)) /* RM0456 - Table 108 */
#define RCC_AHB1ENR_GTZC1EN         (1 << 24)

#define RCC_AHB3ENR (*(volatile uint32_t *)(RCC_BASE + 0x94)) /* RM0456 - Table 108 */
#define RCC_AHB3ENR_GTZC2EN         (1 << 12)
#define RCC_AHB3ENR_PWREN           (1 << 2)

#define RCC_ICSCR1 (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_ICSCR1_MSIRANGE_SHIFT   (28)
#define RCC_ICSCR1_MSIRGSEL         (1 << 23)
#define RCC_ICSCR1_MSIRG_0          (0)

#define RCC_ICSCR2 (*(volatile uint32_t *)(RCC_BASE + 0x0C))
#define RCC_ICSCR2_MSITRIM0_SHIFT   (15)
#define RCC_ICSCR2_MSITRIM0_DEFAULT (0x10)

#define RCC_ICSCR3 (*(volatile uint32_t *)(RCC_BASE + 0x10))
#define RCC_ICSCR3_HSITRIM_SHIFT    (16)
#define RCC_ICSCR3_HSITRIM_DEFAULT  (0x10)

/*** PWR ***/
/*!< Memory & Instance aliases and base addresses for Non-Secure peripherals */
#define PWR_BASE              (0x46020800)   /* RM0456 - Table 4 */

#define PWR_VOSR              (*(volatile uint32_t *)(PWR_BASE + 0x0C))
#define PWR_VOSR_BOOSTEN      (1 << 18)
#define PWR_VOSR_VOS_SHIFT    (16)
#define PWR_VOSR_VOS_4        (0x0)
#define PWR_VOSR_VOS_3        (0x1)
#define PWR_VOSR_VOS_2        (0x2)
#define PWR_VOSR_VOS_1        (0x3)

#define PWR_VOSR_VOSRDY       (1 << 15)
#define PWR_VOSR_BOOSTRDY     (1 << 14)

#define PWR_SVMCR             (*(volatile uint32_t *)(PWR_BASE + 0x10))
#define PWR_SVMCR_IOS2V       (1 << 29)

#define PWR_UCPDR             (*(volatile uint32_t *)(PWR_BASE + 0x2C))
#define PWR_UCPDR_DBDIS       (1 << 0)

/*** FLASH ***/
#define SYSCFG_APB2_CLOCK_ER_VAL    (1 << 0) /* <<RM0438>> - RCC_APB2ENR - SYSCFGEN */

/* Non-Secure only */
#define FLASH_BASE        (0x40022000)   /* <<RM0438>> - Table 4 */
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x28))

/* Register values (for both secure and non secure registers) */
#define FLASH_SR_EOP                        (1 << 0)
#define FLASH_SR_OPERR                      (1 << 1)
#define FLASH_SR_PROGERR                    (1 << 3)
#define FLASH_SR_WRPERR                     (1 << 4)
#define FLASH_SR_PGAERR                     (1 << 5)
#define FLASH_SR_SIZERR                     (1 << 6)
#define FLASH_SR_PGSERR                     (1 << 7)
#define FLASH_SR_OPTWERR                    (1 << 13)
#define FLASH_SR_BSY                        (1 << 16)

#define FLASH_CR_PG                         (1 << 0)
#define FLASH_CR_PER                        (1 << 1)
#define FLASH_CR_MER1                       (1 << 2)
#define FLASH_CR_PNB_SHIFT                  3
#define FLASH_CR_PNB_MASK                   0x7F
#define FLASH_CR_BKER                       (1 << 11)
#define FLASH_CR_MER2                       (1 << 15)
#define FLASH_CR_STRT                       (1 << 16)
#define FLASH_CR_OPTSTRT                    (1 << 17)
#define FLASH_CR_EOPIE                      (1 << 24)
#define FLASH_CR_ERRIE                      (1 << 25)
#define FLASH_CR_OBL_LAUNCH                 (1 << 27)
#define FLASH_CR_INV                        (1 << 29)
#define FLASH_CR_OPTLOCK                    (1 << 30)
#define FLASH_CR_LOCK                       (1 << 31)


#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_LATENCY_MASK              (0x0F)
#define FLASH_ACR_PRFTEN    (1<<8)

#define FLASH_OPTR          (*(volatile uint32_t *)(FLASH_BASE + 0x40))
#define FLASH_OPTR_DBANK     (1 << 21)
#define FLASH_OPTR_SWAP_BANK (1 << 20)

#define FLASHMEM_ADDRESS_SPACE    (0x08000000)
#define FLASH_PAGE_SIZE           (0x2000)      /* 8KB */
#define FLASH_BANK2_BASE          (0x08100000) /*!< Base address of Flash Bank2 */
#define BOOTLOADER_SIZE           (0x8000)
#define FLASH_TOP                 (0x081FFFFF) /*!< FLASH end address  */

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)
#define FLASH_OPTKEY1                         (0x08192A3BU)
#define FLASH_OPTKEY2                         (0x4C5D6E7FU)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR =  (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates ;
}

static RAMFUNCTION void flash_wait_complete(uint8_t bank)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
}

static void RAMFUNCTION flash_clear_errors(uint8_t bank)
{
    FLASH_SR |= (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
                 FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    uint32_t qword[4];
    volatile uint32_t *sr, *cr;

    cr = &FLASH_CR;
    sr = &FLASH_SR;

    flash_clear_errors(0);
    src = (uint32_t *)data;
    dst = (uint32_t *)address;

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
    flash_wait_complete(0);
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

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
        if ((((FLASH_OPTR & FLASH_OPTR_DBANK) == 0) && (p <= FLASH_TOP)) || (p < FLASH_BANK2_BASE)) {
            base = FLASHMEM_ADDRESS_SPACE;
        }
        else if(p >= (FLASH_BANK2_BASE) && (p <= (FLASH_TOP) ))
        {
            bker = FLASH_CR_BKER;
            base = FLASH_BANK2_BASE;
        } else {
            FLASH_CR &= ~FLASH_CR_PER ;
            return 0; /* Address out of range */
        }
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_BKER));
        reg |= ((((p - base)  >> 13) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | bker );
        FLASH_CR = reg;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        flash_wait_complete(0);
    }

    /* If the erase operation is completed, disable the associated bits */
    FLASH_CR &= ~FLASH_CR_PER ;
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

    /* Reset the RCC clock configuration to the default reset state ------------*/
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

    SCB_VTOR = FLASH_SECURE_MMAP_BASE; /* Vector Table Relocation in Internal FLASH */

    FLASH_ACR|=FLASH_ACR_PRFTEN;

    RCC_AHB3ENR |= RCC_AHB3ENR_PWREN;
    RCC_AHB1ENR |= RCC_AHB1ENR_GTZC1EN;
    RCC_AHB3ENR |= RCC_AHB3ENR_GTZC2EN;

    PWR_UCPDR |= PWR_UCPDR_DBDIS;

    PWR_SVMCR |= PWR_SVMCR_IOS2V;
    PWR_VOSR &= ~( (PWR_VOSR_VOS_1 << PWR_VOSR_VOS_SHIFT) | PWR_VOSR_BOOSTEN );
    PWR_VOSR|= ((PWR_VOSR_VOS_1<< PWR_VOSR_VOS_SHIFT) | PWR_VOSR_BOOSTEN);

    /* Wait until VOSRDY is rised */
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

    /*----------------------------- HSI Configuration --------------------------*/
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

    /*-------------------------------- PLL Configuration -----------------------*/

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

    /* Disable PLL1FRACN */
    RCC_PLL1CFGR&= ~RCC_PLL1CFGR_PLL1FRACEN;

    /* Configure PLL  PLL1FRACN */
    reg32 = RCC_PLL1FRACR ;
    reg32 &= ~((1 << 15) | (1 << 14) | (1 << 13) | (1 << 12) | (1 << 11) |
               (1 << 10) | (1 << 9)  | (1 << 8)  | (1 << 7)  | (1 << 6) |
               (1 << 5)  | (1 << 4)  | (1 << 3));
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
    reg32 = RCC_CFGR3 ;
    reg32 &= ~((1 << 6) | (1 << 5) | (1 << 4));
    reg32 |= ((apb3pre) << RCC_CFGR3_PPRE3_SHIFT) ;
    RCC_CFGR3 = reg32;
    DMB();

    /* Disable PWR clk */
    RCC_AHB3ENR&=~RCC_AHB3ENR_PWREN;

}

void hal_init(void)
{
    clock_pll_on(0);
}
