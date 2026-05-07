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
#include <string.h>
#include <image.h>
#include "hal.h"
#include "hal/stm32n6.h"
#include "printf.h"
#include "hal/armv8m_tz.h"

/* OCTOSPI register definitions come from hal/spi/spi_drv_stm32.h (included
 * via stm32n6.h). STM32N6 XSPI2 uses the same IP block as OCTOSPI.
 *
 * Note: We cannot reuse qspi_transfer() from spi_drv_stm32.c because it
 * disables/enables the peripheral on each transfer. The N6 boots via XIP
 * (memory-mapped mode on XSPI2), so we need custom transfer code that:
 *  1) Aborts memory-mapped mode before indirect access
 *  2) Restores memory-mapped mode after each operation
 *  3) Runs from SRAM (RAMFUNCTION) when called from XIP code
 */

/* SPI mode values for OCTOSPI CCR fields */
#define SPI_MODE_NONE       0
#define SPI_MODE_SINGLE     1

/* RAMFUNCTION override for test-app (XIP needs flash ops in SRAM) */
#if defined(RAM_CODE) && !defined(__WOLFBOOT)
    #undef RAMFUNCTION
    #define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#endif

/* OCTOSPI indirect-mode command helper.
 * Handles memory-mapped mode abort/restore and TEF error detection. */
static int RAMFUNCTION octospi_cmd(uint8_t fmode, uint8_t cmd,
    uint32_t addr, uint32_t addrMode,
    uint8_t *data, uint32_t dataSz, uint32_t dataMode,
    uint32_t dummyCycles)
{
    uint32_t ccr;

    /* Abort memory-mapped mode if active */
    if ((OCTOSPI_CR & OCTOSPI_CR_FMODE_MASK) == OCTOSPI_CR_FMODE_MMAP) {
        OCTOSPI_CR |= OCTOSPI_CR_ABORT;
        while (OCTOSPI_CR & OCTOSPI_CR_ABORT)
            ;
    }
    while (OCTOSPI_SR & OCTOSPI_SR_BUSY)
        ;
    OCTOSPI_FCR = OCTOSPI_FCR_CTCF | OCTOSPI_FCR_CTEF | OCTOSPI_FCR_CSMF;

    OCTOSPI_CR = (OCTOSPI_CR & ~OCTOSPI_CR_FMODE_MASK) |
                 OCTOSPI_CR_FMODE(fmode);

    if (dataSz > 0) {
        OCTOSPI_DLR = dataSz - 1;
    }

    ccr = OCTOSPI_CCR_IMODE(SPI_MODE_SINGLE) | OCTOSPI_CCR_ISIZE(0);
    if (addrMode != SPI_MODE_NONE) {
        ccr |= OCTOSPI_CCR_ADMODE(addrMode) | OCTOSPI_CCR_ADSIZE(3);
    }
    if (dataMode != SPI_MODE_NONE) {
        ccr |= OCTOSPI_CCR_DMODE(dataMode);
    }
    OCTOSPI_CCR = ccr;
    OCTOSPI_TCR = OCTOSPI_TCR_DCYC(dummyCycles);
    OCTOSPI_IR = cmd;

    if (addrMode != SPI_MODE_NONE) {
        OCTOSPI_AR = addr;
    }

    if (dataSz > 0 && data != NULL) {
        while (dataSz >= 4) {
            if (fmode == 0) {
                while (!(OCTOSPI_SR & (OCTOSPI_SR_FTF | OCTOSPI_SR_TEF)))
                    ;
                if (OCTOSPI_SR & OCTOSPI_SR_TEF) goto octospi_err;
                OCTOSPI_DR32 = *(uint32_t *)data;
            } else {
                while (!(OCTOSPI_SR & (OCTOSPI_SR_FTF | OCTOSPI_SR_TCF |
                                       OCTOSPI_SR_TEF)))
                    ;
                if (OCTOSPI_SR & OCTOSPI_SR_TEF) goto octospi_err;
                *(uint32_t *)data = OCTOSPI_DR32;
            }
            data += 4;
            dataSz -= 4;
        }
        while (dataSz > 0) {
            if (fmode == 0) {
                while (!(OCTOSPI_SR & (OCTOSPI_SR_FTF | OCTOSPI_SR_TEF)))
                    ;
                if (OCTOSPI_SR & OCTOSPI_SR_TEF) goto octospi_err;
                OCTOSPI_DR = *data;
            } else {
                while (!(OCTOSPI_SR & (OCTOSPI_SR_FTF | OCTOSPI_SR_TCF |
                                       OCTOSPI_SR_TEF)))
                    ;
                if (OCTOSPI_SR & OCTOSPI_SR_TEF) goto octospi_err;
                *data = OCTOSPI_DR;
            }
            data++;
            dataSz--;
        }
    }

    while (!(OCTOSPI_SR & (OCTOSPI_SR_TCF | OCTOSPI_SR_TEF)))
        ;
    if (OCTOSPI_SR & OCTOSPI_SR_TEF) goto octospi_err;
    OCTOSPI_FCR = OCTOSPI_FCR_CTCF;

    return 0;

octospi_err:
    OCTOSPI_FCR = OCTOSPI_FCR_CTEF;
    OCTOSPI_CR |= OCTOSPI_CR_ABORT;
    while (OCTOSPI_CR & OCTOSPI_CR_ABORT)
        ;
    return -1;
}

static void RAMFUNCTION octospi_write_enable(void)
{
    octospi_cmd(0, WRITE_ENABLE_CMD, 0, SPI_MODE_NONE,
                NULL, 0, SPI_MODE_NONE, 0);
}

static void RAMFUNCTION octospi_wait_ready(void)
{
    uint8_t sr;
    do {
        sr = 0;
        octospi_cmd(1, READ_SR_CMD, 0, SPI_MODE_NONE,
                    &sr, 1, SPI_MODE_SINGLE, 0);
    } while (sr & FLASH_SR_BUSY);
}

static void RAMFUNCTION octospi_enable_mmap(void)
{
    /* Abort first if already in mmap mode (BUSY stays set in mmap) */
    if ((OCTOSPI_CR & OCTOSPI_CR_FMODE_MASK) == OCTOSPI_CR_FMODE_MMAP) {
        OCTOSPI_CR |= OCTOSPI_CR_ABORT;
        while (OCTOSPI_CR & OCTOSPI_CR_ABORT)
            ;
    }
    while (OCTOSPI_SR & OCTOSPI_SR_BUSY)
        ;
    OCTOSPI_FCR = OCTOSPI_FCR_CTCF | OCTOSPI_FCR_CTEF | OCTOSPI_FCR_CSMF;

    OCTOSPI_CR = (OCTOSPI_CR & ~OCTOSPI_CR_FMODE_MASK) |
                 OCTOSPI_CR_FMODE_MMAP;

    /* Fast read: single SPI, 4-byte addr, 8 dummy cycles */
    OCTOSPI_CCR = OCTOSPI_CCR_IMODE(SPI_MODE_SINGLE) |
                  OCTOSPI_CCR_ISIZE(0) |
                  OCTOSPI_CCR_ADMODE(SPI_MODE_SINGLE) |
                  OCTOSPI_CCR_ADSIZE(3) |
                  OCTOSPI_CCR_DMODE(SPI_MODE_SINGLE);
    OCTOSPI_TCR = OCTOSPI_TCR_DCYC(8) | OCTOSPI_TCR_SSHIFT;
    OCTOSPI_IR = FAST_READ_4B_CMD;

    DSB();
    ISB();
}

static void RAMFUNCTION dcache_clean_invalidate_by_addr(uint32_t addr,
    uint32_t size)
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

/* OCTOSPI GPIO: PN0-PN11 as AF9 (DQS, CLK, NCS, IO0-IO7) */
static void octospi_gpio_init(void)
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

static void octospi_init(void)
{
    volatile uint32_t delay;

    RCC_AHB5ENR |= RCC_AHB5ENR_XSPI2EN | RCC_AHB5ENR_XSPIMEN;
    RCC_MISCENR |= RCC_MISCENR_XSPIPHYCOMPEN;
    DMB();

    OCTOSPI_CR = 0;
    while (OCTOSPI_SR & OCTOSPI_SR_BUSY)
        ;

    OCTOSPI_DCR1 = OCTOSPI_DCR1_DLYBYP |
                   OCTOSPI_DCR1_DEVSIZE(FLASH_DEVICE_SIZE_LOG2) |
                   OCTOSPI_DCR1_CSHT(3);
    OCTOSPI_DCR2 = OCTOSPI_DCR2_PRESCALER(16);
    while (OCTOSPI_SR & OCTOSPI_SR_BUSY)
        ;

    OCTOSPI_CR = OCTOSPI_CR_FTHRES(1) | OCTOSPI_CR_EN;

    /* NOR flash software reset */
    octospi_cmd(0, RESET_ENABLE_CMD, 0, SPI_MODE_NONE,
                NULL, 0, SPI_MODE_NONE, 0);
    octospi_cmd(0, RESET_MEMORY_CMD, 0, SPI_MODE_NONE,
                NULL, 0, SPI_MODE_NONE, 0);
    for (delay = 0; delay < 100000; delay++)
        ;

    octospi_enable_mmap();
}

/* Configure clocks: PLL1 → 600 MHz CPU (Voltage Scale 1).
 * STM32N6 supports up to 800 MHz at Voltage Scale 0 (PWR_VOSCR_VOS=1).
 *
 * Clock tree:
 *   HSI 64 MHz → PLL1 (M=4, N=75) → VCO 1200 MHz → PDIV1=1 → 1200 MHz
 *     IC1 /2 = 600 MHz → CPU (CPUSW=IC1)
 *     IC2 /3 = 400 MHz → AXI bus (SYSSW=IC2)
 *     IC6 /4 = 300 MHz → system bus C (SYSSW=IC6)
 *     IC11/3 = 400 MHz → system bus D (SYSSW=IC11)
 *   AHB prescaler /2 → HCLK = 300 MHz
 */
static void clock_config(void)
{
    uint32_t reg;

    /* Enable HSI 64 MHz */
    RCC_CSR = RCC_CR_HSION;
    while (!(RCC_SR & RCC_SR_HSIRDY))
        ;

    /* Disable PLL1 before reconfiguring */
    RCC_CCR = RCC_CR_PLL1ON;
    while (RCC_SR & RCC_SR_PLL1RDY)
        ;

    /* PLL1: HSI / 4 * 75 = 1200 MHz VCO.
     * Clear BYP (bypass) — Boot ROM leaves it set, which routes HSI
     * directly to PLL output, skipping the VCO entirely. */
    reg = RCC_PLL1CFGR1;
    reg &= ~(RCC_PLL1CFGR1_SEL_MASK | RCC_PLL1CFGR1_DIVM_MASK |
             RCC_PLL1CFGR1_DIVN_MASK | RCC_PLL1CFGR1_BYP);
    reg |= RCC_PLL1CFGR1_SEL_HSI |
           (4 << RCC_PLL1CFGR1_DIVM_SHIFT) |
           (75 << RCC_PLL1CFGR1_DIVN_SHIFT);
    RCC_PLL1CFGR1 = reg;

    RCC_PLL1CFGR2 = 0; /* no fractional */

    /* PDIV1=1, PDIV2=1 → PLL output = VCO = 1200 MHz.
     * MODSSDIS: disable spread spectrum. PDIVEN: enable PLL output. */
    RCC_PLL1CFGR3 = (1 << RCC_PLL1CFGR3_PDIV1_SHIFT) |
                    (1 << RCC_PLL1CFGR3_PDIV2_SHIFT) |
                    RCC_PLL1CFGR3_MODSSDIS |
                    RCC_PLL1CFGR3_MODSSRST |
                    RCC_PLL1CFGR3_PDIVEN;

    /* Enable PLL1, wait for lock */
    RCC_CSR = RCC_CR_PLL1ON;
    while (!(RCC_SR & RCC_SR_PLL1RDY))
        ;

    /* Configure IC dividers: disable → configure → re-enable.
     * IC divider = INT + 1, SEL: 0=PLL1 (per ST LL driver). */
    RCC_DIVENCR = RCC_DIVENR_IC1EN;
    RCC_IC1CFGR = RCC_ICCFGR_SEL_PLL1 | ((2 - 1) << RCC_ICCFGR_INT_SHIFT);
    RCC_DIVENSR = RCC_DIVENR_IC1EN;

    RCC_DIVENCR = RCC_DIVENR_IC2EN;
    RCC_IC2CFGR = RCC_ICCFGR_SEL_PLL1 | ((3 - 1) << RCC_ICCFGR_INT_SHIFT);
    RCC_DIVENSR = RCC_DIVENR_IC2EN;

    RCC_DIVENCR = RCC_DIVENR_IC6EN;
    RCC_IC6CFGR = RCC_ICCFGR_SEL_PLL1 | ((4 - 1) << RCC_ICCFGR_INT_SHIFT);
    RCC_DIVENSR = RCC_DIVENR_IC6EN;

    RCC_DIVENCR = RCC_DIVENR_IC11EN;
    RCC_IC11CFGR = RCC_ICCFGR_SEL_PLL1 | ((3 - 1) << RCC_ICCFGR_INT_SHIFT);
    RCC_DIVENSR = RCC_DIVENR_IC11EN;

    /* AHB prescaler /2 → HCLK = 300 MHz */
    reg = RCC_CFGR2;
    reg &= ~RCC_CFGR2_HPRE_MASK;
    reg |= (1 << RCC_CFGR2_HPRE_SHIFT);
    RCC_CFGR2 = reg;

    /* Switch CPU to IC1, system bus to IC2/IC6/IC11 */
    reg = RCC_CFGR1;
    reg &= ~(RCC_CFGR1_CPUSW_MASK | RCC_CFGR1_SYSSW_MASK);
    reg |= (0x3 << RCC_CFGR1_CPUSW_SHIFT) |
           (0x3 << RCC_CFGR1_SYSSW_SHIFT);
    RCC_CFGR1 = reg;
    while ((RCC_CFGR1 & RCC_CFGR1_CPUSWS_MASK) !=
           (0x3 << RCC_CFGR1_CPUSWS_SHIFT))
        ;
    while ((RCC_CFGR1 & RCC_CFGR1_SYSSWS_MASK) !=
           (0x3 << RCC_CFGR1_SYSSWS_SHIFT))
        ;
}

#ifdef DEBUG_UART
/* USART1 on PE5 (TX) / PE6 (RX), AF7 */

#define UART_BASE        USART1_BASE
#define UART_CLOCK_FREQ  200000000UL /* PCLK2 = IC2(400MHz) / AHB(2) / APB2(1) */

static void uart_init_baud(uint32_t baud)
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
    UART_CR1(UART_BASE) = 0;
    UART_CR2(UART_BASE) = 0;
    UART_CR3(UART_BASE) = 0;
    UART_BRR(UART_BASE) = (UART_CLOCK_FREQ + baud / 2) / baud;
    UART_CR1(UART_BASE) = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void uart_write(const char *buf, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        while (!(UART_ISR(UART_BASE) & USART_ISR_TXE))
            ;
        UART_TDR(UART_BASE) = buf[i];
    }
    while (!(UART_ISR(UART_BASE) & USART_ISR_TC))
        ;
}
#endif

/* Mark VDDIO supplies valid (required for OCTOSPI GPIO) */
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
#ifdef TZEN
    /* TrustZone enabled: wolfBoot runs Secure, app runs Non-Secure.
     * Configure SAU to define non-secure regions for the application. */
    {
        SAU_CTRL = 0;
        DSB();

        /* Region 0: NSC - reserved for secure gateway veneers */
        sau_init_region(0, 0x24010000, 0x2401FFFF, 1);

        /* Region 1: NS - XSPI2 memory-mapped flash (app XIP + data) */
        sau_init_region(1, 0x70000000, 0x7FFFFFFF, 0);

        /* Region 2: NS - app SRAM (non-secure AXISRAM alias) */
        sau_init_region(2, 0x34000000, 0x343FFFFF, 0);

        /* Region 3: NS - peripheral non-secure aliases (for app) */
        sau_init_region(3, 0x40000000, 0x4FFFFFFF, 0);

        SAU_CTRL = SAU_INIT_CTRL_ENABLE;
        DSB();
        ISB();

        /* Enable SecureFault handler */
        SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
    }
#else
    /* No TrustZone: blanket NSC region allows secure CPU to access all
     * memory regardless of IDAU attribution. */
    {
        SAU_CTRL = 0;
        DSB();
        sau_init_region(0, 0x00000000, 0xFFFFFFE0, 1); /* full range, NSC */
        SAU_CTRL = SAU_INIT_CTRL_ENABLE;
        DSB();
        ISB();
    }
#endif
    clock_config();
    pwr_enable_io_supply();
    icache_enable();
    octospi_gpio_init();
    octospi_init();
    dcache_enable();

#ifdef DEBUG_UART
    uart_init_baud(115200);
    uart_write("wolfBoot Init\n", 14);
    wolfBoot_printf("TrustZone: %s\n",
    #if TZ_SECURE()
        "Secure"
    #else
        "Off"
    #endif
    );
#endif
}

void hal_prepare_boot(void)
{
    octospi_enable_mmap();
    dcache_disable();
    icache_disable();
}

/* Shared NOR flash helpers used by both hal_flash_* and ext_flash_* */
static int RAMFUNCTION nor_flash_write(uint32_t offset, const uint8_t *data,
    int len)
{
    uint32_t page_off, write_sz;
    int remaining = len;
    int ret = 0;
    /* Buffer for data that may reside in XIP flash (memory-mapped XSPI2).
     * The source pointer becomes invalid once XSPI2 leaves mmap mode for
     * the SPI page-program command, so we copy to RAM first. */
    uint8_t page_buf[FLASH_PAGE_SIZE];

    if (len <= 0)
        return 0;

    while (remaining > 0) {
        page_off = offset & (FLASH_PAGE_SIZE - 1);
        write_sz = FLASH_PAGE_SIZE - page_off;
        if ((int)write_sz > remaining)
            write_sz = remaining;

        memcpy(page_buf, data, write_sz);

        octospi_write_enable();
        ret = octospi_cmd(0, PAGE_PROG_4B_CMD,
                 offset, SPI_MODE_SINGLE,
                 page_buf, write_sz, SPI_MODE_SINGLE, 0);
        if (ret < 0)
            break;

        octospi_wait_ready();

        offset += write_sz;
        data += write_sz;
        remaining -= write_sz;
    }

    octospi_enable_mmap();
    return ret;
}

static int RAMFUNCTION nor_flash_erase(uint32_t offset, int len)
{
    uint32_t end;
    int ret = 0;

    if (len <= 0)
        return -1;

    end = offset + len;

    while (offset < end) {
        octospi_write_enable();
        ret = octospi_cmd(0, SEC_ERASE_4B_CMD,
                 offset, SPI_MODE_SINGLE,
                 NULL, 0, SPI_MODE_NONE, 0);
        if (ret < 0)
            break;

        octospi_wait_ready();
        offset += FLASH_SECTOR_SIZE;
    }

    octospi_enable_mmap();
    return ret;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret = nor_flash_write(address - OCTOSPI_MEM_BASE, data, len);
    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(address, len);
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret = nor_flash_erase(address - OCTOSPI_MEM_BASE, len);
    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(address, len);
    return ret;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/* ext_flash API: accepts both device-relative offsets (update/swap) and
 * absolute memory-mapped addresses (boot partition with PART_BOOT_EXT). */

static uint32_t RAMFUNCTION ext_flash_addr(uintptr_t address)
{
    if (address >= OCTOSPI_MEM_BASE)
        return (uint32_t)(address - OCTOSPI_MEM_BASE);
    return (uint32_t)address;
}

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int ret;

    if (len <= 0)
        return 0;

    ret = octospi_cmd(1, FAST_READ_4B_CMD,
             ext_flash_addr(address), SPI_MODE_SINGLE,
             data, len, SPI_MODE_SINGLE, 8);

    octospi_enable_mmap();

    return (ret < 0) ? ret : len;
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data,
    int len)
{
    return nor_flash_write(ext_flash_addr(address), data, len);
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    return nor_flash_erase(ext_flash_addr(address), len);
}

void RAMFUNCTION ext_flash_lock(void)
{
}

void RAMFUNCTION ext_flash_unlock(void)
{
}
