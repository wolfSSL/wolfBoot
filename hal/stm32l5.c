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
#include "stm32l5_partition.h"

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 L5 register configuration */
/*!< Memory & Instance aliases and base addresses for Non-Secure/Secure peripherals */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
/*Secure */
#define RCC_BASE            (0x50021000)   //RM0438 - Table 4
#else
/*Non-Secure */
#define RCC_BASE            (0x40021000)   //RM0438 - Table 4
#endif

#define FLASH_SECURE_MMAP_BASE (0x0C000000)

#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  //RM0438 - Table 77
#define RCC_CR_PLLRDY               (1 << 25) //RM0438 - 9.8.1
#define RCC_CR_PLLON                (1 << 24) //RM0438 - 9.8.1
#define RCC_CR_HSEBYP               (1 << 18) //RM0438 - 9.8.1
#define RCC_CR_HSERDY               (1 << 17) //RM0438 - 9.8.1
#define RCC_CR_HSEON                (1 << 16) //RM0438 - 9.8.1
#define RCC_CR_HSIRDY               (1 << 10) //RM0438 - 9.8.1
#define RCC_CR_HSION                (1 << 8)  //RM0438 - 9.8.1
#define RCC_CR_MSIRANGE_SHIFT       (4)  //RM0438 - 9.8.1
#define RCC_CR_MSIRANGE_11          (11)
#define RCC_CR_MSIRGSEL             (1 << 3)  //RM0438 - 9.8.1
#define RCC_CR_MSIPLLEN             (1 << 2)  //RM0438 - 9.8.1
#define RCC_CR_MSIRDY               (1 << 1)  //RM0438 - 9.8.1
#define RCC_CR_MSION                (1 << 0)  //RM0438 - 9.8.1


#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x08))  //RM0438 - Table 77

/*** APB1&2 PRESCALER ***/
#define RCC_APB_PRESCALER_DIV_NONE 0x0  // 0xx: HCLK not divided
#define RCC_APB_PRESCALER_DIV_2 0x4     // 100: HCLK divided by 2
#define RCC_APB_PRESCALER_DIV_4 0x5     // 101: HCLK divided by 4
#define RCC_APB_PRESCALER_DIV_8 0x6     // 110: HCLK divided by 8
#define RCC_APB_PRESCALER_DIV_16 0x7    // 111: HCLK divided by 16

/*** AHB PRESCALER ***/
#define RCC_AHB_PRESCALER_DIV_NONE 0x0    // 0xxx: SYSCLK not divided
#define RCC_AHB_PRESCALER_DIV_2    0x8    // 1000: SYSCLK divided by 2
#define RCC_AHB_PRESCALER_DIV_4    0x9    // 1001: SYSCLK divided by 4
#define RCC_AHB_PRESCALER_DIV_8   0xA    // 1010: SYSCLK divided by 8
#define RCC_AHB_PRESCALER_DIV_16  0xB    // 1011: SYSCLK divided by 16
#define RCC_AHB_PRESCALER_DIV_64  0xC    // 1100: SYSCLK divided by 64
#define RCC_AHB_PRESCALER_DIV_128 0xD    // 1101: SYSCLK divided by 128
#define RCC_AHB_PRESCALER_DIV_256 0xE    // 1110: SYSCLK divided by 256
#define RCC_AHB_PRESCALER_DIV_512 0xF    // 1111: SYSCLK divided by 512

#define RCC_CFGR_HPRE_SHIFT        (0x04)
#define RCC_CFGR_PPRE2_SHIFT       (0x0B)
#define RCC_CFGR_PPRE1_SHIFT       (0x08)

#define RCC_CFGR_SW_MSI             0x0
#define RCC_CFGR_SW_HSI16           0x1
#define RCC_CFGR_SW_HSE             0x2
#define RCC_CFGR_SW_PLL             0x3

#define RCC_PLLCFGR         (*(volatile uint32_t *)(RCC_BASE + 0x0C))  //RM0438 - Table 77
#define RCC_PLLCFGR_PLLP_SHIFT       (27)
#define RCC_PLLCFGR_PLLR_SHIFT      (25)
#define RCC_PLLCFGR_PLLREN          (1 << 24)

#define RCC_PLLCFGR_PLLQ_SHIFT       (21)
#define RCC_PLLCFGR_PLLQEN           (1 << 20)

#define RCC_PLLCFGR_PLLN_SHIFT       (8)
#define RCC_PLLCFGR_PLLM_SHIFT       (4)

#define RCC_PLLCFGR_QR_DIV_2          0x0
#define RCC_PLLCFGR_QR_DIV_4          0x1
#define RCC_PLLCFGR_QR_DIV_6          0x2
#define RCC_PLLCFGR_QR_DIV_8          0x3

#define RCC_PLLCFGR_P_DIV_7           0x0
#define RCC_PLLCFGR_P_DIV_17          0x1

#define RCC_PLLCKSELR_PLLSRC_NONE    0x0
#define RCC_PLLCKSELR_PLLSRC_MSI     0x1
#define RCC_PLLCKSELR_PLLSRC_HSI16   0x2
#define RCC_PLLCKSELR_PLLSRC_HSE     0x3

#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x58))
#define RCC_APB1ENR_PWREN         (1 << 28)

#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x60))
#define RCC_APB2ENR_SYSCFGEN      (1 << 0)


/*** PWR ***/
/*!< Memory & Instance aliases and base addresses for Non-Secure/Secure peripherals */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
/*Secure */
#define PWR_BASE            (0x50007000)   //RM0438 - Table 4
#else
/*Non-Secure */
#define PWR_BASE            (0x40007000)   //RM0438 - Table 4
#endif

#define PWR_CR1              (*(volatile uint32_t *)(PWR_BASE + 0x00))
#define PWR_CR1_VOS_SHIFT    (9)
#define PWR_CR1_VOS_0        (0x0)
#define PWR_CR1_VOS_1        (0x1)
#define PWR_CR1_VOS_2        (0x2)

#define PWR_CR2              (*(volatile uint32_t *)(PWR_BASE + 0x04))
#define PWR_CR2_IOSV         (1 << 9)
#define PWR_CR3              (*(volatile uint32_t *)(PWR_BASE + 0x08))
#define PWR_CR3_UCPD_DBDIS   (1 << 14)
#define PWR_CR4              (*(volatile uint32_t *)(PWR_BASE + 0x0C))

#define PWR_SR1              (*(volatile uint32_t *)(PWR_BASE + 0x10))
#define PWR_SR2              (*(volatile uint32_t *)(PWR_BASE + 0x14))
#define PWR_SR2_VOSF         (1 << 10)

#define SYSCFG_BASE          (0x50010000) //RM0438 - Table 4



/*** FLASH ***/
#define SYSCFG_APB2_CLOCK_ER_VAL    (1 << 0) //RM0438 - RCC_APB2ENR - SYSCFGEN

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
/*Secure*/
#define FLASH_BASE          (0x50022000)   //RM0438 - Table 4
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x24))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x2C))

#define FLASH_SECBB1       ((volatile uint32_t *)(FLASH_BASE + 0x80)) /* Array */
#define FLASH_SECBB2       ((volatile uint32_t *)(FLASH_BASE + 0xA0)) /* Array */
#define FLASH_SECBB_NREGS  4    /* Array length for the two above */

#define FLASH_NS_BASE          (0x40022000)   //RM0438 - Table 4
#define FLASH_NS_KEYR        (*(volatile uint32_t *)(FLASH_NS_BASE + 0x08))
#define FLASH_NS_OPTKEYR     (*(volatile uint32_t *)(FLASH_NS_BASE + 0x10))
#define FLASH_NS_SR          (*(volatile uint32_t *)(FLASH_NS_BASE + 0x20))
#define FLASH_NS_CR          (*(volatile uint32_t *)(FLASH_NS_BASE + 0x28))



#else
/* Non-Secure only */
#define FLASH_BASE          (0x40022000)   //RM0438 - Table 4
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x28))
#endif

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

#define FLASH_OPTR          (*(volatile uint32_t *)(FLASH_BASE + 0x40))
#define FLASH_OPTR_DBANK     (1 << 22)
#define FLASH_OPTR_SWAP_BANK (1 << 20)

#define FLASHMEM_ADDRESS_SPACE    (0x08000000)
#define FLASH_PAGE_SIZE           (0x800)      /* 2KB */
#define FLASH_BANK2_BASE          (0x08040000) /*!< Base address of Flash Bank2     */
#define BOOTLOADER_SIZE           (0x8000)
#define FLASH_TOP                 (0x0807FFFF) /*!< FLASH end address  */

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)
#define FLASH_OPTKEY1                         (0x08192A3BU)
#define FLASH_OPTKEY2                         (0x4C5D6E7FU)

/* GPIO*/
#define GPIOD_BASE 0x52020C00
#define GPIOG_BASE 0x52021800

#define GPIOD_SECCFGR (*(volatile uint32_t *)(GPIOD_BASE + 0x30))
#define GPIOG_SECCFGR (*(volatile uint32_t *)(GPIOG_BASE + 0x30))

#define LED_BOOT_PIN (12)  //PG12 - Discovery - Green Led
#define LED_USR_PIN (3) //PD3  - Discovery  - Red Led

#define RCC_AHB2_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x4C ))
#define GPIOG_AHB2_CLOCK_ER (1 << 6)
#define GPIOD_AHB2_CLOCK_ER (1 << 3)

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)

#define SCS_BASE  (0xE000E000UL)
#define SCB_BASE  (SCS_BASE + 0x0D00UL)
#define SCB_SHCSR     (*(volatile uint32_t *)(SCB_BASE + 0x24))
#define SCB_SHCSR_SECUREFAULT_EN            (1<<19)

#endif

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
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    while ((FLASH_NS_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
#endif

}

static void RAMFUNCTION flash_clear_errors(uint8_t bank)
{

    FLASH_SR |= ( FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR
#if !(defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
            |
            FLASH_SR_OPTWERR
#endif
            ) ;
#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U))
    FLASH_NS_SR |= ( FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_OPTWERR);
#endif

}

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && (!defined(FLAGS_HOME) || !defined(DISABLE_BACKUP))
static void RAMFUNCTION hal_flash_nonsecure_unlock(void)
{
    flash_wait_complete(0);
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
    flash_wait_complete(0);
    if ((FLASH_NS_CR & FLASH_CR_LOCK) == 0)
        FLASH_NS_CR |= FLASH_CR_LOCK;
}

static void claim_nonsecure_area(uint32_t address, int len)
{
    int page_n, reg_idx;
    uint32_t reg;
    uint32_t end = address + len;


    if (address < FLASH_BANK2_BASE)
        return;
    if (end > (FLASH_TOP + 1))
        return;

    flash_wait_complete(0);
    flash_clear_errors(0);
    while (address < end) {
        page_n = (address - FLASH_BANK2_BASE) / FLASH_PAGE_SIZE;
        reg_idx = page_n / 32;
        int pos;
        pos = page_n % 32;
        hal_flash_nonsecure_unlock();
        FLASH_SECBB2[reg_idx] |= ( 1 << pos);
        ISB();
        flash_wait_complete(0);
        hal_flash_nonsecure_lock();
        /* Erase claimed non-secure page, in secure mode */
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | FLASH_CR_BKER | FLASH_CR_PG | FLASH_CR_MER1 | FLASH_CR_MER2));
        FLASH_CR = reg | ((page_n << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | FLASH_CR_BKER);
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        ISB();
        flash_wait_complete(0);
        address += FLASH_PAGE_SIZE;
    }
    FLASH_CR &= ~FLASH_CR_PER ;
}
#else
#define claim_nonsecure_area(...) do{}while(0)
#endif

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
static void release_nonsecure_area(void)
{
    int i;
    for (i = 0; i < FLASH_SECBB_NREGS; i++)
        FLASH_SECBB2[i] = 0;
}
#else
#define release_nonsecure_area(...) do{}while(0)
#endif


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
  int i = 0;
  uint32_t *src, *dst;
  uint32_t dword[2];
  volatile uint32_t *sr, *cr;

  cr = &FLASH_CR;
  sr = &FLASH_SR;

  flash_clear_errors(0);
  src = (uint32_t *)data;
  dst = (uint32_t *)address;

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
  if (address >= FLASH_BANK2_BASE)
      claim_nonsecure_area(address, len);
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
    flash_wait_complete(0);
    if ((*sr & FLASH_SR_EOP) != 0)
        *sr |= FLASH_SR_EOP;
    *cr &= ~FLASH_CR_PG;
    i+=8;
  }

  release_nonsecure_area();
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

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    flash_wait_complete(0);
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
    flash_wait_complete(0);
    FLASH_CR |= FLASH_CR_OBL_LAUNCH;
    if ((FLASH_CR & FLASH_CR_OPTLOCK) == 0)
        FLASH_CR |= FLASH_CR_OPTLOCK;
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
        flash_wait_complete(0);
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
static void gtzc_init(void)
{
  /*configure SRAM1 */
  SET_GTZC_MPCBBx_S_VCTR(1,0);
  SET_GTZC_MPCBBx_S_VCTR(1,1);
  SET_GTZC_MPCBBx_S_VCTR(1,2);
  SET_GTZC_MPCBBx_S_VCTR(1,3);
  SET_GTZC_MPCBBx_S_VCTR(1,4);
  SET_GTZC_MPCBBx_S_VCTR(1,5);
  SET_GTZC_MPCBBx_S_VCTR(1,6);
  SET_GTZC_MPCBBx_S_VCTR(1,7);
  SET_GTZC_MPCBBx_S_VCTR(1,8);
  SET_GTZC_MPCBBx_S_VCTR(1,9);
  SET_GTZC_MPCBBx_S_VCTR(1,10);
  SET_GTZC_MPCBBx_S_VCTR(1,11);
  SET_GTZC_MPCBBx_S_VCTR(1,12);
  SET_GTZC_MPCBBx_S_VCTR(1,13);
  SET_GTZC_MPCBBx_S_VCTR(1,14);
  SET_GTZC_MPCBBx_S_VCTR(1,15);
  SET_GTZC_MPCBBx_S_VCTR(1,16);
  SET_GTZC_MPCBBx_S_VCTR(1,17);
  SET_GTZC_MPCBBx_S_VCTR(1,18);
  SET_GTZC_MPCBBx_S_VCTR(1,19);
  SET_GTZC_MPCBBx_S_VCTR(1,20);
  SET_GTZC_MPCBBx_S_VCTR(1,21);
  SET_GTZC_MPCBBx_S_VCTR(1,22);
  SET_GTZC_MPCBBx_S_VCTR(1,23);

  /*configure SRAM2 */
  SET_GTZC_MPCBBx_S_VCTR(2,0);
  SET_GTZC_MPCBBx_S_VCTR(2,1);
  SET_GTZC_MPCBBx_S_VCTR(2,2);
  SET_GTZC_MPCBBx_S_VCTR(2,3);
  SET_GTZC_MPCBBx_S_VCTR(2,4);
  SET_GTZC_MPCBBx_S_VCTR(2,5);
  SET_GTZC_MPCBBx_S_VCTR(2,6);
  SET_GTZC_MPCBBx_S_VCTR(2,7);

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
    TZ_SAU_Setup();
#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
    if ((FLASH_OPTR & (FLASH_OPTR_SWAP_BANK | FLASH_OPTR_DBANK)) == FLASH_OPTR_DBANK)
        fork_bootloader();
#endif
    clock_pll_on(0);

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
  /* Enable SecureFault handler (HardFault is default) */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
    gtzc_init();
#endif

}

void hal_prepare_boot(void)
{
    clock_pll_off();
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    led_unsecure();
#endif
}


