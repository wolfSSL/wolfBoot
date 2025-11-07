/* stm32h5.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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


#ifndef STM32H5_DEF_INCLUDED
#define STM32H5_DEF_INCLUDED

#define PERIPH_CLOCK_FREQ (64000000)

/* Assembly helpers */
#ifndef DMB
#define DMB() __asm__ volatile ("dmb")
#endif
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

#if (defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && !defined(NONSECURE_APP))
#   define TZ_SECURE() (1)
#else
#   define TZ_SECURE() (0)
#endif

/* STM32 H5 register configuration */
/*** RCC ***/
#if TZ_SECURE()
/*Secure */
#define RCC_BASE            (0x54020c00)   /* RM0481 - Table 3 */
#else
/*Non-Secure */
#define RCC_BASE            (0x44020C00)   /* RM0481 - Table 3 */
#endif

#define FLASH_SECURE_MMAP_BASE (0x0C000000)

#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0481 - Table 108 */
#define RCC_CR_PLL3RDY               (1 << 29) /*    RM0481 - Table 108 */
#define RCC_CR_PLL3ON                (1 << 28) /*    RM0481 - Table 108 */
#define RCC_CR_PLL2RDY               (1 << 27) /*    RM0481 - Table 108 */
#define RCC_CR_PLL2ON                (1 << 26) /*    RM0481 - Table 108 */
#define RCC_CR_PLL1RDY               (1 << 25) /*    RM0481 - Table 108 */
#define RCC_CR_PLL1ON                (1 << 24) /*    RM0481 - Table 108 */
#define RCC_CR_HSEEXT                (1 << 20) /*    RM0481 - Table 108 */
#define RCC_CR_HSECSSON              (1 << 19) /*    RM0481 - Table 108 */
#define RCC_CR_HSEBYP               (1 << 18) /*    RM0481 - Table 108 */
#define RCC_CR_HSERDY               (1 << 17) /*    RM0481 - Table 108 */
#define RCC_CR_HSEON                (1 << 16) /*    RM0481 - Table 108 */
#define RCC_CR_HSI48RDY             (1 << 13) /*    RM0481 - Table 108 */
#define RCC_CR_HSI48ON              (1 << 12) /*    RM0481 - Table 108 */
#define RCC_CR_CSIKERON             (1 << 10) /*    RM0481 - Table 108 */
#define RCC_CR_CSIRDY               (1 << 9)  /*    RM0481 - Table 108 */
#define RCC_CR_CSION                (1 << 8)  /*    RM0481 - Table 108 */
#define RCC_CR_HSIDIVF              (1 << 5)  /*    RM0481 - Table 108 */
#define RCC_CR_HSIDIV_SHIFT         (3)       /*    RM0481 - Table 108 */
#define RCC_CR_HSIKERON             (1 << 2)  /*    RM0481 - Table 108 */
#define RCC_CR_HSIRDY               (1 << 1)  /*    RM0481 - Table 108 */
#define RCC_CR_HSION                (1 << 0)  /*    RM0481 - Table 108 */

#define RCC_CFGR1                   (*(volatile uint32_t *)(RCC_BASE + 0x1C))  /* RM0481 - 11.8.5 */
#define RCC_CFGR2                   (*(volatile uint32_t *)(RCC_BASE + 0x20))  /* RM0481 - 11.8.6 */

/* CFGR1 - PLL Source selection */
#define RCC_CFGR1_SW_SHIFT      (0x0)
#define RCC_CFGR1_SWS_SHIFT     (0x3)
#define RCC_CFGR1_SW_HSI        (0x0) /* 00: HSI selected as system clock, default after reset */
#define RCC_CFGR1_SW_CSI        (0x1)
#define RCC_CFGR1_SW_HSE        (0x2)
#define RCC_CFGR1_SW_PLL1       (0x3)
#define RCC_CFGR1_SW_MASK       (0x3)

/* HPRE - PPRE1 - PPRE2 - PPRE3 */
#define RCC_CFGR2_HPRE_SHIFT        (0x0)
#define RCC_CFGR2_PPRE1_SHIFT       (0x4)
#define RCC_CFGR2_PPRE2_SHIFT       (0x8)
#define RCC_CFGR2_PPRE3_SHIFT       (0xC)

/* PLL1 Configuration */
#define RCC_PLL1CFGR               (*(volatile uint32_t *)(RCC_BASE + 0x28))  /* RM0481 - Table 108 */
#define RCC_PLL1DIVR               (*(volatile uint32_t *)(RCC_BASE + 0x34))  /* RM0481 - Table 108 */
#define RCC_PLL1FRACR              (*(volatile uint32_t *)(RCC_BASE + 0x38))  /* RM0481 - Table 108 */

/* PLL2 Configuration */
#define RCC_PLL2CFGR               (*(volatile uint32_t *)(RCC_BASE + 0x2C))  /* RM0481 - Table 108 */
#define RCC_PLL2DIVR               (*(volatile uint32_t *)(RCC_BASE + 0x3C))  /* RM0481 - Table 108 */
#define RCC_PLL2FRACR              (*(volatile uint32_t *)(RCC_BASE + 0x40))  /* RM0481 - Table 108 */

#define RCC_PLLCFGR_PLLSRC_SHIFT (0x0)
#define RCC_PLLCFGR_PLLSRC_HSI   (0x1)
#define RCC_PLLCFGR_PLLSRC_CSI   (0x2)
#define RCC_PLLCFGR_PLLSRC_HSE   (0x3)
#define RCC_PLLCFGR_PLLRGE_SHIFT (0x2)
#define RCC_PLLCFGR_RGE_1_2       (0x0) /* Default at boot: 1-2 MHz */
#define RCC_PLLCFGR_RGE_2_4       (0x1) /* 2-4 MHz */
#define RCC_PLLCFGR_RGE_4_8       (0x2) /* 4-8 MHz */
#define RCC_PLLCFGR_RGE_8_16      (0x3) /* 8-16 MHz */
#define RCC_PLLCFGR_PLL1PEN       (1 << 16)
#define RCC_PLLCFGR_PLL1QEN       (1 << 17)
#define RCC_PLLCFGR_PLL1REN       (1 << 18)


#define RCC_PLLCFGR_PLLFRACEN    (1 << 4)
#define RCC_PLLCFGR_PLLVCOSEL    (1 << 5)
#define RCC_PLLCFGR_PLLM_SHIFT   (0x8)
#define RCC_PLLCFGR_PLLPEN       (1 << 16)
#define RCC_PLLCFGR_PLLQEN       (1 << 17)
#define RCC_PLLCFGR_PLLREN       (1 << 18)

#define RCC_PLLDIVR_DIVN_SHIFT    (0)
#define RCC_PLLDIVR_DIVP_SHIFT    (9)
#define RCC_PLLDIVR_DIVQ_SHIFT    (16)
#define RCC_PLLDIVR_DIVR_SHIFT    (24)

#define RCC_PLLFRACR_FRACN_SHIFT  (3)


#define RCC_APB_PRESCALER_DIV_NONE 0x0  /* 0xx: HCLK not divided */
#define RCC_APB_PRESCALER_DIV_2 0x4     /* 100: HCLK divided by 2 */
#define RCC_APB_PRESCALER_DIV_4 0x5     /* 101: HCLK divided by 4 */
#define RCC_APB_PRESCALER_DIV_8 0x6     /* 110: HCLK divided by 8 */
#define RCC_APB_PRESCALER_DIV_16 0x7    /* 111: HCLK divided by 16 */

#define RCC_AHB_PRESCALER_DIV_NONE 0x0    /* 0xxx: SYSCLK not divided */
#define RCC_AHB_PRESCALER_DIV_2    0x8    /* 1000: SYSCLK divided by 2 */
#define RCC_AHB_PRESCALER_DIV_4    0x9    /* 1001: SYSCLK divided by 4 */
#define RCC_AHB_PRESCALER_DIV_8   0xA    /* 1010: SYSCLK divided by 8 */
#define RCC_AHB_PRESCALER_DIV_16  0xB    /* 1011: SYSCLK divided by 16 */
#define RCC_AHB_PRESCALER_DIV_64  0xC    /* 1100: SYSCLK divided by 64 */
#define RCC_AHB_PRESCALER_DIV_128 0xD    /* 1101: SYSCLK divided by 128 */
#define RCC_AHB_PRESCALER_DIV_256 0xE    /* 1110: SYSCLK divided by 256 */
#define RCC_AHB_PRESCALER_DIV_512 0xF    /* 1111: SYSCLK divided by 512 */


#define RCC_CFGR_SW_MSI             0x0
#define RCC_CFGR_SW_HSI16           0x1
#define RCC_CFGR_SW_HSE             0x2
#define RCC_CFGR_SW_PLL             0x3


#define RCC_PLLCKSELR_PLLSRC_NONE    0x0
#define RCC_PLLCKSELR_PLLSRC_MSI     0x1
#define RCC_PLLCKSELR_PLLSRC_HSI16   0x2
#define RCC_PLLCKSELR_PLLSRC_HSE     0x3

#define RCC_CCIPR1          (*(volatile uint32_t *)(RCC_BASE + 0xD8))
#define RCC_CCIPR3          (*(volatile uint32_t *)(RCC_BASE + 0xE0))
#define RCC_CCIPR1_USART3SEL_SHIFT (6)
#define RCC_CCIPR1_USART3SEL_MASK (0x7)
#define RCC_CCIPR3_LPUART1SEL_SHIFT (24)
#define RCC_CCIPR3_LPUART1SEL_MASK (0x7)


#define RCC_CRRCR         (*(volatile uint32_t *)(RCC_BASE + 0x98))
#define RCC_CRRCR_HSI48ON      (1 << 0)
#define RCC_CRRCR_HSI48RDY     (1 << 1)

/*** PWR ***/
/*!< Memory & Instance aliases and base addresses for Non-Secure/Secure peripherals */
#if TZ_SECURE()
/*Secure */
#define PWR_BASE            (0x54020800)   //RM0481 - Table 3
#else
/*Non-Secure */
#define PWR_BASE            (0x44020800)   //RM0481 - Table 3
#endif

#define PWR_VOSCR              (*(volatile uint32_t *)(PWR_BASE + 0x10))
#define PWR_VOSSR              (*(volatile uint32_t *)(PWR_BASE + 0x14))
#define PWR_VOS_SCALE_0      (0x3 << 4)   //RM0481 - 10.11.3
#define PWR_VOS_SCALE_3      (0x0 << 4)   //RM0481 - 10.11.3 - Default on power up
#define PWR_VOS_MASK         (0x3 << 4)   //RM0481 - 10.11.3
#define PWR_VOSRDY           (1 << 3)     //RM0481 - 10.11.4 - Voltage scaling ready

#define PWR_CR2              (*(volatile uint32_t *)(PWR_BASE + 0x04))
#define PWR_CR2_IOSV         (1 << 9)
#define PWR_CR3              (*(volatile uint32_t *)(PWR_BASE + 0x08))
#define PWR_CR3_UCPD_DBDIS   (1 << 14)
#define PWR_CR4              (*(volatile uint32_t *)(PWR_BASE + 0x0C))

#define PWR_SR1              (*(volatile uint32_t *)(PWR_BASE + 0x10))
#define PWR_SR2              (*(volatile uint32_t *)(PWR_BASE + 0x14))
#define PWR_SR2_VOSF         (1 << 10)

#if TZ_SECURE()
/*Secure*/
#define FLASH_BASE          (0x50022000)   //RM0481 - Table 75
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x24))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x2C))



#define FLASH_SECBB1       ((volatile uint32_t *)(FLASH_BASE + 0x0A0)) /* Array */
#define FLASH_SECBB2       ((volatile uint32_t *)(FLASH_BASE + 0x1A0)) /* Array */
#define FLASH_SECBB_NREGS  4    /* Array length for the two above */

#define FLASH_NS_BASE          (0x40022000)   //RM0481 - Table 3
#define FLASH_NS_KEYR        (*(volatile uint32_t *)(FLASH_NS_BASE + 0x08))
#define FLASH_NS_OPTKEYR     (*(volatile uint32_t *)(FLASH_NS_BASE + 0x0C))
#define FLASH_NS_SR          (*(volatile uint32_t *)(FLASH_NS_BASE + 0x20))
#define FLASH_NS_CR          (*(volatile uint32_t *)(FLASH_NS_BASE + 0x28))

#define TZSC_SECCFGR1   *((volatile uint32_t *)(0x50036410))
#define TZSC_SECCFGR1_USART3SEC (1 << 14) /* USART3 */
#define TZSC_SECCFGR2   *((volatile uint32_t *)(0x50036414))
#define TZSC_SECCFGR2_LPUART1SEC (1 << 25) /* LPUART1 */

/* Mapping FLASH_SECCR for bank swapping */
#define FLASH_CCR            (*(volatile uint32_t *)(FLASH_BASE + 0x34))

#else
/* Non-Secure only */
#define FLASH_BASE          (0x40022000)   //RM0481 - Table 3
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_OPTKEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_BASE + 0x28))

/* Mapping FLASH_NSCCR for bank swapping */
#define FLASH_CCR            (*(volatile uint32_t *)(FLASH_BASE + 0x30))

#endif

/* Both secure + non secure */
#define FLASH_OPTCR       (*(volatile uint32_t *)(FLASH_BASE + 0x1C))
#define FLASH_OPSR        (*(volatile uint32_t *)(FLASH_BASE + 0x18))
#define FLASH_OTPBLR_CUR  (*(volatile uint32_t *)(FLASH_BASE + 0x90))
#define FLASH_OTPBLR_PRG  (*(volatile uint32_t *)(FLASH_BASE + 0x94))

#define FLASH_OPSR_DATA_OP          (1 << 21)
#define FLASH_OPSR_BK_OP            (1 << 22)
#define FLASH_OPSR_SYSF_OP          (1 << 23)
#define FLASH_OPSR_OTP_OP           (1 << 24)
#define FLASH_OPSR_CODE_MASK        (0x7 << 29)
#define FLASH_OPSR_CODE_WRITE       (0x1 << 29)
#define FLASH_OPSR_CODE_OBK_ALT_ERASE   (0x2 << 29)
#define FLASH_OPSR_CODE_SEC_ERASE   (0x3 << 29)
#define FLASH_OPSR_CODE_BANK_ERASE  (0x4 << 29)
#define FLASH_OPSR_CODE_MASS_ERASE  (0x5 << 29)
#define FLASH_OPSR_CODE_OPT_CHANGE  (0x6 << 29)
#define FLASH_OPSR_CODE_OBK_SWAP    (0x7 << 29)

#define FLASH_OPTSR_CUR   (*(volatile uint32_t *)(FLASH_BASE + 0x50))
#define FLASH_OPTSR_PRG   (*(volatile uint32_t *)(FLASH_BASE + 0x54))
#define FLASH_OPTSR_SWAP_BANK (1 << 31)


/* Register values (for both secure and non secure registers)
 * RM0481 Table 75 */

#define FLASH_SR_BSY                        (1 << 0)
#define FLASH_SR_WBNE                       (1 << 1)
#define FLASH_SR_DBNE                       (1 << 3)
#define FLASH_SR_EOP                        (1 << 16)
#define FLASH_SR_WRPE                       (1 << 17)
#define FLASH_SR_PGSE                       (1 << 18)
#define FLASH_SR_STRBE                      (1 << 19)
#define FLASH_SR_INCE                       (1 << 20)
#define FLASH_SR_OPTE                       (1 << 21)
#define FLASH_SR_OPTWE                      (1 << 22)

#define FLASH_CCR_CLR_BUSY                  (1 << 0)
#define FLASH_CCR_CLR_WBNE                  (1 << 1)
#define FLASH_CCR_CLR_DBNE                  (1 << 3)
#define FLASH_CCR_CLR_EOP                   (1 << 16)
#define FLASH_CCR_CLR_WRPE                  (1 << 17)
#define FLASH_CCR_CLR_PGSE                  (1 << 18)
#define FLASH_CCR_CLR_STRBE                 (1 << 19)
#define FLASH_CCR_CLR_INCE                  (1 << 20)
#define FLASH_CCR_CLR_OPTE                  (1 << 21)
#define FLASH_CCR_CLR_OPTWE                 (1 << 22)

#define FLASH_CR_LOCK                       (1 << 0)
#define FLASH_CR_PG                         (1 << 1)
#define FLASH_CR_SER                        (1 << 2)
#define FLASH_CR_BER                        (1 << 3)
#define FLASH_CR_FW                         (1 << 4)
#define FLASH_CR_STRT                       (1 << 5)
/* Page number selection:
 * Up to  31 pages: H523/33xx
 * Up to 127 pages: All others
 */
#define FLASH_CR_PNB_SHIFT                  6
#define FLASH_CR_PNB_MASK                   0x7F
#define FLASH_CR_MER                        (1 << 15)
#define FLASH_CR_EOPIE                      (1 << 16)
#define FLASH_CR_WRPERRIE                   (1 << 17)
#define FLASH_CR_PGSERRIE                   (1 << 18)
#define FLASH_CR_STRBERRIE                  (1 << 19)
#define FLASH_CR_INCERRIE                   (1 << 20)
#define FLASH_CR_OBKIE                      (1 << 21)
#define FLASH_CR_OBKWIE                     (1 << 22)
#define FLASH_CR_OPTCHANGEERRIE             (1 << 23)
#define FLASH_CR_BKSEL                      (1 << 31)


#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_LATENCY_MASK              (0x0F)
#define FLASH_ACR_WRHIGHFREQ_MASK           (0x03)
#define FLASH_ACR_WRHIGHFREQ_SHIFT          (4)


#define FLASH_OPTCR          (*(volatile uint32_t *)(FLASH_BASE + 0x1C))
#define FLASH_OPTCR_OPTSTRT   (1 << 1)
#define FLASH_OPTCR_OPTLOCK   (1 << 0)
#define FLASH_OPTCR_SWAP_BANK (1 << 31)

#define FLASHMEM_ADDRESS_SPACE    (0x08000000)
#define FLASH_PAGE_SIZE           (0x2000)      /* 8KB */
#define FLASH_BANK2_BASE          (0x08100000) /*!< Base address of Flash Bank2     */
#define BOOTLOADER_SIZE           (WOLFBOOT_PARTITION_BOOT_ADDRESS - FLASHMEM_ADDRESS_SPACE)
#define FLASH_TOP                 (0x081FFFFF) /*!< FLASH end address (sector 127)  */

#define FLASH_KEY1                            (0x45670123U)
#define FLASH_KEY2                            (0xCDEF89ABU)
#define FLASH_OPTKEY1                         (0x08192A3BU)
#define FLASH_OPTKEY2                         (0x4C5D6E7FU)

/* GPIO*/
#if (TZ_SECURE())
#define GPIOA_BASE 0x52020000
#define GPIOB_BASE 0x52020400
#define GPIOC_BASE 0x52020800
#define GPIOD_BASE 0x52020C00
#define GPIOF_BASE 0x52021400
#define GPIOG_BASE 0x52021800
#else
#define GPIOA_BASE 0x42020000
#define GPIOB_BASE 0x42020400
#define GPIOC_BASE 0x42020800
#define GPIOD_BASE 0x42020C00
#define GPIOF_BASE 0x42021400
#define GPIOG_BASE 0x42021800
#endif

#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPE (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OSPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x0c))
#define GPIOB_ODR   (*(volatile uint32_t *)(GPIOB_BASE + 0x14))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

#define GPIOD_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OTYPE (*(volatile uint32_t *)(GPIOD_BASE + 0x04))
#define GPIOD_OSPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x08))
#define GPIOD_PUPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x0c))
#define GPIOD_ODR   (*(volatile uint32_t *)(GPIOD_BASE + 0x14))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))
#define GPIOD_AFL   (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIOD_AFH   (*(volatile uint32_t *)(GPIOD_BASE + 0x24))


/* RCC AHB2 Clock Enable Register */
#define RCC_AHB2_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C ))
#define GPIOA_AHB2_CLOCK_ER (1 << 0)
#define GPIOB_AHB2_CLOCK_ER (1 << 1)
#define GPIOC_AHB2_CLOCK_ER (1 << 2)
#define GPIOD_AHB2_CLOCK_ER (1 << 3)
#define GPIOF_AHB2_CLOCK_ER (1 << 5)
#define GPIOG_AHB2_CLOCK_ER (1 << 6)
#define TRNG_AHB2_CLOCK_ER  (1 << 18)
#define PKA_AHB2_CLOCK_ER   (1 << 19)
#define SAES_AHB2_CLOCK_ER  (1 << 20)
#define SRAM2_AHB2_CLOCK_ER  (1 << 30)
#define SRAM3_AHB2_CLOCK_ER  (1 << 31)

/* RCC: APB1 and APB2 */
#define RCC_APB2_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0xA4))
#define UART1_APB2_CLOCK_ER_VAL (1 << 14)

#define RCC_APB1L_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x9C))
#define UART3_APB1L_CLOCK_ER_VAL (1 << 18)

#define RCC_AHB2ENR1_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C ))

#define GPIOB_AHB2ENR1_CLOCK_ER (1 << 1)
#define GPIOD_AHB2ENR1_CLOCK_ER (1 << 3)


/* UART */
#if (TZ_SECURE())
#define UART1 (0x54002400) /* Using LPUART1 */
#define UART3 (0x50005800) /* Using USART3 */
#else
#define UART1 (0x44002400)
#define UART3 (0x40004800)
#endif

/* USE_UART1
 * Set to 0 for VCP over USB
 * Set to 1 for Arduino D0, D1 pins on nucleo
 */
#if defined(USE_UART1) && USE_UART1 == 1
#define USE_UART UART1
#else
#define USE_UART UART3
#endif

#define UART_CR1(base) (*(volatile uint32_t *)((base) + 0x00))
#define UART_CR2(base) (*(volatile uint32_t *)((base) + 0x04))
#define UART_CR3(base) (*(volatile uint32_t *)((base) + 0x08))
#define UART_BRR(base) (*(volatile uint32_t *)((base) + 0x0c))
#define UART_ISR(base) (*(volatile uint32_t *)((base) + 0x1c))
#define UART_ICR(base) (*(volatile uint32_t *)((base) + 0x20))
#define UART_RDR(base) (*(volatile uint32_t *)((base) + 0x24))
#define UART_TDR(base) (*(volatile uint32_t *)((base) + 0x28))
#define UART_PRE(base) (*(volatile uint32_t *)((base) + 0x2C))

#define UART_CR1_UART_ENABLE    (1 << 0)
#define UART_CR1_OVER8          (1 << 15)
#define UART_CR1_SYMBOL_LEN     (1 << 12)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR2_STOPBITS       (3 << 12)
#define UART_CR2_LINEN          (1 << 14)
#define UART_CR2_CLKEN          (1 << 11)
#define UART_CR3_HDSEL          (1 << 3)
#define UART_CR3_DEM            (1 << 14)
#define UART_CR3_IREN           (1 << 1)
#define UART_CR3_RXFTIE         (1 << 28)
#define UART_ISR_TX_EMPTY       (1 << 7)
#define UART_ISR_RX_NOTEMPTY    (1 << 5)
#define UART_EPE                (1 << 0)    /* Parity error */
#define UART_EFE                (1 << 1)    /* Framing error */
#define UART_ENE                (1 << 2)    /* Noise error */
#define UART_ORE                (1 << 3)    /* Overrun error */


/* OTP FLASH AREA */
#define FLASH_OTP_BASE 0x08FFF000
#define FLASH_OTP_END  0x08FFF7FF
#define OTP_SIZE             2048
#define OTP_BLOCKS             32

/* UART1 pin configuration */
#define UART1_PIN_AF 7
#define UART1_RX_PIN 7
#define UART1_TX_PIN 6

#define UART3_PIN_AF 7
#define UART3_RX_PIN 9
#define UART3_TX_PIN 8

/* GPIO secure configuration */
#define GPIO_SECCFGR(base) (*(volatile uint32_t *)(base + 0x30))
#define LED_AHB2_ENABLE (GPIOG_AHB2_CLOCK_ER | GPIOB_AHB2_CLOCK_ER | \
        GPIOF_AHB2_CLOCK_ER)
#define LED_BOOT_PIN (4)  /* PG4 - Nucleo board - Orange Led */
#define LED_USR_PIN (0)   /* PB0  - Nucleo board  - Green Led */
#define LED_EXTRA_PIN (4) /* PF4 - Nucleo board - Blue Led */


#endif /* STM32H5_DEF_INCLUDED */
