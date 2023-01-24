/* stm32u5.h
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

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 U5 register configuration */
/*** RCC ***/
/*!< Memory & Instance aliases and base addresses for Non-Secure/Secure peripherals */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
/*Secure */
#define RCC_BASE            (0x56020C00)   /* RM0456 - Table 4 */
#else
/*Non-Secure */
#define RCC_BASE            (0x46020C00)   /* RM0456 - Table 4 */
#endif

#define FLASH_SECURE_MMAP_BASE (0x0C000000)

#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0456 - Table 108 */
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
#define RCC_CR_HSI48ON              (1 << 12)
#define RCC_CR_HSI48RDY             (1 << 13)
#define RCC_CR_MSIPLLEN             (1 << 3)
#define RCC_CR_MSIRDY               (1 << 2)
#define RCC_CR_MSISON               (1 << 0)

#define RCC_CFGR1            (*(volatile uint32_t *)(RCC_BASE + 0x1C)) /* RM0456 - Table 108 */
#define RCC_CFGR1_SWS               (1 << 2)

/*** APB1&2 PRESCALER ***/
#define RCC_APB_PRESCALER_DIV_NONE  0x0  /* 0xx: HCLK not divided */

/*** AHB PRESCALER ***/
 #define RCC_AHB_PRESCALER_DIV_NONE 0x0  /* 0xxx: SYSCLK not divided */

#define RCC_CFGR_SW_MSI             0x0
#define RCC_CFGR_SW_HSI16           0x1
#define RCC_CFGR_SW_HSE             0x2
#define RCC_CFGR_SW_PLL             0x3

#define RCC_CFGR2            (*(volatile uint32_t *)(RCC_BASE + 0x20)) /* RM0456 - Table 108 */

#define RCC_CFGR2_HPRE_SHIFT        (0x00)
#define RCC_CFGR2_PPRE2_SHIFT       (0x08)
#define RCC_CFGR2_PPRE1_SHIFT       (0x04)

#define RCC_CFGR3            (*(volatile uint32_t *)(RCC_BASE + 0x24)) /* RM0456 - Table 108 */
#define RCC_CFGR3_PPRE3_SHIFT       (0x04)

#define RCC_PLL1CFGR         (*(volatile uint32_t *)(RCC_BASE + 0x28))  /* RM0456 - Table 108 */
#define RCC_PLL1CFGR_PLL1REN          (1 << 18)
#define RCC_PLL1CFGR_PLL1QEN          (1 << 17)
#define RCC_PLL1CFGR_PLL1PEN          (1 << 16)
#define RCC_PLL1CFGR_PLL1FRACEN       (1 << 4)
#define RCC_PLL1CFGR_PLL1RGE_SHIFT    (2)
#define RCC_PLL1VCIRANGE_1            0x03

#define RCC_PLL1CFGR_PLLM_SHIFT       (8)
#define RCC_PLL1CFGR_PLL1MBOOST_SHIFT (12)
#define RCC_PLL1CFGR_PLL1MBOOST_DIV4 0x02

#define RCC_PLLCKSELR_PLLSRC_NONE    0x0
#define RCC_PLLCKSELR_PLLSRC_MSI     0x1
#define RCC_PLLCKSELR_PLLSRC_HSI16   0x2
#define RCC_PLLCKSELR_PLLSRC_HSE     0x3


#define RCC_PLL1DIVR         (*(volatile uint32_t *)(RCC_BASE + 0x34))  /* RM0456 - Table 108 */
#define RCC_PLL1DIVR_PLLN_SHIFT       (0)
#define RCC_PLL1DIVR_PLLP_SHIFT       (9)
#define RCC_PLL1DIVR_PLLQ_SHIFT       (16)
#define RCC_PLL1DIVR_PLLR_SHIFT       (24)

#define RCC_PLL1FRACR         (*(volatile uint32_t *)(RCC_BASE + 0x38))  /* RM0456 - Table 108 */
#define RCC_PLL1FRACR_SHIFT       (3)

#define RCC_CIER         (*(volatile uint32_t *)(RCC_BASE + 0x50))  /* RM0456 - Table 108 */

#define RCC_AHB1ENR             (*(volatile uint32_t *)(RCC_BASE + 0x88)) /* RM0456 - Table 108 */
#define RCC_AHB1ENR_GTZC1EN     (1 << 24)

#define RCC_AHB3ENR             (*(volatile uint32_t *)(RCC_BASE + 0x94)) /* RM0456 - Table 108 */
#define RCC_AHB3ENR_GTZC2EN     (1 << 12)
#define RCC_AHB3ENR_PWREN       (1 << 2)

#define RCC_ICSCR1             (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_ICSCR1_MSIRANGE_SHIFT       (28)
#define RCC_ICSCR1_MSIRGSEL     (1 << 23)
#define RCC_ICSCR1_MSIRG_0      (0)

#define RCC_ICSCR2             (*(volatile uint32_t *)(RCC_BASE + 0x0C))
#define RCC_ICSCR2_MSITRIM0_SHIFT       (15)
#define RCC_ICSCR2_MSITRIM0_DEFAULT  (0x10)

#define RCC_ICSCR3             (*(volatile uint32_t *)(RCC_BASE + 0x10))
#define RCC_ICSCR3_HSITRIM_SHIFT       (16)
#define RCC_ICSCR3_HSITRIM_DEFAULT  (0x10)

/*** PWR ***/
/*!< Memory & Instance aliases and base addresses for Non-Secure/Secure peripherals */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
/*Secure */
#define PWR_BASE            (0x56020800)   /* RM0456 - Table 4 */
#else
/*Non-Secure */
#define PWR_BASE            (0x46020800)   /* RM0456 - Table 4 */
#endif

#define PWR_VOSR             (*(volatile uint32_t *)(PWR_BASE + 0x0C))
#define PWR_VOSR_BOOSTEN     (1 << 18)
#define PWR_VOSR_VOS_SHIFT   (16)
#define PWR_VOSR_VOS_4       (0x0)
#define PWR_VOSR_VOS_3       (0x1)
#define PWR_VOSR_VOS_2       (0x2)
#define PWR_VOSR_VOS_1       (0x3)

#define PWR_VOSR_VOSRDY      (1 << 15)
#define PWR_VOSR_BOOSTRDY    (1 << 14)

#define PWR_SVMCR             (*(volatile uint32_t *)(PWR_BASE + 0x10))
#define PWR_SVMCR_IOS2V       (1 << 29)

#define PWR_UCPDR             (*(volatile uint32_t *)(PWR_BASE + 0x2C))
#define PWR_UCPDR_DBDIS       (1 << 0)

/*** FLASH ***/
#define SYSCFG_APB2_CLOCK_ER_VAL    (1 << 0) /* <<RM0438>> - RCC_APB2ENR - SYSCFGEN */

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
/*Secure*/
#define FLASH_BASE          (0x50022000)   /* RM0456 - Table 4 */
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x24))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x2C))

#define FLASH_SECBB1       ((volatile uint32_t *)(FLASH_BASE + 0x80)) /* Array */
#define FLASH_SECBB2       ((volatile uint32_t *)(FLASH_BASE + 0xA0)) /* Array */
#define FLASH_SECBB_NREGS  4    /* Array length for the two above */

#define FLASH_NS_BASE          (0x40022000)   /* RM0456 - Table 4 */
#define FLASH_NS_KEYR        (*(volatile uint32_t *)(FLASH_NS_BASE + 0x08))
#define FLASH_NS_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_NS_SR          (*(volatile uint32_t *)(FLASH_NS_BASE + 0x20))
#define FLASH_NS_CR          (*(volatile uint32_t *)(FLASH_NS_BASE + 0x28))



#else
/* Non-Secure only */
#define FLASH_BASE          (0x40022000)   /* RM0456 - Table 4 */
#define FLASH_NS_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_NS_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_NS_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_NS_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x28))
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
#define FLASH_SR_WDW                        (1 << 17)

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

/* GPIO*/
#define GPIOH_BASE 0x52021C00

#define GPIOH_SECCFGR (*(volatile uint32_t *)(GPIOH_BASE + 0x30))

#define LED_BOOT_PIN (7) /* PH7 - Discovery - Green Led */
#define LED_USR_PIN (6)  /* PH6 - Discovery  - Red Led */

#define RCC_AHB2ENR1_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C ))
#define GPIOH_AHB2ENR1_CLOCK_ER (1 << 7)
#define TRNG_AHB2_CLOCK_ER      (1 << 18)

/* Reset */
#define OPTR_SWAP_BANK (1 << 20)

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY        (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

