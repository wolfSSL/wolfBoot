/* stm32l5.h
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


#ifndef STM32L5_DEF_INCLUDED
#define STM32L5_DEF_INCLUDED
/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 L5 register configuration */
/*** RCC ***/
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

#define RCC_CRRCR         (*(volatile uint32_t *)(RCC_BASE + 0x98))
#define RCC_CRRCR_HSI48ON      (1 << 0)
#define RCC_CRRCR_HSI48RDY     (1 << 1)

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
#define TRNG_AHB2_CLOCK_ER  (1 << 18)


#endif /* STM32L5_DEF_INCLUDED */
