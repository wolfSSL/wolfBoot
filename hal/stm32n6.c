/* stm32n6.c
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

#include <stdint.h>
#include <stddef.h>
#include <image.h>
#include "hal.h"
#include "hal/stm32n6.h"

/* RAMFUNCTION override for test-app (XIP needs flash ops in SRAM) */
#if defined(RAM_CODE) && !defined(__WOLFBOOT)
    #undef RAMFUNCTION
    #define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#endif

/* XSPI2 indirect-mode command helper */
static int RAMFUNCTION xspi_cmd(uint8_t fmode, uint8_t cmd,
    uint32_t addr, uint32_t addrMode,
    uint8_t *data, uint32_t dataSz, uint32_t dataMode,
    uint32_t dummyCycles)
{
    uint32_t ccr;

    /* Abort memory-mapped mode if active */
    if ((XSPI2_CR & XSPI_CR_FMODE_MASK) == XSPI_CR_FMODE_MMAP) {
        XSPI2_CR |= XSPI_CR_ABORT;
        while (XSPI2_CR & XSPI_CR_ABORT)
            ;
    }
    while (XSPI2_SR & XSPI_SR_BUSY)
        ;
    XSPI2_FCR = XSPI_FCR_CTCF | XSPI_FCR_CTEF | XSPI_FCR_CSMF;

    XSPI2_CR = (XSPI2_CR & ~XSPI_CR_FMODE_MASK) | XSPI_CR_FMODE(fmode);

    if (dataSz > 0) {
        XSPI2_DLR = dataSz - 1;
    }

    ccr = XSPI_CCR_IMODE(XSPI_MODE_SINGLE) | XSPI_CCR_ISIZE(0);
    if (addrMode != XSPI_MODE_NONE) {
        ccr |= XSPI_CCR_ADMODE(addrMode) | XSPI_CCR_ADSIZE(3);
    }
    if (dataMode != XSPI_MODE_NONE) {
        ccr |= XSPI_CCR_DMODE(dataMode);
    }
    XSPI2_CCR = ccr;
    XSPI2_TCR = XSPI_TCR_DCYC(dummyCycles);
    XSPI2_IR = cmd;

    if (addrMode != XSPI_MODE_NONE) {
        XSPI2_AR = addr;
    }

    if (dataSz > 0 && data != NULL) {
        while (dataSz >= 4) {
            if (fmode == 0) {
                while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TEF)))
                    ;
                if (XSPI2_SR & XSPI_SR_TEF) goto xspi_err;
                XSPI2_DR32 = *(uint32_t *)data;
            } else {
                while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TCF |
                                     XSPI_SR_TEF)))
                    ;
                if (XSPI2_SR & XSPI_SR_TEF) goto xspi_err;
                *(uint32_t *)data = XSPI2_DR32;
            }
            data += 4;
            dataSz -= 4;
        }
        while (dataSz > 0) {
            if (fmode == 0) {
                while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TEF)))
                    ;
                if (XSPI2_SR & XSPI_SR_TEF) goto xspi_err;
                XSPI2_DR = *data;
            } else {
                while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TCF |
                                     XSPI_SR_TEF)))
                    ;
                if (XSPI2_SR & XSPI_SR_TEF) goto xspi_err;
                *data = XSPI2_DR;
            }
            data++;
            dataSz--;
        }
    }

    while (!(XSPI2_SR & (XSPI_SR_TCF | XSPI_SR_TEF)))
        ;
    if (XSPI2_SR & XSPI_SR_TEF) goto xspi_err;
    XSPI2_FCR = XSPI_FCR_CTCF;

    return 0;

xspi_err:
    XSPI2_FCR = XSPI_FCR_CTEF;
    XSPI2_CR |= XSPI_CR_ABORT;
    while (XSPI2_CR & XSPI_CR_ABORT)
        ;
    return -1;
}

static void RAMFUNCTION xspi_write_enable(void)
{
    xspi_cmd(0, NOR_CMD_WRITE_ENABLE, 0, XSPI_MODE_NONE,
             NULL, 0, XSPI_MODE_NONE, 0);
}

static void RAMFUNCTION xspi_wait_ready(void)
{
    uint8_t sr;
    do {
        sr = 0;
        xspi_cmd(1, NOR_CMD_READ_SR, 0, XSPI_MODE_NONE,
                 &sr, 1, XSPI_MODE_SINGLE, 0);
    } while (sr & NOR_SR_WIP);
}

static void RAMFUNCTION xspi_enable_mmap(void)
{
    /* Abort first if already in mmap mode (BUSY stays set in mmap) */
    if ((XSPI2_CR & XSPI_CR_FMODE_MASK) == XSPI_CR_FMODE_MMAP) {
        XSPI2_CR |= XSPI_CR_ABORT;
        while (XSPI2_CR & XSPI_CR_ABORT)
            ;
    }
    while (XSPI2_SR & XSPI_SR_BUSY)
        ;
    XSPI2_FCR = XSPI_FCR_CTCF | XSPI_FCR_CTEF | XSPI_FCR_CSMF;

    XSPI2_CR = (XSPI2_CR & ~XSPI_CR_FMODE_MASK) | XSPI_CR_FMODE_MMAP;

    /* Fast read: single SPI, 4-byte addr, 8 dummy cycles */
    XSPI2_CCR = XSPI_CCR_IMODE(XSPI_MODE_SINGLE) |
                XSPI_CCR_ISIZE(0) |
                XSPI_CCR_ADMODE(XSPI_MODE_SINGLE) |
                XSPI_CCR_ADSIZE(3) |
                XSPI_CCR_DMODE(XSPI_MODE_SINGLE);
    XSPI2_TCR = XSPI_TCR_DCYC(8) | XSPI_TCR_SSHIFT;
    XSPI2_IR = NOR_CMD_FAST_READ_4B;

    DSB();
    ISB();
}

static void RAMFUNCTION dcache_clean_invalidate_by_addr(uint32_t addr, uint32_t size)
{
    uint32_t line;
    for (line = addr & ~0x1FUL; line < addr + size; line += 32) {
        SCB_DCCIMVAC = line;
    }
    DSB();
    ISB();
}

static void icache_enable(void)
{
    DSB();
    ISB();
    SCB_ICIALLU = 0;
    DSB();
    ISB();
    SCB_CCR |= SCB_CCR_IC;
    DSB();
    ISB();
}

static void dcache_enable(void)
{
    DSB();
    SCB_CCR |= SCB_CCR_DC;
    DSB();
    ISB();
}

static void icache_disable(void)
{
    DSB();
    ISB();
    SCB_CCR &= ~SCB_CCR_IC;
    SCB_ICIALLU = 0;
    DSB();
    ISB();
}

static void dcache_disable(void)
{
    /* Clean+invalidate all lines by set/way before disabling */
    uint32_t sets, ways, set, way, way_shift;
    uint32_t ccsidr;

    CSSELR = 0; /* select L1 data cache */
    DSB();
    ccsidr = CCSIDR;
    sets = ((ccsidr >> 13) & 0x7FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;

    /* Calculate way shift: 32 - log2(ways) */
    way_shift = 32;
    { uint32_t tmp = ways - 1; while (tmp) { way_shift--; tmp >>= 1; } }

    for (way = 0; way < ways; way++) {
        for (set = 0; set < sets; set++) {
            SCB_DCCISW = (way << way_shift) | (set << 5);
        }
    }

    DSB();
    SCB_CCR &= ~SCB_CCR_DC;
    DSB();
    ISB();
}

/* XSPI2 GPIO: PN0-PN11 as AF9 (DQS, CLK, NCS, IO0-IO7) */
static void xspi2_gpio_init(void)
{
    uint32_t reg;
    int pin;

    RCC_AHB4ENR |= RCC_AHB4ENR_GPIONEN;
    DMB();

    /* AF mode, very high speed, no pull */
    reg = GPIO_MODER(GPION_BASE);
    for (pin = 0; pin <= 11; pin++) {
        reg &= ~(0x3 << (pin * 2));
        reg |= (GPIO_MODE_AF << (pin * 2));
    }
    GPIO_MODER(GPION_BASE) = reg;

    reg = GPIO_OSPEEDR(GPION_BASE);
    for (pin = 0; pin <= 11; pin++) {
        reg &= ~(0x3 << (pin * 2));
        reg |= (GPIO_SPEED_VERY_HIGH << (pin * 2));
    }
    GPIO_OSPEEDR(GPION_BASE) = reg;

    reg = GPIO_PUPDR(GPION_BASE);
    for (pin = 0; pin <= 11; pin++) {
        reg &= ~(0x3 << (pin * 2));
    }
    GPIO_PUPDR(GPION_BASE) = reg;

    /* AF9 for PN0-PN7 (AFRL) and PN8-PN11 (AFRH) */
    reg = 0;
    for (pin = 0; pin <= 7; pin++) {
        reg |= (9 << (pin * 4));
    }
    GPIO_AFRL(GPION_BASE) = reg;

    reg = 0;
    for (pin = 0; pin <= 3; pin++) {
        reg |= (9 << (pin * 4));
    }
    GPIO_AFRH(GPION_BASE) = reg;
}

static void xspi2_init(void)
{
    volatile uint32_t delay;

    RCC_AHB5ENR |= RCC_AHB5ENR_XSPI2EN | RCC_AHB5ENR_XSPIMEN;
    RCC_MISCENR |= RCC_MISCENR_XSPIPHYCOMPEN;
    DMB();

    XSPI2_CR = 0;
    while (XSPI2_SR & XSPI_SR_BUSY)
        ;

    XSPI2_DCR1 = XSPI_DCR1_DLYBYP |
                  XSPI_DCR1_DEVSIZE(NOR_DEVICE_SIZE_LOG2) |
                  XSPI_DCR1_CSHT(3);
    XSPI2_DCR2 = XSPI_DCR2_PRESCALER(16);
    while (XSPI2_SR & XSPI_SR_BUSY)
        ;

    XSPI2_CR = XSPI_CR_FTHRES(1) | XSPI_CR_EN;

    /* NOR flash software reset */
    xspi_cmd(0, NOR_CMD_RESET_ENABLE, 0, XSPI_MODE_NONE,
             NULL, 0, XSPI_MODE_NONE, 0);
    xspi_cmd(0, NOR_CMD_RESET_MEMORY, 0, XSPI_MODE_NONE,
             NULL, 0, XSPI_MODE_NONE, 0);
    for (delay = 0; delay < 100000; delay++)
        ;

    xspi_enable_mmap();
}

static void clock_config(void)
{
    /* HSI at 64 MHz (PLL configuration deferred) */
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_SR & RCC_SR_HSIRDY))
        ;
}

#ifdef DEBUG_UART
/* USART1 on PE5 (TX) / PE6 (RX), AF7 */

#define UART_CLOCK_FREQ  64000000UL

static void uart_init(uint32_t baud)
{
    uint32_t reg;

    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOEEN;
    DMB();

    /* PE5/PE6 AF mode */
    reg = GPIO_MODER(GPIOE_BASE);
    reg &= ~((0x3 << (5 * 2)) | (0x3 << (6 * 2)));
    reg |= (GPIO_MODE_AF << (5 * 2)) | (GPIO_MODE_AF << (6 * 2));
    GPIO_MODER(GPIOE_BASE) = reg;

    /* AF7 */
    reg = GPIO_AFRL(GPIOE_BASE);
    reg &= ~((0xF << (5 * 4)) | (0xF << (6 * 4)));
    reg |= (7 << (5 * 4)) | (7 << (6 * 4));
    GPIO_AFRL(GPIOE_BASE) = reg;

    reg = GPIO_OSPEEDR(GPIOE_BASE);
    reg &= ~((0x3 << (5 * 2)) | (0x3 << (6 * 2)));
    reg |= (GPIO_SPEED_HIGH << (5 * 2)) | (GPIO_SPEED_HIGH << (6 * 2));
    GPIO_OSPEEDR(GPIOE_BASE) = reg;

    /* 8N1 */
    USART1_CR1 = 0;
    USART1_CR2 = 0;
    USART1_CR3 = 0;
    USART1_BRR = (UART_CLOCK_FREQ + baud / 2) / baud;
    USART1_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart_write(const char *buf, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        while (!(USART1_ISR & USART_ISR_TXE))
            ;
        USART1_TDR = buf[i];
    }
    while (!(USART1_ISR & USART_ISR_TC))
        ;
}
#endif

/* Mark VDDIO supplies valid (required for XSPI2 GPIO) */
static void pwr_enable_io_supply(void)
{
    RCC_AHB4ENR |= RCC_AHB4ENR_PWREN;
    DMB();
    PWR_SVMCR1 |= PWR_SVMCR1_VDDIO4SV;
    PWR_SVMCR2 |= PWR_SVMCR2_VDDIO5SV;
    PWR_SVMCR3 |= PWR_SVMCR3_VDDIO2SV | PWR_SVMCR3_VDDIO3SV;
    DMB();
}

void hal_init(void)
{
    clock_config();
    pwr_enable_io_supply();
    icache_enable();
    dcache_enable();
    xspi2_gpio_init();
    xspi2_init();

#ifdef DEBUG_UART
    uart_init(115200);
    uart_write("wolfBoot Init\n", 14);
#endif
}

void hal_prepare_boot(void)
{
    xspi_enable_mmap();
    dcache_disable();
    icache_disable();
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t offset;
    uint32_t page_off, write_sz;
    int total = len;
    int ret = 0;

    if (len <= 0)
        return 0;

    offset = address - XSPI2_MEM_BASE;

    while (len > 0) {
        page_off = offset & (NOR_PAGE_SIZE - 1);
        write_sz = NOR_PAGE_SIZE - page_off;
        if ((int)write_sz > len)
            write_sz = len;

        xspi_write_enable();
        ret = xspi_cmd(0, NOR_CMD_PAGE_PROG_4B,
                 offset, XSPI_MODE_SINGLE,
                 (uint8_t *)data, write_sz, XSPI_MODE_SINGLE, 0);
        if (ret < 0)
            break;

        xspi_wait_ready();

        offset += write_sz;
        data += write_sz;
        len -= write_sz;
    }

    xspi_enable_mmap();
    dcache_clean_invalidate_by_addr(address, total);

    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t offset;
    uint32_t end;
    int ret = 0;

    if (len <= 0)
        return -1;

    offset = address - XSPI2_MEM_BASE;
    end = offset + len;

    while (offset < end) {
        xspi_write_enable();
        ret = xspi_cmd(0, NOR_CMD_SECTOR_ERASE_4B,
                 offset, XSPI_MODE_SINGLE,
                 NULL, 0, XSPI_MODE_NONE, 0);
        if (ret < 0)
            break;

        xspi_wait_ready();
        offset += NOR_SECTOR_SIZE;
    }

    xspi_enable_mmap();
    dcache_clean_invalidate_by_addr(address, len);

    return ret;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/* ext_flash API: device-relative offsets (update/swap partitions) */

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int ret;

    if (len <= 0)
        return 0;

    ret = xspi_cmd(1, NOR_CMD_FAST_READ_4B,
             (uint32_t)address, XSPI_MODE_SINGLE,
             data, len, XSPI_MODE_SINGLE, 8);

    xspi_enable_mmap();

    return (ret < 0) ? ret : len;
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uint32_t offset = (uint32_t)address;
    uint32_t page_off, write_sz;
    const uint8_t *src = data;
    int remaining = len;
    int ret = 0;

    if (len <= 0)
        return 0;

    while (remaining > 0) {
        page_off = offset & (NOR_PAGE_SIZE - 1);
        write_sz = NOR_PAGE_SIZE - page_off;
        if ((int)write_sz > remaining)
            write_sz = remaining;

        xspi_write_enable();

        ret = xspi_cmd(0, NOR_CMD_PAGE_PROG_4B,
                 offset, XSPI_MODE_SINGLE,
                 (uint8_t *)src, write_sz, XSPI_MODE_SINGLE, 0);
        if (ret < 0)
            break;

        xspi_wait_ready();

        offset += write_sz;
        src += write_sz;
        remaining -= write_sz;
    }

    xspi_enable_mmap();

    return ret;
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    uint32_t offset = (uint32_t)address;
    uint32_t end;
    int ret = 0;

    if (len <= 0)
        return -1;

    end = offset + len;

    while (offset < end) {
        xspi_write_enable();

        ret = xspi_cmd(0, NOR_CMD_SECTOR_ERASE_4B,
                 offset, XSPI_MODE_SINGLE,
                 NULL, 0, XSPI_MODE_NONE, 0);
        if (ret < 0)
            break;

        xspi_wait_ready();
        offset += NOR_SECTOR_SIZE;
    }

    xspi_enable_mmap();

    return ret;
}

void RAMFUNCTION ext_flash_lock(void)
{
}

void RAMFUNCTION ext_flash_unlock(void)
{
}
