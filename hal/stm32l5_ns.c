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
/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 L5 register configuration */
/*!< Memory & Instance aliases and base addresses for Non-Secure/Secure peripherals */
/*Non-Secure */
#define RCC_BASE            (0x40021000)   //RM0438 - Table 4

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
/*Non-Secure */
#define PWR_BASE            (0x40007000)   //RM0438 - Table 4

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

/*Non-Secure*/
#define FLASH_BASE          (0x40022000)   //RM0438 - Table 4
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x28))

/* Register values */
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
#define FLASH_CR_OPTLOCK                    (1 << 30)
#define FLASH_CR_LOCK                       (1 << 31)

#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_LATENCY_MASK              (0x0F)

#define FLASHMEM_ADDRESS_SPACE    (0x08000000)
#define FLASH_PAGE_SIZE           (0x800) /* 2KB */
#define FLASH_BANK2_BASE          (0x08040000) /*!< Base address of Flash Bank2     */
#define FLASH_TOP                 (0x0807FFFF) /*!< FLASH end address  */

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR =  (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates ;
}

static RAMFUNCTION void flash_wait_complete(uint8_t bank)
{
   while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY);

}

static void RAMFUNCTION flash_clear_errors(uint8_t bank)
{

  FLASH_SR |= ( FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_OPTWERR ) ;

}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
  int i = 0;
  uint32_t *src, *dst;

  flash_clear_errors(0);

  src = (uint32_t *)data;
  dst = (uint32_t *)(address + FLASHMEM_ADDRESS_SPACE);

  while (i < len) {
    FLASH_CR |= FLASH_CR_PG;
    dst[i >> 2] = src[i >> 2];
    dst[(i >> 2) + 1] = src[(i >> 2) + 1];
    flash_wait_complete(0);
    FLASH_CR &= ~FLASH_CR_PG;
    i+=8;
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
    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
       // considering DBANK = 1
        if (p < (FLASH_BANK2_BASE -FLASHMEM_ADDRESS_SPACE) )
        {
          FLASH_CR &= ~FLASH_CR_BKER;
        }
        if(p>=(FLASH_BANK2_BASE -FLASHMEM_ADDRESS_SPACE) && (p <= (FLASH_TOP -FLASHMEM_ADDRESS_SPACE) ))
        {
          FLASH_CR |= FLASH_CR_BKER;
        }

        uint32_t reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT)| FLASH_CR_PER));
        FLASH_CR = reg | (((p >> 11) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER );
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
    while ((PWR_SR2 & PWR_SR2_VOSF) != 0) {};

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

void hal_init(void)
{
    clock_pll_on(0);
}

void hal_prepare_boot(void)
{
    clock_pll_off();
}
