/* stm32g4.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/* STM32G4 family (e.g. STM32G491RE on NUCLEO-G491RE).
 * Cortex-M4F, no TrustZone. Single-bank 512 KB flash, 2 KB pages.
 * Reference: RM0440. Shared register map for the wolfBoot HAL and
 * the test-app -- both pull in the same definitions so wire-up
 * doesn't drift between them. */

#ifndef _STM32G4_H_
#define _STM32G4_H_

#include <stdint.h>

/* Assembly helpers. The "memory" clobber tells the compiler not to
 * reorder loads/stores across the barrier; without it the asm only
 * orders hardware accesses, not the compiler's own scheduling. */
#define DMB() __asm__ volatile ("dmb" ::: "memory")
#define ISB() __asm__ volatile ("isb" ::: "memory")
#define DSB() __asm__ volatile ("dsb" ::: "memory")


/* -------- RCC (AHB1 + 0x1000 = 0x40021000) -------- RM0440 - 7.4 -------- */
#define RCC_BASE                  (0x40021000)
#define RCC_CR                    (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR                  (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_PLLCFGR               (*(volatile uint32_t *)(RCC_BASE + 0x0C))
#define RCC_CRRCR                 (*(volatile uint32_t *)(RCC_BASE + 0x98))
#define RCC_CCIPR                 (*(volatile uint32_t *)(RCC_BASE + 0x88))
#define RCC_AHB2ENR               (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1              (*(volatile uint32_t *)(RCC_BASE + 0x58))
#define RCC_APB1ENR2              (*(volatile uint32_t *)(RCC_BASE + 0x5C))
#define RCC_APB2ENR               (*(volatile uint32_t *)(RCC_BASE + 0x60))

#define RCC_CR_PLLRDY             (1 << 25)
#define RCC_CR_PLLON              (1 << 24)
#define RCC_CR_HSIRDY             (1 << 10)
#define RCC_CR_HSION              (1 << 8)

#define RCC_CFGR_SW_HSI           0x1
#define RCC_CFGR_SW_PLL           0x3
#define RCC_CFGR_SW_MASK          0x3
#define RCC_CFGR_SWS_SHIFT        2

#define RCC_PLLCFGR_PLLREN        (1 << 24)
#define RCC_PLLCFGR_PLLR_SHIFT    25
#define RCC_PLLCFGR_PLLN_SHIFT    8
#define RCC_PLLCFGR_PLLM_SHIFT    4
#define RCC_PLLCFGR_PLLSRC_HSI16  0x2

#define RCC_CRRCR_HSI48ON         (1 << 0)
#define RCC_CRRCR_HSI48RDY        (1 << 1)

#define RCC_AHB2ENR_GPIOAEN       (1 << 0)
#define RCC_APB1ENR1_PWREN        (1 << 28)
#define RCC_APB1ENR2_LPUART1EN    (1 << 0)
#define RCC_APB2ENR_SYSCFGEN      (1 << 0)


/* -------- PWR (APB1 = 0x40007000) -------- RM0440 - 6.4 -------- */
#define PWR_BASE                  (0x40007000)
#define PWR_CR1                   (*(volatile uint32_t *)(PWR_BASE + 0x00))
#define PWR_CR5                   (*(volatile uint32_t *)(PWR_BASE + 0x80))
#define PWR_SR2                   (*(volatile uint32_t *)(PWR_BASE + 0x14))

#define PWR_CR1_VOS_SHIFT         9
#define PWR_CR1_VOS_MASK          (0x3 << PWR_CR1_VOS_SHIFT)
#define PWR_CR1_VOS_RANGE1        (0x1 << PWR_CR1_VOS_SHIFT)
#define PWR_CR5_R1MODE            (1 << 0)    /* 0 = Range 1 Boost */
#define PWR_SR2_VOSF              (1 << 10)


/* -------- FLASH (AHB1 + 0x2000 = 0x40022000) -------- RM0440 - 3.7 -------- */
#define FLASH_BASE                (0x40022000)
#define FLASH_ACR                 (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_KEY                 (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_SR                  (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_CR                  (*(volatile uint32_t *)(FLASH_BASE + 0x14))

#define FLASHMEM_ADDRESS_SPACE    (0x08000000)
#define FLASH_PAGE_SIZE           (0x800)    /* 2KB */

#define FLASH_ACR_LATENCY_MASK    (0x7)
#define FLASH_ACR_PRFTEN          (1 << 8)
#define FLASH_ACR_ICEN            (1 << 9)
#define FLASH_ACR_DCEN            (1 << 10)
#define FLASH_ACR_LATENCY_4WS     (0x4)

/* G4 has a single BSY at bit 16 (no BSY1/BSY2 like G0). */
#define FLASH_SR_BSY              (1 << 16)
#define FLASH_SR_OPTVERR          (1 << 15)
#define FLASH_SR_RDERR            (1 << 14)
#define FLASH_SR_FASTERR          (1 << 9)
#define FLASH_SR_MISERR           (1 << 8)
#define FLASH_SR_PGSERR           (1 << 7)
#define FLASH_SR_SIZERR           (1 << 6)
#define FLASH_SR_PGAERR           (1 << 5)
#define FLASH_SR_WRPERR           (1 << 4)
#define FLASH_SR_PROGERR          (1 << 3)
#define FLASH_SR_OPERR            (1 << 1)
#define FLASH_SR_EOP              (1 << 0)

/* G491 (Cat 3) is single-bank: PNB is 8 bits at [10:3], no BKER. */
#define FLASH_CR_LOCK             (1UL << 31)
#define FLASH_CR_STRT             (1 << 16)
#define FLASH_CR_PER              (1 << 1)
#define FLASH_CR_PG               (1 << 0)
#define FLASH_CR_PNB_SHIFT        3
#define FLASH_CR_PNB_MASK         0xFF

#define FLASH_KEY1                (0x45670123)
#define FLASH_KEY2                (0xCDEF89AB)


/* -------- GPIOA (AHB2 = 0x48000000) -------- RM0440 - 9.4 -------- */
#define GPIOA_BASE                (0x48000000)
#define GPIOA_MODER               (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPER              (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OSPEEDR             (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPDR               (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_ODR                 (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_BSRR                (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_AFRL                (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFRH                (*(volatile uint32_t *)(GPIOA_BASE + 0x24))


/* -------- LPUART1 (APB1 = 0x40008000) -------- RM0440 - 38.7 -------- */
#define LPUART1_BASE              (0x40008000)
#define LPUART1_CR1               (*(volatile uint32_t *)(LPUART1_BASE + 0x00))
#define LPUART1_BRR               (*(volatile uint32_t *)(LPUART1_BASE + 0x0C))
#define LPUART1_ISR               (*(volatile uint32_t *)(LPUART1_BASE + 0x1C))
#define LPUART1_TDR               (*(volatile uint32_t *)(LPUART1_BASE + 0x28))

#define USART_CR1_UE              (1 << 0)
#define USART_CR1_RE              (1 << 2)
#define USART_CR1_TE              (1 << 3)
#define USART_ISR_TEACK           (1 << 21)
#define USART_ISR_TXE             (1 << 7)


/* -------- SYSCLK / boot defaults -------- */
#define STM32G4_SYSCLK_HZ         (170000000UL)
#define STM32G4_UART_BAUD         (115200UL)
/* LPUART_BRR = (256 * f_LPUART) / baud. PCLK1 = SYSCLK on default prescalers. */
#define STM32G4_LPUART1_BRR(baud) \
    ((uint32_t)(((uint64_t)256u * STM32G4_SYSCLK_HZ) / (baud)))


/* -------- NUCLEO-G491RE board specifics -------- */
#define NUCLEO_G491_LED_LD2_PIN   (5)   /* PA5 */
#define NUCLEO_G491_UART_TX_PIN   (2)   /* PA2 - LPUART1_TX */
#define NUCLEO_G491_UART_RX_PIN   (3)   /* PA3 - LPUART1_RX */
#define NUCLEO_G491_UART_AF       (12)


/* Public helpers implemented in hal/stm32g4.c. Available to wolfBoot when
 * DEBUG_UART is set, and to the test-app at all times. */
void uart_init(void);
void uart_write(const char *buf, unsigned int len);

#endif /* _STM32G4_H_ */
