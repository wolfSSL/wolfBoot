/* stm32c5.h
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

/* STM32C5 family (e.g. STM32C5A3ZGT6 on NUCLEO-C5A3ZG).
 * Cortex-M33, no TrustZone in this configuration.
 * 1 MB dual-bank flash on the -ZG variant (2 x 512 KB, 8 KB pages).
 * Reference: stm32c5a3xx.h from STM32CubeC5 v2.0.0.
 *
 * Bus base addresses (from PERIPH_BASE 0x40000000):
 *   APB2 at 0x40010000 (USART1)
 *   AHB1 at 0x40020000 (FLASH controller, ICACHE)
 *   AHB2 at 0x42020000 (GPIO)
 *   AHB3 at 0x44020000 (PWR, RCC)
 */

#ifndef _STM32C5_H_
#define _STM32C5_H_

#include <stdint.h>

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* -------- RCC (AHB3 + 0x0C00 = 0x44020C00) -------- */
#define RCC_BASE                    (0x44020C00)

#define RCC_CR1                     (*(volatile uint32_t *)(RCC_BASE + 0x000))
#define RCC_CR1_HSISON              (1 << 0)
#define RCC_CR1_HSIDIV3ON           (1 << 1)
#define RCC_CR1_HSISRDY             (1 << 4)
#define RCC_CR1_HSIDIV3RDY          (1 << 5)
#define RCC_CR1_PSISON              (1 << 8)
#define RCC_CR1_PSISRDY             (1 << 12)
#define RCC_CR1_HSEON               (1 << 16)
#define RCC_CR1_HSERDY              (1 << 17)
#define RCC_CR1_HSEBYP              (1 << 18)

/* PSI configuration register.  PSIREFSRC[17:16], PSIREF[22:20] and
 * PSIFREQ[29:28] together pick the PSI source/reference/output.
 */
#define RCC_CR2                     (*(volatile uint32_t *)(RCC_BASE + 0x004))
#define RCC_CR2_PSIREFSRC_HSE       (0u << 16)
#define RCC_CR2_PSIREF_48MHZ        ((1u << 22) | (1u << 21))
#define RCC_CR2_PSIFREQ_144MHZ      (1u << 28)
#define RCC_CR2_PSI_FIELDS_MASK     ((3u << 16) | (7u << 20) | (3u << 28))

#define RCC_CFGR1                   (*(volatile uint32_t *)(RCC_BASE + 0x01C))
#define RCC_CFGR1_SW_MASK           (0x3u << 0)
#define RCC_CFGR1_SW_HSIDIV3        (0x0u << 0)
#define RCC_CFGR1_SW_PSIS           (0x3u << 0)
#define RCC_CFGR1_SWS_MASK          (0x3u << 3)
#define RCC_CFGR1_SWS_HSIDIV3       (0x0u << 3)
#define RCC_CFGR1_SWS_PSIS          (0x3u << 3)

#define RCC_CFGR2                   (*(volatile uint32_t *)(RCC_BASE + 0x020))

#define RCC_AHB1ENR                 (*(volatile uint32_t *)(RCC_BASE + 0x088))
#define RCC_AHB1ENR_FLASHEN         (1 << 8)

#define RCC_AHB2ENR                 (*(volatile uint32_t *)(RCC_BASE + 0x08C))
#define RCC_AHB2ENR_GPIOAEN         (1 << 0)
#define RCC_AHB2ENR_GPIOBEN         (1 << 1)
#define RCC_AHB2ENR_GPIOCEN         (1 << 2)
#define RCC_AHB2ENR_GPIODEN         (1 << 3)
#define RCC_AHB2ENR_GPIOGEN         (1 << 6)

#define RCC_AHB4ENR                 (*(volatile uint32_t *)(RCC_BASE + 0x094))

#define RCC_APB1LENR                (*(volatile uint32_t *)(RCC_BASE + 0x09C))
#define RCC_APB1LENR_USART2EN       (1 << 17)
#define RCC_APB2ENR                 (*(volatile uint32_t *)(RCC_BASE + 0x0A4))
#define RCC_APB2ENR_USART1EN        (1 << 14)
#define RCC_APB3ENR                 (*(volatile uint32_t *)(RCC_BASE + 0x0A8))

/* -------- FLASH controller (AHB1 + 0x2000 = 0x40022000) -------- */
#define FLASH_BASE                  (0x40022000)

#define FLASH_ACR                   (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_LATENCY_MASK      (0x0F)
#define FLASH_ACR_LATENCY_4WS       (0x4u)
#define FLASH_ACR_WRHIGHFREQ_MASK   (0x3u << 4)
#define FLASH_ACR_WRHIGHFREQ_DELAY2 (0x2u << 4)
#define FLASH_ACR_PRFTEN            (1 << 8)

#define FLASH_KEYR                  (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_OPTKEYR               (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_OPSR                  (*(volatile uint32_t *)(FLASH_BASE + 0x18))
#define FLASH_OPTCR                 (*(volatile uint32_t *)(FLASH_BASE + 0x1C))
#define FLASH_SR                    (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_CR                    (*(volatile uint32_t *)(FLASH_BASE + 0x28))
#define FLASH_CCR                   (*(volatile uint32_t *)(FLASH_BASE + 0x30))
#define FLASH_OPTSR_CUR             (*(volatile uint32_t *)(FLASH_BASE + 0x50))
#define FLASH_OPTSR_PRG             (*(volatile uint32_t *)(FLASH_BASE + 0x54))

/* OPTSR (current and program) bit defs - bit 31 mirrors OPTCR.SWAP_BANK. */
#define FLASH_OPTSR_SWAP_BANK       (1u << 31)

/* FLASH_SR bits */
#define FLASH_SR_BSY                (1 << 0)
#define FLASH_SR_WBNE               (1 << 1)
#define FLASH_SR_DBNE               (1 << 3)
#define FLASH_SR_EOP                (1 << 16)
#define FLASH_SR_WRPERR             (1 << 17)
#define FLASH_SR_PGSERR             (1 << 18)
#define FLASH_SR_STRBERR            (1 << 19)
#define FLASH_SR_INCERR             (1 << 20)
#define FLASH_SR_OPTCHANGEERR       (1 << 23)

/* FLASH_CR bits */
#define FLASH_CR_LOCK               (1 << 0)
#define FLASH_CR_PG                 (1 << 1)
#define FLASH_CR_PER                (1 << 2)
#define FLASH_CR_BER                (1 << 3)
#define FLASH_CR_FW                 (1 << 4)
#define FLASH_CR_STRT               (1 << 5)
#define FLASH_CR_PNB_SHIFT          (6)
#define FLASH_CR_PNB_MASK           (0x3F)
#define FLASH_CR_MER                (1 << 15)
#define FLASH_CR_BKSEL              (1u << 31)

/* FLASH_OPTCR bits */
#define FLASH_OPTCR_OPTLOCK         (1 << 0)
#define FLASH_OPTCR_OPTSTRT         (1 << 1)
#define FLASH_OPTCR_SWAP_BANK       (1u << 31)

/* FLASH_CCR bits (write 1 to clear corresponding SR flag) */
#define FLASH_CCR_CLR_EOP           (1 << 16)
#define FLASH_CCR_CLR_WRPERR        (1 << 17)
#define FLASH_CCR_CLR_PGSERR        (1 << 18)
#define FLASH_CCR_CLR_STRBERR       (1 << 19)
#define FLASH_CCR_CLR_INCERR        (1 << 20)
#define FLASH_CCR_CLR_OPTCHANGEERR  (1 << 23)

#define FLASHMEM_ADDRESS_SPACE      (0x08000000)
#define FLASH_PAGE_SIZE             (0x2000)        /* 8 KB */
#define FLASH_BANK2_BASE            (0x08080000)    /* 512 KB bank size, 1 MB total */
#define FLASH_TOP                   (0x080FFFFF)    /* end of 1 MB */

/* Bootloader region length, derived from the partition layout.  Used by
 * fork_bootloader() to mirror wolfBoot into bank 2 for DUALBANK_SWAP.
 */
#define BOOTLOADER_SIZE             (WOLFBOOT_PARTITION_BOOT_ADDRESS - \
                                     FLASHMEM_ADDRESS_SPACE)

#define FLASH_KEY1                  (0x45670123)
#define FLASH_KEY2                  (0xCDEF89AB)
#define FLASH_OPTKEY1               (0x08192A3BU)
#define FLASH_OPTKEY2               (0x4C5D6E7FU)

/* -------- GPIO (AHB2 + 0x0000 = 0x42020000) -------- */
#define GPIOA_BASE                  (0x42020000)
#define GPIOB_BASE                  (0x42020400)
#define GPIOC_BASE                  (0x42020800)
#define GPIOD_BASE                  (0x42020C00)
#define GPIOG_BASE                  (0x42021800)

#define GPIOA_MODER                 (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR                 (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR                  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_AFRL                  (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFRH                  (*(volatile uint32_t *)(GPIOA_BASE + 0x24))

#define GPIOG_MODER                 (*(volatile uint32_t *)(GPIOG_BASE + 0x00))
#define GPIOG_PUPDR                 (*(volatile uint32_t *)(GPIOG_BASE + 0x0C))
#define GPIOG_BSRR                  (*(volatile uint32_t *)(GPIOG_BASE + 0x18))

/* -------- USART2 (APB1 + 0x4400 = 0x40004400) -------- */
#define USART2_BASE                 (0x40004400U)
#define USART2_CR1                  (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_BRR                  (*(volatile uint32_t *)(USART2_BASE + 0x0C))
#define USART2_ISR                  (*(volatile uint32_t *)(USART2_BASE + 0x1C))
#define USART2_TDR                  (*(volatile uint32_t *)(USART2_BASE + 0x28))

#define UART_CR1_UE                 (1 << 0)
#define UART_CR1_RE                 (1 << 2)
#define UART_CR1_TE                 (1 << 3)
#define UART_ISR_TC                 (1 << 6)
#define UART_ISR_TXE                (1 << 7)

/* -------- ICACHE (AHB1 + 0x10400 = 0x40030400) -------- */
#define ICACHE_BASE                 (0x40030400)
#define ICACHE_CR                   (*(volatile uint32_t *)(ICACHE_BASE + 0x00))
#define ICACHE_CR_WAYSEL            (1 << 2)
#define ICACHE_CR_1WAY              0U
#define ICACHE_CR_2WAYS             ICACHE_CR_WAYSEL
#define ICACHE_CR_CACHEINV          (1 << 1)
#define ICACHE_CR_CEN               (1 << 0)

#define ICACHE_SR                   (*(volatile uint32_t *)(ICACHE_BASE + 0x04))
#define ICACHE_SR_BUSYF             (1 << 0)
#define ICACHE_SR_BSYENDF           (1 << 1)
#define ICACHE_SR_ERRF              (1 << 2)

/* -------- Reset -------- */
#define AIRCR                       (*(volatile uint32_t *)(0xE000ED0C))
#define AIRCR_VKEY                  (0x05FA << 16)
#define AIRCR_SYSRESETREQ           (1 << 2)

void hal_cache_invalidate(void);
void hal_cache_enable(int way);
void hal_cache_disable(void);

#endif /* _STM32C5_H_ */
