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

    /* Byte-wide DR access driven by FLEVEL instead of FTF. DR32-with-FTHRES
     * polling stalls on transfers > FIFO depth (32 B) because the assumed
     * "FTF means 4 bytes are ready" can race with the FIFO state once we've
     * drained/filled below the threshold and the controller doesn't reassert
     * FTF for the trailing bytes. Polling FLEVEL directly is correct for any
     * transfer size and any threshold. */
    if (dataSz > 0 && data != NULL) {
        uint32_t sr;
        while (dataSz > 0) {
            sr = OCTOSPI_SR;
            if (sr & OCTOSPI_SR_TEF) goto octospi_err;
            if (fmode == 0) {
                /* indirect write: push a byte if FIFO has free space */
                if (OCTOSPI_SR_FLEVEL(sr) < 32) {
                    OCTOSPI_DR = *data;
                    data++;
                    dataSz--;
                }
            } else {
                /* indirect read: pop a byte if FIFO has data */
                if (OCTOSPI_SR_FLEVEL(sr) > 0) {
                    *data = OCTOSPI_DR;
                    data++;
                    dataSz--;
                } else if (sr & OCTOSPI_SR_TCF) {
                    /* Transfer complete and FIFO drained -- should not
                     * happen with dataSz > 0 unless DLR mismatch. */
                    goto octospi_err;
                }
            }
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

    /* Single-SPI FAST_READ_4B (0x0C), 4-byte address, 8 dummy cycles.
     * The MX25UM51245G is in default single-SPI mode after Boot ROM
     * hand-off. */
    OCTOSPI_CCR = OCTOSPI_CCR_IMODE(SPI_MODE_SINGLE) |
                  OCTOSPI_CCR_ISIZE(0) |
                  OCTOSPI_CCR_ADMODE(SPI_MODE_SINGLE) |
                  OCTOSPI_CCR_ADSIZE(3) |
                  OCTOSPI_CCR_DMODE(SPI_MODE_SINGLE);
    OCTOSPI_TCR = OCTOSPI_TCR_DCYC(8);
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
    uint32_t sets, ways, set, way, way_shift, tmp;
    uint32_t ccsidr;

    CSSELR = 0; /* select L1 data cache */
    DSB();
    ccsidr = CCSIDR;
    sets = ((ccsidr >> 13) & 0x7FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;

    /* Calculate way shift: 32 - log2(ways) */
    way_shift = 32;
    tmp = ways - 1;
    while (tmp) {
        way_shift--;
        tmp >>= 1;
    }

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
    /* Bring up XSPI2 from the state Boot ROM leaves. Boot ROM:
     *  - loads the FSBL via XSPI2, then disables the peripheral (CR.EN=0)
     *    and clears DCR1 (DEVSIZE=0)
     *  - leaves XSPIM_CR with MODE=1, which on N6 SWAPS the XSPI<->Port
     *    routing (XSPI2->Port 1, XSPI1->Port 2). The MX25UM51245G is wired
     *    to Port 2, so XSPI2 must be re-routed there before we can talk
     *    to the flash
     *  - leaves the flash itself in single-SPI mode (NOT OPI as initially
     *    suspected; single SPI works fine with FAST_READ_4B)
     */
    RCC_AHB5ENR |= RCC_AHB5ENR_XSPI2EN | RCC_AHB5ENR_XSPIMEN;
    RCC_MISCENR |= RCC_MISCENR_XSPIPHYCOMPEN;
    DMB();

    /* VDDIO3 powers the XSPI2 IO bank. The MX25UM51245G runs at 1.8V on
     * NUCLEO-N657X0-Q, so configure VDDIO3 voltage range = 1.8V (bit 26).
     * Then mark VDDIO3 supply valid. */
    RCC_AHB4ENR |= RCC_AHB4ENR_PWREN;
    DMB();
    PWR_SVMCR3 |= PWR_SVMCR3_VDDIO3VRSEL;   /* 1.8V range */
    PWR_SVMCR3 |= PWR_SVMCR3_VDDIO3SV;      /* supply valid */
    DMB();

    /* XSPIM_CR is only writable while BOTH XSPI1 and XSPI2 are CR.EN=0.
     * Force XSPI1 disabled (Boot ROM enabled its clock and may have left
     * EN set), then RCC-reset XSPI2 to clear its Boot-ROM-leftover state. */
    XSPI1_CR &= ~XSPI_CR_EN;
    DMB();
    RCC_AHB5RSTSR = RCC_AHB5RSTR_XSPI2RST;
    DMB();
    RCC_AHB5RSTCR = RCC_AHB5RSTR_XSPI2RST;
    DMB();

    /* Direct mapping: XSPI2 -> Port 2. Without this, XSPI2 signals don't
     * reach the flash and all reads time out. */
    XSPIM_CR = 0;
    DMB();

    /* Set DEVSIZE for the 64 MB MX25UM51245G (DEVSIZE = log2(size)-1 = 25);
     * CSHT (CS high time) = 3; keep DLYBYP. Prescaler /4 (matches ST BSP). */
    OCTOSPI_DCR1 = OCTOSPI_DCR1_DLYBYP |
                   OCTOSPI_DCR1_DEVSIZE(25) |
                   OCTOSPI_DCR1_CSHT(3);
    OCTOSPI_DCR2 = OCTOSPI_DCR2_PRESCALER(4);

    /* Enable peripheral. FTHRES=4 matches DR32 word access from mmap.
     * XIP mmap requires D-cache enabled and cache-line burst access;
     * single-word XIP reads or non-cacheable mapping stall OCTOSPI with
     * BUSY=1 + full FIFO. */
    OCTOSPI_CR = OCTOSPI_CR_FTHRES(4) | OCTOSPI_CR_EN;

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

    /* Switch CPU and system buses back to HSI BEFORE disabling PLL1.
     * The Boot ROM has already moved CPU/SYS to PLL1-sourced IC1/IC2/IC6/IC11
     * for cold boot; clearing PLL1ON while CPU runs on PLL1 stops the CPU
     * clock and hangs the part. (When loaded via OpenOCD the CPU is on
     * reset-state HSI, which masked this on the development path.) */
    reg = RCC_CFGR1;
    reg &= ~(RCC_CFGR1_CPUSW_MASK | RCC_CFGR1_SYSSW_MASK);
    RCC_CFGR1 = reg;
    while ((RCC_CFGR1 & RCC_CFGR1_CPUSWS_MASK) != 0)
        ;
    while ((RCC_CFGR1 & RCC_CFGR1_SYSSWS_MASK) != 0)
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
    /* TrustZone enabled: wolfBoot runs Secure, NS app runs Non-Secure.
     *
     * Per ST's Template_Isolation_XIP reference (NUCLEO-N657X0-Q,
     * STM32CubeN6 v1.3.0): the N6 IDAU defers to SAU for the XSPI2
     * address range, so a SAU NS region over the secure-alias address
     * 0x70xxxxxx actually classifies the access as NS. This matches
     * ST's pattern of running the NS app at 0x70180400 (secure alias)
     * with SAU REG[3] NS 0x70180000-0x701FFFFF. We mirror that here.
     *
     * SAU regions (only where we override IDAU default):
     *   REG[0]: NS, 0x24000000..0x240FFFFF  -- AXISRAM1 NS alias
     *                                          (NS app stack/data)
     *   REG[1]: NS, 0x40000000..0x4FFFFFFF  -- peripherals NS alias
     *                                          (NS app UART/GPIO)
     *   REG[2]: NS, 0x70020000..0x7011FFFF  -- XSPI2 NOR boot
     *                                          partition (NS XIP)
     *
     * Addresses outside these regions fall back to IDAU defaults --
     * wolfBoot's own code/data at 0x34180400 (AXISRAM2 S alias) stays
     * Secure, the XSPI2 update/swap regions stay Secure. */
    {
        SAU_CTRL = 0;
        DSB();
        sau_init_region(0, 0x24000000, 0x240FFFE0, 0); /* NS: AXISRAM1   */
        sau_init_region(1, 0x40000000, 0x4FFFFFE0, 0); /* NS: peripherals*/
        sau_init_region(2, 0x70180000, 0x7027FFE0, 0); /* NS: boot part. */
        SAU_CTRL = SAU_INIT_CTRL_ENABLE;
        DSB();
        ISB();

        /* Enable SecureFault handler so TZ violations surface rather
         * than escalating silently. */
        SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
    }
#else
    /* No TrustZone: blanket NSC region allows secure CPU to access all
     * memory regardless of IDAU attribution, so wolfBoot can read XIP
     * flash and NS peripherals. */
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

#if TZ_SECURE()
    /* RISAF12 secure phase: cover the full 64 MB XSPI2 NOR window
     * with a base SEC region so wolfBoot's secure-side reads of the
     * boot partition (via 0x70020000) and writes to swap/update
     * succeed. The N6 bus matrix tags transactions by CPU state, not
     * by SAU effective attribute, so a secure CPU cannot "downgrade"
     * its access to NS via SAU alone -- the only way to give the NS
     * application access to the same flash is to flip subregion A to
     * NS just before BLXNS (see hal_prepare_boot()).
     *
     * After Boot ROM hand-off RISAF12 has all regions disabled,
     * which lets secure reads work but blocks NS reads. We make that
     * explicit here so the post-BLXNS NS-side XIP fetch doesn't
     * silently return RAZ. */
    *(volatile uint32_t *)(0x56028258) |= (1u << 14); /* RCC AHB3ENR.RISAFEN */
    DSB();
    /* REG[0] (base region 1): SEC for the 128 KB bootloader area
     * (0..0x1FFFF, ends right before the boot partition). */
    *(volatile uint32_t *)(0x54031044) = 0x00000000;
    *(volatile uint32_t *)(0x54031048) = 0x0001FFFF;
    *(volatile uint32_t *)(0x5403104C) = 0x00FF00FF;
    *(volatile uint32_t *)(0x54031040) = 0x00FF0101;  /* BREN|SEC|PRIV */
    DSB();
#endif

    /* Warmup the OCTOSPI mmap path with one read of the boot partition.
     * Without this delay and warmup access the first XIP read from
     * wolfBoot_start stalls (BUSY stays set indefinitely). */
    {
        volatile uint32_t i;
        volatile uint32_t warmup;
        for (i = 0; i < 100000; i++) {}
        warmup = *(volatile const uint32_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        (void)warmup;
    }
}

void hal_prepare_boot(void)
{
    octospi_enable_mmap();
    dcache_disable();
    icache_disable();
    /* The RISAF12 split (NS for boot partition, SEC elsewhere) is
     * applied later by stm32n6_tz_handoff() in do_boot() so wolfBoot's
     * own reads of the vector table from the boot partition still
     * pass the SEC base region installed by hal_init(). */
}

#if TZ_SECURE()
/* Reconfigure RIF for the secure -> non-secure hand-off. Called from
 * do_boot() right before BLXNS, after the secure side has finished
 * all of its boot-partition reads.
 *
 * Address-to-RISAF mapping per N6 CMSIS header (RIF_AWARE_PERIPH_INDEX
 * _RISAF*):
 *   - RISAF2 (0x54027000) -> AXISRAM1 (SRAM1_AXI at 0x34000000 /
 *                                       0x24000000), 1 MB total
 *   - RISAF3 (0x54028000) -> AXISRAM2 (SRAM2_AXI at 0x34100000 /
 *                                       0x24100000) -- wolfBoot lives
 *                                       here; leave at default SEC
 *   - RISAF12 (0x54031000) -> XSPI2 NOR XIP (64 MB at 0x70000000 /
 *                                            0x90000000)
 *
 * RISAF12 (XSPI2 NOR) -- three non-overlapping base regions:
 *   REG[0]: SEC, 0..0x1FFFF       (bootloader + swap below boot)
 *   REG[1]: NS,  0x20000..0x11FFFF (boot partition -- NS XIP)
 *   REG[2]: SEC, 0x120000..end    (update partition + remainder)
 *
 * RISAF2 (AXISRAM1) -- one base region:
 *   REG[0]: NS, 0..0xFFFFF (full 1 MB)
 *
 * The NS application links its stack at 0x24020000 (top of a 64 KB
 * RAM region at 0x24010000) and its .data/.bss at 0x24010000+, so
 * AXISRAM1 needs an NS-attributed RIF view for the NS CPU to push
 * registers and access globals.
 */
void RAMFUNCTION stm32n6_tz_handoff(void)
{
    /* Disable the bootloader-area RISAF12 SEC region installed by
     * hal_init(). Reason: RISAF filters by CPU security state (not
     * by bus HNONSEC), so leaving REG[0] SEC active would still
     * gate the BLXNS pre-attribution check the secure CPU performs
     * on the NS entry point at 0x70020XXX -- which is inside the
     * boot partition, not bootloader, but the secure-side
     * verification path may also touch the swap sector below it.
     *
     * With all RISAF12 regions disabled the default is allow-all,
     * so both the secure CPU (during the BLXNS target check) and
     * the NS CPU (during post-BLXNS XIP fetches) can read the
     * boot partition. We still configure RISAF2 for AXISRAM1 NS so
     * the NS application's stack pushes succeed.
     *
     * If later we need stricter isolation, the right shape is a
     * RISAF12 region that admits BOTH S and NS transactions via
     * the CID whitelist (set SEC=0 with WRENC/RDENC=0xFF and rely
     * on the bus matrix promoting accesses). The current N6 silicon
     * does not appear to honor that for the secure CPU's pre-check
     * on BLXNS targets, hence the all-default approach. */
    *(volatile uint32_t *)(0x54031040) = 0x00000000; /* REG[0] CFGR: BREN=0 */
    *(volatile uint32_t *)(0x54031080) = 0x00000000; /* REG[1] CFGR: BREN=0 */
    *(volatile uint32_t *)(0x540310C0) = 0x00000000; /* REG[2] CFGR: BREN=0 */
    DSB();

    /* RISAF2 (AXISRAM1) NS region for NS app RAM. NS app's stack
     * lives at 0x24010000-0x2401FFFF (NS alias of AXISRAM1 offset
     * 0x10000-0x1FFFF); this region admits NS transactions for the
     * full 1 MB bank. */
    *(volatile uint32_t *)(0x54027044) = 0x00000000;
    *(volatile uint32_t *)(0x54027048) = 0x000FFFFF;
    *(volatile uint32_t *)(0x5402704C) = 0x00FF00FF;
    *(volatile uint32_t *)(0x54027040) = 0x00FF0001;  /* BREN|NS|PRIV */
    DSB();
}
#endif

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

/* Forward declaration: ext_flash_addr is defined below alongside the
 * ext_flash_* API but is shared with the hal_flash_* path. */
static uint32_t RAMFUNCTION ext_flash_addr(uintptr_t address);

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t off = ext_flash_addr(address);
    int ret = nor_flash_write(off, data, len);
    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(OCTOSPI_MEM_BASE + off, len);
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t off = ext_flash_addr(address);
    int ret = nor_flash_erase(off, len);
    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(OCTOSPI_MEM_BASE + off, len);
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
    /* Accept either alias of the 64 MB XSPI2 NOR region (0x70xxxxxx S,
     * 0x90xxxxxx NS) and convert to a device-relative offset. Anything
     * below both aliases is treated as already-offset (update/swap). */
    if (address >= OCTOSPI_MEM_BASE_NS &&
        address < (OCTOSPI_MEM_BASE_NS + OCTOSPI_MEM_SIZE))
        return (uint32_t)(address - OCTOSPI_MEM_BASE_NS);
    if (address >= OCTOSPI_MEM_BASE_S &&
        address < (OCTOSPI_MEM_BASE_S + OCTOSPI_MEM_SIZE))
        return (uint32_t)(address - OCTOSPI_MEM_BASE_S);
    return (uint32_t)address;
}

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    /* Read via XIP memory-mapped alias. OCTOSPI_MEM_BASE is the alias
     * (0x70xxxxxx for non-TZ, 0x90xxxxxx for TZ) that the controller
     * is accessible through given current GTZC attribution. */
    volatile const uint8_t *src;
    uint32_t off = ext_flash_addr(address);
    int i;
    if (len <= 0)
        return 0;
    src = (volatile const uint8_t *)(OCTOSPI_MEM_BASE + off);
    for (i = 0; i < len; i++)
        data[i] = src[i];
    return len;
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data,
    int len)
{
    uint32_t off = ext_flash_addr(address);
    int ret = nor_flash_write(off, data, len);
    /* Invalidate D-cache for the XIP alias, otherwise later reads of the
     * just-written region return stale data from cache. */
    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(OCTOSPI_MEM_BASE + off, len);
    return ret;
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    uint32_t off = ext_flash_addr(address);
    int ret = nor_flash_erase(off, len);
    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(OCTOSPI_MEM_BASE + off, len);
    return ret;
}

void RAMFUNCTION ext_flash_lock(void)
{
}

void RAMFUNCTION ext_flash_unlock(void)
{
}
