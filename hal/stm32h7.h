/* stm32h7.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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


#ifndef HAL_STM32H7_H
#define HAL_STM32H7_H

#include <stdint.h>
#include "image.h"
#include "hal.h"

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 H7 register configuration */
/*** RCC ***/
#define RCC_BASE            (0x58024400) /* RM0433 - Table 8 */
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0433 - 7.7.2 */
#define RCC_PLLCKSELR       (*(volatile uint32_t *)(RCC_BASE + 0x28))  /* RM0433 - 7.7.11 */
#define RCC_PLLCFGR         (*(volatile uint32_t *)(RCC_BASE + 0x2C))  /* RM0433 - 7.7.12 */
#define RCC_PLL1DIVR        (*(volatile uint32_t *)(RCC_BASE + 0x30))  /* RM0433 - 7.7.13 */

#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x10))  /* RM0433 - 7.7.7 */
#define RCC_D1CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x18))  /* RM0433 - 7.7.8 */
#define RCC_D2CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x1C))  /* RM0433 - 7.7.8 */
#define RCC_D3CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x20))  /* RM0433 - 7.7.9 */

#define RCC_D2CCIP2R        (*(volatile uint32_t *)(RCC_BASE + 0x54))  /* RM0433 - 8.7.21 */

#define APB1_CLOCK_LRST     (*(volatile uint32_t *)(RCC_BASE + 0x90))  /* RM0433 - 8.7.33 - RCC_APB1LRSTR */

#define AHB4_CLOCK_ENR      (*(volatile uint32_t *)(RCC_BASE + 0xE0))  /* RM0433 - 8.7.43 */
#define APB1_CLOCK_LER      (*(volatile uint32_t *)(RCC_BASE + 0xE8))  /* RM0433 - 8.7.45 - RCC_APB1LENR */
#define APB1_CLOCK_HER      (*(volatile uint32_t *)(RCC_BASE + 0xEC))  /* RM0433 - 8.7.46 */
#define APB2_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0xF0))  /* RM0433 - 8.7.47 */
#define AHB3_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0xD4))  /* RM0433 - 8.7.40 */

#define RCC_CR_PLL1RDY              (1 << 25)
#define RCC_CR_PLL1ON               (1 << 24)
#define RCC_CR_HSEBYP               (1 << 18)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 2)
#define RCC_CR_HSION                (1 << 0)

#define RCC_CFGR_SW_HSISYS          0x0
#define RCC_CFGR_SW_PLL             0x3
#define RCC_PLLCFGR_DIVR1EN        (1 << 18)
#define RCC_PLLCFGR_DIVQ1EN        (1 << 17)
#define RCC_PLLCFGR_DIVP1EN        (1 << 16)

#define RCC_PLLCFGR_PLL1VCOSEL     (1 << 1)

#define RCC_PLLCFGR_PLL1RGE_2_4    0x1
#define RCC_PLLCFGR_PLL1RGE_SHIFT  0x2

#define RCC_PLLCKSELR_DIVM1         (1 << 4)
#define RCC_PLLCKSELR_PLLSRC_HSI     0x0
#define RCC_PLLCKSELR_PLLSRC_HSE     0x2

#define RCC_PLLCKSELR_DIVM1_NONE     0x1

/*** GPIO ***/
#define GPIOA_BASE (0x58020000)
#define GPIOB_BASE (0x58020400)
#define GPIOC_BASE (0x58020800)
#define GPIOD_BASE (0x58020C00)
#define GPIOE_BASE (0x58021000)
#define GPIOF_BASE (0x58021400)
#define GPIOG_BASE (0x58021800)

#define GPIO_MODE(base)  (*(volatile uint32_t *)((base) + 0x00))
#define GPIO_OTYPE(base) (*(volatile uint32_t *)((base) + 0x04))
#define GPIO_OSPD(base)  (*(volatile uint32_t *)((base) + 0x08))
#define GPIO_PUPD(base)  (*(volatile uint32_t *)((base) + 0x0c))
#define GPIO_ODR(base)   (*(volatile uint32_t *)((base) + 0x14))
#define GPIO_BSRR(base)  (*(volatile uint32_t *)((base) + 0x18))
#define GPIO_AFRL(base)  (*(volatile uint32_t *)((base) + 0x20))
#define GPIO_AFRH(base)  (*(volatile uint32_t *)((base) + 0x24))


/*** PWR ***/
#define PWR_BASE             (0x58024800) /* RM0433 - Table 8 */
#define PWR_CSR1             (*(volatile uint32_t *)(PWR_BASE + 0x04))  /* RM0433 - 5.8.x */
#define PWR_CSR1_ACTVOSRDY   (1 << 13)
#define PWR_CR3              (*(volatile uint32_t *)(PWR_BASE + 0x0C))  /* RM0433 - 5.8.4 */
#define PWR_CR3_SCUEN        (1 << 2)
#define PWR_CR3_LDOEN        (1 << 1)
#define PWR_D3CR             (*(volatile uint32_t *)(PWR_BASE + 0x18))  /* RM0433 - 5.8.6 */
#define PWR_D3CR_VOSRDY      (1 << 13)
#define PWR_D3CR_VOS_SHIFT   (14)
#define PWR_D3CR_VOS_SCALE_1 (3)

#define SYSCFG_BASE          (0x58000400) /* RM0433 - Table 8 */
#define SYSCFG_PMCR          (*(volatile uint32_t *)(SYSCFG_BASE + 0x04))  /* RM0433 - 5.8.4 */
#define SYSCFG_PWRCR         (*(volatile uint32_t *)(SYSCFG_BASE + 0x04))  /* RM0433 - 5.8.4 */
#define SYSCFG_UR0           (*(volatile uint32_t *)(SYSCFG_BASE + 0x300))  /* RM0433 - 12.3.1.2 */
#define SYSCFG_PWRCR_ODEN    (1 << 0)
#define SYSCFG_UR0_BKS       (1 << 0)   /* bank swap */

/*** APB PRESCALER ***/
#define RCC_PRESCALER_DIV_NONE 0
#define RCC_PRESCALER_DIV_2 8


/*** UART ***/
#ifndef CLOCK_SPEED
#define CLOCK_SPEED 64000000UL /* 120MHz pclk1, 64MHz HSI */
#endif
#ifndef BAUD_RATE
#define BAUD_RATE   115200
#endif
#ifndef UART_PORT
#define UART_PORT   3 /* default to Nucleo VCOM port */
#endif

#if UART_PORT == 3
/* USART3 Base address (connected to ST virtual com port on Nucleo board) */
#define UART_BASE      (0x40004800)
#define UART_GPIO_BASE GPIOD_BASE
#define UART_TX_PIN    8 /* PD8, USART Transmit pin */
#define UART_RX_PIN    9 /* PD9, USART Receive pin */
#define UART_PIN_AF    7 /* AF stands for Alternate Function. USART TX/RX */
#elif UART_PORT == 5
/* USART5 Base address */
#define UART_BASE      (0x40005000)
#define UART_GPIO_BASE GPIOB_BASE
#define UART_TX_PIN    13 /* PB13, USART Transmit pin */
#define UART_RX_PIN    12 /* PB12, USART Receive pin */
#define UART_PIN_AF    14 /* AF stands for Alternate Function. USART TX/RX */
#else
/* USART2 Base address (chosen because of its pin layout on Nucleo board) */
#define UART_BASE      (0x40004400)
#define UART_GPIO_BASE GPIOD_BASE
#define UART_TX_PIN    5 /* PD5, USART Transmit pin */
#define UART_RX_PIN    6 /* PD6, USART Receive pin */
#define UART_PIN_AF    7 /* AF stands for Alternate Function. USART TX/RX */
#endif

/* UART/USART: Defining register start addresses. */
#define UART_CR1(base)    (*(volatile uint32_t *)((base) + 0x00))
#define UART_CR2(base)    (*(volatile uint32_t *)((base) + 0x04))
#define UART_CR3(base)    (*(volatile uint32_t *)((base) + 0x08))
#define UART_BRR(base)    (*(volatile uint32_t *)((base) + 0x0C))
#define UART_RQR(base)    (*(volatile uint32_t *)((base) + 0x18))
#define UART_ISR(base)    (*(volatile uint32_t *)((base) + 0x1C))
#define UART_ICR(base)    (*(volatile uint32_t *)((base) + 0x20))
#define UART_RDR(base)    (*(volatile uint32_t *)((base) + 0x24))
#define UART_TDR(base)    (*(volatile uint32_t *)((base) + 0x28))
#define UART_PRESC(base)  (*(volatile uint32_t *)((base) + 0x2C))

/* UART/USART: Defining register bit placement for CR1 and ISR register for readability. */
#define UART_CR1_UART_ENABLE                (1 << 0)
#define UART_CR1_TX_ENABLE                  (1 << 3)
#define UART_CR1_RX_ENABLE                  (1 << 2)
#define UART_CR1_M1                         (1 << 28)
#define UART_CR1_M0                         (1 << 12)
#define UART_CR1_PARITY_ENABLED             (1 << 10)
#define UART_CR1_PARITY_ODD                 (1 << 9)
#define UART_CR1_FIFOEN                     (1 << 29)
#define UART_CR1_OVER8                      (1 << 15)

#define UART_CR2_STOP_MASK                  (0x3 << 12)
#define UART_CR2_STOP(bits)                 (((bits) & 0x3) << 12)
#define UART_CR2_LINEN                      (1 << 14)
#define UART_CR2_CLKEN                      (1 << 11)

#define UART_CR3_SCEN                       (1 << 5)
#define UART_CR3_HDSEL                      (1 << 3)
#define UART_CR3_IREN                       (1 << 1)

#define UART_ISR_TX_FIFO_NOT_FULL           (1 << 7) /* Transmit Data Empty (TXE) or TX FIFO Not Full (TXFNF) */
#define UART_ISR_RX_FIFO_NOT_EMPTY          (1 << 5)
#define UART_ISR_TRANSMISSION_COMPLETE      (1 << 6)

/* RCC: Defining register bit placement for APB1, APB2, AHB1 and AHB4 register for readability. */
#define RCC_APB1_USART2_EN                  (1 << 17)
#define RCC_APB1_USART3_EN                  (1 << 18)
#define RCC_APB1_UART4_EN                   (1 << 19)
#define RCC_APB1_UART5_EN                   (1 << 20)
#define RCC_APB1_UART7_EN                   (1 << 30)
#define RCC_APB1_UART8_EN                   (1 << 31)
#define RCC_APB2_USART1_EN                  (1 << 4)
#define RCC_APB2_USART6_EN                  (1 << 5)

#define RCC_AHB4_GPIOB_EN                   (1 << 1)
#define RCC_AHB4_GPIOD_EN                   (1 << 3)

/*** QSPI ***/
/* See hal/spi/spi_drv_stm32.c */


/*** FLASH ***/
#define SYSCFG_APB4_CLOCK_ER_VAL    (1 << 0) /* RM0433 - 7.7.48 - RCC_APB4ENR - SYSCFGEN */

#define FLASH_BASE          (0x52002000)   /* RM0433 - Table 8 */
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00)) /* RM0433 - 3.9.1 - FLASH_ACR */

#define FLASH_OPTKEYR       (*(volatile uint32_t *)(FLASH_BASE + 0x08)) /* FLASH option key register */
#define FLASH_OPTCR         (*(volatile uint32_t *)(FLASH_BASE + 0x18)) /* FLASH option control register */
#define FLASH_OPTSR_CUR     (*(volatile uint32_t *)(FLASH_BASE + 0x1C)) /* FLASH option status register */

/* Bank 1 */
#define FLASH_KEYR1         (*(volatile uint32_t *)(FLASH_BASE + 0x04)) /* RM0433 - 3.9.2 - FLASH_KEYR 1 */
#define FLASH_CR1           (*(volatile uint32_t *)(FLASH_BASE + 0x0C)) /* RM0433 - 3.9.4 - FLASH_CR 1 */
#define FLASH_SR1           (*(volatile uint32_t *)(FLASH_BASE + 0x10)) /* RM0433 - 3.9.5 - FLASH_SR 1 */

/* Bank 2 */
#define FLASH_KEYR2         (*(volatile uint32_t *)(FLASH_BASE + 0x104)) /* RM0433 - 3.9.24 - FLASH_KEYR 2 */
#define FLASH_SR2           (*(volatile uint32_t *)(FLASH_BASE + 0x110)) /* RM0433 - 3.9.26 - FLASH_SR 2 */
#define FLASH_CR2           (*(volatile uint32_t *)(FLASH_BASE + 0x10C)) /* RM0433 - 3.9.25 - FLASH_CR 2 */

/* Flash Configuration */
#define FLASHMEM_ADDRESS_SPACE    (0x08000000UL)
#define FLASH_PAGE_SIZE           (0x20000) /* 128KB */
#define FLASH_BANK2_BASE          (0x08100000UL) /*!< Base address of : (up to 1 MB) Flash Bank2 accessible over AXI */
#define FLASH_BANK2_BASE_REL      (FLASH_BANK2_BASE - FLASHMEM_ADDRESS_SPACE)
#define FLASH_TOP                 (0x081FFFFFUL) /*!< FLASH end address  */

/* Register values */
#define FLASH_ACR_LATENCY_MASK              (0x07)
#define FLASH_SR_BSY                        (1 << 0)
#define FLASH_SR_WBNE                       (1 << 1)
#define FLASH_SR_QW                         (1 << 2)
#define FLASH_SR_WRPERR                     (1 << 17)
#define FLASH_SR_PGSERR                     (1 << 18)
#define FLASH_SR_STRBERR                    (1 << 19)
#define FLASH_SR_INCERR                     (1 << 21)
#define FLASH_SR_OPERR                      (1 << 22)
#define FLASH_SR_RDPERR                     (1 << 23)
#define FLASH_SR_RDSERR                     (1 << 24)
#define FLASH_SR_SNECCERR                   (1 << 25)
#define FLASH_SR_DBECCERR                   (1 << 26)
#define FLASH_SR_EOP                        (1 << 16)

#define FLASH_CR_LOCK                       (1 << 0) /* RM0433 - 3.7.5 - FLASH_CR */
#define FLASH_CR_STRT                       (1 << 7)
#define FLASH_CR_PSIZE                      (1 << 4)
#define FLASH_CR_BER                        (1 << 3)
#define FLASH_CR_SER                        (1 << 2)
#define FLASH_CR_PG                         (1 << 1)
#define FLASH_CR2_SPSS2                     (1 << 14)

#define FLASH_OPTSR_CUR_BSY                 (1 << 0)

#define FLASH_OPTCR_OPTLOCK                 (1 << 0)  /* lock option configuration bit */
#define FLASH_OPTCR_OPTSTART                (1 << 1)  /* Option byte start change option configuration bit */
#define FLASH_OPTCR_MER                     (1 << 4)  /* Mass erase request */
#define FLASH_OPTCR_PG_OTP                  (1 << 5)  /* OTP program control bit */
#define FLASH_OPTCR_OPTCHANGEERRIE          (1 << 30) /* Option byte change error interrupt enable bit */
#define FLASH_OPTCR_SWAP_BANK               (1 << 31) /* Bank swapping option configuration bit */

#define FLASH_CR_SNB_SHIFT                  8     /* SNB bits 10:8 */
#define FLASH_CR_SNB_MASK                   0x7   /* SNB bits 10:8 - 3 bits */

#define FLASH_KEY1                          (0x45670123U)
#define FLASH_KEY2                          (0xCDEF89ABU)

#define FLASH_OPT_KEY1                      (0x08192A3BU)
#define FLASH_OPT_KEY2                      (0x4C5D6E7FU)

/* OTP FLASH AREA */
#define FLASH_OTP_BASE 0x08FFF000
#define FLASH_OTP_END  0x08FFF3FF
#define OTP_SIZE       1024
#define OTP_BLOCKS     16


/* STM32H7: Due to ECC functionality, it is not possible to write partition/sector
 * flags and signature more than once. This flags_cache is used to intercept write operations and
 * ensures that the sector is always erased before each write.
 */

#define STM32H7_SECTOR_SIZE 0x20000

#if defined(WOLFBOOT_PARTITION_SIZE) && \
    (WOLFBOOT_PARTITION_SIZE < (2 * STM32H7_SECTOR_SIZE))
#   error "Please use a bigger WOLFBOOT_PARTITION_SIZE, since the last 128KB on each partition will be reserved for bootloader flags"
#endif

#define STM32H7_PART_BOOT_END   (WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)
#define STM32H7_PART_UPDATE_END (WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)
#define STM32H7_WORD_SIZE (32)
#define STM32H7_PART_BOOT_FLAGS_PAGE_ADDRESS \
    (((STM32H7_PART_BOOT_END - 1)   / STM32H7_SECTOR_SIZE) * STM32H7_SECTOR_SIZE)
#define STM32H7_PART_UPDATE_FLAGS_PAGE_ADDRESS \
    (((STM32H7_PART_UPDATE_END - 1) / STM32H7_SECTOR_SIZE) * STM32H7_SECTOR_SIZE)
#define STM32H7_BOOT_FLAGS_PAGE(x) \
    ((x >= STM32H7_PART_BOOT_FLAGS_PAGE_ADDRESS)   && (x < STM32H7_PART_BOOT_END))
#define STM32H7_UPDATE_FLAGS_PAGE(x) \
    ((x >= STM32H7_PART_UPDATE_FLAGS_PAGE_ADDRESS) && (x < STM32H7_PART_UPDATE_END))

#endif /* HAL_STM32H7_H */
