/* stm32u3.c
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

/* STM32U3 family (e.g. NUCLEO-U385RG-Q). Cortex-M33 without TrustZone.
 * Always dual-bank 1 MB flash (2 x 512 KB), 4 KB pages, 64-bit
 * (double-word) write quantum.
 * No traditional PLL -- MSIS switches directly between MSIRC1 (24 MHz)
 * and MSIRC0 (96 MHz).
 */

#include <stdint.h>
#include <image.h>
#include <string.h>
#include "hal/stm32u3.h"
#include "hal.h"
#include "printf.h"


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates) {
        FLASH_ACR = (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates;
        /* RM: read-back to confirm LATENCY accepted before clock switch */
        while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != waitstates)
            ;
    }
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_NS_SR & (FLASH_SR_BSY | FLASH_SR_WDW)) != 0)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    uint32_t sr = FLASH_NS_SR;
    if (sr & (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
              FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR |
              FLASH_SR_OPTWERR)) {
        /* Write 1 to clear (rc_w1) */
        FLASH_NS_SR = sr;
    }
}

/* RM0487 Section 7.3.7: Flash memory programming sequence (double-word) */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *dst;

    dst = (uint32_t *)address;

    while (i < len) {
        uint32_t dword[2];
        int remain = len - i;

        dword[0] = 0xFFFFFFFF;
        dword[1] = 0xFFFFFFFF;
        memcpy((uint8_t *)dword, data + i, remain < 8 ? remain : 8);

        /* RM steps 2-3: check BSY+WDW clear, clear all error flags */
        flash_wait_complete();
        flash_clear_errors();

        /* RM step 5: set PG */
        FLASH_NS_CR |= FLASH_CR_PG;

        /* RM step 6: write first word, then second word */
        dst[i >> 2]       = dword[0];
        ISB();
        dst[(i >> 2) + 1] = dword[1];
        ISB();

        /* RM step 8: wait for BSY clear */
        flash_wait_complete();

        /* RM step 9: clear EOP if set */
        if ((FLASH_NS_SR & FLASH_SR_EOP) != 0)
            FLASH_NS_SR |= FLASH_SR_EOP;

        /* RM step 10: clear PG */
        FLASH_NS_CR &= ~FLASH_CR_PG;
        i += 8;
    }
    hal_cache_invalidate();
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    /* Unlock NS flash controller (TZEN=0, secure unlock not needed) */
    if ((FLASH_NS_CR & FLASH_CR_LOCK) != 0) {
        FLASH_NS_KEYR = FLASH_KEY1;
        DMB();
        FLASH_NS_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_NS_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_NS_CR & FLASH_CR_LOCK) == 0)
        FLASH_NS_CR |= FLASH_CR_LOCK;
}

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_NS_CR & FLASH_CR_OPTLOCK) != 0) {
        FLASH_NS_OPTKEYR = FLASH_OPTKEY1;
        DMB();
        FLASH_NS_OPTKEYR = FLASH_OPTKEY2;
        DMB();
        while ((FLASH_NS_CR & FLASH_CR_OPTLOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_opt_lock(void)
{
    FLASH_NS_CR |= FLASH_CR_OPTSTRT;
    flash_wait_complete();
    FLASH_NS_CR |= FLASH_CR_OBL_LAUNCH;
    if ((FLASH_NS_CR & FLASH_CR_OPTLOCK) == 0)
        FLASH_NS_CR |= FLASH_CR_OPTLOCK;
}

/* Erase — matches STM32U5 hal pattern exactly (same Cortex-M33 flash controller) */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    flash_clear_errors();
    if (len == 0)
        return -1;
    if (address < ARCH_FLASH_OFFSET)
        return -1;

    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t bker = 0;
        uint32_t base;

        if (p > FLASH_TOP) {
            FLASH_NS_CR &= ~FLASH_CR_PER;
            return 0;
        }

        if (p >= FLASH_BANK2_BASE) {
            bker = FLASH_CR_BKER;
            base = FLASH_BANK2_BASE;
        } else {
            base = FLASHMEM_ADDRESS_SPACE;
        }

        /* Single MODIFY_REG: clear PNB+BKER, set page+PER+bker */
        reg = FLASH_NS_CR & ~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) |
                                FLASH_CR_BKER);
        reg |= (((p - base) >> 12) << FLASH_CR_PNB_SHIFT) |
               FLASH_CR_PER | bker;
        FLASH_NS_CR = reg;
        DMB();
        FLASH_NS_CR |= FLASH_CR_STRT;
        flash_wait_complete();
    }
    FLASH_NS_CR &= ~FLASH_CR_PER;
    hal_cache_invalidate();
    return 0;
}

/* --- UART: USART1 on PA9 (TX) / PA10 (RX), AF7 --- */

#define USART1_BASE         (0x40013800U)
#define USART1_CR1          (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_BRR          (*(volatile uint32_t *)(USART1_BASE + 0x0C))
#define USART1_ISR          (*(volatile uint32_t *)(USART1_BASE + 0x1C))
#define USART1_TDR          (*(volatile uint32_t *)(USART1_BASE + 0x28))

#define UART_CR1_UE         (1 << 0)
#define UART_CR1_RE         (1 << 2)
#define UART_CR1_TE         (1 << 3)
#define UART_ISR_TXE        (1 << 7)

#define UART_TX_PIN         (9)
#define UART_RX_PIN         (10)
#define UART_PIN_AF         (7)

#define GPIOA_MODER         (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR         (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_AFRH          (*(volatile uint32_t *)(GPIOA_BASE + 0x24))

#define USART1_PCLK         (96000000U)

/* Available when: wolfBoot with DEBUG_UART, or test-app (always) */
#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

static void uart1_pins_setup(void)
{
    uint32_t reg;

    RCC_AHB2ENR1 |= RCC_AHB2ENR1_GPIOAEN;
    reg = RCC_AHB2ENR1;
    (void)reg;

    reg = GPIOA_MODER & ~(0x3u << (UART_TX_PIN * 2));
    GPIOA_MODER = reg | (0x2u << (UART_TX_PIN * 2));
    reg = GPIOA_MODER & ~(0x3u << (UART_RX_PIN * 2));
    GPIOA_MODER = reg | (0x2u << (UART_RX_PIN * 2));

    reg = GPIOA_AFRH & ~(0xFu << ((UART_TX_PIN - 8) * 4));
    GPIOA_AFRH = reg | (UART_PIN_AF << ((UART_TX_PIN - 8) * 4));
    reg = GPIOA_AFRH & ~(0xFu << ((UART_RX_PIN - 8) * 4));
    GPIOA_AFRH = reg | (UART_PIN_AF << ((UART_RX_PIN - 8) * 4));

    GPIOA_PUPDR &= ~(0x3u << (UART_TX_PIN * 2));
    GPIOA_PUPDR &= ~(0x3u << (UART_RX_PIN * 2));
}

void uart_init(void)
{
    uint32_t reg;

    uart1_pins_setup();

    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    reg = RCC_APB2ENR;
    (void)reg;

    USART1_CR1 &= ~UART_CR1_UE;
    USART1_BRR = USART1_PCLK / 115200;
    USART1_CR1 |= UART_CR1_TE | UART_CR1_RE | UART_CR1_UE;
}

void uart_write(const char *buf, unsigned int sz)
{
    while (sz-- > 0) {
        while ((USART1_ISR & UART_ISR_TXE) == 0)
            ;
        USART1_TDR = *buf++;
    }
}

#endif /* DEBUG_UART || !__WOLFBOOT */

/* Clock: MSIS MSIRC0 (96 MHz) with SMPS + EPOD booster + VOS range 1. */
static void clock_96mhz(void)
{
    uint32_t reg;

    RCC_AHB1ENR2 |= RCC_AHB1ENR2_PWREN;
    reg = RCC_AHB1ENR2;
    (void)reg;

    if ((PWR_SVMSR & PWR_SVMSR_REGS) == 0) {
        PWR_CR3 |= PWR_CR3_REGSEL;
        while ((PWR_SVMSR & PWR_SVMSR_REGS) == 0)
            ;
    }

    reg = RCC_CFGR4;
    reg &= ~(RCC_CFGR4_BOOSTSEL_MASK | RCC_CFGR4_BOOSTDIV_MASK);
    reg |= RCC_CFGR4_BOOSTSEL_MSIS;
    RCC_CFGR4 = reg;

    if ((PWR_VOSR & PWR_VOSR_BOOSTRDY) == 0) {
        PWR_VOSR |= PWR_VOSR_BOOSTEN;
        while ((PWR_VOSR & PWR_VOSR_BOOSTRDY) == 0)
            ;
    }

    if ((PWR_VOSR & PWR_VOSR_R1RDY) == 0) {
        reg = PWR_VOSR;
        reg &= ~(PWR_VOSR_R1EN | PWR_VOSR_R2EN);
        reg |= PWR_VOSR_R1EN;
        PWR_VOSR = reg;
        while ((PWR_VOSR & PWR_VOSR_R1RDY) == 0)
            ;
    }

    flash_set_waitstates(2);
    FLASH_ACR |= FLASH_ACR_PRFTEN;

    /* MSISSEL: 0=MSIRC0(96MHz), 1=MSIRC1(24MHz). Set MSIRGSEL to use ICSCR1. */
    reg = RCC_ICSCR1;
    reg &= ~(RCC_ICSCR1_MSISSEL | RCC_ICSCR1_MSISDIV_MASK);
    reg |= RCC_ICSCR1_MSIRGSEL;
    RCC_ICSCR1 = reg;
    DMB();

    while ((RCC_CR & RCC_CR_MSISRDY) == 0)
        ;

    RCC_CFGR2 = 0;
}

void hal_init(void)
{
    clock_96mhz();
    hal_cache_enable(1);

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    uart_init();
    uart_write("wolfBoot HAL Init\n", sizeof("wolfBoot HAL Init\n") - 1);
#endif
}

void hal_prepare_boot(void)
{
}

void RAMFUNCTION hal_cache_enable(int way)
{
    ICACHE_CR |= (way ? ICACHE_CR_2WAYS : ICACHE_CR_1WAY);
    ICACHE_CR |= ICACHE_CR_CEN;
}

void RAMFUNCTION hal_cache_disable(void)
{
    ICACHE_CR &= ~ICACHE_CR_CEN;
}

void RAMFUNCTION hal_cache_invalidate(void)
{
    if ((ICACHE_CR & ICACHE_CR_CEN) == 0)
        return;
    if ((ICACHE_SR & ICACHE_SR_BUSYF) == 0)
        ICACHE_CR |= ICACHE_CR_CACHEINV;
    /* Wait unconditionally for invalidation to complete */
    while ((ICACHE_SR & ICACHE_SR_BSYENDF) == 0)
        ;
    ICACHE_SR |= ICACHE_SR_BSYENDF;
}
