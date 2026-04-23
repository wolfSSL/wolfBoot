/* stm32u3.h
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

/* STM32U3 family (e.g. STM32U385RG on NUCLEO-U385RG-Q).
 * Cortex-M33, no TrustZone.  1 MB dual-bank flash (2 × 512 KB, 8 KB pages).
 * Reference: RM0487, stm32u385xx.h from STM32Cube_FW_U3_V1.3.0.
 *
 * Peripheral base addresses differ from STM32U5:
 *   AHB1 peripherals (FLASH, ICACHE, PWR, RCC) at 0x40020000 + offset
 *   AHB2 peripherals (GPIO) at 0x42020000 + offset
 *   APB2 peripherals (USART1) at 0x40010000 + offset
 * There is no traditional PLL — MSIS RC0 runs natively at 96 MHz.
 */

#ifndef _STM32U3_H_
#define _STM32U3_H_

#include <stdint.h>

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* -------- RCC (AHB1 + 0x10C00 = 0x40030C00) -------- */
#define RCC_BASE                    (0x40030C00)

#define RCC_CR                      (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CR_MSISON               (1 << 0)
#define RCC_CR_MSISRDY              (1 << 2)
#define RCC_CR_MSIPLL1EN            (1 << 5)
#define RCC_CR_MSIPLL0EN            (1 << 6)
#define RCC_CR_MSIPLL1RDY           (1 << 9)
#define RCC_CR_MSIPLL0RDY           (1 << 10)
#define RCC_CR_HSION                (1 << 11)
#define RCC_CR_HSIRDY               (1 << 13)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEBYP               (1 << 18)
#define RCC_CR_CSSON                (1 << 19)

#define RCC_ICSCR1                  (*(volatile uint32_t *)(RCC_BASE + 0x08))
/* MSIS source selection: bit 23 MSIRGSEL, bits [31:29] MSISDIV */
#define RCC_ICSCR1_MSIRGSEL         (1 << 23)     /* 1 = use ICSCR1 range */
#define RCC_ICSCR1_MSISDIV_SHIFT    (29)
#define RCC_ICSCR1_MSISDIV_MASK     (0x3u << 29)
#define RCC_ICSCR1_MSISDIV_1        (0)            /* /1 */
#define RCC_ICSCR1_MSISDIV_2        (1)            /* /2 */
#define RCC_ICSCR1_MSISDIV_4        (2)            /* /4 */
#define RCC_ICSCR1_MSISDIV_8        (3)            /* /8 */
/* MSIS RC source: bit 31 MSISSEL — 0 = MSIRC1 (24 MHz), 1 = MSIRC0 (96 MHz) */
#define RCC_ICSCR1_MSISSEL          (1u << 31)

#define RCC_CFGR1                   (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#define RCC_CFGR_SW_MSIS            0x0
#define RCC_CFGR_SW_HSI16           0x1
#define RCC_CFGR_SW_HSE             0x2

#define RCC_CFGR2                   (*(volatile uint32_t *)(RCC_BASE + 0x20))
/* CFGR4: EPOD booster config (offset 0x28) */
#define RCC_CFGR4                   (*(volatile uint32_t *)(RCC_BASE + 0x28))
#define RCC_CFGR4_BOOSTSEL_SHIFT    (0)
#define RCC_CFGR4_BOOSTSEL_MASK     (0x3u << 0)
#define RCC_CFGR4_BOOSTSEL_MSIS     (0x1u << 0)    /* MSIS as booster source */
#define RCC_CFGR4_BOOSTDIV_SHIFT    (12)
#define RCC_CFGR4_BOOSTDIV_MASK     (0xFu << 12)

#define RCC_CIER                    (*(volatile uint32_t *)(RCC_BASE + 0x50))

#define RCC_AHB1ENR1                (*(volatile uint32_t *)(RCC_BASE + 0x88))
#define RCC_AHB2ENR1                (*(volatile uint32_t *)(RCC_BASE + 0x8C))
#define RCC_AHB2ENR1_GPIOAEN        (1 << 0)
#define RCC_AHB2ENR1_GPIOBEN        (1 << 1)
#define RCC_AHB2ENR1_GPIOCEN        (1 << 2)
#define RCC_AHB2ENR1_GPIODEN        (1 << 3)

/* AHB1ENR2 at offset 0x094: PWR clock enable is bit 2 */
#define RCC_AHB1ENR2                (*(volatile uint32_t *)(RCC_BASE + 0x94))
#define RCC_AHB1ENR2_PWREN          (1 << 2)

#define RCC_APB1ENR1                (*(volatile uint32_t *)(RCC_BASE + 0x9C))
#define RCC_APB2ENR                 (*(volatile uint32_t *)(RCC_BASE + 0xA4))
#define RCC_APB2ENR_USART1EN        (1 << 14)

/* -------- PWR (AHB1 + 0x10800 = 0x40030800) -------- */
#define PWR_BASE                    (0x40030800)

#define PWR_CR3                     (*(volatile uint32_t *)(PWR_BASE + 0x08))
#define PWR_CR3_REGSEL              (1 << 1)       /* 0=LDO, 1=SMPS */

#define PWR_SVMSR                   (*(volatile uint32_t *)(PWR_BASE + 0x3C))
#define PWR_SVMSR_REGS              (1 << 1)       /* regulator status: 0=LDO, 1=SMPS */

#define PWR_VOSR                    (*(volatile uint32_t *)(PWR_BASE + 0x0C))
#define PWR_VOSR_R1EN               (1 << 0)       /* VOS range 1 enable */
#define PWR_VOSR_R2EN               (1 << 1)       /* VOS range 2 enable */
#define PWR_VOSR_BOOSTEN            (1 << 8)       /* EPOD booster enable */
#define PWR_VOSR_R1RDY              (1 << 16)      /* range 1 ready */
#define PWR_VOSR_R2RDY              (1 << 17)      /* range 2 ready */
#define PWR_VOSR_BOOSTRDY           (1 << 24)      /* EPOD booster ready */

/* -------- FLASH (AHB1 + 0x2000 = 0x40022000) -------- */
#define FLASH_BASE                  (0x40022000)
#define FLASH_ACR                   (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_LATENCY_MASK      (0x0F)
#define FLASH_ACR_PRFTEN            (1 << 8)
#define FLASH_ACR_LPM               (1 << 11)

/* Non-secure flash registers (fail with PGSERR on programmed pages) */
#define FLASH_NS_KEYR               (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_NS_OPTKEYR            (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_NS_SR                 (*(volatile uint32_t *)(FLASH_BASE + 0x20))
#define FLASH_NS_CR                 (*(volatile uint32_t *)(FLASH_BASE + 0x28))

/* Secure flash registers — required for erase/write even with TZEN=0 */
#define FLASH_SKEYR                 (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_SSR                   (*(volatile uint32_t *)(FLASH_BASE + 0x24))
#define FLASH_SCR                   (*(volatile uint32_t *)(FLASH_BASE + 0x2C))

#define FLASH_SR_EOP                (1 << 0)
#define FLASH_SR_OPERR              (1 << 1)
#define FLASH_SR_PROGERR            (1 << 3)
#define FLASH_SR_WRPERR             (1 << 4)
#define FLASH_SR_PGAERR             (1 << 5)
#define FLASH_SR_SIZERR             (1 << 6)
#define FLASH_SR_PGSERR             (1 << 7)
#define FLASH_SR_OPTWERR            (1 << 13)
#define FLASH_SR_BSY                (1 << 16)
#define FLASH_SR_WDW                (1 << 17)

#define FLASH_CR_PG                 (1 << 0)
#define FLASH_CR_PER                (1 << 1)
#define FLASH_CR_MER1               (1 << 2)
#define FLASH_CR_PNB_SHIFT          3
#define FLASH_CR_PNB_MASK           0x7F   /* 7 bits: 64 pages per bank */
#define FLASH_CR_BKER               (1 << 11)
#define FLASH_CR_STRT               (1 << 16)
#define FLASH_CR_OPTSTRT            (1 << 17)
#define FLASH_CR_EOPIE              (1 << 24)
#define FLASH_CR_ERRIE              (1 << 25)
#define FLASH_CR_OBL_LAUNCH         (1 << 27)
#define FLASH_CR_OPTLOCK            (1 << 30)
#define FLASH_CR_LOCK               (1 << 31)

#define FLASH_OPTR                  (*(volatile uint32_t *)(FLASH_BASE + 0x40))

#define FLASHMEM_ADDRESS_SPACE      (0x08000000)
#define FLASH_PAGE_SIZE             (0x1000)        /* 4 KB */
#define FLASH_BANK2_BASE            (0x08080000)    /* always dual-bank on U3 */
#define FLASH_TOP                   (0x080FFFFF)

#define FLASH_KEY1                  (0x45670123)
#define FLASH_KEY2                  (0xCDEF89AB)
#define FLASH_OPTKEY1               (0x08192A3BU)
#define FLASH_OPTKEY2               (0x4C5D6E7FU)

/* -------- GPIO (AHB2, 0x42020000) -------- */
#define GPIOA_BASE                  (0x42020000)
#define GPIOB_BASE                  (0x42020400)
#define GPIOC_BASE                  (0x42020800)
#define GPIOD_BASE                  (0x42020C00)

/* -------- Reset -------- */
#define AIRCR                       (*(volatile uint32_t *)(0xE000ED0C))
#define AIRCR_VKEY                  (0x05FA << 16)
#define AIRCR_SYSRESETREQ           (1 << 2)

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

void hal_cache_invalidate(void);
void hal_cache_enable(int way);
void hal_cache_disable(void);

#endif /* _STM32U3_H_ */
