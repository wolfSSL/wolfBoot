/* stm32g4.c
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

#include <stdint.h>
#include <image.h>
#include "stm32g4.h"

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot STM32G4 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif


static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    /* FLASH_SR error/EOP bits are write-1-to-clear. Assign the mask
     * directly so a read-modify-write doesn't accidentally clear any
     * other W1C bits that happen to be set. */
    FLASH_SR = (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR |
                FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR |
                FLASH_SR_OPTVERR);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    flash_clear_errors();
    FLASH_CR |= FLASH_CR_PG;

    while (i < len) {
        flash_clear_errors();
        if ((len - i > 3) && ((((address + i) & 0x07) == 0) &&
                ((((uint32_t)data) + i) & 0x07) == 0)) {
            src = (uint32_t *)data;
            dst = (uint32_t *)address;
            flash_wait_complete();
            dst[i >> 2] = src[i >> 2];
            dst[(i >> 2) + 1] = src[(i >> 2) + 1];
            flash_wait_complete();
            i += 8;
        } else {
            uint32_t val[2];
            uint8_t *vbytes = (uint8_t *)(val);
            uint32_t base_addr = (address + i) & ~0x7u; /* DW we touch */
            int off = (address + i) & 0x7;              /* byte in DW */
            dst = (uint32_t *)(base_addr);
            val[0] = dst[0];
            val[1] = dst[1];
            while ((off < 8) && (i < len))
                vbytes[off++] = data[i++];
            flash_wait_complete();
            dst[0] = val[0];
            dst[1] = val[1];
            flash_wait_complete();
        }
    }
    /* W1C: assign directly; no RMW. Harmless if EOP isn't set. */
    FLASH_SR = FLASH_SR_EOP;
    FLASH_CR &= ~FLASH_CR_PG;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEY = FLASH_KEY1;
        DMB();
        FLASH_KEY = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;
    uint32_t page_number;
    uint32_t reg;

    if (len == 0)
        return -1;
    address -= FLASHMEM_ADDRESS_SPACE;
    end_address = address + len;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        flash_wait_complete();
        flash_clear_errors();
        page_number = (p >> 11) & FLASH_CR_PNB_MASK;
        reg = FLASH_CR & ~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT);
        FLASH_CR = reg | (page_number << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        flash_wait_complete();
        FLASH_CR &= ~FLASH_CR_PER;
    }
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Switch back to HSI16 as SYSCLK source before turning off the PLL. */
    reg32 = RCC_CFGR;
    reg32 &= ~RCC_CFGR_SW_MASK;
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();
    while (((RCC_CFGR >> RCC_CFGR_SWS_SHIFT) & RCC_CFGR_SW_MASK)
            != RCC_CFGR_SW_HSI)
        ;
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) != 0)
        ;
}

/* Bring SYSCLK up to 170 MHz from HSI16 + PLL with Range 1 Boost mode.
 * Sequence per RM0440 - 6.1.4: PWR boost must precede flash wait states
 * and PLL enable. */
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t pllm = 4;
    uint32_t plln = 85;
    uint32_t pllr = 2;
    uint32_t flash_waitstates = FLASH_ACR_LATENCY_4WS;

    (void)powersave;

    /* Enable PWR clock so VOS/R1MODE writes take effect. */
    RCC_APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC_APB1ENR1;

    /* Voltage scaling Range 1 (boost-capable). */
    reg32 = PWR_CR1;
    reg32 = (reg32 & ~PWR_CR1_VOS_MASK) | PWR_CR1_VOS_RANGE1;
    PWR_CR1 = reg32;
    while ((PWR_SR2 & PWR_SR2_VOSF) != 0)
        ;
    /* Clear R1MODE to enable Boost (required above 150 MHz). */
    PWR_CR5 &= ~PWR_CR5_R1MODE;

    /* HSI16 already on at reset; be explicit. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0)
        ;

    /* Set flash wait states + caches before raising CPU frequency. */
    FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_MASK)
              | flash_waitstates
              | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != flash_waitstates)
        ;

    /* PLL must be off before reprogramming PLLCFGR. */
    RCC_CR &= ~RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) != 0)
        ;

    /* PLL: SRC=HSI16, M=4 (raw 3), N=85, R=2 (raw 0), enable R output.
     * VCO = 16 MHz / 4 * 85 = 340 MHz. SYSCLK = 340 / 2 = 170 MHz. */
    reg32  = RCC_PLLCFGR_PLLSRC_HSI16;
    reg32 |= ((pllm - 1) << RCC_PLLCFGR_PLLM_SHIFT);
    reg32 |= (plln << RCC_PLLCFGR_PLLN_SHIFT);
    reg32 |= (((pllr >> 1) - 1) << RCC_PLLCFGR_PLLR_SHIFT);
    reg32 |= RCC_PLLCFGR_PLLREN;
    RCC_PLLCFGR = reg32;
    DMB();

    RCC_CR |= RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) == 0)
        ;

    /* Switch SYSCLK source to PLL R. */
    reg32 = RCC_CFGR;
    reg32 = (reg32 & ~RCC_CFGR_SW_MASK) | RCC_CFGR_SW_PLL;
    RCC_CFGR = reg32;
    DMB();
    while (((RCC_CFGR >> RCC_CFGR_SWS_SHIFT) & RCC_CFGR_SW_MASK)
            != RCC_CFGR_SW_PLL)
        ;

    /* SYSCFG clock for any post-init configuration. */
    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
}


/* Optional debug UART on NUCLEO-G491RE ST-LINK VCP: LPUART1 PA2/PA3 AF12.
 * Available when: wolfBoot with DEBUG_UART, or test-app (always). */
#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

void uart_init(void)
{
    uint32_t reg;
    const uint32_t tx_pin = NUCLEO_G491_UART_TX_PIN;
    const uint32_t rx_pin = NUCLEO_G491_UART_RX_PIN;

    RCC_AHB2ENR  |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC_AHB2ENR;
    RCC_APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    (void)RCC_APB1ENR2;

    /* PA2, PA3 -> alternate function (MODER = 10b). */
    reg = GPIOA_MODER & ~((0x3u << (tx_pin * 2)) | (0x3u << (rx_pin * 2)));
    GPIOA_MODER = reg | ((0x2u << (tx_pin * 2)) | (0x2u << (rx_pin * 2)));

    /* High speed (11b) so 115200 edges are clean even at 170 MHz. */
    GPIOA_OSPEEDR |= (0x3u << (tx_pin * 2)) | (0x3u << (rx_pin * 2));

    /* AF12 (LPUART1) on PA2 and PA3 - AFRL bits [11:8] and [15:12]. */
    reg = GPIOA_AFRL & ~((0xFu << (tx_pin * 4)) | (0xFu << (rx_pin * 4)));
    GPIOA_AFRL = reg | ((NUCLEO_G491_UART_AF << (tx_pin * 4))
                      | (NUCLEO_G491_UART_AF << (rx_pin * 4)));

    LPUART1_CR1 = 0;
    LPUART1_BRR = STM32G4_LPUART1_BRR(STM32G4_UART_BAUD);
    LPUART1_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    while ((LPUART1_ISR & USART_ISR_TEACK) == 0)
        ;
}

void uart_write(const char *buf, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        while ((LPUART1_ISR & USART_ISR_TXE) == 0)
            ;
        LPUART1_TDR = (uint32_t)(uint8_t)buf[i];
    }
}

#endif /* DEBUG_UART || !__WOLFBOOT */


void hal_init(void)
{
    clock_pll_on(0);
#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    uart_init();
#endif
}

void RAMFUNCTION hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_flash_release();
#endif
#ifdef WOLFBOOT_RESTORE_CLOCK
    clock_pll_off();
#endif
}
